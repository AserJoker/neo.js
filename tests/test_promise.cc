/*
 * M5.3 Promise 核心 + 静态/组合方法端到端测试。
 *
 * 覆盖：构造器/executor、then/catch/finally 链、resolve/reject 静态方法、
 * 微任务队列（JS_RunJobs 驱动）、then 链式返回、handler 抛错传播、
 * all/race/allSettled/any 组合方法。
 *
 * 宿主模式：JS_Eval 求值（Promise 回调是 JS 函数），随后 JS_RunJobs
 * 驱动微任务执行，再读取全局变量验证结果。
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "neojs/neojs.h"

namespace {

/* 创建默认分配器 + ctx；调用方负责 JS_FreeContext 后 JS_FreeAllocator。 */
JSContext *makeCtx(JSAllocator **out_alloc) {
  JSAllocator *a = JS_NewAllocator(nullptr, nullptr, nullptr);
  EXPECT_NE(a, nullptr);
  JSContext *ctx = JS_NewContext(a);
  EXPECT_NE(ctx, nullptr);
  if (out_alloc != nullptr) *out_alloc = a;
  return ctx;
}

/* 从全局对象读属性值（返回句柄，调用方 FreeValue；属性不存在返回 NULL）。 */
JSValue *globalProp(JSContext *ctx, const char *name) {
  JSValue *g = JS_GetGlobalObject(ctx);
  JSValue *v = JS_GetProperty(ctx, g, name);
  JS_FreeValue(ctx, g);
  return v;
}

/* 断言全局对象属性为数值。 */
void expectGlobalNumber(JSContext *ctx, const char *name, double expected) {
  JSValue *v = globalProp(ctx, name);
  ASSERT_NE(v, nullptr) << "global property '" << name << "' missing";
  EXPECT_EQ(JS_GetKind(ctx, v), JS_NUMBER)
      << "global '" << name << "' kind, got: " << JS_ToString(ctx, v);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, v), expected) << "global '" << name << "'";
  JS_FreeValue(ctx, v);
}

/* 断言全局对象属性为字符串。 */
void expectGlobalString(JSContext *ctx, const char *name,
                        const std::string &expected) {
  JSValue *v = globalProp(ctx, name);
  ASSERT_NE(v, nullptr) << "global property '" << name << "' missing";
  EXPECT_EQ(JS_GetKind(ctx, v), JS_STRING)
      << "global '" << name << "' kind, got: " << JS_ToString(ctx, v);
  if (JS_GetKind(ctx, v) == JS_STRING) {
    EXPECT_EQ(std::string(JS_ToString(ctx, v)), expected)
        << "global '" << name << "'";
  }
  JS_FreeValue(ctx, v);
}

/* 断言全局对象属性为布尔值。 */
void expectGlobalBoolean(JSContext *ctx, const char *name, bool expected) {
  JSValue *v = globalProp(ctx, name);
  ASSERT_NE(v, nullptr) << "global property '" << name << "' missing";
  EXPECT_EQ(JS_GetKind(ctx, v), JS_BOOLEAN)
      << "global '" << name << "' kind, got: " << JS_ToString(ctx, v);
  EXPECT_EQ(JS_ToBoolean(ctx, v) != 0, expected) << "global '" << name << "'";
  JS_FreeValue(ctx, v);
}

/* 求值并驱动微任务队列，断言无异常。 */
void evalAndRunJobs(JSContext *ctx, const char *src) {
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr) << "JS_Eval returned null for: " << src;
  EXPECT_NE(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "eval failed: " << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);
  JSValue *jr = JS_RunJobs(ctx);
  EXPECT_EQ(jr, nullptr) << "JS_RunJobs returned error: "
                         << (jr ? JS_ToString(ctx, jr) : "");
  if (jr != nullptr) JS_FreeValue(ctx, jr);
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* M5.3a：构造器 / executor                                                   */
/* -------------------------------------------------------------------------- */

