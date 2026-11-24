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

#include "unicode/Utf8.h"

#include <cmath>
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
        if (next->data() == proto->data()) return true;
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

// —— %StringIteratorPrototype%.next（ES2024 §22.1.5.2.1）——
// 按码点迭代：每次解码一个 UTF-8 码点（代理对视为一个字符）。
Value* stringIteratorNext(IContext* ctx, Value* self, size_t /*argc*/,
                          Value* /*argv*/[]) {
    if (self == nullptr || self->type()->kind() != TypeKind::StringIterator) {
        return detail::typeError(ctx,
                                 "String Iterator.prototype.next called on incompatible receiver");
    }
    auto* d = static_cast<StringIteratorData*>(self->data());
    if (d->iteratedString == nullptr) {
        return createIterResult(ctx, ctx->createUndefined(), true);
    }
    const std::string& source =
        static_cast<StringData*>(d->iteratedString->data())->value;
    if (d->nextIndex >= source.size()) {
        return createIterResult(ctx, ctx->createUndefined(), true);
    }

    size_t index = d->nextIndex;
    uint32_t codePoint = 0;
    if (!unicode::decodeCodePoint(source, index, codePoint)) {
        // 非法字节：按单字节前进，避免死循环。
        index = static_cast<size_t>(d->nextIndex) + 1;
        codePoint = static_cast<unsigned char>(source[d->nextIndex]);
    }
    d->nextIndex = static_cast<uint32_t>(index);

    std::string text;
    unicode::encodeCodePoint(codePoint, text);
    return createIterResult(ctx, ctx->createString(text), false);
}

// String.prototype[@@iterator]（ES2024 §22.1.3.36）：返回字符串迭代器。
Value* stringProtoIterator(IContext* ctx, Value* self, size_t /*argc*/,
                           Value* /*argv*/[]) {
    if (self == nullptr ||
        self->type()->kind() == TypeKind::Undefined ||
        self->type()->kind() == TypeKind::Null) {
        return detail::typeError(ctx,
                                 "String.prototype[Symbol.iterator] called on null or undefined");
    }
    Value* string = detail::toStringValue(ctx, self);
    if (detail::isErrorValue(string)) return string;
    return ctx->createStringIterator(string);
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
    // §27.1.2：NewTarget 为 %Iterator% 本身（或缺失）时不可直接构造；
    // 子类 super() 走 OrdinaryCreateFromConstructor(NewTarget)。
    // 构造钩子的 self 实参为 nullptr（thisArg），故用 ctx->iteratorCtor() 比较。
    Value* nt = ctx->newTarget();
    Value* ctor = ctx->iteratorCtor();
    if (nt == nullptr || nt->type()->kind() == TypeKind::Undefined ||
        ctor == nullptr || nt->data() == ctor->data()) {
        return detail::typeError(ctx,
                                 "Abstract class Iterator not directly constructable");
    }
    Value* proto = ctx->getProperty(nt, "prototype");
    if (detail::isErrorValue(proto)) return proto;
    Value* obj = ctx->createObject();
    if (obj == ctx->oomValue()) return obj;
    if (detail::isObjectKind(proto->type()->kind())) {
        Value* r = ctx->setPrototype(obj, proto);
        if (detail::isErrorValue(r)) return r;
    }
    return obj;
}

