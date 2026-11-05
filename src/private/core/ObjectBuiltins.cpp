// ObjectBuiltins.cpp - Object / Function 构造函数与内建原型（ADR-035）
//
// bootstrap 顺序（关键：原型先于构造函数建立，避免 createObject/createNativeFunction
// 自动挂链时拿到未初始化的原型）：
//   1. Object.prototype（裸对象，原型 null）
//   2. Function.prototype（函数对象，原型 = Object.prototype）
//   3. Object 构造函数（原型 = Function.prototype，prototype 属性 = Object.prototype）
//   4. Function 构造函数（原型 = Function.prototype，prototype 属性 = Function.prototype）
//   5. 挂 prototype 方法
//   6. 注册到 global
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）

#include "core/ObjectBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Object.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <cmath>
#include <string>
#include <vector>

namespace neo::core {

namespace {

// —— Object.prototype 方法 ——

// Object.prototype.toString()（ES2024 §20.1.3.6）：
//   1. 对象先查 @@toStringTag，非 undefined 则用其 ToString 值；
//   2. 否则按类型给出内建 tag。
Value* objectProtoToString(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    if (self == nullptr) return ctx->createString("[object Undefined]");
    // @@toStringTag 优先（仅对对象；原始值无自有属性，跳过）
    if (detail::isObjectKind(self->type()->kind())) {
        Value* tagValue = ctx->getProperty(
            self, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag));
        if (detail::isErrorValue(tagValue)) return tagValue;
        if (tagValue->type()->kind() != TypeKind::Undefined) {
            Value* tagStr = detail::toStringValue(ctx, tagValue);
            if (detail::isErrorValue(tagStr)) return tagStr;
            return ctx->createString(
                "[object " + static_cast<StringData*>(tagStr->data())->value + "]");
        }
    }
    const char* tag = "Object";
    switch (self->type()->kind()) {
        case TypeKind::Undefined: tag = "Undefined"; break;
        case TypeKind::Null:      tag = "Null"; break;
        case TypeKind::Boolean:   tag = "Boolean"; break;
        case TypeKind::Number:    tag = "Number"; break;
        case TypeKind::String:    tag = "String"; break;
        case TypeKind::Symbol:    tag = "Symbol"; break;
        case TypeKind::Function:  tag = "Function"; break;
        case TypeKind::Object:    tag = "Object"; break;
        case TypeKind::Array:     tag = "Array"; break;
        // 包装对象按 [[PrimitiveValue]] 的类型给出 tag（ES2024 §20.1.3.6）。
        // 包装对象的 primitive 恒为 Boolean/Number/String/Symbol 之一（createPrimitiveWrapper 保证）。
        case TypeKind::PrimitiveWrapper: {
            switch (wrapperPrimitiveValue(self)->type()->kind()) {
                case TypeKind::Boolean: tag = "Boolean"; break;
                case TypeKind::Number:  tag = "Number"; break;
                case TypeKind::String:  tag = "String"; break;
                default:                tag = "Symbol"; break;
            }
            break;
        }
        default:                  tag = "Object"; break;
    }
    return ctx->createString(std::string("[object ") + tag + "]");
}

// Object.prototype.valueOf()：返回 this 自身
Value* objectProtoValueOf(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return self != nullptr ? self : ctx->createUndefined();
}

// Object.prototype.hasOwnProperty(key)：只查自有属性（不含原型链）
Value* objectProtoHasOwnProperty(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypeKind sk = self != nullptr ? self->type()->kind() : TypeKind::Undefined;
    if (self == nullptr ||
        (sk != TypeKind::Object && sk != TypeKind::Function && sk != TypeKind::Array &&
         sk != TypeKind::PrimitiveWrapper)) {
        return ctx->createException(
            ctx->createString("Object.prototype.hasOwnProperty called on non-object"));
    }
    if (argc == 0) return ctx->createBoolean(false);
    Value* key = argv[0];
    auto* d = static_cast<ObjectData*>(self->data());
    if (key->type()->kind() == TypeKind::Symbol) {
        return ctx->createBoolean(d->symbols.count(key->data()) != 0);
    }
    // ToPropertyKey（ES2024 §7.1.19）：对象键先 ToPrimitive(STRING)
    std::string k;
    if (Value* err = detail::propertyKeyString(ctx, key, k)) return err;
    // 数组：索引元素与 length 也是自有属性
    if (sk == TypeKind::Array) {
        auto* ad = static_cast<ArrayData*>(self->data());
        if (k == "length") return ctx->createBoolean(true);
        uint32_t index = 0;
        if (parseArrayIndex(k, index)) {
            return ctx->createBoolean(ad->hasElement(index));
        }
    }
    return ctx->createBoolean(d->properties.count(k) != 0);
}

// Object.prototype.isPrototypeOf(v)（ES2024 §20.1.3.3）：
// 沿 v 的原型链查找，命中 this 则 true。
Value* objectProtoIsPrototypeOf(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypeKind sk = self != nullptr ? self->type()->kind() : TypeKind::Undefined;
    if (self == nullptr || !detail::isObjectKind(sk)) {
        return ctx->createException(ctx->createString(
            "Object.prototype.isPrototypeOf called on non-object"));
    }
    if (argc == 0 || !detail::isObjectKind(argv[0]->type()->kind())) {
        return ctx->createBoolean(false);
    }
    for (Value* cur = ctx->getPrototype(argv[0]);
         cur != nullptr && detail::isObjectKind(cur->type()->kind());
         cur = ctx->getPrototype(cur)) {
        if (cur == self) return ctx->createBoolean(true);
    }
    return ctx->createBoolean(false);
}

// Object.prototype.propertyIsEnumerable(key)（ES2024 §20.1.3.4）：
// 自有属性且 enumerable 才为 true。
Value* objectProtoPropertyIsEnumerable(IContext* ctx, Value* self, size_t argc,
                                       Value* argv[]) {
    TypeKind sk = self != nullptr ? self->type()->kind() : TypeKind::Undefined;
    if (self == nullptr || !detail::isObjectKind(sk)) {
        return ctx->createException(ctx->createString(
            "Object.prototype.propertyIsEnumerable called on non-object"));
    }
    if (argc == 0) return ctx->createBoolean(false);
    Value* key = argv[0];
    auto* d = static_cast<ObjectData*>(self->data());

    // symbol 键：查 symbols 容器
    if (key->type()->kind() == TypeKind::Symbol) {
        auto it = d->symbols.find(key->data());
        return ctx->createBoolean(it != d->symbols.end() && it->second.prop.enumerable);
    }
    std::string k;
    if (Value* err = detail::propertyKeyString(ctx, key, k)) return err;
    // 数组索引与 length、String 包装索引与 length
    if (sk == TypeKind::Array) {
        auto* ad = static_cast<ArrayData*>(self->data());
        if (k == "length") return ctx->createBoolean(false);
        uint32_t index = 0;
        if (parseArrayIndex(k, index)) {
            return ctx->createBoolean(ad->hasElement(index));
        }
    }
    if (sk == TypeKind::PrimitiveWrapper) {
        const std::string* s = wrapperStringValue(self);
        if (s != nullptr) {
            if (k == "length") return ctx->createBoolean(false);
            uint32_t index = 0;
            if (parseArrayIndex(k, index) && index < s->size()) {
                return ctx->createBoolean(true);
            }
        }
    }
    auto it = d->properties.find(k);
    return ctx->createBoolean(it != d->properties.end() && it->second.enumerable);
}

// Object.prototype.toLocaleString()（ES2024 §20.1.3.5）：
// 调 this.toString()（toString 不可调用则 TypeError）。
Value* objectProtoToLocaleString(IContext* ctx, Value* self, size_t /*argc*/,
                                 Value* /*argv*/[]) {
    if (self == nullptr) {
        return ctx->createException(
            ctx->createString("Object.prototype.toLocaleString called on null"));
    }
    Value* fn = ctx->getProperty(self, "toString");
    if (detail::isErrorValue(fn)) return fn;
    if (fn->type()->kind() != TypeKind::Function) {
        return ctx->createException(
            ctx->createString("toString is not a function"));
    }
    return ctx->call(fn, self, 0, nullptr);
}

// —— Function.prototype 方法 ——

// Function.prototype.toString()：占位，返回 "[native code]"
Value* functionProtoToString(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                             Value* /*argv*/[]) {
    return ctx->createString("function () { [native code] }");
}

// Function.prototype.call(thisArg, ...args)
Value* functionProtoCall(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (self == nullptr || self->type()->kind() != TypeKind::Function) {
        return ctx->createException(ctx->createString("Function.prototype.call called on non-function"));
    }
    Value* thisArg = argc > 0 ? argv[0] : ctx->createUndefined();
    return ctx->call(self, thisArg, argc > 0 ? argc - 1 : 0, argc > 0 ? argv + 1 : nullptr);
}

// Function.prototype.apply(thisArg, argsArray)（ES2024 §20.2.3.1）
//   argsArray 为 null/undefined → 无参调用；
//   否则按 arrayLike 读 length 与各索引展开为实参（CreateListFromArrayLike）。
Value* functionProtoApply(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (self == nullptr || self->type()->kind() != TypeKind::Function) {
        return ctx->createException(ctx->createString("Function.prototype.apply called on non-function"));
    }
    Value* thisArg = argc > 0 ? argv[0] : ctx->createUndefined();
    if (argc < 2) return ctx->call(self, thisArg, 0, nullptr);

    TypeKind k = argv[1]->type()->kind();
    if (k == TypeKind::Null || k == TypeKind::Undefined) {
        return ctx->call(self, thisArg, 0, nullptr);
    }
    if (!detail::isObjectKind(k)) {
        return ctx->createException(ctx->createString(
            "CreateListFromArrayLike called on non-object"));
    }

    // 读 length（ToLength，ES2024 §7.1.20）
    Value* lenValue = ctx->getProperty(argv[1], "length");
    if (detail::isErrorValue(lenValue)) return lenValue;
    Value* lenNum = detail::toNumberValue(ctx, lenValue);
    if (detail::isErrorValue(lenNum)) return lenNum;
    double rawLen = static_cast<NumberData*>(lenNum->data())->value;
    if (std::isnan(rawLen) || rawLen <= 0.0) rawLen = 0.0;
    if (rawLen > 4294967295.0) rawLen = 4294967295.0;
    uint32_t len = static_cast<uint32_t>(std::floor(rawLen));

    // 巨大 length 不做一次性 reserve（避免 bad_alloc），按需增长
    std::vector<Value*> args;
    args.reserve(len < 65536u ? len : 65536u);
    for (uint32_t i = 0; i < len; ++i) {
        Value* v = ctx->getProperty(argv[1], ctx->createString(std::to_string(i)));
        if (detail::isErrorValue(v)) return v;
        args.push_back(v);
    }
    return ctx->call(self, thisArg, args.size(), args.data());
}

// Function.prototype.bind(thisArg, ...args)（ES2024 §20.2.3.2）
// 返回绑定函数：调用时以 thisArg 为 this、前置 args 再拼接调用实参。
Value* functionProtoBind(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (self == nullptr || self->type()->kind() != TypeKind::Function) {
        return ctx->createException(ctx->createString("Function.prototype.bind called on non-function"));
    }
    Value* thisArg = argc > 0 ? argv[0] : ctx->createUndefined();
    size_t boundCount = argc > 0 ? argc - 1 : 0;
    Value** boundArgs = argc > 0 ? argv + 1 : nullptr;
    return ctx->createBoundFunction(self, thisArg, boundCount, boundArgs);
}

// —— Object / Function 构造函数 ——

// Object(value)（ES2024 §20.1.1.1）：
//   无参/undefined/null → 新建空对象；
//   已是对象（Object/Function/Array/PrimitiveWrapper）→ 原样返回；
//   原始值（Boolean/Number/String/Symbol）→ 包装对象。
Value* objectCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createObject();
    Value* v = argv[0];
    TypeKind k = v->type()->kind();
    if (k == TypeKind::Object || k == TypeKind::Function || k == TypeKind::Array ||
        k == TypeKind::PrimitiveWrapper) {
        return v;
    }
    if (k == TypeKind::Undefined || k == TypeKind::Null) return ctx->createObject();
    // 原始值 → 包装对象（Boolean/Number/String/Symbol）
    return ctx->createPrimitiveWrapper(v);
}

