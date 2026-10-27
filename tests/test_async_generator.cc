/*
 * M5.5c-1 async generator 运行时端到端测试（§27.6）。
 *
 * 覆盖：
 *   - async function* 调用返回 AsyncGenerator 对象（不立即执行），
 *     next() 返回 promise（异步迭代器协议）
 *   - 单个 yield：await next() 返回 {value, done:false}，再 next 返回
 *     {value:undefined, done:true}
 *   - 多个 yield 串行 + yield 表达式接收 next(value) 传入值
 *   - AsyncGeneratorYield：yield 值先 await（yield Promise 值自动 resolve）
 *   - await 与 yield 混合串行
 *   - throw(exception) 注入异常 + try/catch 捕获
 *   - return(value) 强制完成
 *   - 执行中抛错：reject 请求 promise（await 捕获），不冒泡到 JS_RunJobs；
 *     之后 next 返回 {value:undefined, done:true}
 *   - AsyncGenerator 构造器 + Symbol.toStringTag + 不可 new
 *
 * 宿主模式：JS_Eval 求值（async 回调是 JS 函数），JS_RunJobs 驱动微任务，
 * 结果写入 globalThis.* 后断言。
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

/* 断言全局对象属性为 undefined。 */
void expectGlobalUndefined(JSContext *ctx, const char *name) {
  JSValue *v = globalProp(ctx, name);
  ASSERT_NE(v, nullptr) << "global property '" << name << "' missing";
  EXPECT_EQ(JS_GetKind(ctx, v), JS_UNDEFINED)
      << "global '" << name << "' kind, got: " << JS_ToString(ctx, v);
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
/* M5.5c-1：async generator 基础 next/yield/完成                               */
/* -------------------------------------------------------------------------- */

/* async function* 调用返回 AsyncGenerator 对象（非 promise），不立即执行。
 * 首次 await next() 执行到首个 yield 返回 {value:1, done:false}；
 * 再次 next() 越过 yield，函数结束返回 {value:undefined, done:true}。 */
TEST(AsyncGenerator, BasicNextYieldDone) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() { yield 1; }"
      "  var gen = g();"
      "  globalThis.hasNext = (typeof gen.next === 'function');"
      "  globalThis.isThenable = (typeof gen.then === 'function');"
      "  var r1 = await gen.next();"
      "  globalThis.v1 = r1.value;"
      "  globalThis.d1 = r1.done;"
      "  var r2 = await gen.next();"
      "  globalThis.v2 = r2.value;"
      "  globalThis.d2 = r2.done;"
      "})();");

  expectGlobalBoolean(ctx, "hasNext", true);
  /* AsyncGenerator 是异步迭代器，自身不是 thenable。 */
  expectGlobalBoolean(ctx, "isThenable", false);
  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalBoolean(ctx, "d1", false);
  expectGlobalUndefined(ctx, "v2");
  expectGlobalBoolean(ctx, "d2", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 多个 yield 串行 + next(value) 传入值作为 yield 表达式结果。 */
TEST(AsyncGenerator, MultipleYieldsValuePassing) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() {"
      "    var x = yield 1;"
      "    globalThis.gotX = x;"
      "    var y = yield 2;"
      "    globalThis.gotY = y;"
      "    return 3;"
      "  }"
      "  var gen = g();"
      "  var r1 = await gen.next();"
      "  globalThis.v1 = r1.value;"
      "  var r2 = await gen.next('a');"
      "  globalThis.v2 = r2.value;"
      "  var r3 = await gen.next('b');"
      "  globalThis.v3 = r3.value;"
      "  globalThis.d3 = r3.done;"
      "})();");

  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalString(ctx, "gotX", "a");
  expectGlobalNumber(ctx, "v2", 2.0);
  expectGlobalString(ctx, "gotY", "b");
  expectGlobalNumber(ctx, "v3", 3.0);
  expectGlobalBoolean(ctx, "d3", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* AsyncGeneratorYield（§27.6.3.4）：yield 值先 await 再挂起返回。
 * yield Promise 值自动 resolve 后作为 {value,done:false} 的 value。 */
TEST(AsyncGenerator, YieldAwaitValue) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() {"
      "    var v = yield Promise.resolve(42);"
      "    globalThis.gotV = v;"
      "  }"
      "  var gen = g();"
      "  var r1 = await gen.next();"
      "  globalThis.yv = r1.value;"
      "  var r2 = await gen.next('x');"
      "  globalThis.d2 = r2.done;"
      "})();");

  /* yield 值（promise）被 await，r1.value 是 resolve 后的 42。 */
  expectGlobalNumber(ctx, "yv", 42.0);
  /* next('x') 传入值作为 yield 表达式结果。 */
  expectGlobalString(ctx, "gotV", "x");
  expectGlobalBoolean(ctx, "d2", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* await 与 yield 混合：await 挂起经请求队列驱动，随后 yield 挂起。 */
TEST(AsyncGenerator, AwaitAndYieldMix) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() {"
      "    var a = await Promise.resolve(10);"
      "    globalThis.awaited = a;"
      "    var b = yield 20;"
      "    globalThis.yielded = b;"
      "    var c = await Promise.resolve(30);"
      "    globalThis.awaited2 = c;"
      "    return 40;"
      "  }"
      "  var gen = g();"
      "  var r1 = await gen.next();"
      "  globalThis.v1 = r1.value;"
      "  var r2 = await gen.next('mid');"
      "  globalThis.v2 = r2.value;"
      "  globalThis.d2 = r2.done;"
      "})();");

  expectGlobalNumber(ctx, "awaited", 10.0);
  expectGlobalNumber(ctx, "v1", 20.0);
  expectGlobalString(ctx, "yielded", "mid");
  expectGlobalNumber(ctx, "awaited2", 30.0);
  expectGlobalNumber(ctx, "v2", 40.0);
  expectGlobalBoolean(ctx, "d2", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.5c-1：throw / return / 异常路径                                           */
/* -------------------------------------------------------------------------- */

/* throw(exception) 注入异常到挂起点，try/catch 捕获后继续执行。 */
TEST(AsyncGenerator, ThrowInjectedCaught) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() {"
      "    try { yield 1; } catch (e) { globalThis.caught = e; }"
      "    yield 2;"
      "  }"
      "  var gen = g();"
      "  var r1 = await gen.next();"
      "  globalThis.v1 = r1.value;"
      "  var r2 = await gen.throw('boom');"
      "  globalThis.v2 = r2.value;"
      "  globalThis.d2 = r2.done;"
      "  var r3 = await gen.next();"
      "  globalThis.d3 = r3.done;"
      "})();");

  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalString(ctx, "caught", "boom");
  expectGlobalNumber(ctx, "v2", 2.0);
  expectGlobalBoolean(ctx, "d2", false);
  expectGlobalBoolean(ctx, "d3", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* return(value) 强制完成：resolve {value, done:true}，之后 next 不再执行。 */
TEST(AsyncGenerator, ReturnForcesCompletion) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() { yield 1; globalThis.afterYield = true; yield 2; }"
      "  var gen = g();"
      "  var r1 = await gen.next();"
      "  globalThis.v1 = r1.value;"
      "  var r2 = await gen.return(99);"
      "  globalThis.v2 = r2.value;"
      "  globalThis.d2 = r2.done;"
      "  var r3 = await gen.next();"
      "  globalThis.v3 = r3.value;"
      "  globalThis.d3 = r3.done;"
      "})();");

  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalNumber(ctx, "v2", 99.0);
  expectGlobalBoolean(ctx, "d2", true);
  /* return 后不再执行剩余体。 */
  expectGlobalUndefined(ctx, "afterYield");
  expectGlobalUndefined(ctx, "v3");
  expectGlobalBoolean(ctx, "d3", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 执行中抛错：reject 请求 promise（await 捕获），错误不冒泡到 JS_RunJobs；
 * 之后 next 返回 {value:undefined, done:true}。 */
TEST(AsyncGenerator, BodyThrowRejectsRequest) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() { throw 'boom'; yield 1; }"
      "  var gen = g();"
      "  var err = null;"
      "  try { await gen.next(); } catch (e) { err = e; }"
      "  globalThis.err = err;"
      "  var r = await gen.next();"
      "  globalThis.doneAfter = r.done;"
      "  globalThis.valAfter = r.value;"
      "})();");

  expectGlobalString(ctx, "err", "boom");
  expectGlobalBoolean(ctx, "doneAfter", true);
  expectGlobalUndefined(ctx, "valAfter");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* return 穿透 try-finally（与同步 generator 一致，§27.6.3.3 step 9-10）。 */
TEST(AsyncGenerator, ReturnRunsFinally) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  globalThis.seq = '';"
      "  async function* g() {"
      "    try { yield 1; } finally { globalThis.seq += 'F'; }"
      "    yield 2;"
      "  }"
      "  var gen = g();"
      "  await gen.next();"
      "  var r = await gen.return('end');"
      "  globalThis.rv = r.value;"
      "  globalThis.rd = r.done;"
      "})();");

  expectGlobalString(ctx, "seq", "F");
  expectGlobalString(ctx, "rv", "end");
  expectGlobalBoolean(ctx, "rd", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 连续 next 不 await（请求排队）：每个 next 返回的 promise 按序 resolve。
 * 请求队列逐个驱动（§27.6.3.3 AsyncGeneratorResume）。 */
TEST(AsyncGenerator, QueuedRequests) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() {"
      "    yield 1;"
      "    yield 2;"
      "    yield 3;"
      "  }"
      "  var gen = g();"
      "  var p1 = gen.next();"
      "  var p2 = gen.next();"
      "  var p3 = gen.next();"
      "  var p4 = gen.next();"
      "  globalThis.v1 = (await p1).value;"
      "  globalThis.v2 = (await p2).value;"
      "  globalThis.v3 = (await p3).value;"
      "  globalThis.d4 = (await p4).done;"
      "})();");

  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalNumber(ctx, "v2", 2.0);
  expectGlobalNumber(ctx, "v3", 3.0);
  expectGlobalBoolean(ctx, "d4", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 驱动是同步的：return() 时队列已被前序 next 消耗，p1/p2 各自 resolve 对应
 * yield 结果（§27.6.3.3）。return 使当前请求 resolve {value, done:true}。 */
TEST(AsyncGenerator, ReturnAfterYields) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() { yield 1; yield 2; }"
      "  var gen = g();"
      "  var p1 = gen.next();"
      "  var p2 = gen.next();"
      "  var pr = gen.return(99);"
      "  globalThis.rv = (await pr).value;"
      "  globalThis.rd = (await pr).done;"
      "  var r2 = await p2;"
      "  globalThis.v2 = r2.value;"
      "  globalThis.r2d = r2.done;"
      "  var r1 = await p1;"
      "  globalThis.v1 = r1.value;"
      "})();");

  expectGlobalNumber(ctx, "rv", 99.0);
  expectGlobalBoolean(ctx, "rd", true);
  /* p2 在 return 前已正常 resolve yield 2 的结果（done:false）。 */
  expectGlobalNumber(ctx, "v2", 2.0);
  expectGlobalBoolean(ctx, "r2d", false);
  expectGlobalNumber(ctx, "v1", 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* async generator 体抛错：首个请求 reject reason；后续 next 走 completed
 * 分支 resolve {value:undefined, done:true}（§27.6.3.5 step 5-6）。 */
TEST(AsyncGenerator, ThrowThenNextCompleted) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() { throw 'boom'; }"
      "  var gen = g();"
      "  var e1 = null;"
      "  try { await gen.next(); } catch (e) { e1 = e; }"
      "  globalThis.e1 = e1;"
      "  var r = await gen.next();"
      "  globalThis.done = r.done;"
      "  globalThis.val = r.value;"
      "})();");

  expectGlobalString(ctx, "e1", "boom");
  expectGlobalBoolean(ctx, "done", true);
  expectGlobalUndefined(ctx, "val");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* completed 后 throw：reject（§27.6.3.2 step 8），next 仍 resolve done:true。 */
TEST(AsyncGenerator, ThrowAfterCompletedRejects) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() { yield 1; }"
      "  var gen = g();"
      "  await gen.next();"
      "  await gen.next();"
      "  var e = null;"
      "  try { await gen.throw('late'); } catch (x) { e = x; }"
      "  globalThis.e = e;"
      "  var r = await gen.next();"
      "  globalThis.done = r.done;"
      "})();");

  expectGlobalString(ctx, "e", "late");
  expectGlobalBoolean(ctx, "done", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* return 在 finally 中再 yield：return completion 穿透 finally 后以
 * 最后 yield 挂起（§27.6.3.3 与 §27.5.3.3 同构）。 */
TEST(AsyncGenerator, ReturnPausedByFinallyYield) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() {"
      "    try { yield 1; }"
      "    finally { yield 2; }"
      "    yield 3;"
      "  }"
      "  var gen = g();"
      "  await gen.next();"
      "  var r = await gen.return('end');"
      "  globalThis.v = r.value;"
      "  globalThis.d = r.done;"
      "  var r2 = await gen.next();"
      "  globalThis.v2 = r2.value;"
      "  globalThis.d2 = r2.done;"
      "})();");

  /* return 触发 finally 里的 yield 2 挂起（completion 暂存），再 next 越过
   * finally 后以 'end' 完成。 */
  expectGlobalNumber(ctx, "v", 2.0);
  expectGlobalBoolean(ctx, "d", false);
  expectGlobalNumber(ctx, "v2", 3.0);
  expectGlobalBoolean(ctx, "d2", false);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.5c-1：AsyncGenerator 构造器 + Symbol.toStringTag                          */
/* -------------------------------------------------------------------------- */

/* 全局 AsyncGenerator 存在，prototype.constructor 指回 AsyncGenerator，
 * Object.prototype.toString 返回 "[object AsyncGenerator]"。 */
TEST(AsyncGenerator, ConstructorAndToStringTag) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "globalThis.hasAG = typeof AsyncGenerator;"
      "globalThis.protoCtor = AsyncGenerator.prototype.constructor === AsyncGenerator;"
      "var ag = (async function* () { yield 1; })();"
      "globalThis.tag = Object.prototype.toString.call(ag);"
      "globalThis.asyncIter = (typeof ag[Symbol.asyncIterator] === 'function');");

  expectGlobalString(ctx, "hasAG", "function");
  expectGlobalBoolean(ctx, "protoCtor", true);
  expectGlobalString(ctx, "tag", "[object AsyncGenerator]");
  expectGlobalBoolean(ctx, "asyncIter", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* new AsyncGenerator() 抛 TypeError（§27.6.2.2：不可作为构造器）。 */
TEST(AsyncGenerator, ConstructorNotNewable) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r =
      JS_Eval(ctx, "new AsyncGenerator()", std::strlen("new AsyncGenerator()"));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "new AsyncGenerator() should throw TypeError, got: "
      << (r ? JS_ToString(ctx, r) : "");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.5c-2：for await...of（§14.7.5.4）                                        */
/* -------------------------------------------------------------------------- */

/* for await...of 迭代 async generator：逐个 await next()，累加 value。 */
TEST(AsyncGenerator, ForAwaitOfAsyncGenerator) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() {"
      "    yield 1; yield 2; yield 3;"
      "  }"
      "  var sum = 0;"
      "  for await (const v of g()) { sum += v; }"
      "  globalThis.sum = sum;"
      "})();");

  expectGlobalNumber(ctx, "sum", 6.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* for await...of 迭代自定义异步迭代器对象（Symbol.asyncIterator 返回
 * { next() -> promise {value,done} }）。 */
TEST(AsyncGenerator, ForAwaitOfCustomAsyncIterator) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  var ai = {"
      "    [Symbol.asyncIterator]() {"
      "      var i = 0;"
      "      return {"
      "        next() { return Promise.resolve({ value: ++i, done: i > 3 }); }"
      "      };"
      "    }"
      "  };"
      "  var arr = [];"
      "  for await (const v of ai) { arr.push(v); }"
      "  globalThis.len = arr.length;"
      "  globalThis.s0 = arr[0]; globalThis.s1 = arr[1]; globalThis.s2 = arr[2];"
      "})();");

  expectGlobalNumber(ctx, "len", 3.0);
  expectGlobalNumber(ctx, "s0", 1.0);
  expectGlobalNumber(ctx, "s1", 2.0);
  expectGlobalNumber(ctx, "s2", 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* for await...of 回退同步可迭代对象：无 Symbol.asyncIterator 时经
 * AsyncFromSyncIterator（§27.1.4）包装同步迭代器。 */
TEST(AsyncGenerator, ForAwaitOfSyncIterableFallback) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  var sum = 0;"
      "  for await (const v of [1, 2, 3, 4]) { sum += v; }"
      "  globalThis.sum = sum;"
      "})();");

  expectGlobalNumber(ctx, "sum", 10.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* for await...of break 提前退出：调 async iterator.return()（AsyncIteratorClose
 * §14.7.5.4）。验证 return 被调用 + break 后循环终止。 */
TEST(AsyncGenerator, ForAwaitOfBreakCallsReturn) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  var closed = 0;"
      "  var ai = {"
      "    [Symbol.asyncIterator]() {"
      "      var i = 0;"
      "      return {"
      "        next() { return Promise.resolve({ value: ++i, done: false }); },"
      "        return() { closed++; return Promise.resolve({ value: undefined, done: true }); }"
      "      };"
      "    }"
      "  };"
      "  var count = 0;"
      "  for await (const v of ai) {"
      "    count++;"
      "    if (count >= 2) break;"
      "  }"
      "  globalThis.count = count;"
      "  globalThis.closed = closed;"
      "})();");

  expectGlobalNumber(ctx, "count", 2.0);
  expectGlobalNumber(ctx, "closed", 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* for await...of 循环体内 throw：异常经 finally -> AsyncIteratorClose 调
 * return()，随后异常重抛（外层 try/catch 捕获）。 */
TEST(AsyncGenerator, ForAwaitOfThrowCallsReturnAndRethrows) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  var closed = 0;"
      "  var ai = {"
      "    [Symbol.asyncIterator]() {"
      "      var i = 0;"
      "      return {"
      "        next() { return Promise.resolve({ value: ++i, done: false }); },"
      "        return() { closed++; return Promise.resolve({ value: undefined, done: true }); }"
      "      };"
      "    }"
      "  };"
      "  var caught = null;"
      "  try {"
      "    for await (const v of ai) { if (v >= 2) throw 'boom'; }"
      "  } catch (e) { caught = e; }"
      "  globalThis.caught = caught;"
      "  globalThis.closed = closed;"
      "})();");

  expectGlobalString(ctx, "caught", "boom");
  expectGlobalNumber(ctx, "closed", 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* for await...of 迭代 async generator 时 yield 的 Promise 值自动 resolve
 * （AsyncGeneratorYield 语义 + for await 的 await next()）。 */
TEST(AsyncGenerator, ForAwaitOfAwaitingPromiseValues) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* g() {"
      "    yield Promise.resolve(10);"
      "    yield Promise.resolve(20);"
      "  }"
      "  var sum = 0;"
      "  for await (const v of g()) { sum += v; }"
      "  globalThis.sum = sum;"
      "})();");

  expectGlobalNumber(ctx, "sum", 30.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
