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

Value* toObjectOrThrow(IContext* ctx, Value* v);

// —— Object.prototype 方法 ——

// Object.prototype.toString()（ES2024 §20.1.3.6）：
//   1. 对象先查 @@toStringTag，仅当值为原始字符串时采用；
//   2. 否则按内部槽给出内建 tag。
Value* objectProtoToString(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    if (self == nullptr) return ctx->createString("[object Undefined]");
    // 内建 tag 先于 @@toStringTag 求值（ES2024 §20.1.3.6 步骤 4-14）：
    // 若 Get 期间撤销 Proxy，IsArray 已按撤销前状态定值。
    const char* tag = "Object";
    switch (self->type()->kind()) {
        case TypeKind::Undefined: tag = "Undefined"; break;
        case TypeKind::Null:      tag = "Null"; break;
        case TypeKind::Boolean:   tag = "Boolean"; break;
        case TypeKind::Number:    tag = "Number"; break;
        case TypeKind::String:    tag = "String"; break;
        // BigInt/Symbol 不在内建 tag 列表中，其 "BigInt"/"Symbol" 由原型
        // 上的 @@toStringTag 提供（§20.1.3.6 步骤 13-14）。
        case TypeKind::Function:  tag = "Function"; break;
        case TypeKind::Object:    tag = "Object"; break;
        case TypeKind::Array:     tag = "Array"; break;
        case TypeKind::Arguments: tag = "Arguments"; break;
        case TypeKind::Error:     tag = "Error"; break;
        case TypeKind::Date:      tag = "Date"; break;
        case TypeKind::RegExp:    tag = "RegExp"; break;
        // 包装对象按 [[PrimitiveValue]] 的类型给出 tag（ES2024 §20.1.3.6）。
        // 仅 Boolean/Number/String 有内建 tag；BigInt/Symbol 落 "Object"。
        case TypeKind::PrimitiveWrapper: {
            switch (wrapperPrimitiveValue(self)->type()->kind()) {
                case TypeKind::Boolean: tag = "Boolean"; break;
                case TypeKind::Number:  tag = "Number"; break;
                case TypeKind::String:  tag = "String"; break;
                default:                tag = "Object"; break;
            }
            break;
        }
        default:                  tag = "Object"; break;
    }
    // Proxy 的内建 tag 按 target 的内部槽判定：IsArray 递归解包（§7.2.2，
    // 已撤销的 Proxy 抛 TypeError），可调用 Proxy 为 "Function"。
    if (self->type()->kind() == TypeKind::Proxy) {
        Value* target = self;
        bool isArray = false;
        while (target != nullptr && target->type()->kind() == TypeKind::Proxy) {
            if (proxyHandler(target) == nullptr) {
                return detail::typeError(ctx, "Cannot perform IsArray on a revoked proxy");
            }
            target = proxyTarget(target);
        }
        isArray = target != nullptr && target->type()->kind() == TypeKind::Array;
        if (isArray) {
            tag = "Array";
        } else if (detail::isCallable(self)) {
            tag = "Function";
        }
    }
    // @@toStringTag 覆盖内建 tag，但仅当值为原始字符串（步骤 15-16）。
    // 原始值按 ToObject 语义在其包装对象上查找（原型链）。
    Value* tagHolder = self;
    TypeKind sk = self->type()->kind();
    bool primitive = !detail::isObjectKind(sk) && sk != TypeKind::Undefined &&
                     sk != TypeKind::Null;
    if (primitive) {
        tagHolder = ctx->createPrimitiveWrapper(self);
        if (tagHolder == ctx->oomValue()) return tagHolder;
    }
    if (detail::isObjectKind(tagHolder->type()->kind())) {
        Value* tagValue = ctx->getProperty(
            tagHolder, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag));
        if (detail::isErrorValue(tagValue)) return tagValue;
        if (tagValue->type()->kind() == TypeKind::String) {
            return ctx->createString(
                "[object " + static_cast<StringData*>(tagValue->data())->value + "]");
        }
    }
    return ctx->createString(std::string("[object ") + tag + "]");
}

// Object.prototype.valueOf()（ES2024 §20.1.3.7）：返回 ToObject(this)。
Value* objectProtoValueOf(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return toObjectOrThrow(ctx, self != nullptr ? self : ctx->createUndefined());
}

