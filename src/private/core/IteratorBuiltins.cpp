// IteratorBuiltins.cpp - Iterator 抽象类与数组迭代器内建注册实现
//
// bootstrap 顺序（依赖 Object/Function/Array 内建已就绪）：
//   1. %Iterator.prototype%（普通对象，原型 = Object.prototype）
//   2. Iterator 构造函数（抽象类：call/construct 均抛 TypeError）
//   3. %ArrayIteratorPrototype%（普通对象，原型 = %Iterator.prototype%）
//   4. %WrapForValidIteratorPrototype%（普通对象，原型 = %Iterator.prototype%）
//   5. setIteratorBuiltins —— 此后 createArrayIterator / createIteratorWrapper 自动挂链
//   6. Iterator.prototype 上的方法/访问器、Iterator.from
//   7. Array.prototype 的 keys/values/entries/@@iterator
//
// 规范要点：
//   - Iterator.prototype.constructor 是访问器（getter 返回 Iterator，setter 在对象上
//     建自有属性），ES2025 §27.1.3.1。
//   - Iterator.prototype[@@toStringTag] 是 getter，返回 "Iterator"（§27.1.3.4）。
//   - Iterator.prototype[@@iterator] 返回 this（§27.1.3.3）。
//   - Iterator.prototype[@@dispose] 调 this.return()（无 return 则什么都不做）（§27.1.3.2）。
//   - Iterator.from(v)：v 已是"真迭代器"（原型链含 %Iterator.prototype%）则原样返回；
//     否则 ToObject → 取 @@iterator（可调用则调用）→ 取 next → 包成
//     %WrapForValidIteratorPrototype% 实例（§27.1.4.2）。
//   - %ArrayIteratorPrototype%.next：live 读取数组 length 与元素（§23.1.5.2.1）。
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）

#include "core/IteratorBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Iterator.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

