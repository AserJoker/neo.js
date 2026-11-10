// PromiseBuiltins.cpp - Promise 构造函数、then/catch/finally 与 job 投递
//
// 核心流程（ES2024 §27.2）：
//   - CreateResolvingFunctions：resolve/reject 共享一个 alreadyResolved 单元
//   - Promise Resolve Functions（§27.2.1.3.2）：**thenable 兼容**——判断依据是
//     「能否取到可调用的 then」，而不是 instanceof Promise
//   - NewPromiseCapability：new C(executor)，executor 把 resolve/reject 写进 cell
//   - PerformPromiseThen：注册 reaction；已 settle 则立即排 job
//   - PromiseReactionJob：微任务，调用 handler 后 resolve/reject 派生 promise
//
// NativeFn 无 userdata，状态注入有两条路：
//   - captureValue + captured()：job 函数等一次性内部函数（闭包作用域持有状态）；
//   - createBoundFunction：resolve/reject 等需要长期存活、且不能反向持有 promise
//     的函数（bound args 是 data 级引用，不构成 GC 根，避免根环）。
//
// 关联 ADR: 006（异常即值）、018（对象模型）、034（原型-构造函数）、043（Promise）

#include "core/PromiseBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Exception.h"
#include "core/types/Promise.h"
#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

