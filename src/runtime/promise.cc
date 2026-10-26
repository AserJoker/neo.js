/*
 * 内部 C++ 实现：Promise（M5.3，§27.2）与微任务队列（Job Queue）。
 *
 * 遵循 docs/architecture.md §8 B2（控制/异步）与 ECMA-262 §27.2：
 *   - 对象模型见 object.h（ObjectKind::Promise + promise_state_/promise_result_）。
 *   - 决议：promiseResolveInternal / promiseRejectInternal（§27.2.1.4 / §27.2.1.2）。
 *   - 微任务队列由 JSContext 持有（job_queue_），宿主经 JS_RunJobs 驱动。
 *
 * 核心设计（复用 Bound 函数承载捕获数据，避免手写字节码闭包）：
 *   - resolve/reject 闭包 = Bound(internal, undefined, [promise])：调用时把
 *     promise 作为首参转发给内部函数（§27.2.1.3.2 决议函数）。
 *   - reaction job = Bound(ReactionRunner, undefined, [handler, value, resolve,
 *     reject])：PromiseReactionJob（§27.2.2.2）——调用 handler、再 settle 派生
 *     promise。handler 为 undefined 时恒等（原值传递）。
 *   - thenable job = Bound(ThenableRunner, undefined, [promise, resolution,
 *     then])：PromiseResolveThenableJob（§27.2.2.3）——then.call(resolution,
 *     resolve, reject)，实现 then 链式返回 promise 的接合。
 *   - finally 的 thenFinally/catchFinally（§27.2.4.5）用 Bound + 内部函数
 *     （FinallyThen/FinallyCatch）实现：先跑 onFinally，再 resultPromise.then(
 *     () => value / throw reason)。
 *
 * GC 注意：reactions 不直接存对象内部槽（避免改 gcMarkRefs），而是以 Job
 * 函数（Bound 闭包）形式入微任务队列；队列由 JSContext::markRoots 标记。
 */

#include "neojs/runtime/promise.h"

#include <string>
#include <utility>
#include <vector>

#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/iterator.h"
#include "neojs/runtime/object.h"
#include "neojs/runtime/value.h"

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

/* 创建内置函数（Builtin 类别）：JSFunction + 包装 JSValue + 反向指针。
 * 原型链 -> Function.prototype（惰性创建缓存）。 */
JSValue *makeBuiltin(JSContext *ctx, JSBuiltinFn fn, ThisMode mode) {
  JSObject *fproto = nullptr;
  JSValue *fp = functionProto(ctx);
  if (fp != nullptr) fproto = fp->object();
  JSFunction *f = ctx->heap().allocGCObject<JSFunction>(fproto, fn, mode);
  ctx->heap().pin(f); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(f);
  f->setHolder(v);
  ctx->heap().unpin(f);
  return v;
}

/* 内置方法/属性注册（规范语义）：enumerable=false（§19.1.2 等）。 */
void defineMethod(JSObject *obj, const std::string &name, JSValue *value) {
  PropertyDescriptor d;
  d.value = value;
  d.enumerable = false;
  obj->setOwnProperty(name, d);
}

/* 创建 Bound 函数：target 为被调用函数，args 为捕获的[[BoundArguments]]。
 * this 绑定 undefined（决议函数/runner 都不需要 this）。 */
JSValue *makeBound(JSContext *ctx, JSValue *target, std::vector<JSValue *> args) {
  JSObject *fproto = nullptr;
  JSValue *fp = functionProto(ctx);
  if (fp != nullptr) fproto = fp->object();
  JSFunction *f = ctx->heap().allocGCObject<JSFunction>(
      fproto, FunctionKind::Bound, nullptr /* func */);
  f->setBoundData(target, ctx->createValue(Kind::Undefined), std::move(args));
  ctx->heap().pin(f); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(f);
  f->setHolder(v);
  ctx->heap().unpin(f);
  return v;
}

/* 沿原型链查找命名属性描述符（vm.cc 的同名函数在匿名命名空间，
 * 本模块独立定义）。供 thenable 检查 / [[Get]] 语义复用。 */
PropertyDescriptor *findPropDesc(JSObject *obj, const std::string &name) {
  for (JSObject *o = obj; o != nullptr; o = o->prototype()) {
    PropertyDescriptor *desc = o->getOwnProperty(name);
    if (desc != nullptr) return desc;
  }
  return nullptr;
}

/* 创建 Promise 对象：kind=Promise，原型链 -> Promise.prototype，初始
 * pending。reactions 以 Job 闭包形式存微任务队列，不经对象内部槽。 */
JSValue *newPromiseObject(JSContext *ctx) {
  JSObject *proto = nullptr;
  JSValue *pp = promiseProto(ctx);
  if (pp != nullptr) proto = pp->object();
  JSObject *obj = ctx->heap().allocGCObject<JSObject>(ObjectKind::Promise, 0);
  if (proto != nullptr) obj->setPrototype(proto);
  ctx->heap().pin(obj);
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(obj);
  obj->setHolder(v);
  ctx->heap().unpin(obj);
  return v;
}

/* 入队一个微任务 Job（Job 是可调用函数：Bound 闭包）。 */
void enqueueJob(JSContext *ctx, JSValue *job) { ctx->enqueueJob(job); }

/* 入队 reaction job：PromiseReactionJob（§27.2.2.2）。
 *   handler 为 then/catch 的回调（可 undefined）；
 *   value 为 settled 值；resolve/reject 为派生 promise 的决议闭包；
 *   is_reject 标记该 reaction 类型（0=fulfill / 1=reject），
 *   恒等（handler 缺省）时据此决定转发 resolve 或 reject。
 * 用 Bound(ReactionRunner, undefined, [handler, value, resolve, reject, is_reject])。 */
void enqueueReactionJob(JSContext *ctx, JSValue *handler, JSValue *value,
                        JSValue *resolve, JSValue *reject, bool is_reject) {
  std::vector<JSValue *> data;
  data.push_back(handler);
  data.push_back(value);
  data.push_back(resolve);
  data.push_back(reject);
  data.push_back(ctx->createValue(is_reject ? 1.0 : 0.0));
  enqueueJob(ctx, makeBound(
                      ctx, getPromiseInternal(ctx, PromiseInternal::ReactionRunner),
                      std::move(data)));
}

/* 入队 thenable job：PromiseResolveThenableJob（§27.2.2.3）。
 *   promise 为被接合的 promise；resolution 为 thenable；then 为其 then 方法。
 * 用 Bound(ThenableRunner, undefined, [promise, resolution, then])。 */
void enqueueThenableJob(JSContext *ctx, JSValue *promise, JSValue *resolution,
                        JSValue *then) {
  std::vector<JSValue *> data;
  data.push_back(promise);
  data.push_back(resolution);
  data.push_back(then);
  enqueueJob(ctx, makeBound(
                      ctx, getPromiseInternal(ctx, PromiseInternal::ThenableRunner),
                      std::move(data)));
}

/* 创建 resolve/reject 决议闭包（§27.2.1.3.2）：
 * Bound(internal, undefined, [promise])。调用时把 promise 作为首参转发。 */