namespace {

// 建一个迭代器结果对象 { value, done }（ES2024 §7.4.1 CreateIterResultObject）。
// 两个属性均为 writable/enumerable/configurable 的数据属性。
Value* createIterResult(IContext* ctx, Value* value, bool done) {
    Value* result = ctx->createObject();
    if (result == ctx->oomValue()) return result;
    Value* r = ctx->defineProperty(result, "value", value, true, true, true);
    if (detail::isErrorValue(r)) return r;
    r = ctx->defineProperty(result, "done", ctx->createBoolean(done), true, true, true);
    return detail::isErrorValue(r) ? r : result;
}

// 把数组索引读成字符串键（数字索引 → "0"/"1"/...）
Value* indexKey(IContext* ctx, uint32_t index) {
    return ctx->createString(std::to_string(index));
}

// 原型链中是否包含 %Iterator.prototype%（ES2025 §27.1.4.2 step 2 的判定）。
bool inheritsIteratorPrototype(IContext* ctx, Value* v) {
    Value* proto = ctx->iteratorPrototype();
    if (proto == nullptr) return false;
    for (Value* cur = v; cur != nullptr && detail::isObjectKind(cur->type()->kind());) {
        Value* next = ctx->getPrototype(cur);
        if (detail::isErrorValue(next)) return false;
        if (next->type()->kind() == TypeKind::Null) return false;
        if (next == proto) return true;
        cur = next;
    }
    return false;
}

// —— %ArrayIteratorPrototype%.next（ES2024 §23.1.5.2.1）——
// live 语义：每次重新读数组 length 与元素；耗尽后置 iteratedObject=null 并返回 {done:true}。
Value* arrayIteratorNext(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    if (self == nullptr || self->type()->kind() != TypeKind::ArrayIterator) {
        return detail::typeError(ctx, "next method called on incompatible receiver");
    }
    auto* d = static_cast<ArrayIteratorData*>(self->data());
    Value* target = d->iteratedObject;
    if (target == nullptr) {
        return createIterResult(ctx, ctx->createUndefined(), true);
    }
    uint32_t len = 0;
    if (Value* err = detail::lengthOf(ctx, target, len)) return err;
    if (d->nextIndex >= len) {
        d->iteratedObject = nullptr;  // 已耗尽：释放对数组的强引用
        return createIterResult(ctx, ctx->createUndefined(), true);
    }
    uint32_t index = d->nextIndex;
    d->nextIndex = index + 1;
    Value* value = nullptr;
    switch (d->kind) {
        case ArrayIterationKind::Key:
            return createIterResult(ctx, ctx->createNumber(static_cast<double>(index)),
                                    false);
        case ArrayIterationKind::Value:
            value = ctx->getProperty(target, indexKey(ctx, index));
            if (detail::isErrorValue(value)) return value;
            return createIterResult(ctx, value, false);
        case ArrayIterationKind::KeyAndValue: {
            Value* v = ctx->getProperty(target, indexKey(ctx, index));
            if (detail::isErrorValue(v)) return v;
            Value* pair = ctx->createArray();
            if (pair == ctx->oomValue()) return pair;
            auto* pd = static_cast<ArrayData*>(pair->data());
            pd->setElement(0, ctx->createNumber(static_cast<double>(index)));
            pd->setElement(1, v);
            return createIterResult(ctx, pair, false);
        }
    }
    return createIterResult(ctx, ctx->createUndefined(), true);
}

// —— %WrapForValidIteratorPrototype%.next（ES2025 §27.1.4.2.1）——
// 以 [[Iterated]] 为 this 调其 next 方法；结果必须是对象，否则 TypeError。
Value* wrapForValidIteratorNext(IContext* ctx, Value* self, size_t /*argc*/,
                                Value* /*argv*/[]) {
    IteratorWrapperData* d = asIteratorWrapper(self);
    if (d == nullptr || d->iterated == nullptr || d->nextMethod == nullptr) {
        return detail::typeError(ctx, "next method called on incompatible receiver");
    }
    Value* result = ctx->call(d->nextMethod, d->iterated, 0, nullptr);
    if (detail::isErrorValue(result)) return result;
    if (!detail::isObjectKind(result->type()->kind())) {
        return detail::typeError(ctx, "Iterator result is not an object");
    }
    return result;
}

// —— %WrapForValidIteratorPrototype%.return（ES2025 §27.1.4.2.2）——
// 调 [[Iterated]].return()（无参数）；无 return 方法则返回 {value: undefined, done: true}。
Value* wrapForValidIteratorReturn(IContext* ctx, Value* self, size_t /*argc*/,
                                  Value* /*argv*/[]) {
    IteratorWrapperData* d = asIteratorWrapper(self);
    if (d == nullptr || d->iterated == nullptr) {
        return detail::typeError(ctx, "return method called on incompatible receiver");
    }
    Value* ret = ctx->getProperty(d->iterated, "return");
    if (detail::isErrorValue(ret)) return ret;
    if (ret->type()->kind() == TypeKind::Undefined || ret->type()->kind() == TypeKind::Null) {
        return createIterResult(ctx, ctx->createUndefined(), true);
    }
    if (ret->type()->kind() != TypeKind::Function) {
        return detail::typeError(ctx, "Iterator return is not a function");
    }
    Value* result = ctx->call(ret, d->iterated, 0, nullptr);
    if (detail::isErrorValue(result)) return result;
    if (!detail::isObjectKind(result->type()->kind())) {
        return detail::typeError(ctx, "Iterator result is not an object");
    }
    return result;
}

// —— Iterator 构造函数（ES2025 §27.1.2）——
// 抽象类：call 报 "requires new"，construct 报不可直接构造。
Value* iteratorCtorFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return detail::typeError(ctx, "Constructor Iterator requires 'new'");
}

Value* iteratorConstructFn(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                           Value* /*argv*/[]) {
    return detail::typeError(ctx, "Abstract class Iterator not directly constructable");
}

