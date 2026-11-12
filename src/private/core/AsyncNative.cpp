// AsyncNative.cpp - native async 的业务临时变量槽实现
//
// 槽落在 ctx->currentFunctionEnv()（invokeNativeFunction 设置的 execScope），
// 而不是 ctx->current()（可能是 stage 内的块作用域）。恢复协程时
// invokeNativeFunction 会把 currentFunctionEnv 重新指向同一个 execScope。

#include "core/AsyncNative.h"

#include "core/IContext.h"
#include "core/PromiseCapability.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Exception.h"
#include "core/types/Interrupt.h"
#include "core/types/Promise.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

namespace {

// 取当前 native 函数闭包作用域里的捕获值（与 AsyncGeneratorBuiltins 同模式）。
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

Value* undefinedOf(IContext* ctx) { return ctx->createUndefined(); }

// 用 resolve/reject 结束外层 Promise；忽略 resolve/reject 自身的返回值。
void settle(IContext* ctx, Value* fn, Value* value) {
    if (fn == nullptr) return;
    Value* args[1] = {value};
    (void)ctx->call(fn, undefinedOf(ctx), 1, args);
}

void rejectWith(IContext* ctx, Value* reject, Value* reason) {
    settle(ctx, reject, reason);
}

void awaitCoroutine(IContext* ctx, Value* co, Value* awaitable, Value* resolve,
                    Value* reject);

// 解释一次 resume 的结果：Await → 继续订阅；完成 → settle 外层 Promise。
void driveAsyncCoroutine(IContext* ctx, Value* co, Value* res, Value* resolve,
                         Value* reject) {
    if (res == nullptr) res = undefinedOf(ctx);

    TypeKind k = res->type()->kind();
    if (k == TypeKind::Exception || k == TypeKind::Fatal) {
        rejectWith(ctx, reject, reasonOf(res));
        return;
    }

    if (detail::isInterruptValue(res)) {
        InterruptData* ir = asInterrupt(res);
        if (ir->kind == InterruptKind::Await) {
            awaitCoroutine(ctx, co, ir->value, resolve, reject);
            return;
        }
        // async native 不应产生 Yield；Return/Throw 属 VM 注入方向。
        Value* err = detail::typeError(
            ctx, "async native function returned an unsupported interrupt");
        rejectWith(ctx, reject, reasonOf(err));
        return;
    }

    settle(ctx, resolve, res);
}

// await 回调：恢复正常值 / 以 Exception 注入拒绝。
Value* awaitFulfilled(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* co = captured(ctx, "__co");
    Value* resolve = captured(ctx, "__resolve");
    Value* reject = captured(ctx, "__reject");
    if (co == nullptr) return undefinedOf(ctx);
    Value* v = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* res = ctx->resumeCoroutine(co, v);
    driveAsyncCoroutine(ctx, co, res, resolve, reject);
    return undefinedOf(ctx);
}

Value* awaitRejected(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* co = captured(ctx, "__co");
    Value* resolve = captured(ctx, "__resolve");
    Value* reject = captured(ctx, "__reject");
    if (co == nullptr) return undefinedOf(ctx);
    Value* reason = argc > 0 ? argv[0] : undefinedOf(ctx);
    // native 错误通道直接用 Exception；Interrupt(Throw) 仅 VM 专用。
    Value* exc = ctx->createException(reason);
    // createException 只在 OOM 时返回 Fatal；Exception 本身是正常注入值。
    if (exc->type()->kind() == TypeKind::Fatal) {
        rejectWith(ctx, reject, reasonOf(exc));
        return undefinedOf(ctx);
    }
    Value* res = ctx->resumeCoroutine(co, exc);
    driveAsyncCoroutine(ctx, co, res, resolve, reject);
    return undefinedOf(ctx);
}

// Promise.resolve(awaitable).then(onFulfilled, onRejected)
void awaitCoroutine(IContext* ctx, Value* co, Value* awaitable, Value* resolve,
                    Value* reject) {
    Value* ctor = ctx->promiseCtor();
    if (ctor == nullptr) {
        Value* err = detail::typeError(ctx, "Promise is not initialized");
        rejectWith(ctx, reject, reasonOf(err));
        return;
    }
    Value* resolveFn = ctx->getProperty(ctor, "resolve");
    if (detail::isErrorValue(resolveFn)) {
        rejectWith(ctx, reject, reasonOf(resolveFn));
        return;
    }
    Value* promise = ctx->call(resolveFn, ctor, 1, &awaitable);
    if (detail::isErrorValue(promise)) {
        rejectWith(ctx, reject, reasonOf(promise));
        return;
    }
    Value* thenFn = ctx->getProperty(promise, "then");
    if (detail::isErrorValue(thenFn)) {
        rejectWith(ctx, reject, reasonOf(thenFn));
        return;
    }

    Value* onFulfilled = ctx->createNativeFunction(awaitFulfilled, 1);
    Value* onRejected = ctx->createNativeFunction(awaitRejected, 1);
    if (onFulfilled == ctx->oomValue() || onRejected == ctx->oomValue()) {
        rejectWith(ctx, reject, ctx->oomValue());
        return;
    }
    // 回调在后续微任务执行：捕获协程与 capability 以保活。
    ctx->captureValue(onFulfilled, "__co", co);
    ctx->captureValue(onFulfilled, "__resolve", resolve);
    ctx->captureValue(onFulfilled, "__reject", reject);
    ctx->captureValue(onRejected, "__co", co);
    ctx->captureValue(onRejected, "__resolve", resolve);
    ctx->captureValue(onRejected, "__reject", reject);

    Value* args[2] = {onFulfilled, onRejected};
    Value* r = ctx->call(thenFn, promise, 2, args);
    if (detail::isErrorValue(r)) rejectWith(ctx, reject, reasonOf(r));
}

}  // namespace

Value* runAsyncNative(IContext* ctx, Value* fn, Value* thisArg, size_t argc,
                      Value* argv[]) {
    Value* ctor = ctx->promiseCtor();
    if (ctor == nullptr) {
        return detail::typeError(ctx, "Promise is not initialized");
    }
    PromiseCapability cap;
    if (Value* err = createPromiseCapability(ctx, ctor, cap)) return err;

    Value* co = ctx->createCoroutine(fn, thisArg, argc, argv);
    if (co->type()->kind() == TypeKind::Exception ||
        co->type()->kind() == TypeKind::Fatal) {
        return co;
    }

    Value* res = ctx->resumeCoroutine(co, nullptr);
    driveAsyncCoroutine(ctx, co, res, cap.resolve, cap.reject);
    return cap.promise;
}

Value* loadNativeLocal(IContext* ctx, std::string_view name) {
    Scope* env = ctx->currentFunctionEnv();
    return env != nullptr ? env->lookup(name) : nullptr;
}

void storeNativeLocal(IContext* ctx, std::string_view name, Value* value) {
    if (value == nullptr) return;
    Scope* env = ctx->currentFunctionEnv();
    if (env == nullptr) return;
    Value* old = env->lookup(name);
    if (old != nullptr && old != value) env->remove(old);
    env->add(value);
    env->bind(name, value);
}

}  // namespace neo::core