// Object.prototype.hasOwnProperty(key)：只查自有属性（不含原型链）
Value* objectProtoHasOwnProperty(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    // 步骤：先 ToPropertyKey(参数)，再 ToObject(this)（§20.1.3.2）。
    PropertyKey pk;
    if (argc != 0) {
        if (Value* err = toPropertyKey(ctx, argv[0], pk)) return err;
    }
    Value* obj = toObjectOrThrow(ctx, self != nullptr ? self : ctx->createUndefined());
    if (detail::isErrorValue(obj)) return obj;
    if (argc == 0) return ctx->createBoolean(false);
    // 经内部方法 [[GetOwnProperty]]：Proxy 触发 getOwnPropertyDescriptor trap，
    // 索引型 exotic（Array / String 包装）由默认实现覆盖。
    Property desc;
    bool exists = false;
    if (Value* err = obj->type()->getOwnProperty(ctx, obj, pk, exists, desc)) return err;
    return ctx->createBoolean(exists);
}

// Object.prototype.isPrototypeOf(v)（ES2024 §20.1.3.3）：
// 沿 v 的原型链查找，命中 this 则 true。
Value* objectProtoIsPrototypeOf(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    // 步骤 1：V 非对象直接返回 false（先于 ToObject(this)，§20.1.3.3）。
    if (argc == 0 || !detail::isObjectKind(argv[0]->type()->kind())) {
        return ctx->createBoolean(false);
    }
    Value* obj = toObjectOrThrow(ctx, self != nullptr ? self : ctx->createUndefined());
    if (detail::isErrorValue(obj)) return obj;
    for (Value* cur = ctx->getPrototype(argv[0]);
         cur != nullptr && detail::isObjectKind(cur->type()->kind());
         cur = ctx->getPrototype(cur)) {
        if (cur->data() == obj->data()) return ctx->createBoolean(true);
    }
    return ctx->createBoolean(false);
}

// Object.prototype.propertyIsEnumerable(key)（ES2024 §20.1.3.4）：
// 自有属性且 enumerable 才为 true。
Value* objectProtoPropertyIsEnumerable(IContext* ctx, Value* self, size_t argc,
                                       Value* argv[]) {
    // 步骤：先 ToPropertyKey(参数)，再 ToObject(this)（§20.1.3.4）。
    PropertyKey pk;
    if (argc != 0) {
        if (Value* err = toPropertyKey(ctx, argv[0], pk)) return err;
    }
    Value* obj = toObjectOrThrow(ctx, self != nullptr ? self : ctx->createUndefined());
    if (detail::isErrorValue(obj)) return obj;
    if (argc == 0) return ctx->createBoolean(false);
    Property desc;
    bool exists = false;
    if (Value* err = obj->type()->getOwnProperty(ctx, obj, pk, exists, desc)) return err;
    return ctx->createBoolean(exists && desc.enumerable);
}

// Object.prototype.__lookupGetter__ / __lookupSetter__（Annex B.2.2.4/5）：
// 沿原型链找第一个自有属性；是访问器则返回对应函数，否则 undefined。
Value* objectProtoLookupAccessor(IContext* ctx, Value* self, size_t argc,
                                 Value* argv[], bool getter) {
    if (self == nullptr || self->type()->kind() == TypeKind::Undefined ||
        self->type()->kind() == TypeKind::Null) {
        return detail::typeError(ctx, "cannot convert null or undefined to object");
    }
    Value* obj = self;
    if (!detail::isObjectKind(obj->type()->kind())) {
        obj = ctx->createPrimitiveWrapper(obj);
        if (detail::isErrorValue(obj)) return obj;
    }
    if (argc == 0) return ctx->createUndefined();
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, argv[0], pk)) return err;
    for (Value* cur = obj;
         cur != nullptr && detail::isObjectKind(cur->type()->kind());) {
        Property desc;
        bool exists = false;
        if (Value* err = cur->type()->getOwnProperty(ctx, cur, pk, exists, desc)) {
            return err;
        }
        if (exists) {
            if (desc.getter == nullptr && desc.setter == nullptr) {
                return ctx->createUndefined();
            }
            Value* fn = getter ? desc.getter : desc.setter;
            return fn != nullptr ? fn : ctx->createUndefined();
        }
        Value* next = ctx->getPrototype(cur);
        if (detail::isErrorValue(next)) return next;
        cur = next;
    }
    return ctx->createUndefined();
}