JSValue *makeResolutionClosure(JSContext *ctx, PromiseInternal kind,
                               JSValue *promise) {
  std::vector<JSValue *> data;
  data.push_back(promise);
  return makeBound(ctx, getPromiseInternal(ctx, kind), std::move(data));
}

/* §27.2.1.2 RejectPromise：promise 已 settled 则忽略；否则置 rejected +
 * [[PromiseResult]]，入队全部 reject reactions（reaction job）。 */
void rejectPromise(JSContext *ctx, JSValue *promise, JSValue *reason) {
  JSObject *p = promise->object();
  if (p->promiseState() != static_cast<int>(PromiseState::Pending)) return;
  p->setPromiseState(static_cast<int>(PromiseState::Rejected));
  p->setPromiseResult(reason);
  /* 派发 pending 时挂载的 reactions（存于 __reactions__ 数组）。 */
  PropertyDescriptor *rd = p->getOwnProperty("__reactions__");
  if (rd != nullptr && rd->value != nullptr && rd->value->isObject() &&
      rd->value->object()->isArray()) {
    JSObject *list = rd->value->object();
    for (size_t i = 0; i < list->length(); ++i) {
      JSValue *rec = list->getElement(i);
      if (rec == nullptr || !rec->isObject()) continue;
      JSObject *ro = rec->object();
      /* reject reaction：取 on_rejected（rec[1]）。 */
      JSValue *handler = ro->getElement(1);
      JSValue *resolve = ro->getElement(2);
      JSValue *reject = ro->getElement(3);
      if (reject == nullptr) continue;
      /* handler 为 undefined（catch 链无回调）时原值传递（reject 转发）。 */
      if (handler == nullptr || handler->kind() == Kind::Undefined) {
        JSValue *argv[1] = {reason};
        ConversionResult r = call(ctx, reject, ctx->createValue(Kind::Undefined),
                                  argv, 1);
        (void)r;
        continue;
      }
      enqueueReactionJob(ctx, handler, reason, resolve, reject, true);
    }
  }
}

/* §27.2.1.4 ResolvePromise（v1 简化，不支持 thenable 接合——无捕获
 * [[PromiseResolveThenableJobs]] 表）：resolution 为 thenable 时入队
 * thenable job，使 then 链式返回接合到派生 promise；非 thenable 直接
 * settle（fulfilled + result）。 */
void resolvePromise(JSContext *ctx, JSValue *promise, JSValue *resolution) {
  JSObject *p = promise->object();
  if (p->promiseState() != static_cast<int>(PromiseState::Pending)) return;

  /* 取 resolution.then（§27.2.1.4 步骤 6）。非对象 -> 直接 fulfill。 */
  JSValue *then = nullptr;
  if (resolution != nullptr && resolution->isObject()) {
    JSObject *ro = resolution->object();
    PropertyDescriptor *desc = findPropDesc(ro, "then");
    if (desc != nullptr) {
      if (desc->type == PropertyDescriptor::Type::Accessor) {
        if (desc->getter != nullptr && isCallable(desc->getter)) {
          ConversionResult g = call(ctx, desc->getter, resolution, nullptr, 0);
          if (!g.ok()) {
            /* 取 then 抛错 -> reject（§27.2.1.4 步骤 8）。 */
            JSValue *err = g.thrown;
            if (err == nullptr) {
              err = ctx->createValue(Kind::Exception);
              err->setErrorInfo(g.error);
            }
            rejectPromise(ctx, promise, err);
            return;
          }
          then = g.value;
        }
      } else {
        then = desc->value;
      }
    }
  }
  if (then == nullptr || !isCallable(then)) {
    /* 非 thenable：直接 settle（fulfilled）。 */
    p->setPromiseState(static_cast<int>(PromiseState::Fulfilled));
    p->setPromiseResult(resolution);
    /* 派发 pending 时挂载的 reactions。 */
    PropertyDescriptor *rd = p->getOwnProperty("__reactions__");
    if (rd != nullptr && rd->value != nullptr && rd->value->isObject() &&
        rd->value->object()->isArray()) {
      JSObject *list = rd->value->object();
      for (size_t i = 0; i < list->length(); ++i) {
        JSValue *rec = list->getElement(i);
        if (rec == nullptr || !rec->isObject()) continue;
        JSObject *ro = rec->object();
        /* fulfill reaction：取 on_fulfilled（rec[0]）。 */
        JSValue *handler = ro->getElement(0);
        JSValue *resolve = ro->getElement(2);
        JSValue *reject = ro->getElement(3);
        if (resolve == nullptr) continue;
        if (handler == nullptr || handler->kind() == Kind::Undefined) {
          JSValue *argv[1] = {resolution};
          ConversionResult r =
              call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
          (void)r;
          continue;
        }
        enqueueReactionJob(ctx, handler, resolution, resolve, reject, false);
      }
    }
    return;
  }

  /* thenable：入队 PromiseResolveThenableJob。 */
  enqueueThenableJob(ctx, promise, resolution, then);
}

/* 创建派生 promise（then/catch 的返回）：Promise(executor) 语义——先建
 * pending promise，再创建 resolve/reject 闭包。派生 promise 经闭包在
 * reaction job 中 settle。定义于公开区（M5.2c-2 动态 import 复用）。 */

/* Promise.prototype.then（§27.2.4.1）：
 * 1. this 须为 Promise 对象；2. onFulfilled/onRejected 非函数按 undefined
 * 处理（恒等）；3. 创建派生 promise；4. 挂 reaction（pending）或立即
 * 入队 reaction job（已 settled，经微任务调度）。 */
ConversionResult promiseThen(JSContext *ctx, JSValue *thisVal,
                             JSValue *const *args, size_t argc) {
  if (thisVal == nullptr || !thisVal->isObject() ||
      !thisVal->object()->isPromise()) {
    return fail("TypeError: Promise.prototype.then called on non-promise");
  }
  JSObject *p = thisVal->object();
  JSValue *on_fulfilled =
      argc > 0 && args[0] != nullptr && isCallable(args[0]) ? args[0] : nullptr;
  JSValue *on_rejected =
      argc > 1 && args[1] != nullptr && isCallable(args[1]) ? args[1] : nullptr;

  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *derived = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(derived); /* 分配保护：reaction 挂载前防误回收 */

  if (p->promiseState() == static_cast<int>(PromiseState::Pending)) {
    /* 挂到 pending promise 的 reaction 列表（__reactions__ 数组，元素为
     * [on_fulfilled, on_rejected, resolve, reject]）。settle 时按状态取
     * 对应 handler（resolvePromise 取 0，rejectPromise 取 1）统一派发。 */
    JSValue *list = nullptr;
    PropertyDescriptor *rd = p->getOwnProperty("__reactions__");
    if (rd != nullptr && rd->value != nullptr) list = rd->value;
    JSValue *rec = ctx->createArray(4);
    rec->object()->setElement(
        0, on_fulfilled != nullptr ? on_fulfilled
                                   : ctx->createValue(Kind::Undefined));
    rec->object()->setElement(
        1, on_rejected != nullptr ? on_rejected
                                  : ctx->createValue(Kind::Undefined));
    rec->object()->setElement(2, resolve);
    rec->object()->setElement(3, reject);
    if (list == nullptr) {
      list = ctx->createArray(0);
      PropertyDescriptor d;
      d.value = list;
      d.enumerable = false;
      p->setOwnProperty("__reactions__", d);
    }
    list->object()->setElement(list->object()->length(), rec);
    ctx->heap().unpin(derived);
    return ok(derived);
  }

  /* 已 settled：立即入队 reaction job（handler + value + resolve/reject）。
   * is_reject 标记 reaction 类型：恒等（handler 缺省）时据此转发。 */
  bool is_reject =
      p->promiseState() == static_cast<int>(PromiseState::Rejected);
  JSValue *handler =
      p->promiseState() == static_cast<int>(PromiseState::Fulfilled)
          ? (on_fulfilled != nullptr ? on_fulfilled
                                     : ctx->createValue(Kind::Undefined))
          : (on_rejected != nullptr ? on_rejected
                                    : ctx->createValue(Kind::Undefined));
  enqueueReactionJob(ctx, handler, p->promiseResult(), resolve, reject, is_reject);
  ctx->heap().unpin(derived);
  return ok(derived);
}