/* 构造器 + executor 立即 resolve：then 回调经微任务异步执行。 */
TEST(Promise, ConstructorResolve) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = new Promise((resolve, reject) => { resolve(42); });"
      "p.then(v => { globalThis.result = v; });");

  expectGlobalNumber(ctx, "result", 42.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* executor 抛错 -> promise reject，catch 捕获。 */
TEST(Promise, ExecutorThrows) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = new Promise((resolve, reject) => { throw 'boom'; });"
      "p.catch(e => { globalThis.err = e; });");

  expectGlobalString(ctx, "err", "boom");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 构造器缺 executor -> TypeError。 */
TEST(Promise, ConstructorRequiresExecutor) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "new Promise()", std::strlen("new Promise()"));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.3a：then / 链式返回                                                  */
/* -------------------------------------------------------------------------- */

/* then 链式返回：handler 返回值 resolve 到派生 promise，继续链。 */
TEST(Promise, ThenChain) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = new Promise(res => res(1));"
      "p.then(v => v + 1).then(v => v + 1).then(v => { globalThis.result = v; });");

  expectGlobalNumber(ctx, "result", 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 多 then 顺序执行（reactions 按挂载顺序）。 */
TEST(Promise, MultipleThenOrder) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = new Promise(res => res(10));"
      "var order = '';"
      "p.then(() => { order += 'a'; });"
      "p.then(() => { order += 'b'; });"
      "p.then(() => { globalThis.order = order; });");

  expectGlobalString(ctx, "order", "ab");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* pending 期间挂 then，settle 后统一派发。 */
TEST(Promise, ThenAfterPendingThenResolve) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var resolveFn;"
      "var p = new Promise(res => { resolveFn = res; });"
      "p.then(v => { globalThis.result = v; });"
      "resolveFn(99);");

  expectGlobalNumber(ctx, "result", 99.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.3a：reject / catch                                                    */
/* -------------------------------------------------------------------------- */

/* reject + catch 捕获。 */
TEST(Promise, RejectCatch) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = new Promise((res, rej) => rej('nope'));"
      "p.catch(e => { globalThis.err = e; });");

  expectGlobalString(ctx, "err", "nope");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* catch 后继续链（catch 的返回值 resolve 到派生 promise）。 */
TEST(Promise, CatchRecover) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = new Promise((res, rej) => rej('x'));"
      "p.catch(e => 'recovered').then(v => { globalThis.result = v; });");

  expectGlobalString(ctx, "result", "recovered");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* handler 抛错 -> 派生 promise reject，后续 catch 捕获。 */
TEST(Promise, HandlerThrowsPropagates) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = new Promise(res => res(1));"
      "p.then(() => { throw 'handler-boom'; }).catch(e => { globalThis.err = e; });");

  expectGlobalString(ctx, "err", "handler-boom");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 无 catch 的 reject：引擎无 unhandled rejection 事件机制（M5.3a），
 * reject 只置状态，不入队 job，JS_RunJobs 返回 NULL（队列空）。 */
TEST(Promise, UnhandledRejection) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx,
                       "var p = new Promise((res, rej) => rej('unhandled'));",
                       std::strlen("var p = new Promise((res, rej) => rej('unhandled'));"));
  ASSERT_NE(r, nullptr);
  JS_FreeValue(ctx, r);

  JSValue *jr = JS_RunJobs(ctx);
  EXPECT_EQ(jr, nullptr) << "no jobs queued for a bare rejected promise";

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.3a：resolve / reject 静态方法                                            */
/* -------------------------------------------------------------------------- */

/* Promise.resolve(value)：立即 resolve 的 promise，then 微任务执行。 */
TEST(Promise, StaticResolve) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(ctx,
                 "Promise.resolve(7).then(v => { globalThis.result = v; });");

  expectGlobalNumber(ctx, "result", 7.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Promise.reject(reason)：立即 reject 的 promise。 */
TEST(Promise, StaticReject) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "Promise.reject('err').catch(e => { globalThis.err = e; });");

  expectGlobalString(ctx, "err", "err");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Promise.resolve(promise) 原样返回同一 promise（不包装）。 */