// Function(...)：new Function 暂不支持，调用/构造均返回 Exception
Value* functionCtor(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createException(ctx->createString("Function constructor is not implemented"));
}

// —— Object 静态方法（ES2024 §20.1.2）——

// 把键数组装成 Array Value（键已归属 current scope）
Value* keysToArray(IContext* ctx, const std::vector<Value*>& keys) {
    Value* arr = ctx->createArray();
    if (arr == ctx->oomValue()) return arr;
    auto* d = static_cast<ArrayData*>(arr->data());
    for (size_t i = 0; i < keys.size(); ++i) {
        d->setElement(static_cast<uint32_t>(i), keys[i]);
    }
    return arr;
}

// ToObject（ES2024 §7.1.18）：undefined/null 抛 TypeError，其余对象原样返回。
// 原始值包装成临时对象（Object.keys("ab") 等）。
Value* toObjectOrThrow(IContext* ctx, Value* v) {
    TypeKind k = v->type()->kind();
    if (k == TypeKind::Undefined || k == TypeKind::Null) {
        return ctx->createException(
            ctx->createString("Cannot convert undefined or null to object"));
    }
    if (detail::isObjectKind(k)) return v;
    return ctx->createPrimitiveWrapper(v);
}

// Object.keys(obj)：自有 enumerable 字符串键（ES2024 §20.1.2.17）
Value* objectKeys(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    return keysToArray(ctx, neo::core::ownPropertyKeys(ctx, target, true, false));
}