/* Promise.prototype.catch（§27.2.4.2）：then(undefined, onRejected)。 */
ConversionResult promiseCatch(JSContext *ctx, JSValue *thisVal,
                              JSValue *const *args, size_t argc) {
  JSValue *then = thisVal != nullptr && thisVal->isObject()
                      ? thisVal->object()->getProperty("then")
                      : nullptr;
  if (then == nullptr || !isCallable(then)) {
    return fail("TypeError: Promise.prototype.catch called on non-promise");
  }
  JSValue *on_rejected =
      argc > 0 && args[0] != nullptr ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *argv[2] = {ctx->createValue(Kind::Undefined), on_rejected};
  return call(ctx, then, thisVal, argv, 2);
}

/* Promise.prototype.finally（§27.2.4.5）：
 * this.then(thenFinally, catchFinally)。onFinally 执行（副作用），
 * 原值/原 reason 透传到派生 promise：
 *   - fulfill 分支用 Bound(Identity, [on_finally])：Identity(on_finally, value)
 *     先跑 on_finally 再返回 value（透传原值）。
 *   - reject 分支用 Bound(Throw, [on_finally])：Throw(on_finally, reason)
 *     先跑 on_finally 再抛原 reason（经 thrown 传回，派生 promise reject）。 */
ConversionResult promiseFinally(JSContext *ctx, JSValue *thisVal,
                                JSValue *const *args, size_t argc) {
  if (thisVal == nullptr || !thisVal->isObject() ||
      !thisVal->object()->isPromise()) {
    return fail("TypeError: Promise.prototype.finally called on non-promise");
  }
  JSValue *on_finally =
      argc > 0 && args[0] != nullptr ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *then = thisVal->object()->getProperty("then");
  if (then == nullptr || !isCallable(then)) {
    return fail("TypeError: Promise.prototype.finally called on non-promise");
  }
  JSValue *fulfill_wrapper =
      makeBound(ctx, getPromiseInternal(ctx, PromiseInternal::Identity),
                {on_finally});
  JSValue *reject_wrapper =
      makeBound(ctx, getPromiseInternal(ctx, PromiseInternal::Throw),
                {on_finally});
  JSValue *argv[2] = {fulfill_wrapper, reject_wrapper};
  return call(ctx, then, thisVal, argv, 2);
}

/* Promise 构造器（§27.2.3.1）：new Promise(executor)。
 * executor 必须是函数；立即调用 executor(resolve, reject)。 */
ConversionResult promiseConstructor(JSContext *ctx, JSValue *, JSValue *const *args,
                                    size_t argc) {
  if (argc < 1 || args[0] == nullptr || !isCallable(args[0])) {
    return fail("TypeError: Promise executor is not a function");
  }
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：executor 调用期间防误回收 */
  JSValue *executor_args[2] = {resolve, reject};
  ConversionResult r = call(ctx, args[0], ctx->createValue(Kind::Undefined),
                            executor_args, 2);
  if (!r.ok()) {
    if (r.thrown != nullptr) {
      /* JS throw：reject(p, thrown)。 */
      rejectPromise(ctx, p, r.thrown);
    } else {
      /* 内部错误：转 Exception 值 reject。 */
      JSValue *err = ctx->createValue(Kind::Exception);
      err->setErrorInfo(r.error);
      rejectPromise(ctx, p, err);
    }
  }
  ctx->heap().unpin(p);
  return ok(p);
}

/* Promise.resolve（§27.2.5.5）：x 为 Promise 则原样返回；否则
 * new Promise(res => res(x))。 */
ConversionResult promiseResolveStatic(JSContext *ctx, JSValue *, JSValue *const *args,
                                      size_t argc) {
  if (argc > 0 && args[0] != nullptr && args[0]->isObject() &&
      args[0]->object()->isPromise()) {
    return ok(args[0]);
  }
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：决议前防误回收 */
  JSValue *x = argc > 0 ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *argv[1] = {x};
  ConversionResult r = call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
  ctx->heap().unpin(p);
  if (!r.ok()) return r;
  return ok(p);
}

/* Promise.reject（§27.2.5.4）：new Promise((_, rej) => rej(r))。 */
ConversionResult promiseRejectStatic(JSContext *ctx, JSValue *, JSValue *const *args,
                                     size_t argc) {
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：决议前防误回收 */
  JSValue *r = argc > 0 ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *argv[1] = {r};
  ConversionResult rr = call(ctx, reject, ctx->createValue(Kind::Undefined), argv, 1);
  ctx->heap().unpin(p);
  if (!rr.ok()) return rr;
  return ok(p);
}

/* -------------------------------------------------------------------------- */
/* M5.3b 组合方法辅助                                                          */
/* -------------------------------------------------------------------------- */

/* PromiseResolve（§27.2.5.5 抽象运算的简化版）：x 为 Promise 原样返回；
 * 否则创建派生 promise + 调用 resolve 闭包（Promise.resolve 语义）。
 * 组合方法（all/race/any）的每个元素都经此转成 promise。 */
ConversionResult promiseResolveValue(JSContext *ctx, JSValue *x) {
  if (x != nullptr && x->isObject() && x->object()->isPromise()) {
    return ok(x);
  }
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：决议前防误回收 */
  JSValue *v = x != nullptr ? x : ctx->createValue(Kind::Undefined);
  JSValue *argv[1] = {v};
  ConversionResult r = call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
  ctx->heap().unpin(p);
  if (!r.ok()) return r;
  return ok(p);
}

/* 创建组合方法的状态对象：属性承载可变共享状态（与 __reactions__ 同模式）。
 *   resolve/reject  结果 promise 的决议闭包
 *   remaining       剩余元素计数（Number）
 *   result          结果数组（all/allSettled 收集）
 *   errors          错误数组（any 收集 AggregateError）
 *   settled         是否已 settle（any/race 短路）
 * 状态对象由元素回调（Bound 闭包）捕获，经属性读写共享。 */