Value* objectProtoLookupGetter(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return objectProtoLookupAccessor(ctx, self, argc, argv, true);
}

Value* objectProtoLookupSetter(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return objectProtoLookupAccessor(ctx, self, argc, argv, false);
}

// Object.prototype.__defineGetter__ / __defineSetter__（Annex B.2.2.2/3）：
// 把 callable 定义为自有访问器的一侧，保留已有另一侧。
Value* objectProtoDefineAccessor(IContext* ctx, Value* self, size_t argc,
                                 Value* argv[], bool getter) {
    Value* obj = toObjectOrThrow(ctx, self != nullptr ? self : ctx->createUndefined());
    if (detail::isErrorValue(obj)) return obj;
    // 步骤 2：先校验 callable，再 ToPropertyKey（Annex B.2.2.2/3）。
    Value* fn = argc >= 2 ? argv[1] : ctx->createUndefined();
    if (!detail::isCallable(fn)) {
        return detail::typeError(ctx, getter ? "getter is not callable"
                                             : "setter is not callable");
    }
    PropertyKey pk;
    if (argc >= 1) {
        if (Value* err = toPropertyKey(ctx, argv[0], pk)) return err;
    }
    Value* r = getter
                   ? ctx->defineAccessorPartial(obj, pk.value, fn, true, nullptr, false,
                                                true, true)
                   : ctx->defineAccessorPartial(obj, pk.value, nullptr, false, fn, true,
                                                true, true);
    if (detail::isErrorValue(r)) return r;
    return ctx->createUndefined();
}

Value* objectProtoDefineGetter(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return objectProtoDefineAccessor(ctx, self, argc, argv, true);
}

Value* objectProtoDefineSetter(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return objectProtoDefineAccessor(ctx, self, argc, argv, false);
}

// Object.prototype.__proto__ 访问器（Annex B.2.2.1）。
Value* objectProtoGetProto(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* obj = toObjectOrThrow(ctx, self != nullptr ? self : ctx->createUndefined());
    if (detail::isErrorValue(obj)) return obj;
    return ctx->getPrototype(obj);
}

Value* objectProtoSetProto(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObjectOrThrow(ctx, self != nullptr ? self : ctx->createUndefined());
    if (detail::isErrorValue(obj)) return obj;
    Value* proto = argc >= 1 ? argv[0] : ctx->createUndefined();
    TypeKind pk = proto->type()->kind();
    // 非对象且非 null：静默忽略（B.2.2.1.2 步骤 4）。
    if (!detail::isObjectKind(pk) && pk != TypeKind::Null) {
        return ctx->createUndefined();
    }
    // 步骤 4：与当前原型相同则直接成功（即使不可扩展）。
    Value* current = ctx->getPrototype(obj);
    if (detail::isErrorValue(current)) return current;
    if (current == proto ||
        (current != nullptr && proto != nullptr && current->data() == proto->data())) {
        return ctx->createUndefined();
    }
    Value* ext = obj->type()->isExtensible(ctx, obj);
    if (detail::isErrorValue(ext)) return ext;
    if (!detail::toBoolean(ext)) {
        return detail::typeError(ctx, "Cannot set prototype of non-extensible object");
    }
    Value* r = ctx->setPrototype(obj, proto);
    if (detail::isErrorValue(r)) return r;
    if (!detail::toBoolean(r)) {
        return detail::typeError(ctx, "Cyclic __proto__ value");
    }
    return ctx->createUndefined();
}