// —— Iterator.from(v)（ES2025 §27.1.4.2）——
Value* iteratorFrom(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* v = argc > 0 ? argv[0] : ctx->createUndefined();
    // 1. 非对象：仅 String 允许（保持原始值作为 receiver，属性查找自动装箱），
    //    其余（含 number/undefined/null）抛 TypeError。
    TypeKind k = v->type()->kind();
    Value* obj = nullptr;
    if (detail::isObjectKind(k)) {
        obj = v;
    } else if (k == TypeKind::String) {
        obj = v;
    } else {
        return detail::typeError(ctx, "Iterator.from called on non-object");
    }

    // 2. 取 @@iterator；可调用则调用（结果须为对象）。
    //    GetV：base 是 ToObject(V)，receiver 保持原始值（getter 的 this）。
    Value* base = obj;
    if (!detail::isObjectKind(k)) {
        base = ctx->createPrimitiveWrapper(obj);
        if (detail::isErrorValue(base)) return base;
    }
    Value* method = getPropertyWithReceiver(
        ctx, base, ctx->getWellKnownSymbol(WellKnownSymbol::Iterator), obj);
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

// —— Iterator 辅助方法（ES2025 §27.1.3.3）——

Value* requireIteratorObject(IContext* ctx, Value* self, const char* method) {
    if (self == nullptr || !detail::isObjectKind(self->type()->kind())) {
        return detail::typeError(ctx, std::string("Iterator.prototype.") + method +
                                          " called on non-object");
    }
    return nullptr;
}

// GetIteratorDirect：取 next 方法；错误返回错误值，成功返回 nullptr。
Value* directNextMethod(IContext* ctx, Value* obj, Value*& nextMethod) {
    nextMethod = ctx->getProperty(obj, "next");
    if (detail::isErrorValue(nextMethod)) return nextMethod;
    return nullptr;
}

// IteratorStepValue：done 置 true 并返回 nullptr；错误写入 err；否则返回 value。
Value* stepValue(IContext* ctx, Value* iterator, Value* nextMethod, bool& done,
                 Value*& err) {
    done = false;
    err = nullptr;
    Value* result = ctx->call(nextMethod, iterator, 0, nullptr);
    if (detail::isErrorValue(result)) {
        err = result;
        return nullptr;
    }
    if (!detail::isObjectKind(result->type()->kind())) {
        err = detail::typeError(ctx, "Iterator result is not an object");
        return nullptr;
    }
    Value* doneValue = ctx->getProperty(result, "done");
    if (detail::isErrorValue(doneValue)) {
        err = doneValue;
        return nullptr;
    }
    if (detail::toBoolean(doneValue)) {
        done = true;
        return nullptr;
    }
    Value* value = ctx->getProperty(result, "value");
    if (detail::isErrorValue(value)) {
        err = value;
        return nullptr;
    }
    return value;
}

Value* helperFinish(IContext* ctx, IteratorHelperData* d) {
    d->done = true;
    d->underlying = nullptr;
    d->nextMethod = nullptr;
    d->innerIterator = nullptr;
    d->innerNext = nullptr;
    return createIterResult(ctx, ctx->createUndefined(), true);
}

// 步进底层迭代器本身出错：辅助迭代器完成，但不关闭底层（§27.1.3.3 的
// IteratorHelper 是生成器，步进异常直接传播，不经 IfAbruptCloseIterator）。
void helperAbort(IteratorHelperData* d) {
    d->done = true;
    d->underlying = nullptr;
    d->nextMethod = nullptr;
    d->innerIterator = nullptr;
    d->innerNext = nullptr;
}

// 内层迭代器步进出错：按 IfAbruptCloseIterator 关闭外层底层，但不关闭内层。
Value* helperFailUnderlying(IContext* ctx, IteratorHelperData* d, Value* error) {
    d->done = true;
    d->innerIterator = nullptr;
    d->innerNext = nullptr;
    if (d->underlying != nullptr) {
        detail::iteratorClose(ctx, d->underlying, error);
        d->underlying = nullptr;
        d->nextMethod = nullptr;
    }
    return error;
}

// 回调（mapper/predicate）出错：关闭底层迭代器后返回原错误。
Value* helperFail(IContext* ctx, IteratorHelperData* d, Value* error) {
    d->done = true;
    if (d->innerIterator != nullptr) {
        detail::iteratorClose(ctx, d->innerIterator, nullptr);
        d->innerIterator = nullptr;
        d->innerNext = nullptr;
    }
    if (d->underlying != nullptr) {
        detail::iteratorClose(ctx, d->underlying, error);
    }
    return error;
}

Value* iteratorHelperCreate(IContext* ctx, Value* self, IteratorHelperKind kind,
                            Value* callback, double counter) {
    Value* nextMethod = nullptr;
    if (Value* err = directNextMethod(ctx, self, nextMethod)) return err;
    return ctx->createIteratorHelper(self, nextMethod, kind, callback, counter);
}

Value* iteratorMap(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "map")) return err;
    Value* mapper = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(mapper)) {
        // 参数校验失败：先关闭底层迭代器（不读 next），再抛 TypeError。
        return detail::iteratorClose(
            ctx, self,
            detail::typeError(ctx, "Iterator.prototype.map mapper must be callable"));
    }
    return iteratorHelperCreate(ctx, self, IteratorHelperKind::Map, mapper, 0.0);
}

Value* iteratorFilter(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "filter")) return err;
    Value* predicate = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(predicate)) {
        return detail::iteratorClose(
            ctx, self,
            detail::typeError(ctx,
                              "Iterator.prototype.filter predicate must be callable"));
    }
    return iteratorHelperCreate(ctx, self, IteratorHelperKind::Filter, predicate, 0.0);
}

Value* iteratorFlatMap(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "flatMap")) return err;
    Value* mapper = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(mapper)) {
        return detail::iteratorClose(
            ctx, self,
            detail::typeError(ctx,
                              "Iterator.prototype.flatMap mapper must be callable"));
    }
    return iteratorHelperCreate(ctx, self, IteratorHelperKind::FlatMap, mapper, 0.0);
}

// take/drop 的 limit：ToNumber → NaN/负数抛 RangeError（并关闭迭代器）。
Value* iteratorLimit(IContext* ctx, Value* self, size_t argc, Value* argv[],
                     const char* method, double& limit) {
    Value* num = detail::toNumberValue(
        ctx, argc > 0 ? argv[0] : ctx->createUndefined());
    if (detail::isErrorValue(num)) return detail::iteratorClose(ctx, self, num);
    const double n = static_cast<NumberData*>(num->data())->value;
    // §27.1.3.19/20：NaN、有限且 > 2^53-1 → RangeError；
    // 再取 ToIntegerOrInfinity，结果 < 0 才 RangeError（-0.5 → -0 合法）。
    if (std::isnan(n) || (std::isfinite(n) && n > 9007199254740991.0)) {
        Value* err = detail::rangeError(
            ctx, std::string("Iterator.prototype.") + method +
                     " limit must be a non-negative number");
        return detail::iteratorClose(ctx, self, err);
    }
    const double integerLimit = std::isinf(n) ? n : std::trunc(n);
    if (integerLimit < 0.0) {
        Value* err = detail::rangeError(
            ctx, std::string("Iterator.prototype.") + method +
                     " limit must be a non-negative number");
        return detail::iteratorClose(ctx, self, err);
    }
    limit = integerLimit;
    return nullptr;
}

Value* iteratorTake(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "take")) return err;
    double limit = 0.0;
    if (Value* err = iteratorLimit(ctx, self, argc, argv, "take", limit)) return err;
    return iteratorHelperCreate(ctx, self, IteratorHelperKind::Take, nullptr, limit);
}

Value* iteratorDrop(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "drop")) return err;
    double limit = 0.0;
    if (Value* err = iteratorLimit(ctx, self, argc, argv, "drop", limit)) return err;
    return iteratorHelperCreate(ctx, self, IteratorHelperKind::Drop, nullptr, limit);
}

// 重入保护：辅助迭代器底层是生成器，执行中再次 next 须抛 TypeError
// （§27.5.3.2 GeneratorValidate 的 state=executing）。
struct HelperRunningGuard {
    IteratorHelperData* d;
    explicit HelperRunningGuard(IteratorHelperData* data) : d(data) {
        d->running = true;
    }
    ~HelperRunningGuard() { d->running = false; }
};

