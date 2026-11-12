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
#include "core/types/Function.h"
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
        case TypeKind::BigInt:    tag = "BigInt"; break;
        case TypeKind::String:    tag = "String"; break;
        case TypeKind::Symbol:    tag = "Symbol"; break;
        case TypeKind::Function:  tag = "Function"; break;
        case TypeKind::Object:    tag = "Object"; break;
        case TypeKind::Array:     tag = "Array"; break;
        case TypeKind::Error:     tag = "Error"; break;
        // 包装对象按 [[PrimitiveValue]] 的类型给出 tag（ES2024 §20.1.3.6）。
        // 包装对象的 primitive 恒为 Boolean/Number/BigInt/String/Symbol 之一
        // （createPrimitiveWrapper 保证）。
        case TypeKind::PrimitiveWrapper: {
            switch (wrapperPrimitiveValue(self)->type()->kind()) {
                case TypeKind::Boolean: tag = "Boolean"; break;
                case TypeKind::Number:  tag = "Number"; break;
                case TypeKind::BigInt:  tag = "BigInt"; break;
                case TypeKind::String:  tag = "String"; break;
                default:                tag = "Symbol"; break;
            }
            break;
        }
        default:                  tag = "Object"; break;
    }
    // 可调用 Proxy 的内建 tag 是 "Function"（IsCallable 判定，ES2024 §20.1.3.6）。
    if (self->type()->kind() == TypeKind::Proxy && detail::isCallable(self)) {
        tag = "Function";
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
    if (self == nullptr || !detail::isObjectKind(sk)) {
        return detail::typeError(
            ctx, "Object.prototype.hasOwnProperty called on non-object");
    }
    if (argc == 0) return ctx->createBoolean(false);
    // 经内部方法 [[GetOwnProperty]]：Proxy 触发 getOwnPropertyDescriptor trap，
    // 索引型 exotic（Array / String 包装）由默认实现覆盖。
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, argv[0], pk)) return err;
    Property desc;
    bool exists = false;
    if (Value* err = self->type()->getOwnProperty(ctx, self, pk, exists, desc)) return err;
    return ctx->createBoolean(exists);
}

// Object.prototype.isPrototypeOf(v)（ES2024 §20.1.3.3）：
// 沿 v 的原型链查找，命中 this 则 true。
Value* objectProtoIsPrototypeOf(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypeKind sk = self != nullptr ? self->type()->kind() : TypeKind::Undefined;
    if (self == nullptr || !detail::isObjectKind(sk)) {
        return detail::typeError(
            ctx, "Object.prototype.isPrototypeOf called on non-object");
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
        return detail::typeError(
            ctx, "Object.prototype.propertyIsEnumerable called on non-object");
    }
    if (argc == 0) return ctx->createBoolean(false);
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, argv[0], pk)) return err;
    Property desc;
    bool exists = false;
    if (Value* err = self->type()->getOwnProperty(ctx, self, pk, exists, desc)) return err;
    return ctx->createBoolean(exists && desc.enumerable);
}

// Object.prototype.toLocaleString()（ES2024 §20.1.3.5）：
// 调 this.toString()（toString 不可调用则 TypeError）。
Value* objectProtoToLocaleString(IContext* ctx, Value* self, size_t /*argc*/,
                                 Value* /*argv*/[]) {
    if (self == nullptr) {
        return detail::typeError(
            ctx, "Object.prototype.toLocaleString called on null");
    }
    Value* fn = ctx->getProperty(self, "toString");
    if (detail::isErrorValue(fn)) return fn;
    if (!detail::isCallable(fn)) {
        return detail::typeError(ctx, "toString is not a function");
    }
    return ctx->call(fn, self, 0, nullptr);
}

// —— Function.prototype 方法 ——