// Object.prototype.toLocaleString()（ES2024 §20.1.3.5）：
// 调 this.toString()（toString 不可调用则 TypeError）。
Value* objectProtoToLocaleString(IContext* ctx, Value* self, size_t /*argc*/,
                                 Value* /*argv*/[]) {
    // 注：规范不做 ToObject，直接以 this 原值调用其 toString（§20.1.3.5）。
    // 属性查找经 ToObject，但 getter/方法的 this 仍是原值（GetV 语义）。
    Value* obj = self != nullptr ? self : ctx->createUndefined();
    TypeKind sk = obj->type()->kind();
    if (sk == TypeKind::Undefined || sk == TypeKind::Null) {
        return detail::typeError(ctx, "Cannot convert undefined or null to object");
    }
    Value* base = obj;
    if (!detail::isObjectKind(sk)) {
        base = ctx->createPrimitiveWrapper(obj);
        if (detail::isErrorValue(base)) return base;
    }
    Value* fn = getPropertyWithReceiver(ctx, base, ctx->createString("toString"), obj);
    if (detail::isErrorValue(fn)) return fn;
    if (!detail::isCallable(fn)) {
        return detail::typeError(ctx, "toString is not a function");
    }
    return ctx->call(fn, obj, 0, nullptr);
}

// —— Function.prototype 方法 ——

// Function.prototype.toString()（ES2024 §20.2.3.5）：
// 有源码切片时返回切片文本；native/裁剪后无源码时返回 "[native code]"。
Value* functionProtoToString(IContext* ctx, Value* self, size_t /*argc*/,
                             Value* /*argv*/[]) {
    // 非可调用 this 抛 TypeError；可调用 Proxy 无源码，落到 NativeFunction 形式。
    if (self == nullptr || !detail::isCallable(self)) {
        return detail::typeError(ctx, "Function.prototype.toString called on non-function");
    }
    if (self->type()->kind() == TypeKind::Function) {
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
    // 子类化（newTarget 不是当前活动函数）时忽略参数，返回新普通对象
    // （ES2024 §20.1.1.1 步骤 1-2）。
    Value* nt = ctx->newTarget();
    if (nt != nullptr && nt->type()->kind() != TypeKind::Undefined) {
        Value* active = ctx->currentFunction();
        if (active == nullptr || nt->data() != active->data()) {
            return ctx->createObject();
        }
    }
    if (argc == 0) return ctx->createObject();
    Value* v = argv[0];
    TypeKind k = v->type()->kind();
    if (detail::isObjectKind(k)) return v;
    if (k == TypeKind::Undefined || k == TypeKind::Null) return ctx->createObject();
    // 原始值 → 包装对象（Boolean/Number/BigInt/String/Symbol）
    return ctx->createPrimitiveWrapper(v);
}

// Function(...args) / new Function(...args)（ES2024 §20.2.1.1）：
// 前 n-1 个参数是形参源码（逗号拼接），最后一个参数是函数体源码。
// 在全局环境创建，不捕获调用方作用域；严格性只由函数体自身的指令序言决定。
// Function.prototype 自身：可调用、接受任意参数、返回 undefined（§20.2.3）。
Value* functionProtoNoop(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                         Value* /*argv*/[]) {
    return ctx->createUndefined();
}

Value* functionCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    std::string params;
    std::string body;
    for (size_t i = 0; i < argc; ++i) {
        Value* text = ctx->toString(argv[i]);
        if (detail::isErrorValue(text)) return text;
        const std::string& str =
            static_cast<StringData*>(text->data())->value;
        if (i + 1 == argc) {
            body = str;
        } else {
            if (!params.empty()) params += ",";
            params += str;
        }
    }
    // 合成函数表达式后走间接 eval 路径：解析/语义/编译复用现有管线，
    // 且 root_ 固定为 GlobalScope，函数不会闭包调用方局部变量。
    std::string source =
        "(function anonymous(" + params + "\n) {\n" + body + "\n})";
    Value* src = ctx->createString(source);
    if (src == ctx->oomValue()) return src;
    return ctx->evalSource(src, nullptr, false);
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
    if (Value* err = neo::core::ownPropertyKeys(ctx, target, false, false, keys)) return err;
    Value* arr = ctx->createArray();
    if (arr == ctx->oomValue()) return arr;
    auto* d = static_cast<ArrayData*>(arr->data());
    uint32_t outIndex = 0;
    for (Value* key : keys) {
        // EnumerableOwnProperties：逐键 gopd 后立即取值（可观察顺序）。
        PropertyKey pk;
        if (Value* err = toPropertyKey(ctx, key, pk)) return err;
        bool exists = false;
        Property desc;
        if (Value* err = target->type()->getOwnProperty(ctx, target, pk, exists, desc)) {
            return err;
        }
        if (!exists || !desc.enumerable) continue;
        Value* v = ctx->getProperty(target, key);
        if (detail::isErrorValue(v)) return v;
        d->setElement(outIndex++, v);
    }
    return arr;
}