Value* iteratorHelperNext(IContext* ctx, Value* self, size_t /*argc*/,
                          Value* /*argv*/[]) {
    IteratorHelperData* d = asIteratorHelper(self);
    if (d == nullptr) {
        return detail::typeError(ctx, "next method called on incompatible receiver");
    }
    if (d->running) {
        return detail::typeError(ctx, "Iterator Helper is already running");
    }
    if (d->done) return createIterResult(ctx, ctx->createUndefined(), true);
    HelperRunningGuard guard(d);

    for (;;) {
        bool done = false;
        Value* err = nullptr;
        switch (d->kind) {
            case IteratorHelperKind::Map: {
                Value* value = stepValue(ctx, d->underlying, d->nextMethod, done, err);
                if (err != nullptr) {
                    helperAbort(d);
                    return err;
                }
                if (done) return helperFinish(ctx, d);
                Value* args[2] = {value, ctx->createNumber(d->counter)};
                Value* mapped =
                    ctx->call(d->callback, ctx->createUndefined(), 2, args);
                if (detail::isErrorValue(mapped)) return helperFail(ctx, d, mapped);
                d->counter += 1.0;
                return createIterResult(ctx, mapped, false);
            }
            case IteratorHelperKind::Filter: {
                Value* value = stepValue(ctx, d->underlying, d->nextMethod, done, err);
                if (err != nullptr) {
                    helperAbort(d);
                    return err;
                }
                if (done) return helperFinish(ctx, d);
                Value* args[2] = {value, ctx->createNumber(d->counter)};
                Value* selected =
                    ctx->call(d->callback, ctx->createUndefined(), 2, args);
                if (detail::isErrorValue(selected)) return helperFail(ctx, d, selected);
                d->counter += 1.0;
                if (detail::toBoolean(selected)) {
                    return createIterResult(ctx, value, false);
                }
                continue;
            }
            case IteratorHelperKind::Take: {
                if (d->counter == 0.0) {
                    d->done = true;
                    if (Value* closeErr =
                            detail::iteratorClose(ctx, d->underlying, nullptr)) {
                        return closeErr;
                    }
                    d->underlying = nullptr;
                    d->nextMethod = nullptr;
                    return createIterResult(ctx, ctx->createUndefined(), true);
                }
                if (!std::isinf(d->counter)) d->counter -= 1.0;
                Value* value = stepValue(ctx, d->underlying, d->nextMethod, done, err);
                if (err != nullptr) {
                    helperAbort(d);
                    return err;
                }
                if (done) return helperFinish(ctx, d);
                return createIterResult(ctx, value, false);
            }
            case IteratorHelperKind::Drop: {
                while (d->counter > 0.0) {
                    if (!std::isinf(d->counter)) d->counter -= 1.0;
                    stepValue(ctx, d->underlying, d->nextMethod, done, err);
                    if (err != nullptr) {
                        helperAbort(d);
                        return err;
                    }
                    if (done) return helperFinish(ctx, d);
                }
                Value* value = stepValue(ctx, d->underlying, d->nextMethod, done, err);
                if (err != nullptr) {
                    helperAbort(d);
                    return err;
                }
                if (done) return helperFinish(ctx, d);
                return createIterResult(ctx, value, false);
            }
            case IteratorHelperKind::FlatMap: {
                if (d->innerIterator != nullptr) {
                    Value* innerValue = stepValue(ctx, d->innerIterator, d->innerNext,
                                                  done, err);
                    if (err != nullptr) {
                        return helperFailUnderlying(ctx, d, err);
                    }
                    if (!done) return createIterResult(ctx, innerValue, false);
                    d->innerIterator = nullptr;
                    d->innerNext = nullptr;
                }
                Value* value = stepValue(ctx, d->underlying, d->nextMethod, done, err);
                if (err != nullptr) {
                    helperAbort(d);
                    return err;
                }
                if (done) return helperFinish(ctx, d);
                Value* args[2] = {value, ctx->createNumber(d->counter)};
                Value* mapped =
                    ctx->call(d->callback, ctx->createUndefined(), 2, args);
                if (detail::isErrorValue(mapped)) return helperFail(ctx, d, mapped);
                d->counter += 1.0;
                if (!detail::isObjectKind(mapped->type()->kind())) {
                    return helperFail(
                        ctx, d,
                        detail::typeError(ctx,
                                          "Iterator.prototype.flatMap mapper must "
                                          "return an object"));
                }
                Value* method = ctx->getProperty(
                    mapped, ctx->getWellKnownSymbol(WellKnownSymbol::Iterator));
                if (detail::isErrorValue(method)) return helperFail(ctx, d, method);
                Value* inner = nullptr;
                TypeKind mk = method->type()->kind();
                if (mk == TypeKind::Undefined || mk == TypeKind::Null) {
                    // GetIteratorFlattenable：无 @@iterator 时把对象本身当迭代器。
                    inner = mapped;
                } else if (!detail::isCallable(method)) {
                    return helperFail(
                        ctx, d,
                        detail::typeError(ctx,
                                          "Iterator.prototype.flatMap mapper result "
                                          "is not iterable"));
                } else {
                    inner = ctx->call(method, mapped, 0, nullptr);
                    if (detail::isErrorValue(inner)) return helperFail(ctx, d, inner);
                    if (!detail::isObjectKind(inner->type()->kind())) {
                        return helperFail(
                            ctx, d,
                            detail::typeError(
                                ctx,
                                "Iterator.prototype.flatMap @@iterator did "
                                "not return an object"));
                    }
                }
                Value* innerNext = ctx->getProperty(inner, "next");
                if (detail::isErrorValue(innerNext)) return helperFail(ctx, d, innerNext);
                d->innerIterator = inner;
                d->innerNext = innerNext;
                continue;
            }
            case IteratorHelperKind::Chunks: {
                Value* chunk = ctx->createArray();
                if (chunk == ctx->oomValue()) return chunk;
                auto* cd = static_cast<ArrayData*>(chunk->data());
                uint32_t count = 0;
                const double size = d->counter;
                while (static_cast<double>(count) < size) {
                    Value* value =
                        stepValue(ctx, d->underlying, d->nextMethod, done, err);
                    if (err != nullptr) {
                        helperAbort(d);
                        return err;
                    }
                    if (done) {
                        if (count == 0) return helperFinish(ctx, d);
                        // 末块不足：返回部分块并完成。
                        d->done = true;
                        d->underlying = nullptr;
                        d->nextMethod = nullptr;
                        return createIterResult(ctx, chunk, false);
                    }
                    cd->setElement(count, value);
                    ++count;
                }
                return createIterResult(ctx, chunk, false);
            }
            case IteratorHelperKind::Windows: {
                Value* window = ctx->createArray();
                if (window == ctx->oomValue()) return window;
                auto* wd = static_cast<ArrayData*>(window->data());
                uint32_t count = 0;
                // 先装入上一窗口去掉首元素后的缓冲区。
                if (d->buffer != nullptr) {
                    auto* bd = static_cast<ArrayData*>(d->buffer->data());
                    for (uint32_t i = 0; i < bd->length(); ++i) {
                        Value* v = bd->elementAt(i);
                        if (v != nullptr) wd->setElement(count++, v);
                    }
                }
                const double size = d->counter;
                while (static_cast<double>(count) < size) {
                    Value* value =
                        stepValue(ctx, d->underlying, d->nextMethod, done, err);
                    if (err != nullptr) {
                        helperAbort(d);
                        return err;
                    }
                    if (done) {
                        if (d->allowPartial && count > 0) {
                            d->done = true;
                            d->underlying = nullptr;
                            d->nextMethod = nullptr;
                            return createIterResult(ctx, window, false);
                        }
                        return helperFinish(ctx, d);
                    }
                    wd->setElement(count, value);
                    ++count;
                }
                // 保留本窗口去掉首元素后的尾部作为下次的滑动前缀。
                Value* nextBuffer = ctx->createArray();
                if (nextBuffer == ctx->oomValue()) return nextBuffer;
                auto* nbd = static_cast<ArrayData*>(nextBuffer->data());
                for (uint32_t i = 1; i < count; ++i) {
                    Value* v = wd->elementAt(i);
                    if (v != nullptr) nbd->setElement(i - 1, v);
                }
                d->buffer = nextBuffer;
                return createIterResult(ctx, window, false);
            }
            case IteratorHelperKind::Concat: {
                if (d->innerIterator != nullptr) {
                    Value* value =
                        stepValue(ctx, d->innerIterator, d->innerNext, done, err);
                    if (err != nullptr) {
                        helperAbort(d);
                        return err;
                    }
                    if (!done) return createIterResult(ctx, value, false);
                    d->innerIterator = nullptr;
                    d->innerNext = nullptr;
                }
                auto* pd = static_cast<ArrayData*>(d->buffer->data());
                const uint32_t index = static_cast<uint32_t>(d->counter);
                if (index >= pd->length()) return helperFinish(ctx, d);
                d->counter += 1.0;
                Value* pair = pd->elementAt(index);
                auto* pairData = static_cast<ArrayData*>(pair->data());
                Value* item = pairData->elementAt(0);
                Value* method = pairData->elementAt(1);
                if (!detail::isCallable(method)) {
                    helperAbort(d);
                    return detail::typeError(
                        ctx, "Iterator.concat argument is not iterable");
                }
                Value* inner = ctx->call(method, item, 0, nullptr);
                if (detail::isErrorValue(inner)) {
                    helperAbort(d);
                    return inner;
                }
                if (!detail::isObjectKind(inner->type()->kind())) {
                    helperAbort(d);
                    return detail::typeError(
                        ctx, "Iterator.concat @@iterator did not return an object");
                }
                Value* innerNext = ctx->getProperty(inner, "next");
                if (detail::isErrorValue(innerNext)) {
                    helperAbort(d);
                    return innerNext;
                }
                d->innerIterator = inner;
                d->innerNext = innerNext;
                continue;
            }
        }
    }
}

