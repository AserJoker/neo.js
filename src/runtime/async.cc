/*
 * 内部 C++ 实现：async/await 运行时（M5.4，§27.7）。
 *
 * 遵循 docs/architecture.md §8 B2（控制/异步）与 ECMA-262 §27.7：
 *   - async 函数调用（asyncCall）：创建外层 promise + 帧环境（含隐藏绑定
 *     "__async_promise__" 指向外层 promise），执行字节码到首个 await。
 *   - await 挂起（suspendAwait）：VM 现场（操作数栈 + pc + 帧环境 + this +
 *     finally 挂起状态）转移到堆上 AsyncFrame，入队 AwaitResolve/AwaitReject
 *     续延 job（Bound 闭包捕获 AsyncFrame，微任务调度）。
 *   - 续延（resumeAsyncFrame）：job 执行时重建现场（runBytecode 带
 *     start_pc + restore_stack），从挂起点继续到下一个 await 或函数完成；
 *     完成 resolve 外层 promise，异常 reject 外层 promise（§27.7.5.3
 *     AsyncFunctionStart 步骤 6）。
 *
 * GC：AsyncFrame 继承 JSObject（ObjectKind::AsyncFrame），作为 JSValue
 * 载荷被 Bound 闭包捕获（job 队列 -> Bound 闭包 -> AsyncFrame），挂起期间
 * 始终根可达；gcMarkRefs 逐边标记（fn/env/frame_this/promise/操作数栈）。
 */

#include "neojs/runtime/async.h"

#include <utility>
#include <vector>

#include "neojs/runtime/context.h"
#include "neojs/runtime/environment.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/promise.h"
#include "neojs/runtime/value.h"
#include "neojs/runtime/vm.h"