// Object.entries(obj)：自有 enumerable 字符串键的 [key, value] 数组（ES2024 §20.1.2.5）
Value* objectEntries(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, target, false, false, keys)) return err;
    Value* arr = ctx->createArray();
    if (arr == ctx->oomValue()) return arr;
    auto* d = static_cast<ArrayData*>(arr->data());
    uint32_t outIndex = 0;
    for (Value* key : keys) {
        PropertyKey pk;
        if (Value* err = toPropertyKey(ctx, key, pk)) return err;
        bool exists = false;
        Property desc;
        if (Value* err = target->type()->getOwnProperty(ctx, target, pk, exists, desc)) {
            return err;
        }
        if (!exists || !desc.enumerable) continue;
        Value* v = ctx->getProperty(target, key);
        if (detail::isErrorValue(v)) return v;
        Value* pair = ctx->createArray();
        if (pair == ctx->oomValue()) return pair;
        auto* pd = static_cast<ArrayData*>(pair->data());
        pd->setElement(0, key);
        pd->setElement(1, v);
        d->setElement(outIndex++, pair);
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
    // §20.1.2.3.1 step 5.b：只处理 [[Enumerable]] 为 true 的自有键。
    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, from, true, true, keys)) return err;

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
    if (!detail::isObjectKind(argv[0]->type()->kind())) {
        return detail::typeError(ctx, "Object.defineProperties called on non-object");
    }
    Value* target = argv[0];
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
    // Type(O) 非 Object 时直接返回 O（§20.1.2.22 步骤 1）。
    if (!detail::isObjectKind(argv[0]->type()->kind())) return argv[0];
    Value* target = argv[0];
    Value* proto = argv[1];
    TypeKind pk = proto->type()->kind();
    if (pk != TypeKind::Null && !detail::isObjectKind(pk)) {
        return detail::typeError(ctx, "Object prototype may only be an Object or null");
    }
    // %Object.prototype% 是 immutable-prototype exotic：仅允许设为 null。
    Value* objectProto = ctx->objectPrototype();
    if (objectProto != nullptr && target->data() == objectProto->data() &&
        pk != TypeKind::Null) {
        return detail::typeError(ctx, "Immutable prototype object");
    }
    Value* r = ctx->setPrototype(target, proto);
    if (detail::isErrorValue(r)) return r;
    if (!detail::toBoolean(r)) {
        return detail::typeError(ctx, "Cannot set prototype");
    }
    return target;
}

// Object.defineProperty(obj, key, descriptor)：ES2024 §20.1.2.4
// 描述符对象支持 value/writable/enumerable/configurable/get/set。
Value* objectDefineProperty(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    // 规范要求 O 已是对象（不对原始值做 ToObject，§20.1.2.4 步骤 1）。
    if (!detail::isObjectKind(argv[0]->type()->kind())) {
        return detail::typeError(ctx, "Object.defineProperty called on non-object");
    }
    Value* target = argv[0];
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
    if (detail::isErrorValue(r)) return r;
    if (!detail::toBoolean(r)) {
        return detail::typeError(ctx, "Cannot prevent extensions");
    }
    return target;
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
        // 规范只传部分描述符 { [[Configurable]]: false }（frozen 数据属性再加
        // [[Writable]]: false），Proxy trap 必须看到未出现的字段缺失。
        PropertyDescriptor pd;
        pd.hasConfigurable = true;
        pd.configurable = false;
        if (frozen && !isAccessorDescriptor(cur)) {
            pd.hasWritable = true;
            pd.writable = false;
        }
        Value* r = target->type()->definePropertyFromDescriptor(ctx, target, pk, pd);
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

// Object.is(a, b)：SameValue（ES2024 §20.1.2.12）
Value* objectIs(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* x = argc >= 1 ? argv[0] : ctx->createUndefined();
    Value* y = argc >= 2 ? argv[1] : ctx->createUndefined();
    return ctx->createBoolean(detail::sameValue(x, y));
}

// Object.hasOwn(O, P)：ToObject 后查自有属性（ES2024 §20.1.2.13.1）
Value* objectHasOwn(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    if (argc < 2) return ctx->createBoolean(false);
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, argv[1], pk)) return err;
    Property desc;
    bool exists = false;
    if (Value* err = target->type()->getOwnProperty(ctx, target, pk, exists, desc))
        return err;
    return ctx->createBoolean(exists);
}