TEST(Promise, StaticResolveIdentity) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = Promise.resolve(1);"
      "globalThis.same = Promise.resolve(p) === p;");

  expectGlobalBoolean(ctx, "same", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.3a：thenable 接合                                                     */
/* -------------------------------------------------------------------------- */

/* resolve(thenable)：thenable.then(resolve, reject) 接合到 promise。 */
TEST(Promise, ThenableAssimilation) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var thenable = { then: function(res) { res('via-thenable'); } };"
      "Promise.resolve(thenable).then(v => { globalThis.result = v; });");

  expectGlobalString(ctx, "result", "via-thenable");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* promise 链式返回：handler 返回另一个 promise，接合到派生 promise。 */
TEST(Promise, ChainReturnPromise) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = new Promise(res => res(1));"
      "p.then(v => new Promise(r => r(v * 10)))"
      " .then(v => { globalThis.result = v; });");

  expectGlobalNumber(ctx, "result", 10.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.3a：finally                                                             */
/* -------------------------------------------------------------------------- */

/* finally 回调执行，且不改变传递值（简化语义）。 */
TEST(Promise, FinallyRuns) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = Promise.resolve(5);"
      "p.finally(() => { globalThis.fin = true; })"
      " .then(v => { globalThis.result = v; });");

  expectGlobalBoolean(ctx, "fin", true);
  expectGlobalNumber(ctx, "result", 5.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.3a：微任务顺序                                                        */
/* -------------------------------------------------------------------------- */

/* 同步代码在微任务之前执行（then 回调不阻塞当前栈）。 */
TEST(Promise, MicrotaskAfterSync) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var order = '';"
      "Promise.resolve().then(() => { order += 'micro'; });"
      "order += 'sync';"
      "Promise.resolve().then(() => { globalThis.order = order; });");

  expectGlobalString(ctx, "order", "syncmicro");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.3b：all                                                                */
/* -------------------------------------------------------------------------- */

/* Promise.all：全部元素 resolve 后 resolve 结果数组。 */
TEST(Promise, AllResolve) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "Promise.all([Promise.resolve(1), Promise.resolve(2), 3])"
      "  .then(v => { globalThis.result = v.join(','); });");

  expectGlobalString(ctx, "result", "1,2,3");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Promise.all 空数组：立即 resolve 空数组。 */
TEST(Promise, AllEmpty) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "Promise.all([]).then(v => { globalThis.result = v.length; });");

  expectGlobalNumber(ctx, "result", 0.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Promise.all 任一 reject：reject 整个结果 promise（短路）。 */
TEST(Promise, AllRejectsShortCircuit) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "Promise.all([Promise.resolve(1), Promise.reject('bad'), 3])"
      "  .then(() => {}, e => { globalThis.err = e; });");

  expectGlobalString(ctx, "err", "bad");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.3b：race                                                               */
/* -------------------------------------------------------------------------- */

/* Promise.race：先 settle 的元素决定结果。 */
TEST(Promise, RaceFirstResolve) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var resolveA, resolveB;"
      "var pa = new Promise(r => { resolveA = r; });"
      "var pb = new Promise(r => { resolveB = r; });"
      "Promise.race([pa, pb]).then(v => { globalThis.result = v; });"
      "resolveA('first');"
      "resolveB('second');");

  expectGlobalString(ctx, "result", "first");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Promise.race 首个 reject：reject 结果 promise。 */
TEST(Promise, RaceFirstReject) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var rejectA, resolveB;"
      "var pa = new Promise((r, j) => { rejectA = j; });"
      "var pb = new Promise(r => { resolveB = r; });"
      "Promise.race([pa, pb]).then(() => {}, e => { globalThis.err = e; });"
      "rejectA('rej');"
      "resolveB('res');");

  expectGlobalString(ctx, "err", "rej");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.3b：allSettled                                                         */
/* -------------------------------------------------------------------------- */

/* Promise.allSettled：全部 settle 后 resolve 记录数组（含 rejected）。 */
TEST(Promise, AllSettledMixed) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "Promise.allSettled([Promise.resolve(1), Promise.reject('x')])"
      "  .then(v => {"
      "    globalThis.s0 = v[0].status + ':' + v[0].value;"
      "    globalThis.s1 = v[1].status + ':' + v[1].reason;"
      "  });");

  expectGlobalString(ctx, "s0", "fulfilled:1");
  expectGlobalString(ctx, "s1", "rejected:x");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.3b：any                                                                */
/* -------------------------------------------------------------------------- */

