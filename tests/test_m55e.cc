/*
 * M5.5e async generator yield* 委托 async 迭代器端到端测试（§14.4.14 +
 * §27.6.3.6）。
 *
 * 背景（缺口 B，M5.5e）：async generator 内 yield* <expr> 此前编译发同步
 * YieldDelegate 指令，op_YieldDelegate 只认同步 Generator -> 报
 * "SyntaxError: yield* outside of generator function" 静默失败。
 *
 * 实现：新增 AsyncYieldDelegate 指令（async generator 上下文发），经
 * getAsyncIterator（优先 @@asyncIterator、回退 @@iterator 包装
 * AsyncFromSyncIterator）取 inner async iterator，委托状态机
 * （AsyncFrame::delegate_phase/delegate_value + AsyncDelegateResolve/Reject
 * promise handler + asyncDelegateForward/Resume/Await/YieldValue）驱动
 * inner.next()/throw()/return()，产出值 AsyncGeneratorYield，完成/异常注入
 * 外层字节码（finally 穿透）。
 *
 * 覆盖（与 Node 基准一致）：
 *   - 委托 async generator：完整驱动 + 值产出 + 完成后外层继续
 *   - 委托同步可迭代对象（数组）：AsyncFromSyncIterator 包装，同步 settle
 *     路径（asyncDelegateForward 同步 settle 分支）
 *   - throw 转发：inner.throw() 注入 inner，catch 后 yield 恢复
 *   - return 转发：inner.return() + finally 穿透，外层 return value 保留
 *   - @@asyncIterator 优先于 @@iterator（与 for await 的 GetAsyncIterator 一致）
 *   - yield* 非迭代器：TypeError reject 请求 promise
 *   - 委托自定义 async iterator：覆盖 asyncDelegateAwait pending 挂起路径
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

/* 断言全局对象属性（任意 kind）的 toString 为期望值（异常值等）。 */
void expectGlobalStringified(JSContext *ctx, const char *name,
                             const std::string &expected) {
  JSValue *v = globalProp(ctx, name);
  ASSERT_NE(v, nullptr) << "global property '" << name << "' missing";
  EXPECT_EQ(std::string(JS_ToString(ctx, v)), expected)
      << "global '" << name << "'";
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
/* M5.5e-1：yield* 委托 async generator                                        */
/* -------------------------------------------------------------------------- */

/* async function* outer 内 yield* inner()（inner 也是 async function*）：
 * 委托驱动 inner 逐个产出（AsyncGeneratorYield），inner 完成后 yield* 表达式
 * 结算、外层继续执行。seq 记录执行序（o1 i1 i2 i3 o2），res 收集产出值。 */
TEST(AsyncYieldDelegate, ToAsyncGenerator) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  globalThis.seq = '';"
      "  globalThis.res = '';"
      "  async function* inner() {"
      "    globalThis.seq += 'i1'; yield 1;"
      "    globalThis.seq += 'i2'; yield 2;"
      "    globalThis.seq += 'i3';"
      "  }"
      "  async function* outer() {"
      "    globalThis.seq += 'o1'; yield* inner(); globalThis.seq += 'o2';"
      "  }"
      "  for await (const v of outer()) { globalThis.res += v; }"
      "})();");

  expectGlobalString(ctx, "seq", "o1i1i2i3o2");
  expectGlobalString(ctx, "res", "12");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* yield* 委托后外层代码执行 + 返回值语义：手动 next 驱动，产出 1,2 后
 * yield* 表达式结果为 inner 的返回值 99，外层继续，最终 done:true。 */