namespace {

// —— 闭包取值 ——
// invokeNative 为每次调用新建 execScope（parent = 函数 closureScope），
// 故 current()->parent() 即当前 native 函数的闭包作用域。
Value* captured(IContext* ctx, const char* name) {
    Scope* cur = ctx->current();
    Scope* closure = cur != nullptr ? cur->parent() : nullptr;
    return closure != nullptr ? closure->lookup(name) : nullptr;
}

// 异常值 → 拒绝原因（Exception 携带的 Error 对象；其他值原样）
Value* exceptionReason(Value* v) {
    if (v != nullptr && v->type()->kind() == TypeKind::Exception) {
        return static_cast<ExceptionData*>(v->data())->child;
    }
    return v;
}

// 构造一个规范 Error 的「原因」值（不带 Exception 包装）
Value* errorReason(IContext* ctx, ErrorKind kind, const std::string& msg) {
    return exceptionReason(ctx->createErrorException(kind, msg));
}

// 通用单元数组：CreateResolvingFunctions 用 [0] 作 alreadyResolved 布尔；
// NewPromiseCapability 用 [0]/[1] 装 resolve/reject。
Value* makeCell(IContext* ctx) {
    Value* cell = ctx->createArray();
    if (cell == ctx->oomValue()) return cell;
    static_cast<ArrayData*>(cell->data())->setElement(0, ctx->createBoolean(false));
    return cell;
}

bool cellTaken(IContext* /*ctx*/, Value* cell) {
    Value* v = static_cast<ArrayData*>(cell->data())->elementAt(0);
    return v != nullptr && detail::toBoolean(v);
}

void takeCell(IContext* ctx, Value* cell) {
    static_cast<ArrayData*>(cell->data())->setElement(0, ctx->createBoolean(true));
}

// —— 前向声明 ——
Value* reactionJob(IContext* ctx, Value* self, size_t argc, Value* argv[]);
Value* resolveThenableJob(IContext* ctx, Value* self, size_t argc, Value* argv[]);
Value* resolveFunction(IContext* ctx, Value* self, size_t argc, Value* argv[]);
Value* rejectFunction(IContext* ctx, Value* self, size_t argc, Value* argv[]);
Value* speciesConstructor(IContext* ctx, Value* obj, Value* defaultCtor);
Value* promiseResolveCtor(IContext* ctx, Value* ctor, Value* x);

// Promise job 执行器：entity 是内部 job 函数，以 undefined 为 this 调用。
// 不用 defaultTaskRunner——promise job 的异常已在内部转成 reject，不该打印。
Value* promiseJobRunner(IContext* ctx, Value* task) {
    return ctx->call(task, ctx->createUndefined(), 0, nullptr);
}

// —— reaction job 投递 ——
// 把一个 reaction 打包成内部 job 函数并排进微任务队列。
// capability 为空（仅未处理拒绝跟踪）时也照常投递，job 内会直接返回。
void enqueueReactionJob(IContext* ctx, const PromiseReaction& r, Value* argument) {
    Value* job = ctx->createNativeFunction(reactionJob, 0);
    if (job == ctx->oomValue()) return;
    ctx->captureValue(job, "__handler",
                      r.handler != nullptr ? r.handler : ctx->createUndefined());
    ctx->captureValue(job, "__type",
                      ctx->createBoolean(r.type == PromiseReactionType::Reject));
    ctx->captureValue(job, "__argument", argument);
    ctx->captureValue(job, "__capPromise",
                      r.capability.promise != nullptr ? r.capability.promise
                                                      : ctx->createUndefined());
    ctx->captureValue(job, "__capResolve",
                      r.capability.resolve != nullptr ? r.capability.resolve
                                                      : ctx->createUndefined());
    ctx->captureValue(job, "__capReject",
                      r.capability.reject != nullptr ? r.capability.reject
                                                     : ctx->createUndefined());
    ctx->postMicrotask(job, promiseJobRunner);
}

// —— 状态迁移（§27.2.1.4 FulfillPromise / §27.2.1.7 RejectPromise）——

void settlePromise(IContext* ctx, Value* promise, Value* value, PromiseState state) {
    PromiseData* d = asPromise(promise);
    if (d == nullptr || d->state != PromiseState::Pending) return;

    d->result = value;
    d->state = state;
    // 取出并清空 reactions（settle 后不再接受新 reaction 入队）
    std::vector<PromiseReaction> reactions;
    reactions.swap(d->reactions);

    // §27.2.1.4 / §27.2.1.7：只运行与 settle 方向一致的 reaction。
    // Pending 期间 then 会同时登记 fulfill/reject 两条，另一条须丢弃。
    const PromiseReactionType wanted = state == PromiseState::Fulfilled
                                           ? PromiseReactionType::Fulfill
                                           : PromiseReactionType::Reject;
    for (const PromiseReaction& r : reactions) {
        if (r.type != wanted) continue;
        enqueueReactionJob(ctx, r, value);
    }

    // §27.2.1.7 step 13：拒绝且未被处理 → 通知宿主（ADR-043 M4）
    if (state == PromiseState::Rejected && !d->isHandled) {
        ctx->hostPromiseRejectionTracker(promise, false);
    }
}

void fulfillPromise(IContext* ctx, Value* promise, Value* value) {
    settlePromise(ctx, promise, value, PromiseState::Fulfilled);
}

void rejectPromise(IContext* ctx, Value* promise, Value* reason) {
    settlePromise(ctx, promise, reason, PromiseState::Rejected);
}

// —— CreateResolvingFunctions（§27.2.1.3）——

// 状态（promise、cell）通过 **bound args** 传给 resolve/reject，而不是 captureValue：
// bound args 是 data 级引用（FunctionData::boundArgs），不构成 GC 根；
// 若用闭包捕获，promise → resolveFn → 闭包作用域(根) → promise 会形成不可回收的根环。
Value* createResolvingFunctions(IContext* ctx, Value* promise, Value*& resolveOut,
                                Value*& rejectOut) {
    Value* cell = makeCell(ctx);
    if (cell == ctx->oomValue()) return cell;

    Value* undefined = ctx->createUndefined();
    Value* boundArgs[2] = {promise, cell};

    Value* resolveTarget = ctx->createNativeFunction(resolveFunction, 1);
    if (resolveTarget == ctx->oomValue()) return resolveTarget;
    Value* resolve = ctx->createBoundFunction(resolveTarget, undefined, 2, boundArgs);
    if (resolve == ctx->oomValue()) return resolve;
    ctx->setFunctionName(resolve, "");

    Value* rejectTarget = ctx->createNativeFunction(rejectFunction, 1);
    if (rejectTarget == ctx->oomValue()) return rejectTarget;
    Value* reject = ctx->createBoundFunction(rejectTarget, undefined, 2, boundArgs);
    if (reject == ctx->oomValue()) return reject;
    ctx->setFunctionName(reject, "");

    // promise 强持有两者（enumerateChildren 登记），使其在 execScope 销毁后存活。
    if (PromiseData* pd = asPromise(promise)) {
        pd->resolveFn = resolve;
        pd->rejectFn = reject;
    }

    resolveOut = resolve;
    rejectOut = reject;
    return nullptr;
}

// Promise Resolve Functions（§27.2.1.3.2），**thenable 兼容**的核心。
// bound args 前置：argv[0] = promise，argv[1] = cell，argv[2] = resolution。
Value* resolveFunction(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* promise = argc > 0 ? argv[0] : nullptr;
    Value* cell = argc > 1 ? argv[1] : nullptr;
    Value* resolution = argc > 2 ? argv[2] : ctx->createUndefined();
    if (promise == nullptr || cell == nullptr) return ctx->createUndefined();

    if (cellTaken(ctx, cell)) return ctx->createUndefined();
    takeCell(ctx, cell);

    if (resolution == promise) {
        rejectPromise(ctx, promise,
                      errorReason(ctx, ErrorKind::TypeError,
                                  "Chaining cycle detected for promise"));
        return ctx->createUndefined();
    }
    if (!detail::isObjectKind(resolution->type()->kind())) {
        fulfillPromise(ctx, promise, resolution);
        return ctx->createUndefined();
    }

    // thenable 兼容：取 then，可调用则排 job 调用它
    Value* then = ctx->getProperty(resolution, "then");
    if (detail::isErrorValue(then)) {
        rejectPromise(ctx, promise, exceptionReason(then));
        return ctx->createUndefined();
    }
    if (!detail::isCallable(then)) {
        fulfillPromise(ctx, promise, resolution);
        return ctx->createUndefined();
    }

    // 为 thenable 创建**新**的 resolving functions（共享新 alreadyResolved 单元）
    Value* r2 = nullptr;
    Value* j2 = nullptr;
    if (Value* err = createResolvingFunctions(ctx, promise, r2, j2)) return err;

    Value* job = ctx->createNativeFunction(resolveThenableJob, 0);
    if (job == ctx->oomValue()) return job;
    ctx->captureValue(job, "__then", then);
    ctx->captureValue(job, "__thenable", resolution);
    ctx->captureValue(job, "__resolve", r2);
    ctx->captureValue(job, "__reject", j2);
    ctx->postMicrotask(job, promiseJobRunner);
    return ctx->createUndefined();
}

// Promise Reject Functions（§27.2.1.3.1）
// bound args 前置：argv[0] = promise，argv[1] = cell，argv[2] = reason。
Value* rejectFunction(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* promise = argc > 0 ? argv[0] : nullptr;
    Value* cell = argc > 1 ? argv[1] : nullptr;
    Value* reason = argc > 2 ? argv[2] : ctx->createUndefined();
    if (promise == nullptr || cell == nullptr) return ctx->createUndefined();

    if (cellTaken(ctx, cell)) return ctx->createUndefined();
    takeCell(ctx, cell);
    rejectPromise(ctx, promise, reason);
    return ctx->createUndefined();
}

// NewPromiseResolveThenableJob（§27.2.1.3.2 step 9）
Value* resolveThenableJob(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                          Value* /*argv*/[]) {
    Value* then = captured(ctx, "__then");
    Value* thenable = captured(ctx, "__thenable");
    Value* resolve = captured(ctx, "__resolve");
    Value* reject = captured(ctx, "__reject");

    Value* args[2] = {resolve, reject};
    Value* r = ctx->call(then, thenable, 2, args);
    if (detail::isErrorValue(r)) {
        Value* reason = exceptionReason(r);
        ctx->call(reject, ctx->createUndefined(), 1, &reason);
    }
    return ctx->createUndefined();
}

// —— NewPromiseCapability（§27.2.1.5）——

// executor(resolve, reject)：把两者写进 captured 的 cell
Value* capabilityExecutor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* cell = captured(ctx, "__cell");
    if (cell == nullptr) return ctx->createUndefined();
    auto* d = static_cast<ArrayData*>(cell->data());
    d->setElement(0, argc > 0 ? argv[0] : ctx->createUndefined());
    d->setElement(1, argc > 1 ? argv[1] : ctx->createUndefined());
    return ctx->createUndefined();
}

