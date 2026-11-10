// AsyncGeneratorBuiltins.cpp - 异步生成器协议与驱动（ES2024 §27.6 / ADR-045）
//
// 协议要点：
//   - 每个 next/return/throw 创建一个新 Promise（capability）并入队；
//   - 请求串行处理：同一时刻只有一个请求在 Executing/Awaiting*；
//   - yield 的值先 Await（Promise.resolve(v).then）再 resolve {value,done:false}；
//   - Await 拒绝时把异常抛回生成器（yield 表达式处），由函数体决定 catch/finally；
//   - 正常完成/return 的值 await 后 resolve {value,done:true}；
//   - 未启动时 return 直接完成、throw 直接 reject，不运行函数体；
//   - 已完成时 next resolve {undefined,true}、return resolve {value,true}、throw reject。
//
// 驱动是同步 resume + 微任务 await 的组合：resumeCoroutine 是同步原语，
// await 通过 Promise.then 回调在后续微任务里继续。
//
// 关联 ADR: 043（Promise）、045（协程抽象）、046（Interrupt）

#include "core/AsyncGeneratorBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/PromiseCapability.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/AsyncGenerator.h"
#include "core/types/Exception.h"
#include "core/types/Interrupt.h"
#include "core/types/Number.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

namespace {

// —— 小工具 ——

Value* undefinedOf(IContext* ctx) { return ctx->createUndefined(); }

// 迭代器结果对象 { value, done }（ES2024 §7.4.1 CreateIterResultObject）。
Value* createIterResult(IContext* ctx, Value* value, bool done) {
    Value* result = ctx->createObject();
    if (result == ctx->oomValue()) return result;
    Value* r = ctx->defineProperty(result, "value", value, true, true, true);
    if (detail::isErrorValue(r)) return r;
    r = ctx->defineProperty(result, "done", ctx->createBoolean(done), true, true, true);
    return detail::isErrorValue(r) ? r : result;
}

// 取当前 native 函数闭包作用域里的捕获值（与 PromiseBuiltins 同模式）。
Value* captured(IContext* ctx, const char* name) {
    Scope* cur = ctx->current();
    Scope* closure = cur != nullptr ? cur->parent() : nullptr;
    return closure != nullptr ? closure->lookup(name) : nullptr;
}

// 异常值 → 拒绝原因（Exception 携带的 Error 对象；其他值原样）。
Value* reasonOf(Value* v) {
    if (v != nullptr && v->type()->kind() == TypeKind::Exception) {
        return static_cast<ExceptionData*>(v->data())->child;
    }
    return v;
}

// throw 注入：内部已是 Exception 通道值则原样，否则包装成可捕获异常。
Value* asException(IContext* ctx, Value* e) {
    if (e->type()->kind() == TypeKind::Exception) return e;
    return ctx->createException(e);
}

// —— 前向声明 ——
void asyncGeneratorResumeNext(IContext* ctx, Value* gen);
void resumeBody(IContext* ctx, Value* gen, Value* sent);
void awaitValue(IContext* ctx, Value* gen, Value* value, int kind);

// —— 请求 settle ——

void settleResolve(IContext* ctx, AsyncGeneratorData* ag, Value* value, bool done) {
    Value* resolve = ag->currentResolve;
    Value* reject = ag->currentReject;
    ag->currentResolve = nullptr;
    ag->currentReject = nullptr;
    if (resolve == nullptr) return;
    Value* result = createIterResult(ctx, value, done);
    if (detail::isErrorValue(result)) {
        // 结果对象创建失败：改为拒绝
        if (reject != nullptr) ctx->call(reject, undefinedOf(ctx), 1, &result);
        return;
    }
    ctx->call(resolve, undefinedOf(ctx), 1, &result);
}

void settleReject(IContext* ctx, AsyncGeneratorData* ag, Value* reason) {
    Value* reject = ag->currentReject;
    ag->currentResolve = nullptr;
    ag->currentReject = nullptr;
    if (reject == nullptr) return;
    ctx->call(reject, undefinedOf(ctx), 1, &reason);
}

// —— await 回调 ——

// kind: 0 = yield 值（resolve {value,false}），1 = 完成/return 值（resolve {value,true}）
Value* awaitFulfilled(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* gen = captured(ctx, "__gen");
    Value* kindV = captured(ctx, "__kind");
    AsyncGeneratorData* ag = asAsyncGenerator(gen);
    if (ag == nullptr || kindV == nullptr) return undefinedOf(ctx);
    int kind = static_cast<int>(static_cast<NumberData*>(kindV->data())->value);
    Value* v = argc > 0 ? argv[0] : undefinedOf(ctx);

    if (kind == 0) {
        ag->asyncState = AsyncGeneratorState::SuspendedYield;
        settleResolve(ctx, ag, v, false);
    } else {
        ag->asyncState = AsyncGeneratorState::Completed;
        settleResolve(ctx, ag, v, true);
    }
    asyncGeneratorResumeNext(ctx, gen);
    return undefinedOf(ctx);
}

Value* awaitRejected(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* gen = captured(ctx, "__gen");
    Value* kindV = captured(ctx, "__kind");
    AsyncGeneratorData* ag = asAsyncGenerator(gen);
    if (ag == nullptr || kindV == nullptr) return undefinedOf(ctx);
    int kind = static_cast<int>(static_cast<NumberData*>(kindV->data())->value);
    Value* reason = argc > 0 ? argv[0] : undefinedOf(ctx);

    if (kind == 0) {
        // yield 值 await 失败：把异常抛回生成器（yield 表达式处），当前请求保持挂起。
        Value* exc = asException(ctx, reason);
        Value* sig = ctx->createInterrupt(InterruptKind::Throw, ag->resumePoint, exc);
        resumeBody(ctx, gen, sig);
    } else {
        ag->asyncState = AsyncGeneratorState::Completed;
        settleReject(ctx, ag, reason);
        asyncGeneratorResumeNext(ctx, gen);
    }
    return undefinedOf(ctx);
}

// —— Await(value)：Promise.resolve(value).then(onFulfilled, onRejected) ——

void awaitValue(IContext* ctx, Value* gen, Value* value, int kind) {
    AsyncGeneratorData* ag = asAsyncGenerator(gen);
    Value* ctor = ctx->promiseCtor();
    if (ag == nullptr || ctor == nullptr) {
        // 内建未就绪（不应发生）：退化为立即 settle
        if (ag != nullptr) {
            if (kind == 0) {
                ag->asyncState = AsyncGeneratorState::SuspendedYield;
                settleResolve(ctx, ag, value, false);
            } else {
                ag->asyncState = AsyncGeneratorState::Completed;
                settleResolve(ctx, ag, value, true);
            }
            asyncGeneratorResumeNext(ctx, gen);
        }
        return;
    }

    Value* resolveFn = ctx->getProperty(ctor, "resolve");
    if (detail::isErrorValue(resolveFn)) {
        ag->asyncState = AsyncGeneratorState::Completed;
        settleReject(ctx, ag, reasonOf(resolveFn));
        asyncGeneratorResumeNext(ctx, gen);
        return;
    }
    Value* promise = ctx->call(resolveFn, ctor, 1, &value);
    if (detail::isErrorValue(promise)) {
        ag->asyncState = AsyncGeneratorState::Completed;
        settleReject(ctx, ag, reasonOf(promise));
        asyncGeneratorResumeNext(ctx, gen);
        return;
    }

    Value* thenFn = ctx->getProperty(promise, "then");
    if (detail::isErrorValue(thenFn)) {
        ag->asyncState = AsyncGeneratorState::Completed;
        settleReject(ctx, ag, reasonOf(thenFn));
        asyncGeneratorResumeNext(ctx, gen);
        return;
    }

    Value* onFulfilled = ctx->createNativeFunction(awaitFulfilled, 1);
    Value* onRejected = ctx->createNativeFunction(awaitRejected, 1);
    if (onFulfilled == ctx->oomValue() || onRejected == ctx->oomValue()) {
        ag->asyncState = AsyncGeneratorState::Completed;
        settleReject(ctx, ag, ctx->oomValue());
        asyncGeneratorResumeNext(ctx, gen);
        return;
    }
    // 捕获生成器与 kind：job 在后续微任务执行，必须让 GC 保活生成器。
    ctx->captureValue(onFulfilled, "__gen", gen);
    ctx->captureValue(onFulfilled, "__kind",
                      ctx->createNumber(static_cast<double>(kind)));
    ctx->captureValue(onRejected, "__gen", gen);
    ctx->captureValue(onRejected, "__kind",
                      ctx->createNumber(static_cast<double>(kind)));

    Value* args[2] = {onFulfilled, onRejected};
    Value* r = ctx->call(thenFn, promise, 2, args);
    if (detail::isErrorValue(r)) {
        ag->asyncState = AsyncGeneratorState::Completed;
        settleReject(ctx, ag, reasonOf(r));
        asyncGeneratorResumeNext(ctx, gen);
    }
}

// —— resume 一次函数体并解释结果 ——

void resumeBody(IContext* ctx, Value* gen, Value* sent) {
    AsyncGeneratorData* ag = asAsyncGenerator(gen);
    if (ag == nullptr) return;
    ag->asyncState = AsyncGeneratorState::Executing;

    Value* res = ctx->resumeCoroutine(gen, sent);
    if (res == nullptr) res = undefinedOf(ctx);

    TypeKind k = res->type()->kind();
    if (k == TypeKind::Exception || k == TypeKind::Fatal) {
        ag->asyncState = AsyncGeneratorState::Completed;
        settleReject(ctx, ag, reasonOf(res));
        asyncGeneratorResumeNext(ctx, gen);
        return;
    }

    if (detail::isInterruptValue(res)) {
        InterruptData* ir = asInterrupt(res);
        if (ir->kind == InterruptKind::Yield) {
            ag->asyncState = AsyncGeneratorState::AwaitingYield;
            awaitValue(ctx, gen, ir->value, 0);
            return;
        }
        if (ir->kind == InterruptKind::Throw) {
            ag->asyncState = AsyncGeneratorState::Completed;
            settleReject(ctx, ag, reasonOf(ir->value));
            asyncGeneratorResumeNext(ctx, gen);
            return;
        }
        // Return：await 返回值后 resolve {value,true}
        ag->asyncState = AsyncGeneratorState::AwaitingReturn;
        awaitValue(ctx, gen, ir->value, 1);
        return;
    }

    // 正常完成：await 返回值后 resolve {value,true}
    ag->asyncState = AsyncGeneratorState::AwaitingReturn;
    awaitValue(ctx, gen, res, 1);
}

// —— 队列驱动 ——

void asyncGeneratorResumeNext(IContext* ctx, Value* gen) {
    AsyncGeneratorData* ag = asAsyncGenerator(gen);
    if (ag == nullptr) return;
    // 正在执行或等待 await：由后续回调再次驱动
    if (ag->asyncState == AsyncGeneratorState::Executing ||
        ag->asyncState == AsyncGeneratorState::AwaitingYield ||
        ag->asyncState == AsyncGeneratorState::AwaitingReturn) {
        return;
    }
    if (ag->queue.empty()) return;

    // 已完成：按请求类型立即 settle，逐个排空
    if (ag->asyncState == AsyncGeneratorState::Completed) {
        while (!ag->queue.empty()) {
            AsyncGeneratorRequest req = ag->queue.front();
            ag->queue.erase(ag->queue.begin());
            ag->currentResolve = req.resolve;
            ag->currentReject = req.reject;
            switch (req.type) {
                case AsyncGeneratorRequestType::Next:
                    settleResolve(ctx, ag, undefinedOf(ctx), true);
                    break;
                case AsyncGeneratorRequestType::Return:
                    settleResolve(ctx, ag, req.value, true);
                    break;
                case AsyncGeneratorRequestType::Throw:
                    settleReject(ctx, ag, req.value);
                    break;
            }
        }
        return;
    }

    AsyncGeneratorRequest req = ag->queue.front();
    ag->queue.erase(ag->queue.begin());
    ag->currentResolve = req.resolve;
    ag->currentReject = req.reject;

    if (ag->asyncState == AsyncGeneratorState::SuspendedStart) {
        // 未启动：return 直接完成、throw 直接拒绝，不运行函数体
        if (req.type == AsyncGeneratorRequestType::Return) {
            ag->asyncState = AsyncGeneratorState::Completed;
            settleResolve(ctx, ag, req.value, true);
            asyncGeneratorResumeNext(ctx, gen);
            return;
        }
        if (req.type == AsyncGeneratorRequestType::Throw) {
            ag->asyncState = AsyncGeneratorState::Completed;
            settleReject(ctx, ag, req.value);
            asyncGeneratorResumeNext(ctx, gen);
            return;
        }
        // 首次 next 的实参按规范被丢弃
        resumeBody(ctx, gen, undefinedOf(ctx));
        return;
    }

    // SuspendedYield
    Value* sent = nullptr;
    switch (req.type) {
        case AsyncGeneratorRequestType::Next:
            sent = req.value;
            break;
        case AsyncGeneratorRequestType::Return:
            sent = ctx->createInterrupt(InterruptKind::Return, ag->resumePoint,
                                        req.value);
            break;
        case AsyncGeneratorRequestType::Throw:
            sent = ctx->createInterrupt(InterruptKind::Throw, ag->resumePoint,
                                        asException(ctx, req.value));
            break;
    }
    resumeBody(ctx, gen, sent);
}

// —— 入队 ——

Value* asyncGeneratorEnqueue(IContext* ctx, Value* gen,
                             AsyncGeneratorRequestType type, Value* value) {
    AsyncGeneratorData* ag = asAsyncGenerator(gen);
    if (ag == nullptr) {
        return detail::typeError(ctx, "AsyncGenerator method called on incompatible receiver");
    }
    Value* ctor = ctx->promiseCtor();
    if (ctor == nullptr) {
        return detail::typeError(ctx, "Promise intrinsic is not available");
    }
    PromiseCapability cap;
    if (Value* err = createPromiseCapability(ctx, ctor, cap)) return err;

    AsyncGeneratorRequest req;
    req.type = type;
    req.value = value;
    req.promise = cap.promise;
    req.resolve = cap.resolve;
    req.reject = cap.reject;
    ag->queue.push_back(req);

    asyncGeneratorResumeNext(ctx, gen);
    return cap.promise;
}

// —— %AsyncGeneratorPrototype% 方法 ——

Value* asyncGeneratorNext(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* v = argc > 0 ? argv[0] : undefinedOf(ctx);
    return asyncGeneratorEnqueue(ctx, self, AsyncGeneratorRequestType::Next, v);
}

Value* asyncGeneratorReturn(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* v = argc > 0 ? argv[0] : undefinedOf(ctx);
    return asyncGeneratorEnqueue(ctx, self, AsyncGeneratorRequestType::Return, v);
}

Value* asyncGeneratorThrow(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* v = argc > 0 ? argv[0] : undefinedOf(ctx);
    return asyncGeneratorEnqueue(ctx, self, AsyncGeneratorRequestType::Throw, v);
}

Value* asyncIterator(IContext* /*ctx*/, Value* self, size_t /*argc*/,
                     Value* /*argv*/[]) {
    return self;
}

// —— 注册辅助 ——

bool defineMethod(IContext* ctx, Value* obj, const char* name, NativeFn fn,
                  size_t length) {
    Value* m = ctx->createNativeFunction(fn, 0);
    if (m == ctx->oomValue()) return false;
    ctx->setFunctionName(m, name);
    Value* len = ctx->createNumber(static_cast<double>(length));
    if (len == ctx->oomValue()) return false;
    if (ctx->defineProperty(m, "length", len, false, false, true)->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    return ctx->defineProperty(obj, name, m, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

}  // namespace

bool registerAsyncGeneratorBuiltins(IContext* ctx) {
    // —— %AsyncIteratorPrototype%（原型 = Object.prototype）——
    Value* asyncIterProto = ctx->createObject();
    if (asyncIterProto == ctx->oomValue()) return false;
    if (ctx->objectPrototype() != nullptr &&
        ctx->setPrototype(asyncIterProto, ctx->objectPrototype())->type()->kind() ==
            TypeKind::Exception) {
        return false;
    }
    Value* iterMethod = ctx->createNativeFunction(asyncIterator, 0);
    if (iterMethod == ctx->oomValue()) return false;
    ctx->setFunctionName(iterMethod, "[Symbol.asyncIterator]");
    if (ctx->defineProperty(asyncIterProto,
                            ctx->getWellKnownSymbol(WellKnownSymbol::AsyncIterator),
                            iterMethod, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    ctx->setAsyncIteratorPrototype(asyncIterProto);

    // —— %AsyncGeneratorPrototype%（原型 = %AsyncIteratorPrototype%）——
    Value* asyncGenProto = ctx->createObject();
    if (asyncGenProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(asyncGenProto, asyncIterProto)->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setAsyncGeneratorPrototype(asyncGenProto);

    if (!defineMethod(ctx, asyncGenProto, "next", asyncGeneratorNext, 1)) return false;
    if (!defineMethod(ctx, asyncGenProto, "return", asyncGeneratorReturn, 1)) {
        return false;
    }
    if (!defineMethod(ctx, asyncGenProto, "throw", asyncGeneratorThrow, 1)) return false;

    if (ctx->defineProperty(asyncGenProto,
                            ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            ctx->createString("AsyncGenerator"), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    return true;
}

}  // namespace neo::core
