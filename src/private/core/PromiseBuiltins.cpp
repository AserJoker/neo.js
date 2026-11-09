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
//     的函数（bound args 是 data 级引用，不构成 scopeRef 根，避免根环）。
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

    for (const PromiseReaction& r : reactions) {
        enqueueReactionJob(ctx, r, value);
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
// bound args 是 data 级引用（FunctionData::boundArgs），不构成 scopeRef 根；
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
    // 一旦有 reaction 注册，视为已处理（未处理拒绝上报用，M4）
    d->isHandled = true;
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

    // M2：派生 promise 用 %Promise%（@@species 在 M3）
    Value* ctor = ctx->global()->lookup("Promise");
    PromiseCapability cap;
    if (Value* err = newPromiseCapability(ctx, ctor, cap)) return err;
    return performPromiseThen(ctx, self, onFulfilled, onRejected, cap);
}

// catch(onRejected)（§27.2.5.1）：等价 then(undefined, onRejected)
Value* promiseCatch(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* onRejected = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* args[2] = {ctx->createUndefined(), onRejected};
    return promiseThen(ctx, self, 2, args);
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

    Value* ctor = ctx->global()->lookup("Promise");  // M2：%Promise%

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
    // handler 均可省略（§27.2.5.4：undefined 即透传），故 minArgc = 0。
    if (!defineMethod(ctx, promiseProto, "then", promiseThen, 0)) return false;
    if (!defineMethod(ctx, promiseProto, "catch", promiseCatch, 0)) return false;
    if (!defineMethod(ctx, promiseProto, "finally", promiseFinally, 0)) return false;

    // —— 4. @@toStringTag = "Promise"（§27.2.5.5）——
    if (ctx->defineProperty(promiseProto,
                            ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            ctx->createString("Promise"), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 5. 注册到 global ——
    bindGlobal(ctx, "Promise", promiseFn);
    return true;
}

}  // namespace neo::core