// —— Iterator.from(v)（ES2025 §27.1.4.2）——
Value* iteratorFrom(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* v = argc > 0 ? argv[0] : ctx->createUndefined();
    // 1. 非对象：仅 String 允许 ToObject，其余（含 undefined/null）抛 TypeError
    TypeKind k = v->type()->kind();
    Value* obj = nullptr;
    if (detail::isObjectKind(k)) {
        obj = v;
    } else if (k == TypeKind::String) {
        obj = ctx->createPrimitiveWrapper(v);
        if (detail::isErrorValue(obj)) return obj;
    } else {
        return detail::typeError(ctx, "Iterator.from called on non-object");
    }

    // 2. 取 @@iterator；可调用则调用（结果须为对象）
    Value* method = ctx->getProperty(
        obj, ctx->getWellKnownSymbol(WellKnownSymbol::Iterator));
    if (detail::isErrorValue(method)) return method;
    if (method->type()->kind() != TypeKind::Undefined &&
        method->type()->kind() != TypeKind::Null) {
        if (method->type()->kind() != TypeKind::Function) {
            return detail::typeError(ctx, "Iterator.from: @@iterator is not a function");
        }
        Value* iter = ctx->call(method, obj, 0, nullptr);
        if (detail::isErrorValue(iter)) return iter;
        if (!detail::isObjectKind(iter->type()->kind())) {
            return detail::typeError(ctx, "Iterator.from: @@iterator did not return an object");
        }
        obj = iter;
    }

    // 3. 已是真迭代器（原型链含 %Iterator.prototype%）→ 原样返回
    if (inheritsIteratorPrototype(ctx, obj)) return obj;

    // 4. 否则取 next 并包装（next 缺失时包装对象的 next 调用会报错，与规范一致）
    Value* nextMethod = ctx->getProperty(obj, "next");
    if (detail::isErrorValue(nextMethod)) return nextMethod;
    return ctx->createIteratorWrapper(obj, nextMethod);
}

// —— Iterator.prototype 上的访问器与方法 ——

// constructor 访问器 getter：返回 Iterator 构造函数
Value* iteratorProtoConstructorGet(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                                   Value* /*argv*/[]) {
    return ctx->iteratorCtor();
}

// constructor 访问器 setter：在 this 上建自有数据属性
Value* iteratorProtoConstructorSet(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (!detail::isObjectKind(self->type()->kind())) {
        return detail::typeError(ctx, "set Iterator.prototype.constructor called on non-object");
    }
    // minArgc=1 保证 argc >= 1（缺参由 FunctionType 拦截）
    (void)argc;
    Value* r = ctx->defineProperty(self, "constructor", argv[0], true, false, true);
    return detail::isErrorValue(r) ? r : ctx->createUndefined();
}

// @@toStringTag 访问器 getter：返回 "Iterator"
Value* iteratorProtoTagGet(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                           Value* /*argv*/[]) {
    return ctx->createString("Iterator");
}

// @@iterator：返回 this
Value* iteratorProtoIterator(IContext* ctx, Value* self, size_t /*argc*/,
                             Value* /*argv*/[]) {
    return self != nullptr ? self : ctx->createUndefined();
}

// @@dispose：调 this.return()（无 return 则什么都不做），返回 undefined
Value* iteratorProtoDispose(IContext* ctx, Value* self, size_t /*argc*/,
                            Value* /*argv*/[]) {
    if (self == nullptr || !detail::isObjectKind(self->type()->kind())) {
        return detail::typeError(ctx, "Iterator.prototype[Symbol.dispose] called on non-object");
    }
    // IteratorClose 已封装「取 return → 校验可调用 → 调用 → 结果须为对象」，
    // 且无 return 时返回 nullptr（此处忽略返回值，统一返回 undefined）。
    Value* r = detail::iteratorClose(ctx, self, nullptr);
    return (r != nullptr && detail::isErrorValue(r)) ? r : ctx->createUndefined();
}