// 成功返回 nullptr 并填充 out；失败返回 Exception/Fatal。
Value* newPromiseCapability(IContext* ctx, Value* ctor, PromiseCapability& out) {
    // §27.2.1.5 step 1：C 必须是构造函数
    if (!detail::isConstructor(ctor)) {
        return detail::typeError(ctx, "Promise capability constructor is not a constructor");
    }
    Value* cell = makeCell(ctx);
    if (cell == ctx->oomValue()) return cell;
    static_cast<ArrayData*>(cell->data())->setElement(1, ctx->createUndefined());

    Value* executor = ctx->createNativeFunction(capabilityExecutor, 2);
    if (executor == ctx->oomValue()) return executor;
    ctx->captureValue(executor, "__cell", cell);

    Value* args[] = {executor};
    Value* promise = ctx->construct(ctor, 1, args);
    if (detail::isErrorValue(promise)) return promise;

    auto* d = static_cast<ArrayData*>(cell->data());
    Value* resolve = d->elementAt(0);
    Value* reject = d->elementAt(1);
    if (!detail::isCallable(resolve) || !detail::isCallable(reject)) {
        return detail::typeError(ctx, "Promise capability resolve/reject is not callable");
    }
    out.promise = promise;
    out.resolve = resolve;
    out.reject = reject;
    return nullptr;
}

// —— PerformPromiseThen（§27.2.5.4）——

Value* performPromiseThen(IContext* ctx, Value* promise, Value* onFulfilled,
                          Value* onRejected, const PromiseCapability& cap) {
    PromiseData* d = asPromise(promise);
    if (d == nullptr) {
        return detail::typeError(
            ctx, "Method Promise.prototype.then called on incompatible receiver");
    }
    // §27.2.5.4 step 8：注册 reaction 即视为已处理；若此前已因拒绝登记为
    // 未处理，通知宿主移出待上报集合（ADR-043 M4）。
    if (!d->isHandled) {
        d->isHandled = true;
        ctx->hostPromiseRejectionTracker(promise, true);
    }
    // §27.2.5.4 step 3/4：非可调用 handler 一律按 undefined 处理（透传）
    if (!detail::isCallable(onFulfilled)) onFulfilled = ctx->createUndefined();
    if (!detail::isCallable(onRejected)) onRejected = ctx->createUndefined();

    PromiseReaction fulfillReaction;
    fulfillReaction.capability = cap;
    fulfillReaction.type = PromiseReactionType::Fulfill;
    fulfillReaction.handler = onFulfilled;

    PromiseReaction rejectReaction;
    rejectReaction.capability = cap;
    rejectReaction.type = PromiseReactionType::Reject;
    rejectReaction.handler = onRejected;

    if (d->state == PromiseState::Pending) {
        d->reactions.push_back(fulfillReaction);
        d->reactions.push_back(rejectReaction);
        return cap.promise;
    }
    // 已 settle：只投递与当前状态同向的那个 reaction
    if (d->state == PromiseState::Fulfilled) {
        enqueueReactionJob(ctx, fulfillReaction, d->result);
    } else {
        enqueueReactionJob(ctx, rejectReaction, d->result);
    }
    return cap.promise;
}

// PromiseReactionJob（§27.2.2.1）
Value* reactionJob(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    Value* handler = captured(ctx, "__handler");
    Value* type = captured(ctx, "__type");
    Value* argument = captured(ctx, "__argument");
    Value* capPromise = captured(ctx, "__capPromise");
    Value* capResolve = captured(ctx, "__capResolve");
    Value* capReject = captured(ctx, "__capReject");

    // capability 为空（仅未处理拒绝跟踪）→ 无派生 promise
    if (capPromise == nullptr || capPromise->type()->kind() == TypeKind::Undefined) {
        return ctx->createUndefined();
    }
    bool isReject = type != nullptr && detail::toBoolean(type);

    // handler 缺失（undefined/null）→ 按 reaction 方向透传
    if (handler == nullptr || handler->type()->kind() == TypeKind::Undefined ||
        handler->type()->kind() == TypeKind::Null) {
        Value* args[1] = {argument};
        return ctx->call(isReject ? capReject : capResolve, ctx->createUndefined(), 1, args);
    }

    Value* args[1] = {argument};
    Value* result = ctx->call(handler, ctx->createUndefined(), 1, args);
    if (detail::isErrorValue(result)) {
        Value* reason = exceptionReason(result);
        return ctx->call(capReject, ctx->createUndefined(), 1, &reason);
    }
    return ctx->call(capResolve, ctx->createUndefined(), 1, &result);
}

// —— PromiseResolve(C, x)（§27.2.1.6，finally 用）——

Value* promiseResolveCtor(IContext* ctx, Value* ctor, Value* x) {
    if (asPromise(x) != nullptr) {
        Value* xCtor = ctx->getProperty(x, "constructor");
        if (detail::isErrorValue(xCtor)) return xCtor;
        if (xCtor == ctor) return x;
    }
    PromiseCapability cap;
    if (Value* err = newPromiseCapability(ctx, ctor, cap)) return err;
    Value* args[1] = {x};
    Value* r = ctx->call(cap.resolve, ctx->createUndefined(), 1, args);
    if (detail::isErrorValue(r)) return r;
    return cap.promise;
}