Value* iteratorHelperReturn(IContext* ctx, Value* self, size_t /*argc*/,
                            Value* /*argv*/[]) {
    IteratorHelperData* d = asIteratorHelper(self);
    if (d == nullptr) {
        return detail::typeError(ctx, "return method called on incompatible receiver");
    }
    if (d->running) {
        return detail::typeError(ctx, "Iterator Helper is already running");
    }
    if (d->done) return createIterResult(ctx, ctx->createUndefined(), true);
    HelperRunningGuard guard(d);
    d->done = true;
    if (d->innerIterator != nullptr) {
        if (Value* err = detail::iteratorClose(ctx, d->innerIterator, nullptr))
            return err;
        d->innerIterator = nullptr;
        d->innerNext = nullptr;
    }
    if (d->underlying != nullptr) {
        if (Value* err = detail::iteratorClose(ctx, d->underlying, nullptr)) return err;
    }
    d->underlying = nullptr;
    d->nextMethod = nullptr;
    return createIterResult(ctx, ctx->createUndefined(), true);
}

// —— 立即消费的方法 ——

Value* iteratorForEach(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "forEach")) return err;
    Value* procedure = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(procedure)) {
        return detail::iteratorClose(
            ctx, self,
            detail::typeError(
                ctx, "Iterator.prototype.forEach procedure must be callable"));
    }
    Value* nextMethod = nullptr;
    if (Value* err = directNextMethod(ctx, self, nextMethod)) return err;
    double counter = 0.0;
    for (;;) {
        bool done = false;
        Value* err = nullptr;
        Value* value = stepValue(ctx, self, nextMethod, done, err);
        if (err != nullptr) return err;
        if (done) return ctx->createUndefined();
        Value* args[2] = {value, ctx->createNumber(counter)};
        Value* result = ctx->call(procedure, ctx->createUndefined(), 2, args);
        if (detail::isErrorValue(result)) {
            return detail::iteratorClose(ctx, self, result);
        }
        counter += 1.0;
    }
}