/* Promise.any：任一 fulfill 即 resolve。 */
TEST(Promise, AnyFirstFulfill) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var rejA, resolveB;"
      "var pa = new Promise((r, j) => { rejA = j; });"
      "var pb = new Promise(r => { resolveB = r; });"
      "Promise.any([pa, pb]).then(v => { globalThis.result = v; });"
      "rejA('no');"
      "resolveB('yes');");

  expectGlobalString(ctx, "result", "yes");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Promise.any 全 reject：reject AggregateError。 */
TEST(Promise, AnyAllReject) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "Promise.any([Promise.reject('a'), Promise.reject('b')])"
      "  .then(() => {}, e => {"
      "    globalThis.name = e.name;"
      "    globalThis.n = e.errors.length;"
      "  });");

  expectGlobalString(ctx, "name", "AggregateError");
  expectGlobalNumber(ctx, "n", 2.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.3c：总验收                                                              */
/* -------------------------------------------------------------------------- */

/* all：元素延迟 settle 时仍按原顺序收集（index 保序），全部完成后 resolve。 */
TEST(Promise, AllPendingElementsOrder) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var res1, res2, res3;"
      "var p1 = new Promise(r => { res1 = r; });"
      "var p2 = new Promise(r => { res2 = r; });"
      "var p3 = new Promise(r => { res3 = r; });"
      "Promise.all([p1, p2, p3]).then(v => { globalThis.result = v.join(','); });"
      "res2('b');"
      "res3('c');"
      "res1('a');");

  expectGlobalString(ctx, "result", "a,b,c");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* finally 对 reject 透传：reject 时 finally 回调执行，原 reason 保留到链尾。 */
TEST(Promise, FinallyRejectPropagates) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = Promise.reject('boom');"
      "p.finally(() => { globalThis.fin = true; })"
      " .catch(e => { globalThis.err = e; });");

  expectGlobalBoolean(ctx, "fin", true);
  expectGlobalString(ctx, "err", "boom");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* then 的 handler 返回 rejected promise：接合到派生 promise（reject 传播）。 */
TEST(Promise, ChainReturnRejectedPromise) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = Promise.resolve(1);"
      "p.then(() => Promise.reject('inner'))"
      " .catch(e => { globalThis.err = e; });");

  expectGlobalString(ctx, "err", "inner");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* then 非函数参数按恒等处理（value 透传）。 */
TEST(Promise, ThenNonFunctionIdentity) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "Promise.resolve('x').then(42).then(v => { globalThis.result = v; });");

  expectGlobalString(ctx, "result", "x");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* allSettled：元素延迟 settle，记录按顺序且含 fulfilled/rejected 混合。 */
TEST(Promise, AllSettledPendingOrder) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var res, rej;"
      "var p1 = new Promise(r => { res = r; });"
      "var p2 = new Promise((r, j) => { rej = j; });"
      "Promise.allSettled([p1, p2]).then(v => {"
      "  globalThis.s0 = v[0].status + ':' + v[0].value;"
      "  globalThis.s1 = v[1].status + ':' + v[1].reason;"
      "});"
      "rej('nope');"
      "res('ok');");

  expectGlobalString(ctx, "s0", "fulfilled:ok");
  expectGlobalString(ctx, "s1", "rejected:nope");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 嵌套组合：all 内嵌 race/any，结果正确组合。 */
TEST(Promise, NestedCombinators) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var resolveSlow;"
      "var slow = new Promise(r => { resolveSlow = r; });"
      "var fast = Promise.resolve('f');"
      "Promise.all([Promise.race([slow, fast]), Promise.any([Promise.reject('x'), 'y'])])"
      "  .then(v => { globalThis.result = v.join(','); });"
      "resolveSlow('s');");

  expectGlobalString(ctx, "result", "f,y");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* thenable 的 then 异步调用 resolve：经微任务接合到 promise。 */
TEST(Promise, ThenableAsyncResolve) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var cb;"
      "var thenable = { then: function(res) { cb = res; } };"
      "Promise.resolve(thenable).then(v => { globalThis.result = v; });"
      "Promise.resolve().then(() => { cb('async-then'); });");

  expectGlobalString(ctx, "result", "async-then");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