// —— 构造函数 ——

// Promise(executor) 不带 new 调用 → TypeError（§27.2.3.1 step 1）。
Value* promiseCtor(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return detail::typeError(ctx, "Promise constructor cannot be invoked without 'new'");
}

// new Promise(executor)（§27.2.3.1）：
//   executor 必须可调用；同步调用 executor(resolve, reject)；executor 抛错 → reject。
Value* promiseConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc < 1 || !detail::isCallable(argv[0])) {
        return detail::typeError(ctx, "Promise resolver is not a function");
    }
    Value* promise = ctx->createPromise();
    if (promise == ctx->oomValue()) return promise;

    Value* resolve = nullptr;
    Value* reject = nullptr;
    if (Value* err = createResolvingFunctions(ctx, promise, resolve, reject)) return err;

    Value* args[2] = {resolve, reject};
    Value* r = ctx->call(argv[0], ctx->createUndefined(), 2, args);
    if (detail::isErrorValue(r)) {
        Value* reason = exceptionReason(r);
        ctx->call(reject, ctx->createUndefined(), 1, &reason);
    }
    return promise;
}

// —— Promise.prototype.then / catch / finally ——

// then(onFulfilled, onRejected)（§27.2.5.4）
Value* promiseThen(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (asPromise(self) == nullptr) {
        return detail::typeError(
            ctx, "Method Promise.prototype.then called on incompatible receiver");
    }
    Value* onFulfilled = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* onRejected = argc > 1 ? argv[1] : ctx->createUndefined();

    // 派生 promise 用 SpeciesConstructor(self, %Promise%)（§27.2.5.4 step 4）
    Value* defaultCtor = ctx->global()->lookup("Promise");
    Value* ctor = speciesConstructor(ctx, self, defaultCtor);
    if (detail::isErrorValue(ctor)) return ctor;
    PromiseCapability cap;
    if (Value* err = newPromiseCapability(ctx, ctor, cap)) return err;
    return performPromiseThen(ctx, self, onFulfilled, onRejected, cap);
}

// catch(onRejected)（§27.2.5.1）：Invoke(promise, "then", «undefined, onRejected»)。
// 规范要求走属性查找（可被覆写的 then），而非直接调内建 promiseThen。
Value* promiseCatch(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* onRejected = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* thenFn = ctx->getProperty(self, "then");
    if (detail::isErrorValue(thenFn)) return thenFn;
    if (!detail::isCallable(thenFn)) {
        return detail::typeError(ctx, "Promise.prototype.then is not a function");
    }
    Value* args[2] = {ctx->createUndefined(), onRejected};
    return ctx->call(thenFn, self, 2, args);
}

// finally 的 thenFinally：调用 onFinally，等待其结果，再透传原值
Value* finallyThenHandler(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* onFinally = captured(ctx, "__onFinally");
    Value* ctor = captured(ctx, "__ctor");
    // §27.2.5.3：thenFinally 的 value 来自 handler 实参，不是闭包捕获
    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();

    Value* r = ctx->call(onFinally, ctx->createUndefined(), 0, nullptr);
    if (detail::isErrorValue(r)) return r;

    Value* p = promiseResolveCtor(ctx, ctor, r);
    if (detail::isErrorValue(p)) return p;

    // valueThunk：返回原值
    Value* thunk = ctx->createNativeFunction(
        [](IContext* c, Value* /*self*/, size_t, Value*[]) { return captured(c, "__value"); },
        0);
    if (thunk == ctx->oomValue()) return thunk;
    ctx->captureValue(thunk, "__value", value);
    Value* thenFn = ctx->getProperty(p, "then");
    if (detail::isErrorValue(thenFn)) return thenFn;
    Value* args[1] = {thunk};
    return ctx->call(thenFn, p, 1, args);
}

// finally 的 catchFinally：调用 onFinally，等待其结果，再重新抛出原原因
Value* finallyCatchHandler(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* onFinally = captured(ctx, "__onFinally");
    Value* ctor = captured(ctx, "__ctor");
    // §27.2.5.3：catchFinally 的 reason 来自 handler 实参
    Value* reason = argc > 0 ? argv[0] : ctx->createUndefined();

    Value* r = ctx->call(onFinally, ctx->createUndefined(), 0, nullptr);
    if (detail::isErrorValue(r)) return r;

    Value* p = promiseResolveCtor(ctx, ctor, r);
    if (detail::isErrorValue(p)) return p;

    // thrower：以 Exception 值返回 → 拒绝
    Value* thrower = ctx->createNativeFunction(
        [](IContext* c, Value* /*self*/, size_t, Value*[]) {
            return c->createException(captured(c, "__reason"));
        }, 0);
    if (thrower == ctx->oomValue()) return thrower;
    ctx->captureValue(thrower, "__reason", reason);
    Value* thenFn = ctx->getProperty(p, "then");
    if (detail::isErrorValue(thenFn)) return thenFn;
    Value* args[1] = {thrower};
    return ctx->call(thenFn, p, 1, args);
}

// finally(onFinally)（§27.2.5.3）
Value* promiseFinally(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (asPromise(self) == nullptr) {
        return detail::typeError(
            ctx, "Method Promise.prototype.finally called on incompatible receiver");
    }
    Value* onFinally = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* thenFn = ctx->getProperty(self, "then");
    if (detail::isErrorValue(thenFn)) return thenFn;

    if (!detail::isCallable(onFinally)) {
        Value* args[2] = {onFinally, onFinally};
        return ctx->call(thenFn, self, 2, args);
    }

    // §27.2.5.3 step 4：C = SpeciesConstructor(promise, %Promise%)
    Value* ctor = speciesConstructor(ctx, self, ctx->global()->lookup("Promise"));
    if (detail::isErrorValue(ctor)) return ctor;

    Value* thenFinally = ctx->createNativeFunction(finallyThenHandler, 1);
    if (thenFinally == ctx->oomValue()) return thenFinally;
    ctx->captureValue(thenFinally, "__onFinally", onFinally);
    ctx->captureValue(thenFinally, "__ctor", ctor);

    Value* catchFinally = ctx->createNativeFunction(finallyCatchHandler, 1);
    if (catchFinally == ctx->oomValue()) return catchFinally;
    ctx->captureValue(catchFinally, "__onFinally", onFinally);
    ctx->captureValue(catchFinally, "__ctor", ctor);

    Value* args[2] = {thenFinally, catchFinally};
    return ctx->call(thenFn, self, 2, args);
}