// Object.getOwnPropertySymbols(obj)：自有 symbol 键（ES2024 §20.1.2.10）
Value* objectGetOwnPropertySymbols(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                                   Value* argv[]) {
    Value* target = toObjectOrThrow(ctx, argv[0]);
    if (detail::isErrorValue(target)) return target;
    std::vector<Value*> keys;
    if (Value* err = neo::core::ownPropertyKeys(ctx, target, false, true, keys)) return err;
    std::vector<Value*> symbols;
    for (Value* key : keys) {
        if (key->type()->kind() == TypeKind::Symbol) symbols.push_back(key);
    }
    return keysToArray(ctx, symbols);
}

// Object.groupBy(items, callbackfn)：ES2024 §20.1.2.14
Value* objectGroupBy(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc < 2 || !detail::isCallable(argv[1])) {
        return detail::typeError(ctx, "Object.groupBy callback must be callable");
    }
    TypeKind k = argv[0]->type()->kind();
    if (k == TypeKind::Undefined || k == TypeKind::Null) {
        return detail::typeError(ctx, "Object.groupBy requires an iterable");
    }
    std::vector<Value*> items;
    if (Value* err = detail::iterableToList(ctx, argv[0], items)) return err;
    // 结果对象原型为 null（§20.1.2.14 步骤 3）。
    Value* groups = ctx->createObject();
    if (groups == ctx->oomValue()) return groups;
    if (Value* err = ctx->setPrototype(groups, ctx->createNull());
        detail::isErrorValue(err)) {
        return err;
    }
    for (size_t i = 0; i < items.size(); ++i) {
        Value* args[2] = {items[i], ctx->createNumber(static_cast<double>(i))};
        Value* keyValue = ctx->call(argv[1], ctx->createUndefined(), 2, args);
        if (detail::isErrorValue(keyValue)) return keyValue;
        PropertyKey pk;
        if (Value* err = toPropertyKey(ctx, keyValue, pk)) return err;
        Value* group = ctx->getProperty(groups, pk.value);
        if (detail::isErrorValue(group)) return group;
        if (group->type()->kind() == TypeKind::Undefined) {
            group = ctx->createArray();
            if (group == ctx->oomValue()) return group;
            Value* r = ctx->defineProperty(groups, pk.value, group, true, true, true);
            if (detail::isErrorValue(r)) return r;
        }
        Value* lenV = ctx->getProperty(group, ctx->createString("length"));
        if (detail::isErrorValue(lenV)) return lenV;
        double lenD = static_cast<NumberData*>(lenV->data())->value;
        Value* r = ctx->defineProperty(
            group, ctx->createString(std::to_string(static_cast<uint32_t>(lenD))),
            items[i], true, true, true);
        if (detail::isErrorValue(r)) return r;
    }
    return groups;
}