// —— Array.prototype 迭代器方法 ——

// Array.prototype.keys / values / entries：创建对应 kind 的迭代器。
// this 走 ToObject（"ab".keys() 可用）；undefined/null 抛 TypeError。
Value* arrayIteratorMethod(IContext* ctx, Value* self, ArrayIterationKind kind) {
    TypeKind k = self != nullptr ? self->type()->kind() : TypeKind::Undefined;
    if (self == nullptr || k == TypeKind::Undefined || k == TypeKind::Null) {
        return detail::typeError(ctx, "Array.prototype iterator method called on null or undefined");
    }
    Value* obj = detail::isObjectKind(k) ? self : ctx->createPrimitiveWrapper(self);
    if (detail::isErrorValue(obj)) return obj;
    return ctx->createArrayIterator(obj, kind);
}

Value* arrayKeys(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return arrayIteratorMethod(ctx, self, ArrayIterationKind::Key);
}

Value* arrayValues(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return arrayIteratorMethod(ctx, self, ArrayIterationKind::Value);
}

Value* arrayEntries(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return arrayIteratorMethod(ctx, self, ArrayIterationKind::KeyAndValue);
}

// 把值挂到 global scope：add 跟踪 + bind 名字
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->global()->add(v);
    ctx->global()->bind(name, v);
}

// 定义方法（writable/不可枚举/可配置，与 ES 一致）
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

// 定义以 symbol 为键的方法
bool defineSymbolMethod(IContext* ctx, Value* target, WellKnownSymbol which,
                        const char* name, NativeFn fn, size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, ctx->getWellKnownSymbol(which), f, true, false,
                               true)
               ->type()->kind() != TypeKind::Exception;
}

}  // namespace