// —— SpeciesConstructor（§7.3.20）——
// 取 O.constructor，未定义则用 defaultCtor；否则取 C[@@species]，
// null/undefined 回退 defaultCtor，非可调用报 TypeError。
// 注：规范要求 IsConstructor(S)，本实现尚无可构造性判定，用 isCallable 近似。
Value* speciesConstructor(IContext* ctx, Value* obj, Value* defaultCtor) {
    Value* c = ctx->getProperty(obj, "constructor");
    if (detail::isErrorValue(c)) return c;
    if (c->type()->kind() == TypeKind::Undefined) return defaultCtor;
    if (!detail::isObjectKind(c->type()->kind())) {
        return detail::typeError(ctx, "Promise constructor property is not an object");
    }
    Value* s = ctx->getProperty(c, ctx->getWellKnownSymbol(WellKnownSymbol::Species));
    if (detail::isErrorValue(s)) return s;
    TypeKind sk = s->type()->kind();
    if (sk == TypeKind::Null || sk == TypeKind::Undefined) return defaultCtor;
    if (!detail::isConstructor(s)) {
        return detail::typeError(ctx, "Promise @@species is not a constructor");
    }
    return s;
}

// Promise[@@species] getter：返回 this（§27.2.4.5）
Value* promiseSpeciesGetter(IContext* /*ctx*/, Value* self, size_t /*argc*/,
                            Value* /*argv*/[]) {
    return self;
}

// —— 组合器共享状态 ——
// 状态单元（数组）：
//   [0] remainingElementsCount（Number）
//   [1] values / errors（Array）
//   [2] capability.promise
//   [3] capability.resolve
//   [4] capability.reject
Value* makeCombinatorCell(IContext* ctx, const PromiseCapability& cap) {
    Value* cell = ctx->createArray();
    if (cell == ctx->oomValue()) return cell;
    auto* cd = static_cast<ArrayData*>(cell->data());
    cd->setElement(0, ctx->createNumber(1.0));
    Value* values = ctx->createArray();
    if (values == ctx->oomValue()) return values;
    cd->setElement(1, values);
    cd->setElement(2, cap.promise);
    cd->setElement(3, cap.resolve);
    cd->setElement(4, cap.reject);
    return cell;
}

// remainingElementsCount -= 1；返回是否归零
bool decrementRemaining(IContext* ctx, Value* cell, double& remainingOut) {
    auto* cd = static_cast<ArrayData*>(cell->data());
    double n = static_cast<NumberData*>(cd->elementAt(0)->data())->value - 1.0;
    cd->setElement(0, ctx->createNumber(n));
    remainingOut = n;
    return n == 0.0;
}

// 元素函数：bound args 前置 [index, cell, calledCell]，实际实参为 value。
Value* makeElementFunction(IContext* ctx, NativeFn fn, uint32_t index, Value* cell,
                           Value* calledCell) {
    Value* target = ctx->createNativeFunction(fn, 0);
    if (target == ctx->oomValue()) return target;
    Value* bound[3] = {ctx->createNumber(static_cast<double>(index)), cell, calledCell};
    return ctx->createBoundFunction(target, ctx->createUndefined(), 3, bound);
}

// 从元素函数的 bound args 取出 index / cell / calledCell；重复调用返回 false。
bool elementPrologue(IContext* ctx, size_t argc, Value* argv[], uint32_t& index,
                     Value*& cell, Value*& value) {
    if (argc < 4) return false;
    Value* calledCell = argv[2];
    if (cellTaken(ctx, calledCell)) return false;
    takeCell(ctx, calledCell);
    index = static_cast<uint32_t>(
        static_cast<NumberData*>(argv[0]->data())->value);
    cell = argv[1];
    value = argv[3];
    return true;
}

// 把结果写进 values[index]，remaining 归零则 resolve(values)。
Value* storeAndMaybeResolve(IContext* ctx, Value* cell, uint32_t index, Value* value) {
    auto* cd = static_cast<ArrayData*>(cell->data());
    static_cast<ArrayData*>(cd->elementAt(1)->data())->setElement(index, value);
    double remaining = 0;
    if (!decrementRemaining(ctx, cell, remaining)) {
        return ctx->createUndefined();
    }
    Value* args[1] = {cd->elementAt(1)};
    Value* r = ctx->call(cd->elementAt(3), ctx->createUndefined(), 1, args);
    return r;
}

// Promise.all 的 resolve element（§27.2.4.1.2）
Value* allResolveElement(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    uint32_t index = 0;
    Value* cell = nullptr;
    Value* value = nullptr;
    if (!elementPrologue(ctx, argc, argv, index, cell, value)) {
        return ctx->createUndefined();
    }
    return storeAndMaybeResolve(ctx, cell, index, value);
}

// allSettled 的结果对象 {status, value|reason}
Value* makeSettledResult(IContext* ctx, const char* status, const char* key,
                         Value* payload) {
    Value* obj = ctx->createObject();
    if (obj == ctx->oomValue()) return obj;
    Value* r = ctx->defineProperty(obj, "status", ctx->createString(status), true, true,
                                   true);
    if (detail::isErrorValue(r)) return r;
    r = ctx->defineProperty(obj, key, payload, true, true, true);
    if (detail::isErrorValue(r)) return r;
    return obj;  // defineProperty 返回 value，不是 obj
}

