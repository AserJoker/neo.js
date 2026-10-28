/*
 * 内部 C++ 实现：AsyncGenerator 运行时（M5.5c-1，§27.6）。
 *
 * 遵循 docs/architecture.md §8 B2（控制/异步）与 ECMA-262 §27.6：
 *   - 调用 async generator 函数返回 AsyncGenerator 对象（state=suspended-start），
 *     不立即执行；next/throw/return 经 [[AsyncGeneratorQueue]] 请求队列
 *     逐个驱动执行（§27.6.3 AsyncGeneratorResume）。
 *   - next/throw/return 返回 Promise（异步迭代器协议 §27.6.3.1/2/3）：
 *     入队 AsyncGeneratorRequest（{completion, capability} 包装对象），
 *     队列空且状态非 executing 时立即驱动；否则等前序请求完成。
 *   - yield 值是 AsyncYield：yield 值先 await（PromiseResolve 接合 + 挂起
 *     恢复续延），再挂起返回 {value,done:false} 的 promise（§27.6.3.4
 *     AsyncGeneratorYield）。编译器在 async generator 的 yield 前发射 Await
 *     指令实现——op_Await 挂起/恢复机制天然复用。
 *   - 函数完成/异常：settle 当前请求的 capability（§27.6.3.3 step 9-10），
 *     驱动队列中下一个请求。
 *
 * 复用 Generator 的现场机制（feedback_vm_design.md：async/generator 全 env
 * 持有）：
 *   - AsyncGenerator 内嵌 AsyncFrame（gen_frame_ 槽，[[AsyncGeneratorContext]]），
 *     挂起时保存 VM 现场（pc/stack/env/this/pending）；
 *   - 状态复用 gen_state_（0 suspended-start / 1 suspended-yield / 2 executing
 *     / 3 completed，§27.6.2 AsyncGeneratorState）；
 *   - AsyncFrame.async_request 槽（async.h）存当前正在执行的请求包装对象，
 *     op_Yield 挂起时 resolve 其 capability {value,done}；
 *   - 请求队列 async_queue_（object.h）存 AsyncGeneratorRequest 包装对象
 *     （{completion, capability} 属性，GC 引用边）。
 *
 * GC：AsyncGenerator 是 JSObject（ObjectKind::AsyncGenerator），async_queue_
 * 元素经 gcMarkRefs 标记；AsyncFrame 的 async_request 经其 gcMarkRefs 标记。
 * 请求包装对象/决议闭包被队列持有，挂起期间始终根可达。
 *
 * 注意：与 generator 架构一致，AsyncGenerator.prototype 方法注册
 * （installAsyncGeneratorBuiltins + next/throw/return wrapper + 全局构造器）
 * 在 builtins.cc（复用其匿名命名空间的 makeBuiltin/defineMethod）。
 */

#include "neojs/runtime/async_generator.h"

#include <utility>
#include <vector>

#include "neojs/runtime/async.h"
#include "neojs/runtime/async_iterator.h"
#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/environment.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/iterator.h"
#include "neojs/runtime/object.h"
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

/* 创建 {value, done} 迭代器结果对象（next/throw/return 的 promise 决议值）。 */
JSValue *createIterResult(JSContext *ctx, JSValue *value, bool done) {
  JSValue *r = ctx->createObject(objectProto(ctx)->object());
  r->object()->setProperty("value",
                           value != nullptr ? value
                                            : ctx->createValue(Kind::Undefined));
  r->object()->setProperty("done", ctx->createValue(done));
  return r;
}

/* 创建 AsyncGeneratorRequest 包装对象
 * （{completion, completion_value, capability_resolve, capability_reject}
 * 属性，§27.6.1.2）。completion 编码 mode（0 next / 1 throw / 2 return），
 * capability 为 promise 的 resolve/reject 决议闭包（Bound 函数捕获
 * promise，§27.6.3.5 AsyncGeneratorComplete step 5-6 分别调用）。GC 引用边：
 * 对象属性持有 completion value + 决议闭包。 */