Value* iteratorSome(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "some")) return err;
    Value* predicate = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(predicate)) {
        return detail::iteratorClose(
            ctx, self,
            detail::typeError(ctx,
                              "Iterator.prototype.some predicate must be callable"));
    }
    Value* nextMethod = nullptr;
    if (Value* err = directNextMethod(ctx, self, nextMethod)) return err;
    double counter = 0.0;
    for (;;) {
        bool done = false;
        Value* err = nullptr;
        Value* value = stepValue(ctx, self, nextMethod, done, err);
        if (err != nullptr) return err;
        if (done) return ctx->createBoolean(false);
        Value* args[2] = {value, ctx->createNumber(counter)};
        Value* result = ctx->call(predicate, ctx->createUndefined(), 2, args);
        if (detail::isErrorValue(result)) {
            return detail::iteratorClose(ctx, self, result);
        }
        if (detail::toBoolean(result)) {
            if (Value* closeErr = detail::iteratorClose(ctx, self, nullptr)) {
                return closeErr;
            }
            return ctx->createBoolean(true);
        }
        counter += 1.0;
    }
}

Value* iteratorEvery(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "every")) return err;
    Value* predicate = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(predicate)) {
        return detail::iteratorClose(
            ctx, self,
            detail::typeError(ctx,
                              "Iterator.prototype.every predicate must be callable"));
    }
    Value* nextMethod = nullptr;
    if (Value* err = directNextMethod(ctx, self, nextMethod)) return err;
    double counter = 0.0;
    for (;;) {
        bool done = false;
        Value* err = nullptr;
        Value* value = stepValue(ctx, self, nextMethod, done, err);
        if (err != nullptr) return err;
        if (done) return ctx->createBoolean(true);
        Value* args[2] = {value, ctx->createNumber(counter)};
        Value* result = ctx->call(predicate, ctx->createUndefined(), 2, args);
        if (detail::isErrorValue(result)) {
            return detail::iteratorClose(ctx, self, result);
        }
        if (!detail::toBoolean(result)) {
            if (Value* closeErr = detail::iteratorClose(ctx, self, nullptr)) {
                return closeErr;
            }
            return ctx->createBoolean(false);
        }
        counter += 1.0;
    }
}

Value* iteratorFind(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "find")) return err;
    Value* predicate = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(predicate)) {
        return detail::iteratorClose(
            ctx, self,
            detail::typeError(ctx,
                              "Iterator.prototype.find predicate must be callable"));
    }
    Value* nextMethod = nullptr;
    if (Value* err = directNextMethod(ctx, self, nextMethod)) return err;
    double counter = 0.0;
    for (;;) {
        bool done = false;
        Value* err = nullptr;
        Value* value = stepValue(ctx, self, nextMethod, done, err);
        if (err != nullptr) return err;
        if (done) return ctx->createUndefined();
        Value* args[2] = {value, ctx->createNumber(counter)};
        Value* result = ctx->call(predicate, ctx->createUndefined(), 2, args);
        if (detail::isErrorValue(result)) {
            return detail::iteratorClose(ctx, self, result);
        }
        if (detail::toBoolean(result)) {
            if (Value* closeErr = detail::iteratorClose(ctx, self, nullptr)) {
                return closeErr;
            }
            return value;
        }
        counter += 1.0;
    }
}

Value* iteratorReduce(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "reduce")) return err;
    Value* reducer = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(reducer)) {
        return detail::iteratorClose(
            ctx, self,
            detail::typeError(ctx,
                              "Iterator.prototype.reduce reducer must be callable"));
    }
    Value* nextMethod = nullptr;
    if (Value* err = directNextMethod(ctx, self, nextMethod)) return err;

    Value* accumulator = nullptr;
    double counter = 0.0;
    if (argc > 1) {
        accumulator = argv[1];
    } else {
        bool done = false;
        Value* err = nullptr;
        accumulator = stepValue(ctx, self, nextMethod, done, err);
        if (err != nullptr) return err;
        if (done) {
            return detail::typeError(
                ctx, "Reduce of empty iterator with no initial value");
        }
        counter = 1.0;
    }

    for (;;) {
        bool done = false;
        Value* err = nullptr;
        Value* value = stepValue(ctx, self, nextMethod, done, err);
        if (err != nullptr) return err;
        if (done) return accumulator;
        Value* args[3] = {accumulator, value, ctx->createNumber(counter)};
        Value* result = ctx->call(reducer, ctx->createUndefined(), 3, args);
        if (detail::isErrorValue(result)) {
            return detail::iteratorClose(ctx, self, result);
        }
        accumulator = result;
        counter += 1.0;
    }
}

Value* iteratorToArray(IContext* ctx, Value* self, size_t /*argc*/,
                       Value* /*argv*/[]) {
    if (Value* err = requireIteratorObject(ctx, self, "toArray")) return err;
    Value* nextMethod = nullptr;
    if (Value* err = directNextMethod(ctx, self, nextMethod)) return err;
    Value* array = ctx->createArray();
    if (array == ctx->oomValue()) return array;
    auto* data = static_cast<ArrayData*>(array->data());
    uint32_t index = 0;
    for (;;) {
        bool done = false;
        Value* err = nullptr;
        Value* value = stepValue(ctx, self, nextMethod, done, err);
        if (err != nullptr) return err;
        if (done) return array;
        data->setElement(index, value);
        ++index;
    }
}