// Function.prototype.toString()（ES2024 §20.2.3.5）：
// 有源码切片时返回切片文本；native/裁剪后无源码时返回 "[native code]"。
Value* functionProtoToString(IContext* ctx, Value* self, size_t /*argc*/,
                             Value* /*argv*/[]) {
    // Proxy 没有源码表示，规范要求抛 TypeError（ES2024 §20.2.3.5 step 2）。
    if (self != nullptr && self->type()->kind() == TypeKind::Proxy) {
        return detail::typeError(ctx, "Function.prototype.toString on Proxy");
    }
    if (self != nullptr && self->type()->kind() == TypeKind::Function) {
        auto* d = static_cast<FunctionData*>(self->data());
        if (d->source.source != nullptr) {
            const std::string& text = d->source.source->text;
            size_t offset = d->source.offset < text.size() ? d->source.offset : text.size();
            size_t length = d->source.length;
            if (length > text.size() - offset) length = text.size() - offset;
            return ctx->createString(text.substr(offset, length));
        }
    }
    return ctx->createString("function () { [native code] }");
}

// Function.prototype.call(thisArg, ...args)
Value* functionProtoCall(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (!detail::isCallable(self)) {
        return detail::typeError(ctx, "Function.prototype.call called on non-function");
    }
    Value* thisArg = argc > 0 ? argv[0] : ctx->createUndefined();
    return ctx->call(self, thisArg, argc > 0 ? argc - 1 : 0, argc > 0 ? argv + 1 : nullptr);
}

// Function.prototype.apply(thisArg, argsArray)（ES2024 §20.2.3.1）
//   argsArray 为 null/undefined → 无参调用；
//   否则按 arrayLike 读 length 与各索引展开为实参（CreateListFromArrayLike）。
Value* functionProtoApply(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (!detail::isCallable(self)) {
        return detail::typeError(ctx, "Function.prototype.apply called on non-function");
    }
    Value* thisArg = argc > 0 ? argv[0] : ctx->createUndefined();
    if (argc < 2) return ctx->call(self, thisArg, 0, nullptr);

    TypeKind k = argv[1]->type()->kind();
    if (k == TypeKind::Null || k == TypeKind::Undefined) {
        return ctx->call(self, thisArg, 0, nullptr);
    }
    if (!detail::isObjectKind(k)) {
        return detail::typeError(ctx, "CreateListFromArrayLike called on non-object");
    }

    // 读 length（ToLength，ES2024 §7.1.20）
    uint32_t len = 0;
    if (Value* err = detail::lengthOf(ctx, argv[1], len)) return err;

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

// Function.prototype[@@hasInstance](V)（ES2024 §20.2.3.6）：
//   直接走 OrdinaryHasInstance（不查 @@hasInstance，避免无限递归）。
Value* functionProtoHasInstance(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* v = argc > 0 ? argv[0] : ctx->createUndefined();
    return detail::ordinaryHasInstance(ctx, self, v);
}

// Function.prototype.bind(thisArg, ...args)（ES2024 §20.2.3.2）
// 返回绑定函数：调用时以 thisArg 为 this、前置 args 再拼接调用实参。
Value* functionProtoBind(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (!detail::isCallable(self)) {
        return detail::typeError(ctx, "Function.prototype.bind called on non-function");
    }
    Value* thisArg = argc > 0 ? argv[0] : ctx->createUndefined();
    size_t boundCount = argc > 0 ? argc - 1 : 0;
    Value** boundArgs = argc > 0 ? argv + 1 : nullptr;
    return ctx->createBoundFunction(self, thisArg, boundCount, boundArgs);
}

// —— Object / Function 构造函数 ——

// Object(value)（ES2024 §20.1.1.1）：
//   无参/undefined/null → 新建空对象；
//   已是对象（含迭代器）→ 原样返回；
//   原始值（Boolean/Number/BigInt/String/Symbol）→ 包装对象。
Value* objectCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createObject();
    Value* v = argv[0];
    TypeKind k = v->type()->kind();
    if (detail::isObjectKind(k)) return v;
    if (k == TypeKind::Undefined || k == TypeKind::Null) return ctx->createObject();
    // 原始值 → 包装对象（Boolean/Number/BigInt/String/Symbol）
    return ctx->createPrimitiveWrapper(v);
}