JSValue *makeCombinatorState(JSContext *ctx, JSValue *resolve, JSValue *reject) {
  JSValue *state = ctx->createObject(nullptr);
  ctx->heap().pin(state); /* 分配保护：属性挂载前防误回收 */
  state->object()->setProperty("__resolve__", resolve);
  state->object()->setProperty("__reject__", reject);
  state->object()->setProperty("__remaining__", ctx->createValue(0.0));
  state->object()->setProperty("__result__", ctx->createArray(0));
  state->object()->setProperty("__errors__", ctx->createArray(0));
  state->object()->setProperty("__settled__", ctx->createValue(false));
  ctx->heap().unpin(state);
  return state;
}

/* 创建元素回调闭包：Bound(ElementRunner, [state, index])。
 * ElementRunner 内部函数按组合类型（PromiseInternal 枚举）分派：
 *   AllElement / AllSettledElement / AnyElement / RaceElement。 */
JSValue *makeElementClosure(JSContext *ctx, PromiseInternal kind, JSValue *state,
                            size_t index) {
  std::vector<JSValue *> data;
  data.push_back(state);
  data.push_back(ctx->createValue(static_cast<double>(index)));
  return makeBound(ctx, getPromiseInternal(ctx, kind), std::move(data));
}

/* 状态对象属性读取辅助。 */
JSValue *stateProp(JSObject *s, const char *name) { return s->getProperty(name); }

/* 状态对象属性写入辅助。 */
void setStateProp(JSObject *s, const char *name, JSValue *v) {
  s->setProperty(name, v);
}

/* 创建 AggregateError（any 全 reject 时 reject 之，§27.2.5.1）：
 * 用 Error 构造器创建（name=AggregateError），errors 属性指向 errors 数组。
 * 引擎无独立 AggregateError 构造器（M5.6+ 补齐），此处用 Error 近似。 */
ConversionResult makeAggregateError(JSContext *ctx, JSValue *errors) {
  JSValue *global = ctx->globalObject();
  JSValue *error_ctor = global->object()->getProperty("Error");
  if (error_ctor == nullptr || !isCallable(error_ctor)) {
    JSValue *err = ctx->createValue(Kind::Exception);
    err->setErrorInfo("Error: AggregateError construction failed");
    return fail(err->errorInfo());
  }
  /* message 参数传 undefined（errorConstructor 对 undefined 不调 toString，
   * 数组会 ToPrimitive 失败），errors 单独设属性（§27.2.5.1 步骤 10-12）。 */
  JSValue *msg = ctx->createValue(Kind::Undefined);
  JSValue *argv[1] = {msg};
  ConversionResult r = construct(ctx, error_ctor, argv, 1);
  if (!r.ok()) return r;
  if (r.value != nullptr) {
    ctx->heap().pin(r.value);
    r.value->object()->setProperty("name", ctx->createValue(std::string("AggregateError")));
    r.value->object()->setProperty("errors", errors);
    ctx->heap().unpin(r.value);
  }
  return r;
}

/* -------------------------------------------------------------------------- */
/* M5.3b：组合方法静态入口                                                     */
/* -------------------------------------------------------------------------- */

/* Promise.all（§27.2.5.3）：遍历 iterable，每元素 promiseResolve + then
 * 收集到 result[index]；全部完成后 resolve(result)。
 * 元素回调 = AllElement(state, index)：result[index]=value，remaining--，
 * remaining==0 时 resolve(result)。任一元素 reject -> reject(reason)（短路）。 */
ConversionResult promiseAllStatic(JSContext *ctx, JSValue *, JSValue *const *args,
                                  size_t argc) {
  JSValue *iterable = argc > 0 ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：遍历期间防误回收 */

  ConversionResult it = getIterator(ctx, iterable);
  if (!it.ok()) {
    ctx->heap().unpin(p);
    return fail(it.error);
  }

  JSValue *state = makeCombinatorState(ctx, resolve, reject);
  ctx->heap().pin(state); /* 分配保护：遍历期间防误回收 */
  size_t index = 0;
  for (;;) {
    ConversionResult step = iteratorStep(ctx, it.value);
    if (step.value == nullptr && !step.error.empty()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(step.error);
    }
    if (step.value == nullptr) break; /* 迭代结束 */
    ConversionResult v = iteratorValue(ctx, step.value);
    if (!v.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(v.error);
    }
    ConversionResult pv = promiseResolveValue(ctx, v.value);
    if (!pv.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(pv.error);
    }
    /* 更新 remaining 计数（初始 0，每元素 +1）。 */
    JSValue *rem = stateProp(state->object(), "__remaining__");
    double n = (rem != nullptr && rem->isNumber()) ? rem->toNumber() : 0.0;
    setStateProp(state->object(), "__remaining__",
                 ctx->createValue(n + 1.0));
    /* 元素 promise.then(AllElement(state, index), reject)。 */
    JSValue *on_fulfilled = makeElementClosure(ctx, PromiseInternal::AllElement,
                                               state, index);
    JSValue *then = pv.value->object()->getProperty("then");
    if (then == nullptr || !isCallable(then)) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail("TypeError: element is not a promise");
    }
    JSValue *argv[2] = {on_fulfilled, reject};
    ConversionResult r = call(ctx, then, pv.value, argv, 2);
    if (!r.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return r;
    }
    ++index;
  }

  /* 空 iterable：立即 resolve 空数组。 */
  JSValue *rem = stateProp(state->object(), "__remaining__");
  double n = (rem != nullptr && rem->isNumber()) ? rem->toNumber() : 0.0;
  if (n == 0.0) {
    JSValue *result = stateProp(state->object(), "__result__");
    JSValue *argv[1] = {result};
    ConversionResult r =
        call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
    ctx->heap().unpin(state);
    ctx->heap().unpin(p);
    if (!r.ok()) return r;
    return ok(p);
  }

  ctx->heap().unpin(state);
  ctx->heap().unpin(p);
  return ok(p);
}

/* Promise.race（§27.2.5.6）：任一元素 settle 即 settle（先到先得）。
 * 元素回调 = RaceElement(state, index)：value 时 resolve(value)，reason 时
 * reject(reason)，settled 短路防重复。 */
ConversionResult promiseRaceStatic(JSContext *ctx, JSValue *, JSValue *const *args,
                                   size_t argc) {
  JSValue *iterable = argc > 0 ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：遍历期间防误回收 */

  ConversionResult it = getIterator(ctx, iterable);
  if (!it.ok()) {
    ctx->heap().unpin(p);
    return fail(it.error);
  }

  JSValue *state = makeCombinatorState(ctx, resolve, reject);
  ctx->heap().pin(state); /* 分配保护：遍历期间防误回收 */
  for (;;) {
    ConversionResult step = iteratorStep(ctx, it.value);
    if (step.value == nullptr && !step.error.empty()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(step.error);
    }
    if (step.value == nullptr) break;
    ConversionResult v = iteratorValue(ctx, step.value);
    if (!v.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(v.error);
    }
    ConversionResult pv = promiseResolveValue(ctx, v.value);
    if (!pv.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(pv.error);
    }
    JSValue *on_fulfilled = makeElementClosure(ctx, PromiseInternal::RaceElement,
                                               state, 0);
    JSValue *on_rejected = makeElementClosure(ctx, PromiseInternal::RaceElement,
                                              state, 0x80000000u);
    JSValue *then = pv.value->object()->getProperty("then");
    if (then == nullptr || !isCallable(then)) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail("TypeError: element is not a promise");
    }
    JSValue *argv[2] = {on_fulfilled, on_rejected};
    ConversionResult r = call(ctx, then, pv.value, argv, 2);
    if (!r.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return r;
    }
  }

  ctx->heap().unpin(state);
  ctx->heap().unpin(p);
  return ok(p);
}