// Object.values(obj)：自有 enumerable 字符串键对应的值（ES2024 §20.1.2.23）
Value* objectValues(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    std::vector<Value*> keys = neo::core::ownPropertyKeys(ctx, target, true, false);
    Value* arr = ctx->createArray();
    if (arr == ctx->oomValue()) return arr;
    auto* d = static_cast<ArrayData*>(arr->data());
    for (size_t i = 0; i < keys.size(); ++i) {
        Value* v = ctx->getProperty(target, keys[i]);
        if (detail::isErrorValue(v)) return v;
        d->setElement(static_cast<uint32_t>(i), v);
    }
    return arr;
}

// Object.entries(obj)：自有 enumerable 字符串键的 [key, value] 数组（ES2024 §20.1.2.5）
Value* objectEntries(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    std::vector<Value*> keys = neo::core::ownPropertyKeys(ctx, target, true, false);
    Value* arr = ctx->createArray();
    if (arr == ctx->oomValue()) return arr;
    auto* d = static_cast<ArrayData*>(arr->data());
    for (size_t i = 0; i < keys.size(); ++i) {
        Value* v = ctx->getProperty(target, keys[i]);
        if (detail::isErrorValue(v)) return v;
        Value* pair = ctx->createArray();
        if (pair == ctx->oomValue()) return pair;
        auto* pd = static_cast<ArrayData*>(pair->data());
        pd->setElement(0, keys[i]);
        pd->setElement(1, v);
        d->setElement(static_cast<uint32_t>(i), pair);
    }
    return arr;
}