// Promise.allSettled 的 fulfill element（§27.2.4.2.2）
Value* allSettledFulfillElement(IContext* ctx, Value* /*self*/, size_t argc,
                                Value* argv[]) {
    uint32_t index = 0;
    Value* cell = nullptr;
    Value* value = nullptr;
    if (!elementPrologue(ctx, argc, argv, index, cell, value)) {
        return ctx->createUndefined();
    }
    Value* obj = makeSettledResult(ctx, "fulfilled", "value", value);
    if (detail::isErrorValue(obj)) return obj;
    return storeAndMaybeResolve(ctx, cell, index, obj);
}

// Promise.allSettled 的 reject element（§27.2.4.2.3）
Value* allSettledRejectElement(IContext* ctx, Value* /*self*/, size_t argc,
                               Value* argv[]) {
    uint32_t index = 0;
    Value* cell = nullptr;
    Value* reason = nullptr;
    if (!elementPrologue(ctx, argc, argv, index, cell, reason)) {
        return ctx->createUndefined();
    }
    Value* obj = makeSettledResult(ctx, "rejected", "reason", reason);
    if (detail::isErrorValue(obj)) return obj;
    return storeAndMaybeResolve(ctx, cell, index, obj);
}

// 构造 AggregateError(errors, "All promises were rejected")
Value* makeAggregateError(IContext* ctx, Value* errors) {
    Value* ctor = ctx->global()->lookup("AggregateError");
    if (ctor == nullptr) {
        return detail::typeError(ctx, "internal: missing AggregateError");
    }
    Value* args[2] = {errors, ctx->createString("All promises were rejected")};
    return ctx->construct(ctor, 2, args);
}

// Promise.any 的 reject element（§27.2.4.3.2）
Value* anyRejectElement(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    uint32_t index = 0;
    Value* cell = nullptr;
    Value* error = nullptr;
    if (!elementPrologue(ctx, argc, argv, index, cell, error)) {
        return ctx->createUndefined();
    }
    auto* cd = static_cast<ArrayData*>(cell->data());
    static_cast<ArrayData*>(cd->elementAt(1)->data())->setElement(index, error);

    double remaining = 0;
    if (!decrementRemaining(ctx, cell, remaining)) return ctx->createUndefined();
    Value* agg = makeAggregateError(ctx, cd->elementAt(1));
    if (detail::isErrorValue(agg)) return agg;
    Value* args[1] = {agg};
    return ctx->call(cd->elementAt(4), ctx->createUndefined(), 1, args);
}

// GetPromiseResolve(C)（§27.2.4.1.1 step 2）：取 C.resolve 并要求可调用。
// 组合器必须走它而不是直接调内建 PromiseResolve——否则用户覆写 C.resolve 不生效。
Value* getPromiseResolve(IContext* ctx, Value* ctor) {
    Value* resolve = ctx->getProperty(ctor, "resolve");
    if (detail::isErrorValue(resolve)) return resolve;
    if (!detail::isCallable(resolve)) {
        return detail::typeError(ctx, "Promise resolve is not a function");
    }
    return resolve;
}

// —— 组合器主流程（§27.2.4.1.1 / .2.1 / .3.1 / .4.1）——
enum class PromiseCombinator { All, AllSettled, Any };

// 成功返回 nullptr；失败返回 Exception/Fatal（调用方负责 reject capability）。
Value* performCombinator(IContext* ctx, Value* iterable, Value* ctor,
                         const PromiseCapability& cap, PromiseCombinator kind) {
    // §27.2.4.1.1 step 2：先取 C.resolve（失败则整体失败，且此时尚未取迭代器）
    Value* promiseResolve = getPromiseResolve(ctx, ctor);
    if (detail::isErrorValue(promiseResolve)) return promiseResolve;

    Value* cell = makeCombinatorCell(ctx, cap);
    if (cell == ctx->oomValue()) return cell;
    auto* cd = static_cast<ArrayData*>(cell->data());
    Value* values = cd->elementAt(1);

    Value* iterator = detail::getIterator(ctx, iterable);
    if (detail::isErrorValue(iterator)) return iterator;

    uint32_t index = 0;
    for (;;) {
        Value* next = detail::iteratorNext(ctx, iterator);
        if (detail::isErrorValue(next)) {
            return detail::iteratorClose(ctx, iterator, next);
        }
        Value* err = nullptr;
        bool done = detail::iteratorComplete(ctx, next, err);
        if (err != nullptr) return detail::iteratorClose(ctx, iterator, err);
        if (done) break;

        Value* nextValue = detail::iteratorValue(ctx, next);
        if (detail::isErrorValue(nextValue)) {
            return detail::iteratorClose(ctx, iterator, nextValue);
        }
        // values.append(undefined)
        static_cast<ArrayData*>(values->data())->setElement(index, ctx->createUndefined());

        // §27.2.4.1.1 step 8.b：调 C.resolve(nextValue)
        Value* resolveArgs[1] = {nextValue};
        Value* nextPromise = ctx->call(promiseResolve, ctor, 1, resolveArgs);
        if (detail::isErrorValue(nextPromise)) {
            return detail::iteratorClose(ctx, iterator, nextPromise);
        }

        Value* calledCell = makeCell(ctx);
        if (calledCell == ctx->oomValue()) return calledCell;

        Value* onFulfilled = nullptr;
        Value* onRejected = cap.reject;
        switch (kind) {
            case PromiseCombinator::All:
                onFulfilled = makeElementFunction(ctx, allResolveElement, index, cell,
                                                  calledCell);
                break;
            case PromiseCombinator::AllSettled:
                onFulfilled = makeElementFunction(ctx, allSettledFulfillElement, index,
                                                  cell, calledCell);
                onRejected = makeElementFunction(ctx, allSettledRejectElement, index,
                                                 cell, calledCell);
                break;
            case PromiseCombinator::Any:
                onFulfilled = cap.resolve;
                onRejected = makeElementFunction(ctx, anyRejectElement, index, cell,
                                                 calledCell);
                break;
        }
        if (detail::isErrorValue(onFulfilled)) return onFulfilled;
        if (detail::isErrorValue(onRejected)) return onRejected;

        Value* thenFn = ctx->getProperty(nextPromise, "then");
        if (detail::isErrorValue(thenFn)) {
            return detail::iteratorClose(ctx, iterator, thenFn);
        }
        Value* thenArgs[2] = {onFulfilled, onRejected};
        Value* r = ctx->call(thenFn, nextPromise, 2, thenArgs);
        if (detail::isErrorValue(r)) return detail::iteratorClose(ctx, iterator, r);

        ++index;
        double n = static_cast<NumberData*>(cd->elementAt(0)->data())->value + 1.0;
        cd->setElement(0, ctx->createNumber(n));
    }

    double remaining = 0;
    if (decrementRemaining(ctx, cell, remaining)) {
        if (kind == PromiseCombinator::Any) {
            // 空 iterable（或全部已 reject）→ AggregateError
            Value* agg = makeAggregateError(ctx, values);
            if (detail::isErrorValue(agg)) return agg;
            Value* args[1] = {agg};
            Value* r = ctx->call(cap.reject, ctx->createUndefined(), 1, args);
            if (detail::isErrorValue(r)) return r;
        } else {
            Value* args[1] = {values};
            Value* r = ctx->call(cap.resolve, ctx->createUndefined(), 1, args);
            if (detail::isErrorValue(r)) return r;
        }
    }
    return nullptr;
}

