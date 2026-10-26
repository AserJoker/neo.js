/*
 * 内部 C++ 实现：Promise（M5.3，§27.2）与微任务队列（Job Queue）。
 *
 * 遵循 docs/architecture.md §8 B2（控制/异步）与 ECMA-262 §27.2：
 *   - Promise 对象模型：ObjectKind::Promise（object.h），内部槽
 *     [[PromiseState]]/[[PromiseResult]] 存 JSObject；reactions 按类型
 *     （fulfill/reject）存 JSObject（PromiseReaction 列表）。
 *   - 决议：promiseResolveInternal / promiseRejectInternal 内部函数
 *     （§27.2.1.4 ResolvePromise / §27.2.1.2 RejectPromise），经
 *     [[Get]] thenable 检查 + 微任务入队。
 *   - 微任务队列：JSContext 持有（jobQueueSize/enqueueJob/dequeueJob），
 *     每个 Job 是一个可调用 JSFunction，宿主经 JS_RunJobs 驱动执行。
 *
 * 设计（复用 Bound 函数承载捕获数据，避免手写字节码闭包）：
 *   - resolve/reject 闭包 = Bound(internal, undefined, [promise])：
 *     调用时把 promise 作为首参转发给内部函数（§27.2.1.3.2 决议函数）。
 *   - reaction job = Bound(ReactionRunner, undefined, [handler, value,
 *     resolve, reject])：PromiseReactionJob（§27.2.2.2）——调用 handler、
 *     再 settle 派生 promise。handler 为 undefined 时恒等（原值传递）。
 *   - thenable job = Bound(ThenableRunner, undefined, [promise, resolution,
 *     then])：PromiseResolveThenableJob（§27.2.2.3）——then.call(resolution,
 *     resolve, reject)，实现 then 链式返回 promise 的接合。
 *   - finally 的 thenFinally/catchFinally（§27.2.4.5）用 Bound + 内部
 *     函数（FinallyThen/FinallyCatch）实现：先跑 onFinally，再
 *     resultPromise.then(() => value / throw reason)。
 *
 * 规范对照（ECMA-262）：
 *   §27.2.1.2   RejectPromise
 *   §27.2.1.4   ResolvePromise
 *   §27.2.2.2   PromiseReactionJob
 *   §27.2.2.3   PromiseResolveThenableJob
 *   §27.2.4.1   Promise.prototype.then
 *   §27.2.4.2   Promise.prototype.catch
 *   §27.2.4.5   Promise.prototype.finally
 *   §27.2.5.4   Promise.reject
 *   §27.2.5.5   Promise.resolve
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_PROMISE_H
#define NEOJS_RUNTIME_PROMISE_H

#include "neojs/runtime/conversion.h"

namespace neojs {

class JSContext;
class JSValue;

/* Promise 状态（[[PromiseState]]，§27.2.2）：
 * 与 object.h 的 promise_state_ 整型存储一致。 */
enum class PromiseState : uint8_t {
  Pending = 0,
  Fulfilled = 1,
  Rejected = 2,
};

/* Promise reaction 类型（[[Type]]，§27.2.2.2 步骤 2）。 */
enum class PromiseReactionType : uint8_t {
  Fulfill = 0,
  Reject = 1,
};

/* 共享内部函数索引（JSContext::promiseInternal 缓存，进根集）。
 * 惰性创建 + 复用：Bound 闭包的 target 引用，保持稳定。 */
enum class PromiseInternal : uint8_t {
  Resolve,        /* promiseResolveInternal(promise, resolution)   §27.2.1.4 */
  Reject,         /* promiseRejectInternal(promise, reason)        §27.2.1.2 */
  ReactionRunner, /* promiseReactionJobRunner(handler, value, resolve, reject) §27.2.2.2 */
  ThenableRunner, /* promiseThenableJobRunner(promise, resolution, then)     §27.2.2.3 */
  Identity,       /* 返回捕获值（finally 的 () => value） */
  Throw,          /* 抛出捕获值（finally 的 () => { throw reason }） */
  FinallyThen,    /* promiseFinallyThen(promise, onFinally, value)  §27.2.4.5 */
  FinallyCatch,   /* promiseFinallyCatch(promise, onFinally, reason) §27.2.4.5 */
  /* M5.3b 组合方法元素回调（§27.2.5.1/.2/.3/.6）：
   * 捕获 state 对象（属性承载可变状态）+ index。 */
  AllElement,     /* Promise.all 元素决议（§27.2.5.3） */
  AllSettledElement, /* Promise.allSettled 元素决议（§27.2.5.2） */
  AnyElement,     /* Promise.any 元素决议（§27.2.5.1） */
  RaceElement,    /* Promise.race 元素决议（§27.2.5.6） */
  Count,
};

/* 创建 Promise 对象（ObjectKind::Promise，原型链 Promise.prototype）。
 * 初始状态 pending，result 未定。GC 引用边由 JSObject 管理。 */
JSValue *createPromise(JSContext *ctx);

/* 创建派生 promise + resolve/reject 决议闭包（M5.2c-2 动态 import 复用，
 * §27.2.1.3.2）：先建 pending promise，再创建决议闭包（Bound 函数捕获
 * promise）。out_resolve/out_reject 可空。 */
JSValue *createDerivedPromise(JSContext *ctx, JSValue **out_resolve,
                              JSValue **out_reject);

/* Promise.prototype（M5.3a，§27.2.4）：惰性创建 + 缓存 + 进根集。 */
JSValue *promiseProto(JSContext *ctx);

/* 注册 Promise 构造器 + Promise.prototype 方法 + 静态方法 + 共享内部
 * 函数（installBuiltins 调用）。 */
void installPromiseBuiltins(JSContext *ctx);

/* 共享内部函数（惰性创建 + 缓存 + 进根集）：Bound 闭包 target。 */
JSValue *getPromiseInternal(JSContext *ctx, PromiseInternal idx);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_PROMISE_H