namespace neojs {

namespace {

ConversionResult ok(JSValue *v) {
  ConversionResult r;
  r.value = v;
  return r;
}

ConversionResult fail(const std::string &error) {
  ConversionResult r;
  r.error = error;
  return r;
}

/* 沿环境链查找隐藏绑定（外层 promise），供 suspendAwait 定位当前 async
 * 帧所属的外层 promise。嵌套 async 各自 env 链首段含自身绑定。 */
JSValue *findEnvBinding(JSEnvironment *env, const std::string &name) {
  for (JSEnvironment *e = env; e != nullptr; e = e->parent()) {
    if (e->hasBinding(name)) return e->getBinding(name);
  }
  return nullptr;
}

/* 创建 await 续延 Bound 闭包：Bound(AwaitResolve/AwaitReject, undefined,
 * [async_frame_value])。作为 reaction handler 挂到 awaitable 上，settle 时
 * 经 PromiseReactionJob 调用，value/reason 作为调用参数转发给 target
 * （target 收到 [async_frame, value]）。 */
JSValue *makeAwaitContinuation(JSContext *ctx, JSValue *frame_v,
                               PromiseInternal kind) {
  std::vector<JSValue *> data;
  data.push_back(frame_v);
  return makeBoundFunction(ctx, getPromiseInternal(ctx, kind), std::move(data));
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* AsyncFrame（挂起帧）                                                       */
/* -------------------------------------------------------------------------- */

AsyncFrame::AsyncFrame()
    : JSObject(ObjectKind::AsyncFrame, static_cast<size_t>(0)) {}

/* 引用边标记：fn/env/frame_this/promise + 操作数栈每个元素。 */
void AsyncFrame::gcMarkRefs(Heap *heap) {
  JSObject::gcMarkRefs(heap); /* 原型/属性等基类边（AsyncFrame 无属性，空操作） */
  if (fn != nullptr) heap->markRoot(fn);
  if (env != nullptr) heap->markRoot(env);
  if (frame_this != nullptr) heap->markRoot(frame_this);
  if (promise != nullptr) heap->markRoot(promise);
  if (yield_value != nullptr) heap->markRoot(yield_value);
  if (delegate_iterator != nullptr) heap->markRoot(delegate_iterator);
  for (JSValue *v : stack) {
    if (v != nullptr) heap->markRoot(v);
  }
  /* pending.thrown（JS throw 值）经 pending_handle 临时句柄保护（根集），
   * 无需重复标记；has_pending 但 pending_handle==0（内部错误串）无引用。 */
}

/* 创建包装 AsyncFrame 的 JSValue（两段式：frame 已分配，包 JSValue）。 */
JSValue *wrapAsyncFrame(JSContext *ctx, AsyncFrame *frame) {
  ctx->heap().pin(frame); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(frame);
  frame->setHolder(v);
  ctx->heap().unpin(frame);
  return v;
}

/* 取 AsyncFrame（v 的 object 载荷须为 AsyncFrame；否则返回 nullptr）。 */
AsyncFrame *asAsyncFrame(JSValue *v) {
  if (v == nullptr || !v->isObject()) return nullptr;
  JSObject *o = v->object();
  if (o->kind() != ObjectKind::AsyncFrame) return nullptr;
  return static_cast<AsyncFrame *>(o);
}

/* -------------------------------------------------------------------------- */
/* await 挂起                                                                */
/* -------------------------------------------------------------------------- */

bool suspendAwait(JSContext *ctx, JSFunction *fn, size_t next_pc,
                  std::vector<JSValue *> &stack, JSEnvironment *env,
                  JSValue *frame_this, const ConversionResult &pending,
                  bool has_pending, uint32_t pending_handle,
                  JSValue *awaitable) {
  /* 创建 AsyncFrame + 包装 JSValue。 */
  AsyncFrame *af = ctx->heap().allocGCObject<AsyncFrame>();
  JSValue *afv = wrapAsyncFrame(ctx, af);
  ctx->heap().pin(afv); /* 分配保护：挂 reaction 前防误回收 */

  /* 保存现场。 */
  af->fn = fn;
  af->pc = next_pc;
  af->stack = stack; /* 拷贝操作数栈（含 await 操作数） */
  af->env = env;
  af->frame_this = frame_this;
  af->pending = pending;
  af->has_pending = has_pending;
  af->pending_handle = pending_handle;

  /* 定位外层 promise：沿 env 链查找隐藏绑定（asyncCall 写入）。 */
  af->promise = findEnvBinding(env, kAsyncPromiseBinding);

  /* §27.7.4.1 Await 步骤 9：把 AwaitResolve/AwaitReject 续延 reaction 挂到
   * awaitable 上（awaitable.then(awaitResolve, awaitReject)）。settle 时经
   * PromiseReactionJob 调用 handler 并传 value/reason（Bound 转发给 target
   * [async_frame, value]）。awaitable 已 fulfilled/rejected 则立即入队
   * reaction job（promiseThen 已 settled 分支），经微任务调度续延。 */
  JSValue *on_fulfilled =
      makeAwaitContinuation(ctx, afv, PromiseInternal::AwaitResolve);
  JSValue *on_rejected =
      makeAwaitContinuation(ctx, afv, PromiseInternal::AwaitReject);
  ConversionResult r =
      attachAwaitReactions(ctx, awaitable, on_fulfilled, on_rejected);
  ctx->heap().unpin(afv);
  if (!r.ok()) {
    return false; /* 挂 reaction 失败：内部错误，调用方按异常处理 */
  }
  return true;
}

/* -------------------------------------------------------------------------- */
/* 续延：重建现场继续执行                                                     */
/* -------------------------------------------------------------------------- */

ConversionResult resumeAsyncFrame(JSContext *ctx, AsyncFrame *frame,
                                  JSValue *result, bool is_reject) {
  if (frame == nullptr || frame->fn == nullptr) {
    return fail("Error: invalid async frame on resume");
  }

  /* 恢复值入现场：
   *   - AwaitResolve：value 压栈（await 表达式的结果）。
   *   - AwaitReject：rejection 值作为异常进入（throw 语义），经异常表/
   *     跨帧传播（try/catch 可捕获）——通过 runBytecode 的 initial_thrown
   *     参数注入，从挂起点后第一条指令起派发异常。 */
  std::vector<JSValue *> restore_stack = std::move(frame->stack);
  JSValue *thrown = nullptr;
  if (!is_reject) {
    restore_stack.push_back(result);
  } else {
    thrown = result;
  }

  /* 外层 promise（settle 用）：挂起时已存于帧内。 */
  JSValue *promise = frame->promise;

  /* 重建现场执行：从挂起点（pc）继续，采用挂起时的栈副本。
   * frame_this 经临时句柄保护（call() Script 分支同机制——此处直接由
   * runBytecode 内部压栈前保护，无句柄；runBytecode 的 thisVal 参数在
   * 执行期间受 vm_stacks_ 根集保护：LoadThis 压栈后受栈根集保护）。 */
  ConversionResult r = runBytecode(
      ctx, frame->fn->bytecode(), frame->frame_this, nullptr, 0, frame->env,
      frame->fn, frame->pc, &restore_stack, thrown);

  if (r.suspended) {
    /* 又挂起：新现场已存新 AsyncFrame 并入队新 job，外层 promise 未 settle
     * （等下一个微任务）。返回 suspended 信号（job 正常完成）。 */
    return r;
  }

  if (!r.ok()) {
    /* 执行中抛错：reject 外层 promise（§27.7.5.3 步骤 6），错误继续向上
     * 传播（JS_RunJobs 报未捕获）。 */
    if (promise != nullptr && promise->isObject() &&
        promise->object()->isPromise()) {
      JSValue *reason = r.thrown;
      if (reason == nullptr) {
        reason = ctx->createValue(Kind::Exception);
        reason->setErrorInfo(r.error);
      }
      JSValue *argv[1] = {reason};
      /* 经 reject 决议闭包 settle（复用 Promise 内部机制）。 */
      JSValue *reject = makeBoundFunction(
          ctx, getPromiseInternal(ctx, PromiseInternal::Reject), {promise});
      (void)call(ctx, reject, ctx->createValue(Kind::Undefined), argv, 1);
    }
    return r;
  }

  /* 函数完整执行：resolve 外层 promise（r.value 为返回值）。 */
  if (promise != nullptr && promise->isObject() &&
      promise->object()->isPromise()) {
    JSValue *resolve = makeBoundFunction(
        ctx, getPromiseInternal(ctx, PromiseInternal::Resolve), {promise});
    JSValue *argv[1] = {r.value};
    (void)call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
  }
  return ok(r.value);
}

/* -------------------------------------------------------------------------- */
/* async 调用入口                                                            */
/* -------------------------------------------------------------------------- */

ConversionResult asyncCall(JSContext *ctx, JSFunction *fn, JSValue *thisVal,
                           JSValue *const *args, size_t argc) {
  if (fn == nullptr) return fail("Error: null async function");

  /* 1. 创建外层 promise（§27.7.5.3 步骤 5）。 */
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *promise = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(promise); /* 分配保护：创建 env/执行前防误回收 */

  /* 2. 创建帧环境（parent = 函数 [[Environment]]），参数按名写入 + 写入
   *    隐藏绑定 __async_promise__（外层 promise）。 */
  JSEnvironment *call_env =
      ctx->heap().allocGCObject<JSEnvironment>(fn->environment());
  ctx->heap().pin(call_env); /* 分配保护：绑定写入前防误回收 */
  const FunctionBytecode &bc = fn->bytecode();
  size_t nparams = argc < bc.locals.size() ? argc : bc.locals.size();
  for (size_t i = 0; i < nparams; ++i) {
    if (i < argc && args[i] != nullptr && !bc.locals[i].empty()) {
      call_env->setBinding(bc.locals[i], args[i]);
    }
  }
  call_env->setBinding(kAsyncPromiseBinding, promise);

  /* 3. 执行字节码到首个 await 或完成。 */
  ConversionResult r =
      runBytecode(ctx, bc, thisVal, args, argc, call_env, fn);

  ctx->heap().unpin(call_env);

  if (r.suspended) {
    /* 首个 await 挂起：现场已存 AsyncFrame 并入队续延 job。返回 promise。 */
    ctx->heap().unpin(promise);
    return ok(promise);
  }

  if (!r.ok()) {
    /* 执行中抛错：reject 外层 promise（§27.7.5.3 步骤 6）。 */
    JSValue *reason = r.thrown;
    if (reason == nullptr) {
      reason = ctx->createValue(Kind::Exception);
      reason->setErrorInfo(r.error);
    }
    JSValue *argv[1] = {reason};
    (void)call(ctx, reject, ctx->createValue(Kind::Undefined), argv, 1);
    ctx->heap().unpin(promise);
    return ok(promise); /* async 调用永不向调用方抛错（错误进 promise） */
  }

  /* 4. 函数完整执行：resolve 外层 promise（r.value 为返回值）。 */
  JSValue *argv[1] = {r.value};
  (void)call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
  ctx->heap().unpin(promise);
  return ok(promise);
}

}  // namespace neojs