/* Promise.allSettled（§27.2.5.2）：每元素 settle 时记录
 * {status:'fulfilled', value} 或 {status:'rejected', reason}；全部完成
 * resolve(记录数组)。不短路（reject 也收集）。
 * 元素回调 = AllSettledElement(state, index)，参数为 (value) 或 (reason)。 */
ConversionResult promiseAllSettledStatic(JSContext *ctx, JSValue *,
                                         JSValue *const *args, size_t argc) {
  JSValue *iterable = argc > 0 ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：遍历期间防误回收 */

  ConversionResult it = getIterator(ctx, iterable);
  if (!it.ok()) {
    ctx->heap().unpin(p);
    return fail(it.error);
  }

  JSValue *state = makeCombinatorState(ctx, resolve, reject);
  ctx->heap().pin(state); /* 分配保护：遍历期间防误回收 */
  size_t index = 0;
  for (;;) {
    ConversionResult step = iteratorStep(ctx, it.value);
    if (step.value == nullptr && !step.error.empty()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(step.error);
    }
    if (step.value == nullptr) break;
    ConversionResult v = iteratorValue(ctx, step.value);
    if (!v.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(v.error);
    }
    ConversionResult pv = promiseResolveValue(ctx, v.value);
    if (!pv.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(pv.error);
    }
    JSValue *rem = stateProp(state->object(), "__remaining__");
    double n = (rem != nullptr && rem->isNumber()) ? rem->toNumber() : 0.0;
    setStateProp(state->object(), "__remaining__", ctx->createValue(n + 1.0));
    JSValue *on_fulfilled = makeElementClosure(
        ctx, PromiseInternal::AllSettledElement, state, index);
    /* reject 闭包：index 高位标记（fulfill=index / reject=index|0x80000000）。 */
    JSValue *on_rejected = makeElementClosure(
        ctx, PromiseInternal::AllSettledElement, state,
        index | 0x80000000u);
    JSValue *then = pv.value->object()->getProperty("then");
    if (then == nullptr || !isCallable(then)) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail("TypeError: element is not a promise");
    }
    JSValue *argv[2] = {on_fulfilled, on_rejected};
    ConversionResult r = call(ctx, then, pv.value, argv, 2);
    if (!r.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return r;
    }
    ++index;
  }

  JSValue *rem = stateProp(state->object(), "__remaining__");
  double n = (rem != nullptr && rem->isNumber()) ? rem->toNumber() : 0.0;
  if (n == 0.0) {
    JSValue *result = stateProp(state->object(), "__result__");
    JSValue *argv[1] = {result};
    ConversionResult r =
        call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
    ctx->heap().unpin(state);
    ctx->heap().unpin(p);
    if (!r.ok()) return r;
    return ok(p);
  }

  ctx->heap().unpin(state);
  ctx->heap().unpin(p);
  return ok(p);
}

/* Promise.any（§27.2.5.1）：任一元素 fulfill -> resolve(value)；全部
 * reject -> reject(AggregateError(errors))。
 * 元素回调 = AnyElement(state, index)：fulfill 时 resolve(value)（短路）；
 * reject 时 errors.push(reason)，remaining--，全 reject 后 reject AggregateError。 */
ConversionResult promiseAnyStatic(JSContext *ctx, JSValue *, JSValue *const *args,
                                  size_t argc) {
  JSValue *iterable = argc > 0 ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：遍历期间防误回收 */

  ConversionResult it = getIterator(ctx, iterable);
  if (!it.ok()) {
    ctx->heap().unpin(p);
    return fail(it.error);
  }

  JSValue *state = makeCombinatorState(ctx, resolve, reject);
  ctx->heap().pin(state); /* 分配保护：遍历期间防误回收 */
  size_t index = 0;
  for (;;) {
    ConversionResult step = iteratorStep(ctx, it.value);
    if (step.value == nullptr && !step.error.empty()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(step.error);
    }
    if (step.value == nullptr) break;
    ConversionResult v = iteratorValue(ctx, step.value);
    if (!v.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(v.error);
    }
    ConversionResult pv = promiseResolveValue(ctx, v.value);
    if (!pv.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail(pv.error);
    }
    JSValue *rem = stateProp(state->object(), "__remaining__");
    double n = (rem != nullptr && rem->isNumber()) ? rem->toNumber() : 0.0;
    setStateProp(state->object(), "__remaining__", ctx->createValue(n + 1.0));
    JSValue *on_fulfilled = makeElementClosure(ctx, PromiseInternal::AnyElement,
                                               state, index);
    JSValue *on_rejected = makeElementClosure(ctx, PromiseInternal::AnyElement,
                                              state, index | 0x80000000u);
    JSValue *then = pv.value->object()->getProperty("then");
    if (then == nullptr || !isCallable(then)) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return fail("TypeError: element is not a promise");
    }
    JSValue *argv[2] = {on_fulfilled, on_rejected};
    ConversionResult r = call(ctx, then, pv.value, argv, 2);
    if (!r.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return r;
    }
    ++index;
  }

  /* 空 iterable：reject AggregateError([])。 */
  JSValue *rem = stateProp(state->object(), "__remaining__");
  double n = (rem != nullptr && rem->isNumber()) ? rem->toNumber() : 0.0;
  if (n == 0.0) {
    ConversionResult agg = makeAggregateError(ctx, ctx->createArray(0));
    if (!agg.ok()) {
      ctx->heap().unpin(state);
      ctx->heap().unpin(p);
      return agg;
    }
    JSValue *argv[1] = {agg.value};
    ConversionResult r =
        call(ctx, reject, ctx->createValue(Kind::Undefined), argv, 1);
    ctx->heap().unpin(state);
    ctx->heap().unpin(p);
    if (!r.ok()) return r;
    return ok(p);
  }

  ctx->heap().unpin(state);
  ctx->heap().unpin(p);
  return ok(p);
}

/* 内部函数：promiseResolveInternal(promise, resolution)（§27.2.1.4）。
 * Bound 调用时捕获的 promise 作为首参。 */
ConversionResult promiseResolveInternal(JSContext *ctx, JSValue *,
                                        JSValue *const *args, size_t argc) {
  JSValue *promise = argc > 0 ? args[0] : nullptr;
  JSValue *resolution = argc > 1 ? args[1] : ctx->createValue(Kind::Undefined);
  if (promise == nullptr || !promise->isObject() ||
      !promise->object()->isPromise()) {
    return fail("TypeError: invalid promise in resolve");
  }
  resolvePromise(ctx, promise, resolution);
  return ok(ctx->createValue(Kind::Undefined));
}