// Object.assign(target, ...sources)：复制自有 enumerable 属性（ES2024 §20.1.2.1）
// target 缺失/非对象由 minArgc=2 与 toObjectOrThrow 兜底。
Value* objectAssign(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    for (size_t i = 1; i < argc; ++i) {
        Value* src = argv[i];
        TypeKind sk = src->type()->kind();
        if (sk == TypeKind::Undefined || sk == TypeKind::Null) continue;  // 跳过
        Value* from = toObjectOrThrow(ctx, src);
        if (detail::isErrorValue(from)) return from;
        std::vector<Value*> keys = neo::core::ownPropertyKeys(ctx, from, true, true);
        for (Value* key : keys) {
            Value* v = ctx->getProperty(from, key);
            if (detail::isErrorValue(v)) return v;
            Value* r = ctx->setProperty(target, key, v);
            if (detail::isErrorValue(r)) return r;
        }
    }
    return target;
}

// Object.create(proto, properties)：新建对象并设置原型（ES2024 §20.1.2.2）
// properties 描述符对象本阶段不支持（第二参数非 undefined 时忽略其内容）。
Value* objectCreate(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    TypeKind pk = argv[0]->type()->kind();
    if (pk != TypeKind::Null && !detail::isObjectKind(pk)) {
        return ctx->createException(
            ctx->createString("Object prototype may only be an Object or null"));
    }
    Value* obj = ctx->createObject();
    if (obj == ctx->oomValue()) return obj;
    if (pk == TypeKind::Null) {
        static_cast<ObjectData*>(obj->data())->prototype = nullptr;
    } else {
        Value* r = ctx->setPrototype(obj, argv[0]);
        if (detail::isErrorValue(r)) return r;
    }
    return obj;
}