// Object.fromEntries(iterable)：ES2024 §20.1.2.7
Value* objectFromEntries(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* argv[]) {
    TypeKind k = argv[0]->type()->kind();
    if (k == TypeKind::Undefined || k == TypeKind::Null) {
        return detail::typeError(ctx, "Object.fromEntries requires an iterable");
    }
    // 流式迭代：条目非法/取值失败时须 IteratorClose（§20.1.2.7 / §7.4.6）。
    Value* iterator = detail::getIterator(ctx, argv[0]);
    if (detail::isErrorValue(iterator)) return iterator;
    Value* result = ctx->createObject();
    if (result == ctx->oomValue()) return result;
    for (;;) {
        Value* next = detail::iteratorNext(ctx, iterator);
        if (detail::isErrorValue(next)) {
            return detail::iteratorClose(ctx, iterator, next);
        }
        Value* err = nullptr;
        bool done = detail::iteratorComplete(ctx, next, err);
        if (err != nullptr) return detail::iteratorClose(ctx, iterator, err);
        if (done) break;
        Value* entry = detail::iteratorValue(ctx, next);
        if (detail::isErrorValue(entry)) {
            return detail::iteratorClose(ctx, iterator, entry);
        }
        if (!detail::isObjectKind(entry->type()->kind())) {
            Value* typeErr = detail::typeError(ctx, "Iterator value is not an entry object");
            return detail::iteratorClose(ctx, iterator, typeErr);
        }
        Value* key = ctx->getProperty(entry, "0");
        if (detail::isErrorValue(key)) return detail::iteratorClose(ctx, iterator, key);
        Value* value = ctx->getProperty(entry, "1");
        if (detail::isErrorValue(value)) {
            return detail::iteratorClose(ctx, iterator, value);
        }
        PropertyKey pk;
        if (Value* kerr = toPropertyKey(ctx, key, pk)) {
            return detail::iteratorClose(ctx, iterator, kerr);
        }
        Value* r = ctx->defineProperty(result, pk.value, value, true, true, true);
        if (detail::isErrorValue(r)) return detail::iteratorClose(ctx, iterator, r);
    }
    return result;
}

// 把值定义为 global object 属性（全局环境记录的对象记录）。
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->defineGlobal(name, v, true, false, true);
}