// Function(...)：new Function 暂不支持，调用/构造均返回 Exception
Value* functionCtor(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return detail::typeError(ctx, "Function constructor is not implemented");
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
        return detail::typeError(ctx, "Cannot convert undefined or null to object");
    }
    if (detail::isObjectKind(k)) return v;
    return ctx->createPrimitiveWrapper(v);
}

// Object.keys(obj)：自有 enumerable 字符串键（ES2024 §20.1.2.17）
Value* objectKeys(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, target, true, false, keys)) return err;
    return keysToArray(ctx, keys);
}

// Object.values(obj)：自有 enumerable 字符串键对应的值（ES2024 §20.1.2.23）
Value* objectValues(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, target, true, false, keys)) return err;
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
    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, target, true, false, keys)) return err;
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
        std::vector<Value*> keys;
        if (Value* err = neo::core::ownPropertyKeys(ctx, from, true, true, keys)) return err;
        for (Value* key : keys) {
            Value* v = ctx->getProperty(from, key);
            if (detail::isErrorValue(v)) return v;
            Value* r = ctx->setProperty(target, key, v);
            if (detail::isErrorValue(r)) return r;
        }
    }
    return target;
}

// ObjectDefineProperties(obj, props)（ES2024 §20.1.2.3.1）：
// 先取 props 的全部自有键并转换描述符，再逐个定义到 obj。
Value* definePropertiesFromObject(IContext* ctx, Value* target, Value* props) {
    Value* from = toObjectOrThrow(ctx, props);
    if (detail::isErrorValue(from)) return from;
    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, from, false, true, keys)) return err;

    struct Pending {
        PropertyKey key;
        PropertyDescriptor desc;
    };
    std::vector<Pending> pending;
    pending.reserve(keys.size());
    for (Value* key : keys) {
        Value* desc = ctx->getProperty(from, key);
        if (detail::isErrorValue(desc)) return desc;
        if (!detail::isObjectKind(desc->type()->kind())) {
            return detail::typeError(ctx, "Property description must be an object");
        }
        PropertyDescriptor pd;
        if (Value* err = partialPropertyDescriptorFromValue(ctx, desc, pd)) return err;
        PropertyKey pk;
        if (Value* err = toPropertyKey(ctx, key, pk)) return err;
        pending.push_back(Pending{pk, pd});
    }
    for (const Pending& item : pending) {
        // DefinePropertyOrThrow：内部方法返回 false 时抛 TypeError。
        Value* r = target->type()->definePropertyFromDescriptor(ctx, target, item.key,
                                                                item.desc);
        if (detail::isErrorValue(r)) return r;
        if (r->type()->kind() != TypeKind::Boolean) return r;
        if (!detail::toBoolean(r)) {
            return detail::typeError(ctx, "Cannot define property");
        }
    }
    return nullptr;
}

// Object.defineProperties(obj, props)：ES2024 §20.1.2.3
Value* objectDefineProperties(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                              Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    if (Value* err = definePropertiesFromObject(ctx, target, argv[1])) return err;
    return target;
}

// Object.create(proto, properties)：新建对象并设置原型（ES2024 §20.1.2.2）
Value* objectCreate(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    TypeKind pk = argv[0]->type()->kind();
    if (pk != TypeKind::Null && !detail::isObjectKind(pk)) {
        return detail::typeError(ctx, "Object prototype may only be an Object or null");
    }
    Value* obj = ctx->createObject();
    if (obj == ctx->oomValue()) return obj;
    if (pk == TypeKind::Null) {
        static_cast<ObjectData*>(obj->data())->prototype = nullptr;
    } else {
        Value* r = ctx->setPrototype(obj, argv[0]);
        if (detail::isErrorValue(r)) return r;
    }
    if (argc > 1 && argv[1]->type()->kind() != TypeKind::Undefined) {
        if (Value* err = definePropertiesFromObject(ctx, obj, argv[1])) return err;
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
        return detail::typeError(ctx, "Object prototype may only be an Object or null");
    }
    Value* r = ctx->setPrototype(target, proto);
    return detail::isErrorValue(r) ? r : target;
}