TEST(AsyncYieldDelegate, ManualNextDrivesDelegate) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* inner() { yield 1; yield 2; return 99; }"
      "  async function* outer() {"
      "    var r = yield* inner();"
      "    globalThis.yieldStarResult = r;"
      "  }"
      "  var gen = outer();"
      "  var r1 = await gen.next();"
      "  globalThis.v1 = r1.value; globalThis.d1 = r1.done;"
      "  var r2 = await gen.next();"
      "  globalThis.v2 = r2.value; globalThis.d2 = r2.done;"
      "  var r3 = await gen.next();"
      "  globalThis.v3 = r3.value; globalThis.d3 = r3.done;"
      "})();");

  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalBoolean(ctx, "d1", false);
  expectGlobalNumber(ctx, "v2", 2.0);
  expectGlobalBoolean(ctx, "d2", false);
  expectGlobalNumber(ctx, "yieldStarResult", 99.0);
  expectGlobalUndefined(ctx, "v3");
  expectGlobalBoolean(ctx, "d3", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.5e-2：yield* 委托同步可迭代对象（AsyncFromSyncIterator）                 */
/* -------------------------------------------------------------------------- */

/* yield* [1,2,3]：getAsyncIterator 回退 @@iterator 包装 AsyncFromSyncIterator。
 * inner.next() 同步返回结果 promise（已 Fulfilled）——覆盖 asyncDelegateForward
 * 同步 settle 分支。产出 1,2,3 后 done，外层继续。 */
TEST(AsyncYieldDelegate, ToSyncIterableArray) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  globalThis.seq = '';"
      "  async function* outer() {"
      "    globalThis.seq += 'o1';"
      "    yield* [1, 2, 3];"
      "    globalThis.seq += 'o2';"
      "  }"
      "  var gen = outer();"
      "  var r1 = await gen.next();"
      "  globalThis.v1 = r1.value; globalThis.d1 = r1.done;"
      "  var r2 = await gen.next();"
      "  globalThis.v2 = r2.value; globalThis.d2 = r2.done;"
      "  var r3 = await gen.next();"
      "  globalThis.v3 = r3.value; globalThis.d3 = r3.done;"
      "  var r4 = await gen.next();"
      "  globalThis.v4 = r4.value; globalThis.d4 = r4.done;"
      "})();");

  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalBoolean(ctx, "d1", false);
  expectGlobalNumber(ctx, "v2", 2.0);
  expectGlobalBoolean(ctx, "d2", false);
  expectGlobalNumber(ctx, "v3", 3.0);
  expectGlobalBoolean(ctx, "d3", false);
  expectGlobalUndefined(ctx, "v4");
  expectGlobalBoolean(ctx, "d4", true);
  expectGlobalString(ctx, "seq", "o1o2");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.5e-3：throw 转发                                                         */
/* -------------------------------------------------------------------------- */

/* gen.throw('boom') 经 yield* 转发给 inner：inner 的 try/catch 捕获后
 * yield 'recovered'，外层收到 {value:'recovered', done:false}。 */
TEST(AsyncYieldDelegate, ThrowForwardToInner) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* inner() {"
      "    try { yield 1; }"
      "    catch (e) { globalThis.caught = e; yield 'recovered'; }"
      "  }"
      "  async function* outer() { yield* inner(); }"
      "  var gen = outer();"
      "  var r1 = await gen.next();"
      "  globalThis.v1 = r1.value; globalThis.d1 = r1.done;"
      "  var r2 = await gen.throw('boom');"
      "  globalThis.v2 = r2.value; globalThis.d2 = r2.done;"
      "})();");

  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalBoolean(ctx, "d1", false);
  expectGlobalString(ctx, "caught", "boom");
  expectGlobalString(ctx, "v2", "recovered");
  expectGlobalBoolean(ctx, "d2", false);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.5e-4：return 转发 + finally 穿透                                         */
/* -------------------------------------------------------------------------- */

/* gen.return('bye') 经 yield* 转发给 inner：inner 的 finally 执行，外层
 * finally 也执行，最终外层以 return completion（'bye'）完成
 * （{value:'bye', done:true}）——§14.4.14 step 6.b return 穿透。 */