// Object.getPrototypeOf(obj)：ES2024 §20.1.2.12
Value* objectGetPrototypeOf(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    return ctx->getPrototype(target);
}

// Object.setPrototypeOf(obj, proto)：ES2024 §20.1.2.22
Value* objectSetPrototypeOf(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    Value* proto = argv[1];
    TypeKind pk = proto->type()->kind();
    if (pk != TypeKind::Null && !detail::isObjectKind(pk)) {
        return ctx->createException(
            ctx->createString("Object prototype may only be an Object or null"));
    }
    Value* r = ctx->setPrototype(target, proto);
    return detail::isErrorValue(r) ? r : target;
}

// Object.defineProperty(obj, key, descriptor)：ES2024 §20.1.2.4
// 描述符对象支持 value/writable/enumerable/configurable/get/set。
Value* objectDefineProperty(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    Value* key = argv[1];
    Value* desc = argv[2];
    if (!detail::isObjectKind(desc->type()->kind())) {
        return ctx->createException(
            ctx->createString("Property description must be an object"));
    }
    // 读取描述符字段（缺省按规范默认值：全 false）
    auto field = [&](const char* name) -> Value* {
        return ctx->getProperty(desc, name);
    };
    Value* getter = field("get");
    if (detail::isErrorValue(getter)) return getter;
    Value* setter = field("set");
    if (detail::isErrorValue(setter)) return setter;
    Value* enumerableV = field("enumerable");
    if (detail::isErrorValue(enumerableV)) return enumerableV;
    Value* configurableV = field("configurable");
    if (detail::isErrorValue(configurableV)) return configurableV;

    bool hasGetter = getter->type()->kind() != TypeKind::Undefined;
    bool hasSetter = setter->type()->kind() != TypeKind::Undefined;
    bool enumerable = detail::toBoolean(enumerableV);
    bool configurable = detail::toBoolean(configurableV);

    if (hasGetter || hasSetter) {
        // 访问器描述符：get/set 缺省为 undefined → 按 null 处理
        Value* g = hasGetter ? getter : nullptr;
        Value* s = hasSetter ? setter : nullptr;
        Value* r = ctx->defineAccessor(target, key, g, s, enumerable, configurable);
        return detail::isErrorValue(r) ? r : target;
    }

    Value* value = field("value");
    if (detail::isErrorValue(value)) return value;
    if (value->type()->kind() == TypeKind::Undefined) value = ctx->createUndefined();
    Value* writableV = field("writable");
    if (detail::isErrorValue(writableV)) return writableV;
    bool writable = detail::toBoolean(writableV);
    Value* r = ctx->defineProperty(target, key, value, writable, enumerable, configurable);
    return detail::isErrorValue(r) ? r : target;
}

// Object.getOwnPropertyNames(obj)：自有字符串键（含不可枚举，ES2024 §20.1.2.11）
Value* objectGetOwnPropertyNames(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                                 Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    return keysToArray(ctx, neo::core::ownPropertyKeys(ctx, target, false, false));
}

// 把值挂到 global scope：add 跟踪 + bind 名字
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->global()->add(v);
    ctx->global()->bind(name, v);
}

// 定义 prototype 方法（writable/不可枚举/可配置，与 ES 一致）
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

// 定义构造函数静态方法（writable/不可枚举/可配置，与 ES 一致）
bool defineStatic(IContext* ctx, Value* ctor, const char* name, NativeFn fn,
                  size_t minArgc) {
    return defineMethod(ctx, ctor, name, fn, minArgc);
}

}  // namespace