// Object.defineProperty(obj, key, descriptor)：ES2024 §20.1.2.4
// 描述符对象支持 value/writable/enumerable/configurable/get/set。
Value* objectDefineProperty(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    PropertyKey key;
    if (Value* err = toPropertyKey(ctx, argv[1], key)) return err;
    Value* desc = argv[2];
    if (!detail::isObjectKind(desc->type()->kind())) {
        return detail::typeError(ctx, "Property description must be an object");
    }
    PropertyDescriptor pd;
    if (Value* err = partialPropertyDescriptorFromValue(ctx, desc, pd)) return err;
    Value* r = target->type()->definePropertyFromDescriptor(ctx, target, key, pd);
    if (detail::isErrorValue(r)) return r;
    if (r->type()->kind() != TypeKind::Boolean) return r;
    if (!detail::toBoolean(r)) return detail::typeError(ctx, "Cannot define property");
    return target;
}

// Object.getOwnPropertyDescriptor(obj, key)：ES2024 §20.1.2.6
Value* objectGetOwnPropertyDescriptor(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                                      Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    PropertyKey key;
    if (Value* err = toPropertyKey(ctx, argv[1], key)) return err;
    bool exists = false;
    Property prop;
    if (Value* err = target->type()->getOwnProperty(ctx, target, key, exists, prop)) return err;
    if (!exists) return ctx->createUndefined();
    return propertyDescriptorToValue(ctx, prop);
}

// Object.getOwnPropertyDescriptors(obj)：ES2024 §20.1.2.9
Value* objectGetOwnPropertyDescriptors(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                                       Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, target, false, true, keys)) return err;

    Value* result = ctx->createObject();
    if (result == ctx->oomValue()) return result;
    for (Value* key : keys) {
        PropertyKey pk;
        if (Value* err = toPropertyKey(ctx, key, pk)) return err;
        bool exists = false;
        Property prop;
        if (Value* err = target->type()->getOwnProperty(ctx, target, pk, exists, prop)) {
            return err;
        }
        if (!exists) continue;
        Value* desc = propertyDescriptorToValue(ctx, prop);
        if (detail::isErrorValue(desc)) return desc;
        Value* r = ctx->defineProperty(result, key, desc, true, true, true);
        if (detail::isErrorValue(r)) return r;
    }
    return result;
}

// Object.isExtensible(obj)：ES2024 §20.1.2.14
Value* objectIsExtensible(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = argv[0];
    if (!detail::isObjectKind(target->type()->kind())) return ctx->createBoolean(false);
    return target->type()->isExtensible(ctx, target);
}

// Object.preventExtensions(obj)：ES2024 §20.1.2.19
Value* objectPreventExtensions(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                               Value* argv[]) {
    Value* target = argv[0];
    if (!detail::isObjectKind(target->type()->kind())) return target;
    Value* r = target->type()->preventExtensions(ctx, target);
    return detail::isErrorValue(r) ? r : target;
}

// 按描述符调用 [[DefineOwnProperty]]（内部方法返回 Boolean 或 Exception）。
Value* applyPropertyDescriptor(IContext* ctx, Value* target, Value* keyValue,
                               const Property& p) {
    return isAccessorDescriptor(p)
               ? target->type()->defineAccessor(ctx, target, keyValue, p.getter, p.setter,
                                                p.enumerable, p.configurable)
               : target->type()->defineProperty(ctx, target, keyValue, p.value, p.writable,
                                                p.enumerable, p.configurable);
}