TEST(AsyncYieldDelegate, ReturnForwardThroughFinally) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  globalThis.seq = '';"
      "  async function* inner() {"
      "    try { yield 1; } finally { globalThis.seq += 'if'; }"
      "  }"
      "  async function* outer() {"
      "    try { yield* inner(); } finally { globalThis.seq += 'of'; }"
      "  }"
      "  var gen = outer();"
      "  var r1 = await gen.next();"
      "  globalThis.v1 = r1.value; globalThis.d1 = r1.done;"
      "  var r2 = await gen.return('bye');"
      "  globalThis.v2 = r2.value; globalThis.d2 = r2.done;"
      "})();");

  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalBoolean(ctx, "d1", false);
  expectGlobalString(ctx, "seq", "ifof");
  expectGlobalString(ctx, "v2", "bye");
  expectGlobalBoolean(ctx, "d2", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.5e-5：@@asyncIterator 优先 + 非迭代器 TypeError                           */
/* -------------------------------------------------------------------------- */

/* 同时有 @@iterator 与 @@asyncIterator 时优先 @@asyncIterator（与
 * for await 的 GetAsyncIterator 一致，§14.7.5.1）。@@iterator 不被调用。 */
TEST(AsyncYieldDelegate, AsyncIteratorPreferredOverSync) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  globalThis.syncCalled = false;"
      "  var both = {"
      "    [Symbol.iterator]() { globalThis.syncCalled = true; return [][Symbol.iterator](); },"
      "    [Symbol.asyncIterator]() {"
      "      var i = 0;"
      "      return { next() { return Promise.resolve({ value: ++i, done: i > 2 }); } };"
      "    }"
      "  };"
      "  async function* outer() { yield* both; }"
      "  var gen = outer();"
      "  var r1 = await gen.next();"
      "  globalThis.v1 = r1.value; globalThis.d1 = r1.done;"
      "  var r2 = await gen.next();"
      "  globalThis.v2 = r2.value; globalThis.d2 = r2.done;"
      "  var r3 = await gen.next();"
      "  globalThis.v3 = r3.value; globalThis.d3 = r3.done;"
      "})();");

  expectGlobalBoolean(ctx, "syncCalled", false);
  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalNumber(ctx, "v2", 2.0);
  expectGlobalUndefined(ctx, "v3");
  expectGlobalBoolean(ctx, "d3", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* yield* 42（非迭代器）：getAsyncIterator 报 TypeError，reject 请求 promise
 * （await gen.next() 捕获），之后 next 返回 done:true。 */
TEST(AsyncYieldDelegate, NotIterableRejects) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* outer() { yield* 42; }"
      "  var gen = outer();"
      "  var e = null;"
      "  try { await gen.next(); } catch (x) { e = x; }"
      "  globalThis.e = e;"
      "  var r = await gen.next();"
      "  globalThis.done = r.done;"
      "})();");

  expectGlobalStringified(ctx, "e", "TypeError: obj is not async iterable");
  expectGlobalBoolean(ctx, "done", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 委托自定义 async iterator（@@asyncIterator 返回 {next() -> promise}）
 * 覆盖 asyncDelegateAwait pending 挂起路径（非同步 settle）。 */
TEST(AsyncYieldDelegate, CustomAsyncIteratorPendingAwait) {
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
      "  async function* outer() { yield* ai; }"
      "  var gen = outer();"
      "  var r1 = await gen.next();"
      "  globalThis.v1 = r1.value; globalThis.d1 = r1.done;"
      "  var r2 = await gen.next();"
      "  globalThis.v2 = r2.value; globalThis.d2 = r2.done;"
      "  var r3 = await gen.next();"
      "  globalThis.v3 = r3.value; globalThis.d3 = r3.done;"
      "  var r4 = await gen.next();"
      "  globalThis.v4 = r4.value; globalThis.d4 = r4.done;"
      "})();");

  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalBoolean(ctx, "d1", false);
  expectGlobalNumber(ctx, "v2", 2.0);
  expectGlobalBoolean(ctx, "d2", false);
  expectGlobalNumber(ctx, "v3", 3.0);
  expectGlobalBoolean(ctx, "d3", false);
  expectGlobalUndefined(ctx, "v4");
  expectGlobalBoolean(ctx, "d4", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