// 定义 prototype 方法（writable/不可枚举/可配置，与 ES 一致）
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc, bool strict = false, int length = -1) {
    // 内建原型/静态方法不可构造且无自有 prototype 属性（§20.1.3）。
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* f = ctx->createNativeFunction(fn, minArgc, {}, opts);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    // length 与最小实参数分离：规范 length 可大于允许的最少实参。
    if (length >= 0 &&
        ctx->defineProperty(f, "length", ctx->createNumber(static_cast<double>(length)),
                            false, false, true)
                ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // 内部算法需要 Throw=true 的 Set 语义时，标在函数 closureScope 上，
    // 使其不受调用方 sloppy 影响（见 setFunctionStrict）。
    if (strict) ctx->setFunctionStrict(f);
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

// 定义构造函数静态方法（writable/不可枚举/可配置，与 ES 一致）
bool defineStatic(IContext* ctx, Value* ctor, const char* name, NativeFn fn,
                  size_t minArgc, bool strict = false, int length = -1) {
    return defineMethod(ctx, ctor, name, fn, minArgc, strict, length);
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
    Value* functionProto = ctx->createNativeFunction(functionProtoNoop, 0, {}, fprotoOpts);
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
    // Object.length = 1（§20.1.2.1），允许零实参调用。
    if (ctx->defineProperty(objectFn, "length", ctx->createNumber(1.0), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
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
    // Function.length = 1（§20.2.1.1），允许零实参调用。
    if (ctx->defineProperty(functionFn, "length", ctx->createNumber(1.0), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
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
    // 缺参时按 undefined 键/值处理并返回 false（ES2024 §20.1.3），不校验最少参数；
    // length 按规范单独给出。
    if (!defineMethod(ctx, objectProto, "hasOwnProperty", objectProtoHasOwnProperty, 0,
                      false, 1))
        return false;
    if (!defineMethod(ctx, objectProto, "isPrototypeOf", objectProtoIsPrototypeOf, 0,
                      false, 1))
        return false;
    if (!defineMethod(ctx, objectProto, "propertyIsEnumerable",
                      objectProtoPropertyIsEnumerable, 0, false, 1))
        return false;
    if (!defineMethod(ctx, objectProto, "toLocaleString", objectProtoToLocaleString, 0))
        return false;
    if (!defineMethod(ctx, objectProto, "__lookupGetter__", objectProtoLookupGetter, 0,
                      false, 1))
        return false;
    if (!defineMethod(ctx, objectProto, "__lookupSetter__", objectProtoLookupSetter, 0,
                      false, 1))
        return false;
    if (!defineMethod(ctx, objectProto, "__defineGetter__", objectProtoDefineGetter, 0,
                      false, 2))
        return false;
    if (!defineMethod(ctx, objectProto, "__defineSetter__", objectProtoDefineSetter, 0,
                      false, 2))
        return false;
    // Object.prototype.__proto__ 访问器（Annex B.2.2.1）
    {
        FunctionCreateOptions opts;
        opts.constructible = false;
        opts.hasPrototypeProperty = false;
        Value* getFn = ctx->createNativeFunction(objectProtoGetProto, 0, {}, opts);
        if (getFn == ctx->oomValue()) return false;
        ctx->setFunctionName(getFn, "get __proto__");
        Value* setFn = ctx->createNativeFunction(objectProtoSetProto, 0, {}, opts);
        if (setFn == ctx->oomValue()) return false;
        ctx->setFunctionName(setFn, "set __proto__");
        if (ctx->defineProperty(setFn, "length", ctx->createNumber(1.0), false, false, true)
                ->type()->kind() == TypeKind::Exception) {
            return false;
        }
        if (detail::isErrorValue(ctx->defineAccessor(objectProto, "__proto__", getFn,
                                                     setFn, false, true))) {
            return false;
        }
    }

    // —— 6. Function.prototype 方法 ——
    if (!defineMethod(ctx, functionProto, "toString", functionProtoToString, 0)) return false;
    // minArgc=0：无参调用合法（thisArg 按 undefined 处理，ES2024 §20.2.3.3）
    if (!defineMethod(ctx, functionProto, "call", functionProtoCall, 0, false, 1))
        return false;
    if (!defineMethod(ctx, functionProto, "apply", functionProtoApply, 0, false, 2))
        return false;
    if (!defineMethod(ctx, functionProto, "bind", functionProtoBind, 0, false, 1))
        return false;
    // Function.prototype[@@hasInstance]（ES2024 §20.2.3.6）：
    // writable=false / 不可枚举 / configurable=false（规范明确不可配置）
    {
        FunctionCreateOptions hiOpts;
        hiOpts.constructible = false;
        hiOpts.hasPrototypeProperty = false;
        Value* hasInstance =
            ctx->createNativeFunction(functionProtoHasInstance, 0, {}, hiOpts);
        if (hasInstance == ctx->oomValue()) return false;
        ctx->setFunctionName(hasInstance, "[Symbol.hasInstance]");
        if (ctx->defineProperty(hasInstance, "length", ctx->createNumber(1.0), false,
                                false, true)
                ->type()->kind() == TypeKind::Exception) {
            return false;
        }
        if (ctx->defineProperty(functionProto,
                                ctx->getWellKnownSymbol(WellKnownSymbol::HasInstance),
                                hasInstance, false, false, false)
                ->type()->kind() == TypeKind::Exception) {
            return false;
        }
    }

    // Function.prototype.caller / arguments：受限访问器，读写均抛 TypeError。
    {
        Value* thrower = ctx->throwTypeError();
        if (thrower == ctx->oomValue()) return false;
        if (ctx->defineAccessor(functionProto, "caller", thrower, thrower, false,
                                true)
                ->type()->kind() == TypeKind::Exception) {
            return false;
        }
        if (ctx->defineAccessor(functionProto, "arguments", thrower, thrower,
                                false, true)
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
    if (!defineStatic(ctx, objectFn, "assign", objectAssign, 1, /*strict=*/true, 2))
        return false;
    if (!defineStatic(ctx, objectFn, "create", objectCreate, 1, false, 2)) return false;
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
    if (!defineStatic(ctx, objectFn, "getOwnPropertySymbols",
                      objectGetOwnPropertySymbols, 1))
        return false;
    if (!defineStatic(ctx, objectFn, "is", objectIs, 0, false, 2)) return false;
    if (!defineStatic(ctx, objectFn, "hasOwn", objectHasOwn, 2)) return false;
    if (!defineStatic(ctx, objectFn, "fromEntries", objectFromEntries, 1)) return false;
    if (!defineStatic(ctx, objectFn, "groupBy", objectGroupBy, 0, false, 2)) return false;

    // —— 8. 注册到 global ——
    bindGlobal(ctx, "Object", objectFn);
    bindGlobal(ctx, "Function", functionFn);
    return true;
}

}  // namespace neo::core