// SetIntegrityLevel（ES2024 §7.3.15）：sealed 收紧 configurable，
// frozen 再收紧数据属性的 writable。返回 Boolean 或 Exception。
Value* setIntegrityLevel(IContext* ctx, Value* target, bool frozen) {
    Value* prev = target->type()->preventExtensions(ctx, target);
    if (detail::isErrorValue(prev)) return prev;
    if (!detail::toBoolean(prev)) return ctx->createBoolean(false);

    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, target, false, true, keys)) return err;
    for (Value* key : keys) {
        PropertyKey pk;
        if (Value* err = toPropertyKey(ctx, key, pk)) return err;
        bool exists = false;
        Property cur;
        if (Value* err = target->type()->getOwnProperty(ctx, target, pk, exists, cur)) {
            return err;
        }
        if (!exists) continue;
        Property desc = cur;
        desc.configurable = false;
        if (frozen && !isAccessorDescriptor(cur)) desc.writable = false;
        Value* r = applyPropertyDescriptor(ctx, target, key, desc);
        if (detail::isErrorValue(r)) return r;
        if (!detail::toBoolean(r)) return ctx->createBoolean(false);
    }
    return ctx->createBoolean(true);
}

// TestIntegrityLevel（ES2024 §7.3.16）：返回 Boolean 或 Exception。
Value* testIntegrityLevel(IContext* ctx, Value* target, bool frozen) {
    Value* ext = target->type()->isExtensible(ctx, target);
    if (detail::isErrorValue(ext)) return ext;
    if (detail::toBoolean(ext)) return ctx->createBoolean(false);

    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, target, false, true, keys)) return err;
    for (Value* key : keys) {
        PropertyKey pk;
        if (Value* err = toPropertyKey(ctx, key, pk)) return err;
        bool exists = false;
        Property desc;
        if (Value* err = target->type()->getOwnProperty(ctx, target, pk, exists, desc)) {
            return err;
        }
        if (!exists) continue;
        if (desc.configurable) return ctx->createBoolean(false);
        if (frozen && !isAccessorDescriptor(desc) && desc.writable) {
            return ctx->createBoolean(false);
        }
    }
    return ctx->createBoolean(true);
}

// Object.seal(obj)：ES2024 §20.1.2.20
Value* objectSeal(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = argv[0];
    if (!detail::isObjectKind(target->type()->kind())) return target;
    Value* r = setIntegrityLevel(ctx, target, /*frozen=*/false);
    if (detail::isErrorValue(r)) return r;
    if (!detail::toBoolean(r)) return detail::typeError(ctx, "Cannot seal object");
    return target;
}

// Object.freeze(obj)：ES2024 §20.1.2.6
Value* objectFreeze(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = argv[0];
    if (!detail::isObjectKind(target->type()->kind())) return target;
    Value* r = setIntegrityLevel(ctx, target, /*frozen=*/true);
    if (detail::isErrorValue(r)) return r;
    if (!detail::toBoolean(r)) return detail::typeError(ctx, "Cannot freeze object");
    return target;
}

// Object.isSealed(obj)：ES2024 §20.1.2.15
Value* objectIsSealed(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = argv[0];
    if (!detail::isObjectKind(target->type()->kind())) return ctx->createBoolean(true);
    return testIntegrityLevel(ctx, target, /*frozen=*/false);
}

// Object.isFrozen(obj)：ES2024 §20.1.2.13
Value* objectIsFrozen(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = argv[0];
    if (!detail::isObjectKind(target->type()->kind())) return ctx->createBoolean(true);
    return testIntegrityLevel(ctx, target, /*frozen=*/true);
}

// Object.getOwnPropertyNames(obj)：自有字符串键（含不可枚举，ES2024 §20.1.2.11）
Value* objectGetOwnPropertyNames(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                                 Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, target, false, false, keys)) return err;
    return keysToArray(ctx, keys);
}

// 把值挂到 global scope：add 跟踪 + bind 名字
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->global()->add(v);
    ctx->global()->bind(name, v);
}

// 定义 prototype 方法（writable/不可枚举/可配置，与 ES 一致）
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc, bool strict = false) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    // 内部算法需要 Throw=true 的 Set 语义时，标在函数 closureScope 上，
    // 使其不受调用方 sloppy 影响（见 setFunctionStrict）。
    if (strict) ctx->setFunctionStrict(f);
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

// 定义构造函数静态方法（writable/不可枚举/可配置，与 ES 一致）
bool defineStatic(IContext* ctx, Value* ctor, const char* name, NativeFn fn,
                  size_t minArgc, bool strict = false) {
    return defineMethod(ctx, ctor, name, fn, minArgc, strict);
}

}  // namespace