bool registerIteratorBuiltins(IContext* ctx) {
    // 各步骤互相独立，用按位与累积失败标记（不短路）：任何一步 OOM/定义失败
    // 都在末尾统一返回 false，避免为每个步骤写一个不可测的提前返回分支。
    bool ok = true;

    // —— 1. %Iterator.prototype%（普通对象，原型 = Object.prototype）——
    Value* iteratorProto = ctx->createObject();
    ok &= iteratorProto != ctx->oomValue();
    if (!ok) return false;
    ok &= ctx->setPrototype(iteratorProto, ctx->objectPrototype())->type()->kind() !=
          TypeKind::Exception;

    // —— 2. Iterator 构造函数（抽象类）——
    Value* iteratorFn = ctx->createNativeFunction(iteratorCtorFn, 0);
    ok &= iteratorFn != ctx->oomValue();
    if (!ok) return false;
    ctx->setFunctionName(iteratorFn, "Iterator");
    // new 走专用钩子（报抽象类不可构造）
    ctx->setFunctionConstructor(iteratorFn, iteratorConstructFn);
    // Iterator.prototype 属性指向 iteratorProto（不可写/不可枚举/不可配置）
    ok &= ctx->defineProperty(iteratorFn, "prototype", iteratorProto, false, false, false)
              ->type()->kind() != TypeKind::Exception;

    // —— 3. %ArrayIteratorPrototype%（原型 = %Iterator.prototype%）——
    Value* arrayIterProto = ctx->createObject();
    ok &= arrayIterProto != ctx->oomValue();
    if (!ok) return false;
    ok &= ctx->setPrototype(arrayIterProto, iteratorProto)->type()->kind() !=
          TypeKind::Exception;

    // —— 4. %WrapForValidIteratorPrototype%（原型 = %Iterator.prototype%）——
    Value* wrapperProto = ctx->createObject();
    ok &= wrapperProto != ctx->oomValue();
    if (!ok) return false;
    ok &= ctx->setPrototype(wrapperProto, iteratorProto)->type()->kind() !=
          TypeKind::Exception;

    // —— 5. 原型就绪：此后 createArrayIterator / createIteratorWrapper 自动挂链 ——
    ctx->setIteratorBuiltins(iteratorProto, iteratorFn, arrayIterProto, wrapperProto);

    // —— 6. Iterator.prototype 成员 ——
    // constructor 访问器（ES2025 §27.1.3.1）：getter 返回 Iterator，setter 建自有属性
    Value* ctorGet = ctx->createNativeFunction(iteratorProtoConstructorGet, 0);
    ok &= ctorGet != ctx->oomValue();
    if (!ok) return false;
    ctx->setFunctionName(ctorGet, "get constructor");
    Value* ctorSet = ctx->createNativeFunction(iteratorProtoConstructorSet, 1);
    ok &= ctorSet != ctx->oomValue();
    if (!ok) return false;
    ctx->setFunctionName(ctorSet, "set constructor");
    ok &= ctx->defineAccessor(iteratorProto, "constructor", ctorGet, ctorSet, false, true)
              ->type()->kind() != TypeKind::Exception;

    // @@toStringTag 访问器（§27.1.3.4）：getter 返回 "Iterator"
    Value* tagGet = ctx->createNativeFunction(iteratorProtoTagGet, 0);
    ok &= tagGet != ctx->oomValue();
    if (!ok) return false;
    ctx->setFunctionName(tagGet, "get [Symbol.toStringTag]");
    ok &= ctx->defineAccessor(iteratorProto,
                              ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                              tagGet, nullptr, false, true)
              ->type()->kind() != TypeKind::Exception;

    // @@iterator（§27.1.3.3）：返回 this
    ok &= defineSymbolMethod(ctx, iteratorProto, WellKnownSymbol::Iterator,
                             "[Symbol.iterator]", iteratorProtoIterator, 0);
    // @@dispose（§27.1.3.2）：调 this.return()
    ok &= defineSymbolMethod(ctx, iteratorProto, WellKnownSymbol::Dispose,
                             "[Symbol.dispose]", iteratorProtoDispose, 0);

    // Iterator.from（§27.1.4.2）
    ok &= defineMethod(ctx, iteratorFn, "from", iteratorFrom, 1);

    // —— 7. %ArrayIteratorPrototype% 成员 ——
    ok &= defineMethod(ctx, arrayIterProto, "next", arrayIteratorNext, 0);
    // @@toStringTag = "Array Iterator"（不可写/不可枚举/可配置）
    ok &= ctx->defineProperty(arrayIterProto,
                              ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                              ctx->createString("Array Iterator"), false, false, true)
              ->type()->kind() != TypeKind::Exception;

    // —— 8. %WrapForValidIteratorPrototype% 成员 ——
    ok &= defineMethod(ctx, wrapperProto, "next", wrapForValidIteratorNext, 0);
    ok &= defineMethod(ctx, wrapperProto, "return", wrapForValidIteratorReturn, 0);

    // —— 9. Array.prototype 迭代器方法 ——
    Value* arrayProto = ctx->arrayPrototype();
    if (arrayProto == nullptr) return false;
    ok &= defineMethod(ctx, arrayProto, "keys", arrayKeys, 0);
    ok &= defineMethod(ctx, arrayProto, "values", arrayValues, 0);
    ok &= defineMethod(ctx, arrayProto, "entries", arrayEntries, 0);
    // Array.prototype[@@iterator] === Array.prototype.values（§23.1.3.39）
    Value* valuesFn = ctx->getProperty(arrayProto, "values");
    ok &= !detail::isErrorValue(valuesFn);
    if (!ok) return false;
    ok &= ctx->defineProperty(arrayProto, ctx->getWellKnownSymbol(WellKnownSymbol::Iterator),
                              valuesFn, true, false, true)
              ->type()->kind() != TypeKind::Exception;

    // —— 10. 注册到 global ——
    bindGlobal(ctx, "Iterator", iteratorFn);
    return ok;
}

}  // namespace neo::core