/* 内部函数：promiseRejectInternal(promise, reason)（§27.2.1.2）。 */
ConversionResult promiseRejectInternal(JSContext *ctx, JSValue *,
                                       JSValue *const *args, size_t argc) {
  JSValue *promise = argc > 0 ? args[0] : nullptr;
  JSValue *reason = argc > 1 ? args[1] : ctx->createValue(Kind::Undefined);
  if (promise == nullptr || !promise->isObject() ||
      !promise->object()->isPromise()) {
    return fail("TypeError: invalid promise in reject");
  }
  rejectPromise(ctx, promise, reason);
  return ok(ctx->createValue(Kind::Undefined));
}

/* 内部函数：promiseReactionJobRunner(handler, value, resolve, reject, is_reject)
 * （§27.2.2.2 PromiseReactionJob）：
 * 1. handler 为 undefined -> 原值传递（fulfill 时 resolve(value) /
 *    reject 时 reject(value)，按 is_reject 决定）；
 * 2. 否则调用 handler(value)，结果 resolve 到派生 promise；
 * 3. handler 抛错 -> reject 到派生 promise。 */
ConversionResult promiseReactionJobRunner(JSContext *ctx, JSValue *,
                                          JSValue *const *args, size_t argc) {
  JSValue *handler = argc > 0 ? args[0] : nullptr;
  JSValue *value = argc > 1 ? args[1] : nullptr;
  JSValue *resolve = argc > 2 ? args[2] : nullptr;
  JSValue *reject = argc > 3 ? args[3] : nullptr;
  bool is_reject = argc > 4 && args[4] != nullptr && args[4]->toBoolean();

  ConversionResult call_result;
  if (handler == nullptr || !isCallable(handler)) {
    /* 恒等：按 reaction 类型转发——fulfill 走 resolve(value)，reject 走
     * reject(value)。 */
    JSValue *fn = is_reject ? reject : resolve;
    JSValue *argv[1] = {value};
    call_result = call(ctx, fn, ctx->createValue(Kind::Undefined), argv, 1);
  } else {
    JSValue *argv[1] = {value};
    call_result = call(ctx, handler, ctx->createValue(Kind::Undefined), argv, 1);
    if (call_result.ok()) {
      JSValue *argv2[1] = {call_result.value};
      call_result =
          call(ctx, resolve, ctx->createValue(Kind::Undefined), argv2, 1);
    }
  }
  if (!call_result.ok()) {
    /* handler 抛错 / resolve 失败：reject(reason)。JS throw 优先用原值。 */
    JSValue *reason = call_result.thrown;
    if (reason == nullptr) {
      JSValue *err = ctx->createValue(Kind::Exception);
      err->setErrorInfo(call_result.error);
      reason = err;
    }
    JSValue *argv[1] = {reason};
    return call(ctx, reject, ctx->createValue(Kind::Undefined), argv, 1);
  }
  return call_result;
}

/* 内部函数：promiseThenableJobRunner(promise, resolution, then)
 * （§27.2.2.3 PromiseResolveThenableJob）：
 * 1. 创建 resolve/reject 闭包；2. then.call(resolution, resolve, reject)；
 * 3. 调用失败 -> reject(reason)。 */
ConversionResult promiseThenableJobRunner(JSContext *ctx, JSValue *,
                                          JSValue *const *args, size_t argc) {
  JSValue *promise = argc > 0 ? args[0] : nullptr;
  JSValue *resolution = argc > 1 ? args[1] : nullptr;
  JSValue *then = argc > 2 ? args[2] : nullptr;
  if (then == nullptr || !isCallable(then)) {
    return fail("TypeError: then is not callable");
  }
  JSValue *resolve = makeResolutionClosure(ctx, PromiseInternal::Resolve, promise);
  JSValue *reject = makeResolutionClosure(ctx, PromiseInternal::Reject, promise);
  JSValue *argv[2] = {resolve, reject};
  ConversionResult r = call(ctx, then, resolution, argv, 2);
  if (!r.ok()) {
    JSValue *reason = r.thrown;
    if (reason == nullptr) {
      JSValue *err = ctx->createValue(Kind::Exception);
      err->setErrorInfo(r.error);
      reason = err;
    }
    JSValue *argv2[1] = {reason};
    return call(ctx, reject, ctx->createValue(Kind::Undefined), argv2, 1);
  }
  return ok(ctx->createValue(Kind::Undefined));
}

/* 内部函数：Identity —— finally 的 then 恒等包装（§27.2.4.5 的
 * thenFinally 简化）：Bound 捕获 on_finally 于 [[BoundArguments]] 首项，
 * 调用时参数为 (on_finally, value)。先执行 onFinally（副作用），再
 * **透传原值** value（finally 不改变传递值）。 */
ConversionResult promiseIdentity(JSContext *ctx, JSValue *,
                                 JSValue *const *args, size_t argc) {
  JSValue *on_finally = argc > 0 ? args[0] : nullptr;
  JSValue *value = argc > 1 ? args[1] : ctx->createValue(Kind::Undefined);
  if (on_finally != nullptr && isCallable(on_finally)) {
    ConversionResult r = call(ctx, on_finally, ctx->createValue(Kind::Undefined),
                              nullptr, 0);
    if (!r.ok()) return r; /* onFinally 抛错 -> 派生 promise reject */
  }
  return ok(value); /* 透传原值（fulfill 路径） */
}

/* 内部函数：Throw —— finally 的 catchFinally 简化（§27.2.4.5）：
 * Bound 捕获 on_finally 于 [[BoundArguments]] 首项，调用时参数为
 * (on_finally, reason)。先执行 onFinally（副作用），再**抛出原 reason**
 * （经 thrown 传回，promiseReactionJobRunner 会 reject 到派生 promise）。 */
ConversionResult promiseThrow(JSContext *ctx, JSValue *,
                              JSValue *const *args, size_t argc) {
  JSValue *on_finally = argc > 0 ? args[0] : nullptr;
  JSValue *reason = argc > 1 ? args[1] : ctx->createValue(Kind::Undefined);
  if (on_finally != nullptr && isCallable(on_finally)) {
    ConversionResult r = call(ctx, on_finally, ctx->createValue(Kind::Undefined),
                              nullptr, 0);
    if (!r.ok()) return r; /* onFinally 抛错 -> 该错误作为派生 promise 的 reason */
  }
  /* 透传原 reason：经 thrown 标记为 JS 层 throw（reaction runner 会 reject）。 */
  ConversionResult r;
  r.thrown = reason;
  return r;
}

/* 内部函数：promiseFinallyThen(promise, onFinally, value)（§27.2.4.5，预留）。 */
ConversionResult promiseFinallyThen(JSContext *ctx, JSValue *,
                                    JSValue *const *args, size_t argc) {
  (void)args;
  (void)argc;
  return ok(ctx->createValue(Kind::Undefined));
}

/* 内部函数：promiseFinallyCatch(promise, onFinally, reason)（§27.2.4.5，预留）。 */
ConversionResult promiseFinallyCatch(JSContext *ctx, JSValue *,
                                     JSValue *const *args, size_t argc) {
  (void)args;
  (void)argc;
  return ok(ctx->createValue(Kind::Undefined));
}