JSValue *createAsyncGeneratorRequest(JSContext *ctx, int mode, JSValue *value,
                                     JSValue *resolve, JSValue *reject) {
  JSValue *req = ctx->createObject(objectProto(ctx)->object());
  JSObject *o = req->object();
  o->setProperty("completion", ctx->createValue(static_cast<double>(mode)));
  o->setProperty("completion_value",
                 value != nullptr ? value : ctx->createValue(Kind::Undefined));
  o->setProperty("capability", resolve);
  o->setProperty("capability_reject", reject);
  return req;
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* AsyncGenerator 调用入口                                                      */
/* -------------------------------------------------------------------------- */

ConversionResult asyncGeneratorCall(JSContext *ctx, JSFunction *fn,
                                    JSValue *thisVal, JSValue *const *args,
                                    size_t argc) {
  if (fn == nullptr) return fail("Error: null async generator function");

  /* 创建 AsyncGenerator 对象（state=SuspendedStart），原型链挂
   * AsyncGenerator.prototype。 */
  JSObject *proto = nullptr;
  JSValue *agp = asyncGeneratorProto(ctx);
  if (agp != nullptr) proto = agp->object();
  JSObject *obj = ctx->heap().allocGCObject<JSObject>(ObjectKind::AsyncGenerator, 0);
  if (proto != nullptr) obj->setPrototype(proto);
  ctx->heap().pin(obj); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  JSValue *gen_v = ctx->heap().allocGCObject<JSValue>();
  gen_v->setObject(obj);
  obj->setHolder(gen_v);
  ctx->heap().unpin(obj);
  ctx->heap().pin(gen_v); /* 分配保护：建帧 + 挂现场前防误回收 */

  /* 创建挂起帧（AsyncFrame，复用 async/generator 现场保存机制）：pc=0，
   * 空栈。首次 next() 驱动执行时从这里重建现场开始（AsyncGeneratorStart）。 */
  AsyncFrame *af = ctx->heap().allocGCObject<AsyncFrame>();
  JSValue *afv = wrapAsyncFrame(ctx, af);
  ctx->heap().pin(afv); /* 分配保护：写入字段前防误回收 */

  af->fn = fn;
  af->pc = 0;
  af->stack.clear();
  af->frame_this = thisVal;
  af->promise = nullptr; /* async generator 无外层 promise（异步迭代器协议） */
  af->yield_value = nullptr;
  af->delegate_iterator = nullptr;
  af->async_request = nullptr;

  /* 创建帧环境（parent = 函数 [[Environment]]），参数按名写入。
   * 与 generatorCall/asyncCall 一致——首次驱动执行时 runBytecode 用此 env。 */
  JSEnvironment *call_env =
      ctx->heap().allocGCObject<JSEnvironment>(fn->environment());
  ctx->heap().pin(call_env);
  const FunctionBytecode &bc = fn->bytecode();
  size_t nparams = argc < bc.locals.size() ? argc : bc.locals.size();
  for (size_t i = 0; i < nparams; ++i) {
    if (i < argc && args[i] != nullptr && !bc.locals[i].empty()) {
      call_env->setBinding(bc.locals[i], args[i]);
    }
  }
  af->env = call_env;

  /* 写入隐藏绑定 __generator__（op_Yield/op_Await 沿 env 链查找当前
   * async generator）。与 generatorCall 同模式。 */
  call_env->setBinding(kGeneratorBinding, gen_v);

  ctx->heap().unpin(call_env);

  /* 挂现场到 AsyncGenerator（[[AsyncGeneratorContext]]）。 */
  obj->setAsyncGeneratorContext(afv);
  obj->setAsyncGeneratorState(
      static_cast<int>(AsyncGeneratorState::SuspendedStart));

  ctx->heap().unpin(afv);
  ctx->heap().unpin(gen_v);
  return ok(gen_v);
}

/* -------------------------------------------------------------------------- */
/* 请求驱动（§27.6.3 AsyncGeneratorResume 近似）                                */
/* -------------------------------------------------------------------------- */

/* 前向声明：驱动队列（定义于 resumeAsyncGenerator 之后）。resumeAsyncGenerator
 * 的 AsyncYield 挂起分支与 suspendAsyncYield 调用它，循环依赖故前向声明。 */
static void driveAsyncGeneratorQueue(JSContext *ctx, JSValue *gen_v);

/* 前向声明：M5.5e yield* 委托转发（定义于 resumeAsyncGenerator 之后）。
 * resumeAsyncGenerator 的委托分支（delegate_iterator 非空）调用它，循环依赖
 * （asyncDelegateForward 结果处理又调 resumeAsyncGenerator）。 */
static ConversionResult asyncDelegateForward(JSContext *ctx, JSValue *gen_v,
                                             AsyncFrame *frame, JSValue *value,
                                             int mode);

/* 前向声明：沿环境链查找当前 AsyncGenerator 对象（定义于 suspendAsyncYield 附近，
 * asyncDelegateResume 在其之前使用，故前向声明）。 */
static JSValue *findAsyncGenerator(JSEnvironment *env);

/* 从挂起帧重建现场，继续执行到下一个 AsyncYield 挂起、await 挂起或函数完成。
 * 结果处理：
 *   - suspended 且 frame->async_request 非空（await 挂起）：请求未结算
 *     （suspendAwait 已把现场转移并传播 async_request 到新帧），保持
 *     Executing，返回 suspended 信号（等 awaitable settle 后续延）；
 *   - suspended 且 async_request 为空（AsyncYield 挂起）：suspendAsyncYield
 *     已 resolve capability {value,done:false} + 置 SuspendedYield，驱动
 *     队列中下一个请求（若有）；
 *   - 完成/异常：asyncGeneratorFrameSettled 结算当前请求 + 队列全部剩余
 *     请求（§27.6.3.5 AsyncGeneratorComplete），错误进请求 promise 不冒泡。 */
static ConversionResult resumeAsyncGenerator(JSContext *ctx, JSValue *gen_v,
                                             JSValue *value, int mode,
                                             bool is_first) {
  JSObject *gen = gen_v->object();
  JSValue *afv = gen->asyncGeneratorContext();
  AsyncFrame *frame = asAsyncFrame(afv);
  if (frame == nullptr || frame->fn == nullptr) {
    return fail("Error: invalid async generator context");
  }

  /* M5.5e：yield* 委托挂起（frame->delegate_iterator 非空）——请求不压栈/
   * 注入外层，转发给 inner async iterator（§14.4.14 step 5 委托循环）。
   * 挂起时 state 是 SuspendedYield（AsyncGeneratorYield 后驱动队列）或
   * Executing（委托内部 await，此处不会到——Executing 防重入不驱动）。 */
  if (frame->delegate_iterator != nullptr) {
    gen->setAsyncGeneratorState(static_cast<int>(AsyncGeneratorState::Executing));
    return asyncDelegateForward(ctx, gen_v, frame, value, mode);
  }

  /* 首次驱动（SuspendedStart）：补 numLocals 个 undefined 局部槽占位
   * （与 generator 首次 resume 一致，参数已在 env）。is_first 由调用方
   * driveAsyncGeneratorQueue 判定（置 Executing 前读 state——此处读到的
   * 已是 Executing，不能据此判断）。 */
  std::vector<JSValue *> restore_stack = std::move(frame->stack);
  JSValue *thrown = nullptr;
  if (is_first) {
    const FunctionBytecode &bc = frame->fn->bytecode();
    for (size_t i = 0; i < bc.numLocals; ++i) {
      restore_stack.push_back(ctx->createValue(Kind::Undefined));
    }
  }
  if (mode == 0) {
    /* next：非首次压栈 value（yield 表达式的值）。首次不压（首条指令即
     * 函数体开头，AsyncGeneratorStart 不接收值）。 */
    if (!is_first) {
      restore_stack.push_back(value != nullptr ? value
                                               : ctx->createValue(Kind::Undefined));
    }
  } else if (mode == 1) {
    /* throw：异常注入（首次 throw 在 SuspendedStart 视作 completed 抛出，
     * 由 asyncGeneratorProtoNext 的 completed 分支处理；此处非首次）。 */
    thrown = (value != nullptr) ? value : ctx->createValue(Kind::Undefined);
  }
  /* mode == 2（return）：注入 return completion（initial_return），
   * 穿透 try-finally 后以 return value 完成。 */
  JSValue *ret_val = (mode == 2 && value != nullptr)
                         ? value
                         : ctx->createValue(Kind::Undefined);

  /* 续延执行：从挂起点继续到下个 AsyncYield 挂起或函数完成。 */
  ConversionResult r = runBytecode(
      ctx, frame->fn->bytecode(), frame->frame_this, nullptr, 0, frame->env,
      frame->fn, frame->pc, &restore_stack, thrown,
      mode == 2 ? ret_val : nullptr);

  if (r.suspended) {
    if (frame->async_request != nullptr) {
      /* await 挂起：请求未结算（suspendAwait 已传播到新帧，AsyncGenerator
       * 保持 Executing），等 awaitable settle 后 resumeAsyncFrame 续延。
       * 返回值无意义（driveAsyncGeneratorQueue 忽略），返回占位。 */
      return ok(createIterResult(ctx, ctx->createValue(Kind::Undefined), false));
    }
    /* AsyncYield 挂起：suspendAsyncYield 已 resolve capability + 置
     * SuspendedYield + 清 async_request。驱动队列中下一个请求（若有）。 */
    JSValue *yv = frame->yield_value;
    if (yv == nullptr) yv = ctx->createValue(Kind::Undefined);
    driveAsyncGeneratorQueue(ctx, gen_v);
    return ok(createIterResult(ctx, yv, false));
  }

  /* 函数完成（正常返回或抛错）：结算当前请求 + 队列剩余请求。
   * 错误进请求 promise（reject），不向驱动方冒泡。 */
  return asyncGeneratorFrameSettled(ctx, frame, r);
}

/* 驱动队列（§27.6.3.3 步骤 1-8）：若队列非空且状态非 executing/completed，
 * 取队首请求，写 frame->async_request，置 executing，按请求 mode 恢复执行。
 * resumeAsyncGenerator 完成/挂起后把 state 置回 SuspendedYield/Completed
 * 并清 frame->async_request；若还有排队请求且非 executing，继续驱动。 */
static void driveAsyncGeneratorQueue(JSContext *ctx, JSValue *gen_v) {
  JSObject *gen = gen_v->object();
  const int state = gen->asyncGeneratorState();
  if (state == static_cast<int>(AsyncGeneratorState::Executing)) {
    return; /* 正在执行：新请求已入队，等当前完成后再驱动 */
  }
  if (state == static_cast<int>(AsyncGeneratorState::Completed)) {
    return; /* 已完成：后续请求由 asyncGeneratorProtoNext 的 completed 分支处理 */
  }
  std::vector<JSValue *> &queue = gen->asyncGeneratorQueue();
  if (queue.empty()) return;

  /* 取队首请求。 */
  JSValue *req = queue.front();
  queue.erase(queue.begin());
  ctx->heap().pin(req); /* 分配保护：写 frame + 执行期间防误回收 */

  /* 写当前请求到挂起帧（op_Yield 挂起时 resolve 其 capability）。 */
  AsyncFrame *frame = asAsyncFrame(gen->asyncGeneratorContext());
  if (frame == nullptr) {
    ctx->heap().unpin(req);
    return;
  }
  frame->async_request = req;

  /* 读请求 completion（mode）与 value。 */
  int mode = 0;
  JSValue *value = ctx->createValue(Kind::Undefined);
  if (req->isObject()) {
    JSValue *m = req->object()->getProperty("completion");
    if (m != nullptr && m->isNumber()) mode = static_cast<int>(m->toNumber());
    JSValue *v = req->object()->getProperty("completion_value");
    if (v != nullptr) value = v;
  }

  /* 置 executing 后恢复执行（防重入：执行期间新 next() 请求只入队）。
   * is_first 在此判定（state 尚未被改写，仍为挂起态）。 */
  const bool is_first =
      state == static_cast<int>(AsyncGeneratorState::SuspendedStart);
  gen->setAsyncGeneratorState(static_cast<int>(AsyncGeneratorState::Executing));
  (void)resumeAsyncGenerator(ctx, gen_v, value, mode, is_first);

  ctx->heap().unpin(req);

  /* 若还有排队请求（resumeAsyncGenerator 可能完成多个？不会——单步驱动
   * 一个请求；但异常/完成路径已置 Completed，后续请求由 completed 分支
   * 处理，无需在此循环）。 */
}

/* -------------------------------------------------------------------------- */
/* M5.5e yield* 委托 async 迭代器（§14.4.14 + §27.6.3.6）                     */
/* -------------------------------------------------------------------------- */

/* PromiseResolve 接合（§14.7.5.3 step 6 / Await 同构）：x 是 Promise 直接用；
 * 否则包一层 pending promise 并同步 resolve（resolve 后 reaction 经微任务
 * 入队——挂起恢复时序与规范一致）。返回 promise。 */
JSValue *delegatePromiseResolve(JSContext *ctx, JSValue *x) {
  if (x != nullptr && x->isObject() && x->object()->isPromise()) {
    return x;
  }
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：决议前防误回收 */
  JSValue *argv[1] = {x};
  ConversionResult rr = call(ctx, resolve, ctx->createValue(Kind::Undefined),
                             argv, 1);
  ctx->heap().unpin(p);
  if (!rr.ok()) return p; /* 接合失败：返回 pending promise（Await 挂起） */
  return p;
}

/* 读迭代结果 done（true/false）。无 done 视作 false（继续迭代）。 */
bool delegateIterResultDone(JSValue *iter_result) {
  if (iter_result == nullptr || !iter_result->isObject()) return true;
  JSValue *d = iter_result->object()->getProperty("done");
  return (d != nullptr) && toBoolean(d);
}

/* 读迭代结果 value（缺省 undefined）。 */
JSValue *delegateIterResultValue(JSContext *ctx, JSValue *iter_result) {
  if (iter_result == nullptr || !iter_result->isObject()) {
    return ctx->createValue(Kind::Undefined);
  }
  JSValue *v = iter_result->object()->getProperty("value");
  return (v != nullptr) ? v : ctx->createValue(Kind::Undefined);
}

/* 把内部错误（error 串）转 Exception JSValue（与 asyncGeneratorFrameSettled
 * 的异常值构造同款）。 */
static JSValue *delegateExceptionValue(JSContext *ctx,
                                       const std::string &error) {
  JSValue *e = ctx->createValue(Kind::Exception);
  e->setErrorInfo(error);
  return e;
}

/* 保存 VM 现场到 AsyncFrame（op_AsyncYieldDelegate 首次委托前调用，与
 * suspendAsyncYield 的保存段同构——现场是委托挂起/恢复的基点）。 */
void saveAsyncFrameForDelegate(JSContext *ctx, AsyncFrame *af,
                                      JSFunction *fn, size_t next_pc,
                                      std::vector<JSValue *> &stack,
                                      JSEnvironment *env, JSValue *frame_this,
                                      const ConversionResult &pending,
                                      bool has_pending, uint32_t pending_handle,
                                      bool pending_is_return) {
  (void)ctx;
  af->fn = fn;
  af->pc = next_pc;
  af->stack = stack; /* 拷贝操作数栈 */
  af->env = env;
  af->frame_this = frame_this;
  af->pending = pending;
  af->has_pending = has_pending;
  af->pending_handle = pending_handle;
  af->pending_is_return = pending_is_return;
}

/* await 一个 promise 并走委托状态机续延（非 Await 指令）。
 *   Fulfilled -> 同步返回 resolve 值（r.value）；
 *   Rejected  -> 同步返回 thrown（r.thrown）；
 *   pending   -> 挂 AsyncDelegateResolve/Reject（Bound 捕获 AsyncGenerator
 *                内嵌 frame 的包装值），置 phase，返回 suspended=true。
 * 现场已在 frame（op_AsyncYieldDelegate 保存 / 委托挂起保留），不覆盖。 */
ConversionResult asyncDelegateAwait(JSContext *ctx, AsyncFrame *af,
                                           JSValue *gen_v, JSValue *awaitable,
                                           AsyncDelegatePhase phase) {
  if (awaitable == nullptr || !awaitable->isObject() ||
      !awaitable->object()->isPromise()) {
    return fail("TypeError: delegate await on non-promise");
  }
  JSObject *po = awaitable->object();
  const int st = po->promiseState();
  if (st == static_cast<int>(PromiseState::Fulfilled)) {
    ConversionResult r;
    r.value = po->promiseResult();
    return r;
  }
  if (st == static_cast<int>(PromiseState::Rejected)) {
    ConversionResult r;
    r.thrown = po->promiseResult();
    return r;
  }

  /* pending：置 phase + 挂 AsyncDelegateResolve/Reject 续延。 */
  af->delegate_phase = phase;
  uint32_t h = ctx->handles().alloc(awaitable); /* 分配 handler 前保护 awaitable */
  JSValue *afv = gen_v->object()->asyncGeneratorContext();
  std::vector<JSValue *> data;
  data.push_back(afv);
  JSValue *on_f = makeBoundFunction(
      ctx, getPromiseInternal(ctx, PromiseInternal::AsyncDelegateResolve),
      std::move(data));
  std::vector<JSValue *> data2;
  data2.push_back(afv);
  JSValue *on_r = makeBoundFunction(
      ctx, getPromiseInternal(ctx, PromiseInternal::AsyncDelegateReject),
      std::move(data2));
  ConversionResult r = attachAwaitReactions(ctx, awaitable, on_f, on_r);
  ctx->handles().free(h);
  if (!r.ok()) return r;
  ConversionResult ret;
  ret.suspended = true;
  return ret;
}

/* AsyncGeneratorYield（§27.6.3.4）——委托产出值已 await 完成：复用
 * suspendAsyncYield——resolve 当前请求 capability {value, done:false} +
 * 保存现场 + 置 SuspendedYield + 清 async_request + 驱动队列。返回
 * suspended 信号（调用方据此结束 runBytecode 或委托状态机）。 */
ConversionResult asyncDelegateYield(JSContext *ctx, AsyncFrame *af,
                                           JSValue *gen_v, JSValue *value) {
  if (!suspendAsyncYield(ctx, gen_v, af->fn, af->pc, af->stack, af->env,
                         af->frame_this, af->pending, af->has_pending,
                         af->pending_handle, af->pending_is_return, value)) {
    return fail("Error: failed to suspend async generator frame on yield*");
  }
  ConversionResult r;
  r.suspended = true;
  return r;
}

/* inner 产出值（done=false）：先 await 值（AsyncGeneratorYield 前接合，
 * §27.6.3.4 step 7-9 同构）再 AsyncGeneratorYield。
 *   1. delegate_phase = AwaitYieldValue
 *   2. promiseResolve(inner.value) + asyncDelegateAwait
 *   3. resolve 后 asyncDelegateYield（suspendAsyncYield 结算请求）；
 *      reject 后清 delegate + 异常注入外层字节码。 */
ConversionResult asyncDelegateYieldValue(JSContext *ctx, AsyncFrame *af,
                                                JSValue *gen_v,
                                                JSValue *iter_result) {
  JSValue *inner_val = delegateIterResultValue(ctx, iter_result);
  af->delegate_phase = AsyncDelegatePhase::AwaitYieldValue;
  JSValue *p = delegatePromiseResolve(ctx, inner_val);
  ConversionResult r = asyncDelegateAwait(ctx, af, gen_v, p,
                                          AsyncDelegatePhase::AwaitYieldValue);
  if (r.suspended) return r; /* 挂起，等 AsyncDelegateResolve */
  if (!r.ok()) {
    /* yield 值 await reject：清 delegate，异常注入外层字节码。 */
    af->delegate_iterator = nullptr;
    af->delegate_phase = AsyncDelegatePhase::None;
    JSValue *thrown = r.thrown != nullptr
                          ? r.thrown
                          : delegateExceptionValue(ctx, r.error);
    return resumeAsyncGenerator(ctx, gen_v, thrown, 1, false);
  }
  return asyncDelegateYield(ctx, af, gen_v, r.value);
}

/* 委托状态机核心恢复（AsyncDelegateResolve/Reject 调用，§14.4.14 委托循环）。
 * 按 frame->delegate_phase 分派：
 *   - reject：清 delegate，异常注入外层字节码（外层 try/catch 可捕）
 *   - AwaitYieldValue：yield 值已 await，AsyncGeneratorYield
 *   - AwaitReturn + done：用 delegate_value 注入外层 return completion（穿透 finally）
 *   - AwaitNext/AwaitThrow + done：压 inner.value 续延外层字节码（yield* 表达式结果）
 *   - done=false：asyncDelegateYieldValue（先 await 产出值再 AsyncGeneratorYield）
 *   - 结果非对象：TypeError 注入外层 */
ConversionResult asyncDelegateResume(JSContext *ctx, AsyncFrame *frame,
                                     JSValue *result, bool is_reject) {
  JSValue *gen_v = findAsyncGenerator(frame->env);
  if (gen_v == nullptr) {
    return fail("Error: async generator not found on delegate resume");
  }
  const AsyncDelegatePhase phase = frame->delegate_phase;
  if (phase == AsyncDelegatePhase::None) {
    return fail("Error: delegate resume with no phase");
  }

  if (is_reject) {
    /* inner 方法抛错 / yield 值 await reject：清 delegate，异常注入外层字节码
     * （从 frame->pc 续延 + initial_thrown，外层 try/catch 可捕）。 */
    frame->delegate_iterator = nullptr;
    frame->delegate_phase = AsyncDelegatePhase::None;
    JSValue *thrown =
        (result != nullptr) ? result : ctx->createValue(Kind::Undefined);
    return resumeAsyncGenerator(ctx, gen_v, thrown, 1, false);
  }

  if (phase == AsyncDelegatePhase::AwaitYieldValue) {
    /* yield 值已 await 完成：AsyncGeneratorYield（结算请求 + 挂起）。 */
    return asyncDelegateYield(ctx, frame, gen_v, result);
  }

  /* 结果须为对象（§14.4.14：iter_result 必须 Object）。 */
  if (result == nullptr || !result->isObject()) {
    frame->delegate_iterator = nullptr;
    frame->delegate_phase = AsyncDelegatePhase::None;
    JSValue *thrown = delegateExceptionValue(
        ctx, "TypeError: iterator result is not an object");
    return resumeAsyncGenerator(ctx, gen_v, thrown, 1, false);
  }
  const bool done = delegateIterResultDone(result);

  if (phase == AsyncDelegatePhase::AwaitReturn) {
    if (done) {
      /* inner.return 返回 done=true：inner 已关闭。外层 return() 的 return
       * completion 不因 inner 关闭而丢失——用 delegate_value 注入外层
       * （initial_return 穿透外层 finally，§14.4.14 step 6.b）。 */
      JSValue *retval = (frame->delegate_value != nullptr)
                            ? frame->delegate_value
                            : ctx->createValue(Kind::Undefined);
      frame->delegate_iterator = nullptr;
      frame->delegate_phase = AsyncDelegatePhase::None;
      return resumeAsyncGenerator(ctx, gen_v, retval, 2, false);
    }
    /* inner.return 返回 done=false（inner 捕获 return 继续迭代）：
     * AsyncGeneratorYield（产出值）。 */
    return asyncDelegateYieldValue(ctx, frame, gen_v, result);
  }

  /* AwaitNext / AwaitThrow：done=true 则 inner 完成，压 inner.value 续延外层
   * 字节码（yield* 表达式结果 = inner.value）；done=false 则 AsyncGeneratorYield。 */
  if (done) {
    JSValue *inner_val = delegateIterResultValue(ctx, result);
    frame->delegate_iterator = nullptr;
    frame->delegate_phase = AsyncDelegatePhase::None;
    return resumeAsyncGenerator(ctx, gen_v, inner_val, 0, false);
  }
  return asyncDelegateYieldValue(ctx, frame, gen_v, result);
}

/* 委托转发：resumeAsyncGenerator 委托分支（delegate_iterator 非空）调用。
 * 把外层请求（next/throw/return）转发给 inner async iterator（§14.4.14
 * step 5 循环）：
 *   mode 0 -> inner.next(value)
 *   mode 1 -> inner.throw(value)（无 throw 方法 -> 异常注入外层）
 *   mode 2 -> inner.return(value)（无 return 方法 -> return completion 注入外层）
 * 调方法得 promise -> PromiseResolve 接合 -> asyncDelegateAwait（挂起或同步 settle）。
 * 结果由 asyncDelegateResume 处理。 */
static ConversionResult asyncDelegateForward(JSContext *ctx, JSValue *gen_v,
                                             AsyncFrame *frame, JSValue *value,
                                             int mode) {
  JSValue *iterator = frame->delegate_iterator;
  const char *name =
      (mode == 1) ? "throw" : (mode == 2) ? "return" : "next";
  JSValue *key = ctx->createValue(std::string(name));
  ConversionResult m = getMethod(ctx, iterator, key);
  if (m.value == nullptr && (!m.error.empty() || m.thrown != nullptr)) {
    /* getMethod 内部错误（属性不可调用等）：透传。 */
    frame->delegate_iterator = nullptr;
    frame->delegate_phase = AsyncDelegatePhase::None;
    JSValue *thrown = m.thrown != nullptr
                          ? m.thrown
                          : delegateExceptionValue(ctx, m.error);
    return resumeAsyncGenerator(ctx, gen_v, thrown, 1, false);
  }
  if (m.value == nullptr) {
    /* 无方法：
     *   mode 1（throw）-> 终止委托，把 value 当异常注入外层（§14.4.14
     *     step 5.b.iii：CloseIterator + 外层 throw value）。
     *   mode 2（return）-> 无 return 方法，return completion 注入外层
     *     （穿透 finally）。
     *   mode 0（next）-> TypeError（iterator.next 缺失）。 */
    frame->delegate_iterator = nullptr;
    frame->delegate_phase = AsyncDelegatePhase::None;
    JSValue *v = (value != nullptr) ? value : ctx->createValue(Kind::Undefined);
    if (mode == 1 || mode == 2) {
      return resumeAsyncGenerator(ctx, gen_v, v, mode, false);
    }
    return resumeAsyncGenerator(
        ctx, gen_v,
        delegateExceptionValue(ctx, "TypeError: iterator.next is not a function"),
        1, false);
  }

  /* 调 inner 方法（带 value 参数）。 */
  JSValue *argv[1] = {
      (value != nullptr) ? value : ctx->createValue(Kind::Undefined)};
  ConversionResult c = call(ctx, m.value, iterator, argv, 1);
  if (!c.ok()) {
    /* 方法调用同步抛错：清 delegate，异常注入外层字节码。 */
    frame->delegate_iterator = nullptr;
    frame->delegate_phase = AsyncDelegatePhase::None;
    JSValue *thrown = c.thrown != nullptr
                          ? c.thrown
                          : delegateExceptionValue(ctx, c.error);
    return resumeAsyncGenerator(ctx, gen_v, thrown, 1, false);
  }

  /* mode 2（return）：暂存外层 return value——inner.return 返回 done=true
   * 后经它注入外层 return completion（穿透 finally）。 */
  if (mode == 2) {
    frame->delegate_value = (value != nullptr)
                                ? value
                                : ctx->createValue(Kind::Undefined);
  }

  /* PromiseResolve 接合 + await。 */
  const AsyncDelegatePhase ph = (mode == 1)   ? AsyncDelegatePhase::AwaitThrow
                                : (mode == 2) ? AsyncDelegatePhase::AwaitReturn
                                              : AsyncDelegatePhase::AwaitNext;
  /* 先置 phase：同步 settle 分支不走 asyncDelegateAwait 的挂起设值，须在此
   * 预设（否则 asyncDelegateResume 读到上一轮残留 phase 而误判）。 */
  frame->delegate_phase = ph;
  JSValue *p = delegatePromiseResolve(ctx, c.value);
  ConversionResult r = asyncDelegateAwait(ctx, frame, gen_v, p, ph);
  if (r.suspended) return r; /* 挂起：等 AsyncDelegateResolve/Reject 恢复 */
  /* 同步 settle：结果 promise 已 Fulfilled/Rejected（如 AsyncFromSyncIterator
   * 的 next 同步完成）。统一走 asyncDelegateResume 按 phase 处理（done 判断 /
   * 压 inner.value 续延外层 / AsyncGeneratorYield / 异常注入外层）。 */
  const bool rej = !r.ok(); /* 同步 Rejected：value 为 nullptr、thrown 非空 */
  return asyncDelegateResume(ctx, frame, rej ? r.thrown : r.value, rej);
}

/* -------------------------------------------------------------------------- */
/* AsyncGenerator.prototype.next / throw / return                              */
/* -------------------------------------------------------------------------- */

ConversionResult asyncGeneratorProtoNext(JSContext *ctx, JSValue *self,
                                         JSValue *const *args, size_t argc,
                                         int mode) {
  if (self == nullptr || !self->isObject() ||
      !self->object()->isAsyncGenerator()) {
    return fail("TypeError: not an async generator");
  }
  JSObject *gen = self->object();
  JSValue *value = (argc > 0 && args[0] != nullptr)
                       ? args[0]
                       : ctx->createValue(Kind::Undefined);
  /* 创建 promise + resolve/reject 决议闭包（capability，§27.6.3.1 step 4）。
   * 异常路径（函数体抛错 / completed 后 throw）用 reject 闭包 settle。 */
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *promise = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(promise); /* 分配保护：入队前防误回收 */

  const int state = gen->asyncGeneratorState();
  if (state == static_cast<int>(AsyncGeneratorState::Completed)) {
    /* completed：next/throw/return 立即 settle 对应结果（§27.6.3.1 step 6-8）：
     *   - next：resolve {value:undefined, done:true}
     *   - return：resolve {value, done:true}
     *   - throw：reject value（§27.6.3.1 step 8：capability.[[Reject]]） */
    JSValue *argv[1] = {nullptr};
    if (mode == 1) {
      argv[0] = value; /* reject 值 */
      (void)call(ctx, reject, ctx->createValue(Kind::Undefined), argv, 1);
    } else {
      JSValue *v = (mode == 2) ? value : ctx->createValue(Kind::Undefined);
      argv[0] = createIterResult(ctx, v, true);
      (void)call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
    }
    ctx->heap().unpin(promise);
    return ok(promise);
  }

  /* 入队 AsyncGeneratorRequest（{completion, capability}）。 */
  JSValue *req = createAsyncGeneratorRequest(ctx, mode, value, resolve, reject);
  ctx->heap().pin(req); /* 分配保护：入队列前防误回收 */
  gen->asyncGeneratorQueue().push_back(req);
  ctx->heap().unpin(req);

  /* 队列空且非 executing 时立即驱动（§27.6.3.3：AsyncGeneratorResume）。 */
  driveAsyncGeneratorQueue(ctx, self);

  ctx->heap().unpin(promise);
  return ok(promise);
}

/* AsyncGenerator.prototype[Symbol.asyncIterator]：返回 this（§27.6.3.4），
 * 使 async generator 对象自身是异步迭代器。 */
ConversionResult asyncGeneratorSelfIterator(JSContext *ctx, JSValue *thisVal,
                                            JSValue *const *args, size_t argc) {
  (void)args;
  (void)argc;
  if (thisVal == nullptr) return ok(ctx->createValue(Kind::Undefined));
  return ok(thisVal);
}

/* -------------------------------------------------------------------------- */
/* AsyncYield 挂起 / await 续延钩子                                             */
/* -------------------------------------------------------------------------- */

/* 沿环境链查找隐藏绑定 __generator__，定位当前 AsyncGenerator 对象。
 * 非 async generator 返回 nullptr。 */
static JSValue *findAsyncGenerator(JSEnvironment *env) {
  for (JSEnvironment *e = env; e != nullptr; e = e->parent()) {
    if (e->hasBinding(kGeneratorBinding)) {
      JSValue *gv = e->getBinding(kGeneratorBinding);
      if (gv != nullptr && gv->isObject() &&
          gv->object()->isAsyncGenerator()) {
        return gv;
      }
      return nullptr;
    }
  }
  return nullptr;
}

bool suspendAsyncYield(JSContext *ctx, JSValue *gen_v, JSFunction *fn,
                       size_t next_pc, std::vector<JSValue *> &stack,
                       JSEnvironment *env, JSValue *frame_this,
                       const ConversionResult &pending, bool has_pending,
                       uint32_t pending_handle, bool pending_is_return,
                       JSValue *yield_val) {
  if (gen_v == nullptr || !gen_v->isObject() ||
      !gen_v->object()->isAsyncGenerator()) {
    return false;
  }
  JSObject *gen = gen_v->object();
  JSValue *afv = gen->asyncGeneratorContext();
  AsyncFrame *frame = asAsyncFrame(afv);
  if (frame == nullptr) {
    return false;
  }

  /* 1. resolve 当前请求 capability {value:yield_val, done:false}
   *    （§27.6.3.4 AsyncGeneratorYield step 11）。yield 值已由编译器在
   *    yield 前插 Await 指令 await 过（await 挂起/恢复续延天然复用）。 */
  JSValue *req = frame->async_request;
  if (req != nullptr && req->isObject()) {
    JSValue *cap = req->object()->getProperty("capability");
    if (cap != nullptr) {
      JSValue *argv[1] = {
          createIterResult(ctx, yield_val != nullptr ? yield_val
                                                    : ctx->createValue(Kind::Undefined),
                           false)};
      (void)call(ctx, cap, ctx->createValue(Kind::Undefined), argv, 1);
    }
  }

  /* 2. 保存现场到 AsyncGenerator 内嵌的 AsyncFrame（与 suspendYield 同构）。 */
  frame->fn = fn;
  frame->pc = next_pc;
  frame->stack = stack; /* 拷贝操作数栈（yield 值已弹栈） */
  frame->env = env;
  frame->frame_this = frame_this;
  frame->pending = pending;
  frame->has_pending = has_pending;
  frame->pending_handle = pending_handle;
  frame->pending_is_return = pending_is_return;
  frame->yield_value = yield_val;

  /* 3. 置 SuspendedYield + 清 async_request（请求已结算）。 */
  gen->setAsyncGeneratorState(
      static_cast<int>(AsyncGeneratorState::SuspendedYield));
  frame->async_request = nullptr;

  /* 4. 驱动队列中下一个请求（若有排队，§27.6.3.3：AsyncGeneratorResume 在
   *    请求完成后继续驱动队列）。 */
  driveAsyncGeneratorQueue(ctx, gen_v);

  (void)ctx;
  return true;
}

void asyncGeneratorAfterSuspend(JSContext *ctx, AsyncFrame *frame) {
  if (frame == nullptr) return;
  JSValue *gen_v = findAsyncGenerator(frame->env);
  if (gen_v == nullptr) return;
  JSObject *gen = gen_v->object();
  /* AsyncYield 挂起（请求已由 suspendAsyncYield 结算）：现场已保存到
   * gen_frame_，驱动队列中下一个请求（若有排队）。await 挂起（Executing，
   * async_request 已由 suspendAwait 传播到新帧）则不动——等 awaitable
   * settle 后续延。 */
  if (gen->asyncGeneratorState() ==
      static_cast<int>(AsyncGeneratorState::SuspendedYield)) {
    driveAsyncGeneratorQueue(ctx, gen_v);
  }
}

ConversionResult asyncGeneratorFrameSettled(JSContext *ctx, AsyncFrame *frame,
                                            ConversionResult result) {
  JSValue *gen_v = findAsyncGenerator(frame != nullptr ? frame->env : nullptr);
  if (gen_v == nullptr) {
    return ok(ctx->createValue(Kind::Undefined));
  }
  JSObject *gen = gen_v->object();

  /* 当前请求 + capability（driveAsyncGeneratorQueue 或 suspendAwait 传播）。
   * capability = resolve 闭包，capability_reject = reject 闭包
   * （§27.6.3.5 step 5-6 分别调用）。 */
  JSValue *req = (frame != nullptr) ? frame->async_request : nullptr;
  JSValue *resolve = nullptr, *reject = nullptr;
  if (req != nullptr && req->isObject()) {
    resolve = req->object()->getProperty("capability");
    reject = req->object()->getProperty("capability_reject");
  }

  /* 置 Completed + 清现场（§27.6.3.5 AsyncGeneratorComplete step 2-3）。 */
  gen->setAsyncGeneratorContext(nullptr);
  gen->setAsyncGeneratorState(static_cast<int>(AsyncGeneratorState::Completed));

  if (!result.ok()) {
    /* 执行中抛错：reject 当前请求 capability + 队列全部剩余请求
     * （§27.6.3.5 step 5：全部 reject reason）。错误进请求 promise，不冒泡。 */
    JSValue *reason = result.thrown;
    if (reason == nullptr) {
      reason = ctx->createValue(Kind::Exception);
      reason->setErrorInfo(result.error);
    }
    JSValue *reject_fn = (reject != nullptr) ? reject : resolve;
    if (reject_fn != nullptr) {
      JSValue *argv[1] = {reason};
      (void)call(ctx, reject_fn, ctx->createValue(Kind::Undefined), argv, 1);
    }
    std::vector<JSValue *> &queue = gen->asyncGeneratorQueue();
    for (JSValue *q : queue) {
      if (q == nullptr || !q->isObject()) continue;
      JSValue *cap = q->object()->getProperty("capability_reject");
      if (cap == nullptr) cap = q->object()->getProperty("capability");
      if (cap != nullptr) {
        JSValue *argv[1] = {reason};
        (void)call(ctx, cap, ctx->createValue(Kind::Undefined), argv, 1);
      }
    }
    queue.clear();
    return ok(ctx->createValue(Kind::Undefined)); /* 错误不向驱动方冒泡 */
  }

  /* 正常完成：resolve 当前请求 {value:retval, done:true} + 队列全部剩余
   * 请求 {value:undefined, done:true}（§27.6.3.5 step 6）。 */
  JSValue *retval = result.value;
  if (retval == nullptr) retval = ctx->createValue(Kind::Undefined);
  if (resolve != nullptr) {
    JSValue *argv[1] = {createIterResult(ctx, retval, true)};
    (void)call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
  }
  std::vector<JSValue *> &queue = gen->asyncGeneratorQueue();
  for (JSValue *q : queue) {
    if (q == nullptr || !q->isObject()) continue;
    JSValue *cap = q->object()->getProperty("capability");
    if (cap != nullptr) {
      JSValue *argv[1] = {createIterResult(ctx, ctx->createValue(Kind::Undefined),
                                           true)};
      (void)call(ctx, cap, ctx->createValue(Kind::Undefined), argv, 1);
    }
  }
  queue.clear();
  return ok(ctx->createValue(Kind::Undefined));
}

/* -------------------------------------------------------------------------- */
/* AsyncGenerator.prototype（惰性创建 + 缓存 + 进根集）                          */
/* -------------------------------------------------------------------------- */

/* 取 AsyncGenerator.prototype（惰性创建 + 缓存 + 进根集）。
 * asyncGeneratorCall 新建 AsyncGenerator 对象的原型链根
 * （next/throw/return + Symbol.asyncIterator 所在）。原型链 -> Object.prototype。
 * 注意：方法注册在 builtins.cc 的 installAsyncGeneratorBuiltins（M5.5c-1）。 */
JSValue *asyncGeneratorProto(JSContext *ctx) {
  JSValue *p = ctx->asyncGeneratorProto();
  if (p != nullptr) return p;
  p = ctx->createObject(objectProto(ctx)->object());
  ctx->heap().pin(p); /* 分配保护（M3.3）：进根集前防误回收 */
  ctx->setAsyncGeneratorProto(p); /* 进根集（markRoots 遍历 async_generator_proto_） */
  ctx->heap().unpin(p);
  return p;
}

}  // namespace neojs