bool registerObjectFunctionBuiltins(IContext* ctx) {
    // —— 1. Object.prototype（裸对象，原型 null）——
    // 此时 objectPrototype_ 尚未赋值，createObject 不会自动挂链
    Value* objectProto = ctx->createObject();
    if (objectProto == ctx->oomValue()) return false;

    // —— 2. Function.prototype（函数对象，原型 = Object.prototype）——
    // 用占位 native 创建，稍后其行为由实际调用方决定（通常不会被直接调用）。
    // Function.prototype 不是构造器，也没有 prototype 属性（ES2024 §20.2.3）。
    FunctionCreateOptions fprotoOpts;
    fprotoOpts.constructible = false;
    fprotoOpts.hasPrototypeProperty = false;
    Value* functionProto = ctx->createNativeFunction(functionCtor, 0, {}, fprotoOpts);
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
    // Function.prototype[@@hasInstance]（ES2024 §20.2.3.6）：
    // writable=false / 不可枚举 / configurable=false（规范明确不可配置）
    {
        Value* hasInstance = ctx->createNativeFunction(functionProtoHasInstance, 0);
        if (hasInstance == ctx->oomValue()) return false;
        ctx->setFunctionName(hasInstance, "[Symbol.hasInstance]");
        if (ctx->defineProperty(functionProto,
                                ctx->getWellKnownSymbol(WellKnownSymbol::HasInstance),
                                hasInstance, false, false, false)
                ->type()->kind() == TypeKind::Exception) {
            return false;
        }
    }

    // —— 7. Object 静态方法（ES2024 §20.1.2）——
    if (!defineStatic(ctx, objectFn, "keys", objectKeys, 1)) return false;
    if (!defineStatic(ctx, objectFn, "values", objectValues, 1)) return false;
    if (!defineStatic(ctx, objectFn, "entries", objectEntries, 1)) return false;
    // Object.assign(target) 单参合法（无源对象），仅要求 target 存在。
    // 内部 Set 使用 Throw=true，故函数局部严格，不受调用方 sloppy 影响。
    if (!defineStatic(ctx, objectFn, "assign", objectAssign, 1, /*strict=*/true))
        return false;
    if (!defineStatic(ctx, objectFn, "create", objectCreate, 1)) return false;
    if (!defineStatic(ctx, objectFn, "getPrototypeOf", objectGetPrototypeOf, 1)) return false;
    if (!defineStatic(ctx, objectFn, "setPrototypeOf", objectSetPrototypeOf, 2)) return false;
    if (!defineStatic(ctx, objectFn, "defineProperty", objectDefineProperty, 3)) return false;
    if (!defineStatic(ctx, objectFn, "getOwnPropertyDescriptor",
                      objectGetOwnPropertyDescriptor, 2))
        return false;
    if (!defineStatic(ctx, objectFn, "getOwnPropertyDescriptors",
                      objectGetOwnPropertyDescriptors, 1))
        return false;
    if (!defineStatic(ctx, objectFn, "defineProperties", objectDefineProperties, 2))
        return false;
    if (!defineStatic(ctx, objectFn, "isExtensible", objectIsExtensible, 1)) return false;
    if (!defineStatic(ctx, objectFn, "preventExtensions", objectPreventExtensions, 1))
        return false;
    if (!defineStatic(ctx, objectFn, "seal", objectSeal, 1)) return false;
    if (!defineStatic(ctx, objectFn, "freeze", objectFreeze, 1)) return false;
    if (!defineStatic(ctx, objectFn, "isSealed", objectIsSealed, 1)) return false;
    if (!defineStatic(ctx, objectFn, "isFrozen", objectIsFrozen, 1)) return false;
    if (!defineStatic(ctx, objectFn, "getOwnPropertyNames", objectGetOwnPropertyNames, 1))
        return false;

    // —— 8. 注册到 global ——
    bindGlobal(ctx, "Object", objectFn);
    bindGlobal(ctx, "Function", functionFn);
    return true;
}

}  // namespace neo::core