/* -------------------------------------------------------------------------- */
/* M5.3b：组合方法元素回调                                                    */
/* -------------------------------------------------------------------------- */

/* Promise.all 元素回调（§27.2.5.3 的 resolve element 函数）：
 * Bound 捕获 [state, index]，调用时参数为 (value)。
 * 流程：result[index]=value；remaining--；remaining==0 时 resolve(result)。
 * 返回 undefined。 */
ConversionResult promiseAllElement(JSContext *ctx, JSValue *,
                                   JSValue *const *args, size_t argc) {
  JSValue *state = argc > 0 ? args[0] : nullptr;
  JSValue *index_v = argc > 1 ? args[1] : nullptr;
  JSValue *value = argc > 2 ? args[2] : ctx->createValue(Kind::Undefined);
  if (state == nullptr || !state->isObject() || index_v == nullptr ||
      !index_v->isNumber()) {
    return fail("TypeError: invalid Promise.all element state");
  }
  JSObject *s = state->object();
  size_t index = static_cast<size_t>(index_v->toNumber());
  JSValue *result = stateProp(s, "__result__");
  if (result != nullptr && result->isObject() && result->object()->isArray()) {
    result->object()->setElement(index, value); /* 收集到结果数组 */
  }
  JSValue *rem = stateProp(s, "__remaining__");
  double n = (rem != nullptr && rem->isNumber()) ? rem->toNumber() : 0.0;
  double dec = n - 1.0;
  setStateProp(s, "__remaining__", ctx->createValue(dec));
  if (dec == 0.0) {
    /* 全部完成：resolve(result)。 */
    JSValue *resolve = stateProp(s, "__resolve__");
    JSValue *argv[1] = {result};
    ConversionResult r =
        call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
    return r;
  }
  return ok(ctx->createValue(Kind::Undefined));
}

/* Promise.allSettled 元素回调（§27.2.5.2）：
 * Bound 捕获 [state, index]，调用时参数为 (value)（fulfilled）或
 * (reason)（rejected）。**不短路**：reject 也收集。
 * 流程：按当前状态（rejected？）写 result[index] 记录对象，remaining--，
 * remaining==0 时 resolve(result)。 */
ConversionResult promiseAllSettledElement(JSContext *ctx, JSValue *,
                                          JSValue *const *args, size_t argc) {
  JSValue *state = argc > 0 ? args[0] : nullptr;
  JSValue *index_v = argc > 1 ? args[1] : nullptr;
  JSValue *val = argc > 2 ? args[2] : ctx->createValue(Kind::Undefined);
  if (state == nullptr || !state->isObject() || index_v == nullptr ||
      !index_v->isNumber()) {
    return fail("TypeError: invalid Promise.allSettled element state");
  }
  JSObject *s = state->object();
  size_t index = static_cast<size_t>(index_v->toNumber());
  /* 区分 fulfill/reject：Bound 的 index 用 0/1 特判？不——用捕获的
   * [state, index] 不足以区分。规范用两个不同闭包（resolveElement /
   * rejectElement）。这里用 index 高位标记：allSettled 的 fulfill 闭包
   * index = index，reject 闭包 index = index | 0x80000000。 */
  bool is_reject = (index & 0x80000000u) != 0;
  size_t real_index = index & 0x7FFFFFFFu;
  /* 记录对象 {status, value/reason}。 */
  JSValue *rec = ctx->createObject(nullptr);
  ctx->heap().pin(rec); /* 分配保护：属性挂载前防误回收 */
  rec->object()->setProperty(
      "status", ctx->createValue(std::string(is_reject ? "rejected" : "fulfilled")));
  rec->object()->setProperty(is_reject ? "reason" : "value", val);
  ctx->heap().unpin(rec);
  JSValue *result = stateProp(s, "__result__");
  if (result != nullptr && result->isObject() && result->object()->isArray()) {
    result->object()->setElement(real_index, rec);
  }
  JSValue *rem = stateProp(s, "__remaining__");
  double n = (rem != nullptr && rem->isNumber()) ? rem->toNumber() : 0.0;
  double dec = n - 1.0;
  setStateProp(s, "__remaining__", ctx->createValue(dec));
  if (dec == 0.0) {
    JSValue *resolve = stateProp(s, "__resolve__");
    JSValue *argv[1] = {result};
    ConversionResult r =
        call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
    return r;
  }
  return ok(ctx->createValue(Kind::Undefined));
}

/* Promise.any 元素回调（§27.2.5.1）：
 * Bound 捕获 [state, index]，调用时参数为 (value)（fulfilled）或
 * (reason)（rejected）。
 * 流程：fulfilled -> resolve(value)（短路）；rejected -> errors.push(reason)，
 * remaining--，remaining==0 时 reject(AggregateError(errors))。 */
ConversionResult promiseAnyElement(JSContext *ctx, JSValue *,
                                   JSValue *const *args, size_t argc) {
  JSValue *state = argc > 0 ? args[0] : nullptr;
  JSValue *index_v = argc > 1 ? args[1] : nullptr;
  JSValue *val = argc > 2 ? args[2] : ctx->createValue(Kind::Undefined);
  if (state == nullptr || !state->isObject() || index_v == nullptr ||
      !index_v->isNumber()) {
    return fail("TypeError: invalid Promise.any element state");
  }
  JSObject *s = state->object();
  /* 区分 fulfill/reject：同 allSettled，用 index 高位标记（fulfill=0 / reject=1）。 */
  bool is_reject = (static_cast<size_t>(index_v->toNumber()) & 0x80000000u) != 0;
  if (!is_reject) {
    /* fulfilled：resolve(value) 短路。 */
    JSValue *resolve = stateProp(s, "__resolve__");
    JSValue *argv[1] = {val};
    ConversionResult r =
        call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
    return r;
  }
  /* rejected：收集错误。 */
  JSValue *errors = stateProp(s, "__errors__");
  if (errors != nullptr && errors->isObject() && errors->object()->isArray()) {
    errors->object()->setElement(errors->object()->length(), val);
  }
  JSValue *rem = stateProp(s, "__remaining__");
  double n = (rem != nullptr && rem->isNumber()) ? rem->toNumber() : 0.0;
  double dec = n - 1.0;
  setStateProp(s, "__remaining__", ctx->createValue(dec));
  if (dec == 0.0) {
    /* 全部 reject：reject(AggregateError(errors))。 */
    ConversionResult agg = makeAggregateError(ctx, errors);
    if (!agg.ok()) return agg;
    JSValue *reject = stateProp(s, "__reject__");
    JSValue *argv[1] = {agg.value};
    ConversionResult r =
        call(ctx, reject, ctx->createValue(Kind::Undefined), argv, 1);
    return r;
  }
  return ok(ctx->createValue(Kind::Undefined));
}

/* Promise.race 元素回调（§27.2.5.6）：任一 settle 即 settle（先到先得）。
 * Bound 捕获 [state, index]，调用时参数为 (value)（fulfilled）或
 * (reason)（rejected）。settled 标志防重复 settle（race 语义）。 */