bool registerObjectFunctionBuiltins(IContext* ctx) {
    // —— 1. Object.prototype（裸对象，原型 null）——
    // 此时 objectPrototype_ 尚未赋值，createObject 不会自动挂链
    Value* objectProto = ctx->createObject();
    if (objectProto == ctx->oomValue()) return false;

    // —— 2. Function.prototype（函数对象，原型 = Object.prototype）——
    // 用占位 native 创建，稍后其行为由实际调用方决定（通常不会被直接调用）
    Value* functionProto = ctx->createNativeFunction(functionCtor, 0);
    if (functionProto == ctx->oomValue()) return false;
    ctx->setFunctionName(functionProto, "");
    if (ctx->setPrototype(functionProto, objectProto)->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // 原型就绪：后续 createObject/createNativeFunction 自动挂链
    ctx->setBuiltinPrototypes(objectProto, functionProto);

    // —— 3. Object 构造函数 ——
    Value* objectFn = ctx->createNativeFunction(objectCtor, 0);
    if (objectFn == ctx->oomValue()) return false;
    ctx->setFunctionName(objectFn, "Object");
    // Object.prototype 属性指向 objectProto（不可写/不可枚举/不可配置，ES2024 §20.1.2.4）
    if (ctx->defineProperty(objectFn, "prototype", objectProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // objectProto.constructor = Object
    if (ctx->defineProperty(objectProto, "constructor", objectFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 4. Function 构造函数 ——
    Value* functionFn = ctx->createNativeFunction(functionCtor, 0);
    if (functionFn == ctx->oomValue()) return false;
    ctx->setFunctionName(functionFn, "Function");
    // Function.prototype 属性指向 functionProto（不可写/不可枚举/不可配置）
    if (ctx->defineProperty(functionFn, "prototype", functionProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // functionProto.constructor = Function
    if (ctx->defineProperty(functionProto, "constructor", functionFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 5. Object.prototype 方法 ——
    if (!defineMethod(ctx, objectProto, "toString", objectProtoToString, 0)) return false;
    if (!defineMethod(ctx, objectProto, "valueOf", objectProtoValueOf, 0)) return false;
    // 三个方法缺参时按 undefined 键/值处理并返回 false（ES2024 §20.1.3），不校验最少参数
    if (!defineMethod(ctx, objectProto, "hasOwnProperty", objectProtoHasOwnProperty, 0))
        return false;
    if (!defineMethod(ctx, objectProto, "isPrototypeOf", objectProtoIsPrototypeOf, 0))
        return false;
    if (!defineMethod(ctx, objectProto, "propertyIsEnumerable",
                      objectProtoPropertyIsEnumerable, 0))
        return false;
    if (!defineMethod(ctx, objectProto, "toLocaleString", objectProtoToLocaleString, 0))
        return false;

    // —— 6. Function.prototype 方法 ——
    if (!defineMethod(ctx, functionProto, "toString", functionProtoToString, 0)) return false;
    // minArgc=0：无参调用合法（thisArg 按 undefined 处理，ES2024 §20.2.3.3）
    if (!defineMethod(ctx, functionProto, "call", functionProtoCall, 0)) return false;
    if (!defineMethod(ctx, functionProto, "apply", functionProtoApply, 0)) return false;
    if (!defineMethod(ctx, functionProto, "bind", functionProtoBind, 0)) return false;

    // —— 7. Object 静态方法（ES2024 §20.1.2）——
    if (!defineStatic(ctx, objectFn, "keys", objectKeys, 1)) return false;
    if (!defineStatic(ctx, objectFn, "values", objectValues, 1)) return false;
    if (!defineStatic(ctx, objectFn, "entries", objectEntries, 1)) return false;
    // Object.assign(target) 单参合法（无源对象），仅要求 target 存在
    if (!defineStatic(ctx, objectFn, "assign", objectAssign, 1)) return false;
    if (!defineStatic(ctx, objectFn, "create", objectCreate, 1)) return false;
    if (!defineStatic(ctx, objectFn, "getPrototypeOf", objectGetPrototypeOf, 1)) return false;
    if (!defineStatic(ctx, objectFn, "setPrototypeOf", objectSetPrototypeOf, 2)) return false;
    if (!defineStatic(ctx, objectFn, "defineProperty", objectDefineProperty, 3)) return false;
    if (!defineStatic(ctx, objectFn, "getOwnPropertyNames", objectGetOwnPropertyNames, 1))
        return false;

    // —— 8. 注册到 global ——
    bindGlobal(ctx, "Object", objectFn);
    bindGlobal(ctx, "Function", functionFn);
    return true;
}

}  // namespace neo::core
