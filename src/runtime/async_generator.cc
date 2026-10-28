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
#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/environment.h"
#include "neojs/runtime/function.h"
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