// —— Iterator.prototype.includes（ES2026 §27.1.3.x）——
// skippedElements 必须是 Number 且为整数（不强制转换）；非法时关闭底层。
Value* includesSkipCount(IContext* ctx, Value* self, size_t argc, Value* argv[],
                         double& toSkip) {
    if (argc < 2 || argv[1]->type()->kind() == TypeKind::Undefined) {
        toSkip = 0.0;
        return nullptr;
    }
    Value* arg = argv[1];
    if (arg->type()->kind() != TypeKind::Number) {
        return detail::iteratorClose(
            ctx, self,
            detail::typeError(ctx,
                              "Iterator.prototype.includes skippedElements must be a "
                              "number"));
    }
    const double n = static_cast<NumberData*>(arg->data())->value;
    if (!std::isinf(n) && std::trunc(n) != n) {
        return detail::iteratorClose(
            ctx, self,
            detail::typeError(ctx,
                              "Iterator.prototype.includes skippedElements must be "
                              "an integral number"));
    }
    if (n < 0.0 || (std::isfinite(n) && n > 9007199254740991.0)) {
        return detail::iteratorClose(
            ctx, self,
            detail::rangeError(ctx,
                               "Iterator.prototype.includes skippedElements out of "
                               "range"));
    }
    toSkip = n;
    return nullptr;
}

Value* iteratorIncludes(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "includes")) return err;
    Value* search = argc > 0 ? argv[0] : ctx->createUndefined();
    double toSkip = 0.0;
    if (Value* err = includesSkipCount(ctx, self, argc, argv, toSkip)) return err;

    Value* nextMethod = nullptr;
    if (Value* err = directNextMethod(ctx, self, nextMethod)) return err;

    double skipped = 0.0;
    for (;;) {
        bool done = false;
        Value* err = nullptr;
        Value* value = stepValue(ctx, self, nextMethod, done, err);
        if (err != nullptr) return err;
        if (done) return ctx->createBoolean(false);
        if (skipped < toSkip) {
            skipped += 1.0;
            continue;
        }
        bool equal = false;
        if (value->type()->kind() == TypeKind::Number &&
            search->type()->kind() == TypeKind::Number) {
            const double a = static_cast<NumberData*>(value->data())->value;
            const double b = static_cast<NumberData*>(search->data())->value;
            equal = (a == b) || (std::isnan(a) && std::isnan(b));
        } else {
            equal = detail::strictEquals(value, search);
        }
        if (equal) {
            if (Value* closeErr = detail::iteratorClose(ctx, self, nullptr)) {
                return closeErr;
            }
            return ctx->createBoolean(true);
        }
    }
}

// —— Iterator.prototype.join（ES2026 §27.1.3.x）——
Value* iteratorJoin(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "join")) return err;

    std::string separator = ",";
    if (argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined) {
        Value* sep = detail::toStringValue(ctx, argv[0]);
        if (detail::isErrorValue(sep)) {
            // separator 强制转换失败：关闭底层迭代器（不读 next）。
            return detail::iteratorClose(ctx, self, sep);
        }
        separator = static_cast<StringData*>(sep->data())->value;
    }

    Value* nextMethod = nullptr;
    if (Value* err = directNextMethod(ctx, self, nextMethod)) return err;

    std::string result;
    bool first = true;
    for (;;) {
        bool done = false;
        Value* err = nullptr;
        Value* value = stepValue(ctx, self, nextMethod, done, err);
        if (err != nullptr) return err;
        if (done) return ctx->createString(result);
        if (!first) result += separator;
        first = false;
        TypeKind vk = value->type()->kind();
        if (vk == TypeKind::Undefined || vk == TypeKind::Null) continue;
        Value* text = detail::toStringValue(ctx, value);
        if (detail::isErrorValue(text)) {
            return detail::iteratorClose(ctx, self, text);
        }
        result += static_cast<StringData*>(text->data())->value;
    }
}

// chunkSize / windowSize 校验：必须是 Number 且为 1..2^32-1 的整数。
// 非 Number / NaN / Infinity / 非整数 → TypeError；越界 → RangeError。
// 失败时关闭底层迭代器。
Value* iteratorSizeArg(IContext* ctx, Value* self, Value* arg, const char* method,
                       double& out) {
    auto fail = [&](Value* error) {
        return detail::iteratorClose(ctx, self, error);
    };
    if (arg->type()->kind() != TypeKind::Number) {
        return fail(detail::typeError(
            ctx, std::string("Iterator.prototype.") + method +
                     " size must be an integral number"));
    }
    const double n = static_cast<NumberData*>(arg->data())->value;
    if (std::isnan(n) || std::isinf(n) || std::trunc(n) != n) {
        return fail(detail::typeError(
            ctx, std::string("Iterator.prototype.") + method +
                     " size must be an integral number"));
    }
    if (n < 1.0 || n > 4294967295.0) {
        return fail(detail::rangeError(
            ctx, std::string("Iterator.prototype.") + method +
                     " size must be between 1 and 2^32-1"));
    }
    out = n;
    return nullptr;
}

Value* iteratorChunks(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "chunks")) return err;
    double size = 0.0;
    if (Value* err = iteratorSizeArg(ctx, self, argc > 0 ? argv[0] : ctx->createUndefined(),
                                     "chunks", size)) {
        return err;
    }
    return iteratorHelperCreate(ctx, self, IteratorHelperKind::Chunks, nullptr, size);
}