// 组合器入口：建 capability，跑主流程；失败则 reject 并返回 promise。
Value* promiseCombinatorEntry(IContext* ctx, Value* self, size_t argc, Value* argv[],
                              PromiseCombinator kind) {
    if (!detail::isConstructor(self)) {
        return detail::typeError(ctx, "Promise combinator called on non-constructor");
    }
    PromiseCapability cap;
    if (Value* err = newPromiseCapability(ctx, self, cap)) return err;

    Value* iterable = argc > 0 ? argv[0] : ctx->createUndefined();
    if (Value* err = performCombinator(ctx, iterable, self, cap, kind)) {
        Value* reason = exceptionReason(err);
        Value* args[1] = {reason};
        Value* r = ctx->call(cap.reject, ctx->createUndefined(), 1, args);
        if (detail::isErrorValue(r)) return r;
    }
    return cap.promise;
}

Value* promiseAll(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return promiseCombinatorEntry(ctx, self, argc, argv, PromiseCombinator::All);
}
Value* promiseAllSettled(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return promiseCombinatorEntry(ctx, self, argc, argv, PromiseCombinator::AllSettled);
}
Value* promiseAny(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return promiseCombinatorEntry(ctx, self, argc, argv, PromiseCombinator::Any);
}

// Promise.race（§27.2.4.4.1）：不收集结果，首个 settle 直接透传。
Value* promiseRace(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (!detail::isConstructor(self)) {
        return detail::typeError(ctx, "Promise.race called on non-constructor");
    }
    PromiseCapability cap;
    if (Value* err = newPromiseCapability(ctx, self, cap)) return err;

    // §27.2.4.4.1 step 2：先取 C.resolve
    Value* promiseResolve = getPromiseResolve(ctx, self);
    if (detail::isErrorValue(promiseResolve)) {
        Value* reason = exceptionReason(promiseResolve);
        Value* args[1] = {reason};
        Value* r = ctx->call(cap.reject, ctx->createUndefined(), 1, args);
        if (detail::isErrorValue(r)) return r;
        return cap.promise;
    }

    Value* iterable = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* iterator = detail::getIterator(ctx, iterable);
    if (detail::isErrorValue(iterator)) {
        Value* reason = exceptionReason(iterator);
        Value* args[1] = {reason};
        Value* r = ctx->call(cap.reject, ctx->createUndefined(), 1, args);
        if (detail::isErrorValue(r)) return r;
        return cap.promise;
    }

    for (;;) {
        Value* next = detail::iteratorNext(ctx, iterator);
        if (detail::isErrorValue(next)) {
            Value* closed = detail::iteratorClose(ctx, iterator, next);
            Value* reason = exceptionReason(closed);
            Value* args[1] = {reason};
            Value* r = ctx->call(cap.reject, ctx->createUndefined(), 1, args);
            if (detail::isErrorValue(r)) return r;
            return cap.promise;
        }
        Value* err = nullptr;
        bool done = detail::iteratorComplete(ctx, next, err);
        if (err != nullptr) {
            Value* reason = exceptionReason(err);
            Value* args[1] = {reason};
            Value* r = ctx->call(cap.reject, ctx->createUndefined(), 1, args);
            if (detail::isErrorValue(r)) return r;
            return cap.promise;
        }
        if (done) return cap.promise;

        Value* nextValue = detail::iteratorValue(ctx, next);
        if (detail::isErrorValue(nextValue)) {
            Value* reason = exceptionReason(nextValue);
            Value* args[1] = {reason};
            Value* r = ctx->call(cap.reject, ctx->createUndefined(), 1, args);
            if (detail::isErrorValue(r)) return r;
            return cap.promise;
        }
        // §27.2.4.4.1 step 6.a：调 C.resolve(nextValue)
        Value* resolveArgs[1] = {nextValue};
        Value* nextPromise = ctx->call(promiseResolve, self, 1, resolveArgs);
        if (detail::isErrorValue(nextPromise)) {
            Value* reason = exceptionReason(nextPromise);
            Value* args[1] = {reason};
            Value* r = ctx->call(cap.reject, ctx->createUndefined(), 1, args);
            if (detail::isErrorValue(r)) return r;
            return cap.promise;
        }
        Value* thenFn = ctx->getProperty(nextPromise, "then");
        if (detail::isErrorValue(thenFn)) {
            Value* reason = exceptionReason(thenFn);
            Value* args[1] = {reason};
            Value* r = ctx->call(cap.reject, ctx->createUndefined(), 1, args);
            if (detail::isErrorValue(r)) return r;
            return cap.promise;
        }
        Value* thenArgs[2] = {cap.resolve, cap.reject};
        Value* r = ctx->call(thenFn, nextPromise, 2, thenArgs);
        if (detail::isErrorValue(r)) {
            Value* reason = exceptionReason(r);
            Value* args[1] = {reason};
            Value* rr = ctx->call(cap.reject, ctx->createUndefined(), 1, args);
            if (detail::isErrorValue(rr)) return rr;
            return cap.promise;
        }
    }
}