ConversionResult promiseRaceElement(JSContext *ctx, JSValue *,
                                    JSValue *const *args, size_t argc) {
  JSValue *state = argc > 0 ? args[0] : nullptr;
  JSValue *index_v = argc > 1 ? args[1] : nullptr;
  JSValue *val = argc > 2 ? args[2] : ctx->createValue(Kind::Undefined);
  if (state == nullptr || !state->isObject() || index_v == nullptr ||
      !index_v->isNumber()) {
    return fail("TypeError: invalid Promise.race element state");
  }
  JSObject *s = state->object();
  JSValue *settled = stateProp(s, "__settled__");
  if (settled != nullptr && settled->toBoolean()) {
    return ok(ctx->createValue(Kind::Undefined)); /* 已 settle，忽略后续 */
  }
  setStateProp(s, "__settled__", ctx->createValue(true));
  bool is_reject = (static_cast<size_t>(index_v->toNumber()) & 0x80000000u) != 0;
  JSValue *fn = is_reject ? stateProp(s, "__reject__") : stateProp(s, "__resolve__");
  JSValue *argv[1] = {val};
  return call(ctx, fn, ctx->createValue(Kind::Undefined), argv, 1);
}

/* 内部函数分发表（下标 = PromiseInternal 枚举值，须严格对应）。 */
JSBuiltinFn kPromiseInternalFns[] = {
    promiseResolveInternal,   /* Resolve */
    promiseRejectInternal,    /* Reject */
    promiseReactionJobRunner, /* ReactionRunner */
    promiseThenableJobRunner, /* ThenableRunner */
    promiseIdentity,          /* Identity */
    promiseThrow,             /* Throw */
    promiseFinallyThen,       /* FinallyThen */
    promiseFinallyCatch,      /* FinallyCatch */
    promiseAllElement,        /* AllElement */
    promiseAllSettledElement, /* AllSettledElement */
    promiseAnyElement,        /* AnyElement */
    promiseRaceElement,       /* RaceElement */
};
static_assert(sizeof(kPromiseInternalFns) / sizeof(kPromiseInternalFns[0]) ==
                  static_cast<size_t>(PromiseInternal::Count),
              "PromiseInternal dispatch table mismatch");

}  // namespace

/* 共享内部函数（惰性创建 + 缓存 + 进根集）：Bound 闭包的 target。
 * 定义在 neojs 命名空间（对应头文件声明），供匿名命名空间的
 * makeBound/enqueueReactionJob 等经头文件声明调用。 */
JSValue *getPromiseInternal(JSContext *ctx, PromiseInternal idx) {
  JSValue *v = ctx->promiseInternal(static_cast<int>(idx));
  if (v != nullptr) return v;
  JSBuiltinFn fn = kPromiseInternalFns[static_cast<int>(idx)];
  v = makeBuiltin(ctx, fn, ThisMode::Strict);
  ctx->heap().pin(v); /* 分配保护：进根集前防误回收 */
  ctx->setPromiseInternal(static_cast<int>(idx), v); /* 进根集（markRoots 遍历） */
  ctx->heap().unpin(v);
  return v;
}

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                    */
/* -------------------------------------------------------------------------- */

/* 创建 Promise 对象（ObjectKind::Promise，原型链 Promise.prototype）。
 * 初始状态 pending，result 未定。reactions 以 Job 闭包形式存微任务队列，
 * 队列由 JSContext::markRoots 标记；promise_result_ 经 gcMarkRefs 标记。 */
JSValue *createPromise(JSContext *ctx) { return newPromiseObject(ctx); }

/* 创建派生 promise + 决议闭包（M5.2c-2 动态 import 复用，§27.2.1.3.2）：
 * 先建 pending promise，再创建 resolve/reject 闭包（Bound 函数捕获
 * promise）。派生 promise 经闭包在 reaction job 中 settle。 */
JSValue *createDerivedPromise(JSContext *ctx, JSValue **out_resolve,
                              JSValue **out_reject) {
  JSValue *p = newPromiseObject(ctx);
  ctx->heap().pin(p); /* 分配保护：创建闭包前防误回收 */
  if (out_resolve != nullptr) {
    *out_resolve = makeResolutionClosure(ctx, PromiseInternal::Resolve, p);
  }
  if (out_reject != nullptr) {
    *out_reject = makeResolutionClosure(ctx, PromiseInternal::Reject, p);
  }
  ctx->heap().unpin(p);
  return p;
}

/* Promise.prototype（M5.3a，§27.2.4）：惰性创建 + 缓存 + 进根集。
 * 原型链 -> Object.prototype。 */
JSValue *promiseProto(JSContext *ctx) {
  JSValue *p = ctx->promiseProtoValue();
  if (p != nullptr) return p;
  p = ctx->createObject(objectProto(ctx)->object());
  ctx->heap().pin(p); /* 分配保护（M3.3）：进根集前防误回收 */
  ctx->setPromiseProtoValue(p); /* 进根集（markRoots 遍历） */
  ctx->heap().unpin(p);
  return p;
}

/* 注册 Promise 构造器 + Promise.prototype 方法 + 静态方法 + 共享内部函数
 * （installBuiltins 调用）。 */
void installPromiseBuiltins(JSContext *ctx) {
  JSValue *global = ctx->globalObject();

  /* Promise 构造器（§27.2.3.1）：new Promise(executor)。 */
  JSValue *promise = makeBuiltin(ctx, promiseConstructor, ThisMode::Strict);
  ctx->heap().pin(promise); /* 分配保护：挂 global 前防误回收 */
  global->object()->setProperty("Promise", promise);
  ctx->heap().unpin(promise);

  JSValue *pp = promiseProto(ctx);
  ctx->heap().pin(pp); /* 分配保护（M3.3）：入根前防误回收 */
  promise->object()->setProperty("prototype", pp);
  defineMethod(pp->object(), "constructor", promise);
  defineMethod(pp->object(), "then",
               makeBuiltin(ctx, promiseThen, ThisMode::Strict));
  defineMethod(pp->object(), "catch",
               makeBuiltin(ctx, promiseCatch, ThisMode::Strict));
  defineMethod(pp->object(), "finally",
               makeBuiltin(ctx, promiseFinally, ThisMode::Strict));
  ctx->heap().unpin(pp); /* 已挂 ctor->prototype 入根（M3.3） */

  /* 静态方法（§27.2.5）：resolve/reject/all/race/allSettled/any。 */
  JSObject *pf = promise->object();
  defineMethod(pf, "resolve",
               makeBuiltin(ctx, promiseResolveStatic, ThisMode::Strict));
  defineMethod(pf, "reject",
               makeBuiltin(ctx, promiseRejectStatic, ThisMode::Strict));
  defineMethod(pf, "all",
               makeBuiltin(ctx, promiseAllStatic, ThisMode::Strict));
  defineMethod(pf, "race",
               makeBuiltin(ctx, promiseRaceStatic, ThisMode::Strict));
  defineMethod(pf, "allSettled",
               makeBuiltin(ctx, promiseAllSettledStatic, ThisMode::Strict));
  defineMethod(pf, "any",
               makeBuiltin(ctx, promiseAnyStatic, ThisMode::Strict));
}

}  // namespace neojs