Value* iteratorWindows(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (Value* err = requireIteratorObject(ctx, self, "windows")) return err;
    double size = 0.0;
    if (Value* err = iteratorSizeArg(ctx, self, argc > 0 ? argv[0] : ctx->createUndefined(),
                                     "windows", size)) {
        return err;
    }
    // undersized：缺省 "only-full"；否则必须是这两个字符串之一（不强制转换）。
    bool allowPartial = false;
    if (argc > 1 && argv[1]->type()->kind() != TypeKind::Undefined) {
        Value* u = argv[1];
        if (u->type()->kind() != TypeKind::String) {
            return detail::iteratorClose(
                ctx, self,
                detail::typeError(ctx,
                                  "Iterator.prototype.windows undersized must be "
                                  "'only-full' or 'allow-partial'"));
        }
        const std::string& s = static_cast<StringData*>(u->data())->value;
        if (s == "allow-partial") {
            allowPartial = true;
        } else if (s != "only-full") {
            return detail::iteratorClose(
                ctx, self,
                detail::typeError(ctx,
                                  "Iterator.prototype.windows undersized must be "
                                  "'only-full' or 'allow-partial'"));
        }
    }
    Value* helper =
        iteratorHelperCreate(ctx, self, IteratorHelperKind::Windows, nullptr, size);
    if (detail::isErrorValue(helper)) return helper;
    IteratorHelperData* d = asIteratorHelper(helper);
    if (d == nullptr) return helper;
    d->allowPartial = allowPartial;
    d->buffer = ctx->createArray();
    if (d->buffer == ctx->oomValue()) return d->buffer;
    return helper;
}

// —— Iterator.concat（ES2026 §27.1.4.x）——
// 构造期：逐个校验参数是对象并读取 @@iterator（只读一次，不调用）。
// 迭代期：按顺序调用 @@iterator 建立内层迭代器并转发 next/return。
Value* iteratorConcat(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* pairs = ctx->createArray();
    if (pairs == ctx->oomValue()) return pairs;
    auto* pd = static_cast<ArrayData*>(pairs->data());
    for (size_t i = 0; i < argc; ++i) {
        Value* item = argv[i];
        if (!detail::isObjectKind(item->type()->kind())) {
            return detail::typeError(ctx,
                                     "Iterator.concat arguments must be objects");
        }
        Value* method =
            ctx->getProperty(item, ctx->getWellKnownSymbol(WellKnownSymbol::Iterator));
        if (detail::isErrorValue(method)) return method;
        if (!detail::isCallable(method)) {
            return detail::typeError(ctx,
                                     "Iterator.concat argument is not iterable");
        }
        Value* pair = ctx->createArray();
        if (pair == ctx->oomValue()) return pair;
        auto* pairData = static_cast<ArrayData*>(pair->data());
        pairData->setElement(0, item);
        pairData->setElement(1, method);
        pd->setElement(static_cast<uint32_t>(i), pair);
    }
    Value* helper =
        iteratorHelperCreate(ctx, pairs, IteratorHelperKind::Concat, nullptr, 0.0);
    if (detail::isErrorValue(helper)) return helper;
    IteratorHelperData* d = asIteratorHelper(helper);
    if (d == nullptr) return helper;
    d->buffer = pairs;
    // Concat 的底层不是单个迭代器，清掉占位的 nextMethod。
    d->nextMethod = nullptr;
    return helper;
}

// —— Iterator.prototype 上的访问器与方法 ——

// constructor 访问器 getter：返回 Iterator 构造函数
Value* iteratorProtoConstructorGet(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                                   Value* /*argv*/[]) {
    return ctx->iteratorCtor();
}

// SetterThatIgnoresPrototypeProperties（§27.1.3.1/§27.1.3.4）：
// 非对象 → TypeError；this 是 home（%Iterator.prototype%）→ TypeError；
// 否则在 this 上建/更新自有数据属性。
Value* iteratorProtoSetter(IContext* ctx, Value* self, Value* value, Value* key,
                           const char* name) {
    if (!detail::isObjectKind(self->type()->kind())) {
        return detail::typeError(ctx, std::string("set Iterator.prototype.") + name +
                                          " called on non-object");
    }
    Value* home = ctx->iteratorPrototype();
    if (home != nullptr && self->data() == home->data()) {
        return detail::typeError(ctx, std::string("cannot set Iterator.prototype.") +
                                          name + " on the home object");
    }
    Value* r = ctx->defineProperty(self, key, value, true, false, true);
    return detail::isErrorValue(r) ? r : ctx->createUndefined();
}

Value* iteratorProtoConstructorSet(IContext* ctx, Value* self, size_t /*argc*/,
                                   Value* argv[]) {
    return iteratorProtoSetter(ctx, self, argv[0], ctx->createString("constructor"),
                               "constructor");
}

// @@toStringTag 访问器 getter：返回 "Iterator"
Value* iteratorProtoTagGet(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                           Value* /*argv*/[]) {
    return ctx->createString("Iterator");
}

Value* iteratorProtoTagSet(IContext* ctx, Value* self, size_t /*argc*/,
                           Value* argv[]) {
    return iteratorProtoSetter(
        ctx, self, argv[0],
        ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag), "[Symbol.toStringTag]");
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

// 把值定义为 global object 属性（全局环境记录的对象记录）。
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->defineGlobal(name, v, true, false, true);
}