// —— Promise.resolve / Promise.reject（§27.2.4.7 / §27.2.4.8）——

Value* promiseResolveStatic(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (!detail::isConstructor(self)) {
        return detail::typeError(ctx, "Promise.resolve called on non-constructor");
    }
    Value* x = argc > 0 ? argv[0] : ctx->createUndefined();
    return promiseResolveCtor(ctx, self, x);
}

Value* promiseRejectStatic(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (!detail::isConstructor(self)) {
        return detail::typeError(ctx, "Promise.reject called on non-constructor");
    }
    PromiseCapability cap;
    if (Value* err = newPromiseCapability(ctx, self, cap)) return err;
    Value* r = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* args[1] = {r};
    Value* res = ctx->call(cap.reject, ctx->createUndefined(), 1, args);
    if (detail::isErrorValue(res)) return res;
    return cap.promise;
}

// Promise.withResolvers()（ES2024 §27.2.4.9）：返回 { promise, resolve, reject }。
Value* promiseWithResolvers(IContext* ctx, Value* self, size_t /*argc*/,
                            Value* /*argv*/[]) {
    if (!detail::isConstructor(self)) {
        return detail::typeError(ctx, "Promise.withResolvers called on non-constructor");
    }
    PromiseCapability cap;
    if (Value* err = newPromiseCapability(ctx, self, cap)) return err;

    Value* obj = ctx->createObject();
    if (obj == ctx->oomValue()) return obj;
    Value* r = ctx->defineProperty(obj, "promise", cap.promise, true, true, true);
    if (detail::isErrorValue(r)) return r;
    r = ctx->defineProperty(obj, "resolve", cap.resolve, true, true, true);
    if (detail::isErrorValue(r)) return r;
    r = ctx->defineProperty(obj, "reject", cap.reject, true, true, true);
    if (detail::isErrorValue(r)) return r;
    return obj;
}

// 把值挂到 global scope：add 跟踪 + bind 名字
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->global()->add(v);
    ctx->global()->bind(name, v);
}

// 定义 prototype 方法（writable/不可枚举/可配置，与 ES 一致）。
// lengthOverride 非负时覆盖 "length" 属性（规范 length ≠ 实参校验下限的情形）。
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc, int lengthOverride = -1) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
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

}  // namespace

bool registerPromiseBuiltins(IContext* ctx) {
    // —— 1. Promise.prototype（普通对象，原型 = Object.prototype）——
    Value* promiseProto = ctx->createObject();
    if (promiseProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(promiseProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }

    // 原型就绪：后续 createPromise 自动挂链
    ctx->setPromisePrototype(promiseProto);

    // —— 2. Promise 构造函数 ——
    Value* promiseFn = ctx->createNativeFunction(promiseCtor, 1);
    if (promiseFn == ctx->oomValue()) return false;
    ctx->setFunctionName(promiseFn, "Promise");
    ctx->setFunctionConstructor(promiseFn, promiseConstruct);
    if (ctx->defineProperty(promiseFn, "prototype", promiseProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(promiseProto, "constructor", promiseFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 3. Promise.prototype 方法 ——
    // minArgc = 0：handler 可省略（§27.2.5.4：undefined 即透传）。
    // 但规范的 length 属性都是 1，故显式覆盖（minArgc 同时用于实参校验，不能直接设为 1）。
    if (!defineMethod(ctx, promiseProto, "then", promiseThen, 0, 1)) return false;
    if (!defineMethod(ctx, promiseProto, "catch", promiseCatch, 0, 1)) return false;
    if (!defineMethod(ctx, promiseProto, "finally", promiseFinally, 0, 1)) return false;

    // —— 4. @@toStringTag = "Promise"（§27.2.5.5）——
    if (ctx->defineProperty(promiseProto,
                            ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            ctx->createString("Promise"), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 5. Promise[@@species]（§27.2.4.5）：getter 返回 this ——
    Value* speciesGet = ctx->createNativeFunction(promiseSpeciesGetter, 0);
    if (speciesGet == ctx->oomValue()) return false;
    ctx->setFunctionName(speciesGet, "get [Symbol.species]");
    if (ctx->defineAccessor(promiseFn,
                            ctx->getWellKnownSymbol(WellKnownSymbol::Species),
                            speciesGet, nullptr, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 6. 静态方法 ——
    if (!defineMethod(ctx, promiseFn, "resolve", promiseResolveStatic, 1)) return false;
    if (!defineMethod(ctx, promiseFn, "reject", promiseRejectStatic, 1)) return false;
    if (!defineMethod(ctx, promiseFn, "all", promiseAll, 1)) return false;
    if (!defineMethod(ctx, promiseFn, "race", promiseRace, 1)) return false;
    if (!defineMethod(ctx, promiseFn, "allSettled", promiseAllSettled, 1)) return false;
    if (!defineMethod(ctx, promiseFn, "any", promiseAny, 1)) return false;
    if (!defineMethod(ctx, promiseFn, "withResolvers", promiseWithResolvers, 0)) {
        return false;
    }

    // —— 7. 注册到 global ——
    bindGlobal(ctx, "Promise", promiseFn);
    return true;
}

}  // namespace neo::core