// 定义方法（writable/不可枚举/可配置，与 ES 一致）。
// minArgc 与 length 分离：缺参是合法调用（按 undefined 走规范校验），
// 故 minArgc 通常为 0，而 length 仍按规范声明。
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc, int lengthOverride = -1) {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* f = ctx->createNativeFunction(fn, minArgc, {}, opts);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    if (lengthOverride >= 0) {
        Value* len = ctx->createNumber(static_cast<double>(lengthOverride));
        if (len == ctx->oomValue()) return false;
        if (ctx->defineProperty(f, "length", len, false, false, true)->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

// 定义以 symbol 为键的方法
bool defineSymbolMethod(IContext* ctx, Value* target, WellKnownSymbol which,
                        const char* name, NativeFn fn, size_t minArgc,
                        int lengthOverride = -1) {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* f = ctx->createNativeFunction(fn, minArgc, {}, opts);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    if (lengthOverride >= 0) {
        Value* len = ctx->createNumber(static_cast<double>(lengthOverride));
        if (len == ctx->oomValue()) return false;
        if (ctx->defineProperty(f, "length", len, false, false, true)->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }
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

    // —— 3b. %StringIteratorPrototype%（原型 = %Iterator.prototype%）——
    Value* stringIterProto = ctx->createObject();
    ok &= stringIterProto != ctx->oomValue();
    if (!ok) return false;
    ok &= ctx->setPrototype(stringIterProto, iteratorProto)->type()->kind() !=
          TypeKind::Exception;

    // —— 4. %WrapForValidIteratorPrototype%（原型 = %Iterator.prototype%）——
    Value* wrapperProto = ctx->createObject();
    ok &= wrapperProto != ctx->oomValue();
    if (!ok) return false;
    ok &= ctx->setPrototype(wrapperProto, iteratorProto)->type()->kind() !=
          TypeKind::Exception;

    // —— 4b. %IteratorHelperPrototype%（原型 = %Iterator.prototype%）——
    Value* helperProto = ctx->createObject();
    ok &= helperProto != ctx->oomValue();
    if (!ok) return false;
    ok &= ctx->setPrototype(helperProto, iteratorProto)->type()->kind() !=
          TypeKind::Exception;

    // —— 5. 原型就绪：此后各迭代器工厂自动挂链 ——
    ctx->setIteratorBuiltins(iteratorProto, iteratorFn, arrayIterProto,
                             stringIterProto, wrapperProto);
    ctx->setIteratorHelperPrototype(helperProto);

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
    Value* tagSet = ctx->createNativeFunction(iteratorProtoTagSet, 1);
    ok &= tagSet != ctx->oomValue();
    if (!ok) return false;
    ctx->setFunctionName(tagSet, "set [Symbol.toStringTag]");
    ok &= ctx->defineAccessor(iteratorProto,
                              ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                              tagGet, tagSet, false, true)
              ->type()->kind() != TypeKind::Exception;

    // @@iterator（§27.1.3.3）：返回 this
    ok &= defineSymbolMethod(ctx, iteratorProto, WellKnownSymbol::Iterator,
                             "[Symbol.iterator]", iteratorProtoIterator, 0);
    // @@dispose（§27.1.3.2）：调 this.return()
    ok &= defineSymbolMethod(ctx, iteratorProto, WellKnownSymbol::Dispose,
                             "[Symbol.dispose]", iteratorProtoDispose, 0);

    // Iterator.prototype 辅助方法（§27.1.3.3）。
    // 缺参合法（undefined 走规范校验），故 minArgc=0，length 按规范声明。
    ok &= defineMethod(ctx, iteratorProto, "map", iteratorMap, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "filter", iteratorFilter, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "take", iteratorTake, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "drop", iteratorDrop, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "flatMap", iteratorFlatMap, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "reduce", iteratorReduce, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "toArray", iteratorToArray, 0, 0);
    ok &= defineMethod(ctx, iteratorProto, "forEach", iteratorForEach, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "some", iteratorSome, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "every", iteratorEvery, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "find", iteratorFind, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "includes", iteratorIncludes, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "join", iteratorJoin, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "chunks", iteratorChunks, 0, 1);
    ok &= defineMethod(ctx, iteratorProto, "windows", iteratorWindows, 0, 1);

    // Iterator.from（§27.1.4.2）
    ok &= defineMethod(ctx, iteratorFn, "from", iteratorFrom, 0, 1);
    // Iterator.concat（§27.1.4.x）
    ok &= defineMethod(ctx, iteratorFn, "concat", iteratorConcat, 0, 0);

    // —— 7. %ArrayIteratorPrototype% 成员 ——
    ok &= defineMethod(ctx, arrayIterProto, "next", arrayIteratorNext, 0);
    // @@toStringTag = "Array Iterator"（不可写/不可枚举/可配置）
    ok &= ctx->defineProperty(arrayIterProto,
                              ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                              ctx->createString("Array Iterator"), false, false, true)
              ->type()->kind() != TypeKind::Exception;

    // —— 7b. %StringIteratorPrototype% 成员 ——
    ok &= defineMethod(ctx, stringIterProto, "next", stringIteratorNext, 0);
    ok &= ctx->defineProperty(stringIterProto,
                              ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                              ctx->createString("String Iterator"), false, false, true)
              ->type()->kind() != TypeKind::Exception;

    // —— 8. %WrapForValidIteratorPrototype% 成员 ——
    ok &= defineMethod(ctx, wrapperProto, "next", wrapForValidIteratorNext, 0);
    ok &= defineMethod(ctx, wrapperProto, "return", wrapForValidIteratorReturn, 0);

    // —— 8b. %IteratorHelperPrototype% 成员 ——
    ok &= defineMethod(ctx, helperProto, "next", iteratorHelperNext, 0);
    ok &= defineMethod(ctx, helperProto, "return", iteratorHelperReturn, 0);
    ok &= ctx->defineProperty(helperProto,
                              ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                              ctx->createString("Iterator Helper"), false, false, true)
              ->type()->kind() != TypeKind::Exception;

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

    // —— 9b. String.prototype[@@iterator]（§22.1.3.36）——
    Value* stringProto = ctx->stringPrototype();
    if (stringProto == nullptr) return false;
    ok &= defineSymbolMethod(ctx, stringProto, WellKnownSymbol::Iterator,
                             "[Symbol.iterator]", stringProtoIterator, 0);

    // —— 10. 注册到 global ——
    bindGlobal(ctx, "Iterator", iteratorFn);
    return ok;
}

}  // namespace neo::core
