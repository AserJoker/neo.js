/*
 * M5.5a-3 generator 运行时端到端测试（§27.5）。
 *
 * 覆盖：
 *   - g() 返回 Generator（不立即执行），首次 next() 才执行到首个 yield
 *   - 单个 yield：next 返回 {value, done:false}，再 next 返回 {value:undefined,done:true}
 *   - 多个 yield 串行
 *   - yield 表达式接收 next(value) 传入值
 *   - generator 函数正常 return：done:true + return 值
 *   - throw(exception) 注入异常 + try/catch 捕获
 *   - return(value) 强制完成
 *   - 完成后再 next：始终 {value:undefined, done:true}
 *
 * 宿主模式：JS_Eval 求值（generator 函数是 JS 函数），同步驱动 next()
 * 调用，结果写入 globalThis.* 后断言。
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
/* M5.5a-3：generator 基础 next/yield/完成                                       */
/* -------------------------------------------------------------------------- */

/* g() 返回 Generator，首次 next() 执行到首个 yield 返回 {value:1, done:false}。
 * 再次 next() 越过 yield，函数结束返回 {value:undefined, done:true}。 */
TEST(Generator, BasicNextYieldDone) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() { yield 1; }"
      "var gen = g();"
      "var r1 = gen.next();"
      "globalThis.v1 = r1.value;"
      "globalThis.d1 = r1.done;"
      "var r2 = gen.next();"
      "globalThis.v2 = r2.value;"
      "globalThis.d2 = r2.done;");

  expectGlobalNumber(ctx, "v1", 1.0);
  expectGlobalBoolean(ctx, "d1", false);
  expectGlobalUndefined(ctx, "v2");
  expectGlobalBoolean(ctx, "d2", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* g() 不调用 next() 时函数体未执行（GeneratorStart：suspended-start）。 */
TEST(Generator, NoNextBodyNotExecuted) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() { globalThis.executed = true; yield 1; }"
      "var gen = g();"
      "globalThis.executed = false;");

  /* g() 仅创建 generator，未触发执行。 */
  expectGlobalBoolean(ctx, "executed", false);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 多个 yield 串行：next 依次返回每个 yield 值。 */
TEST(Generator, MultipleYields) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() { yield 1; yield 2; yield 3; }"
      "var gen = g();"
      "var r1 = gen.next(); var r2 = gen.next(); var r3 = gen.next();"
      "var r4 = gen.next();"
      "globalThis.seq = '' + r1.value + r2.value + r3.value + r4.value;"
      "globalThis.d4 = r4.done;");

  expectGlobalString(ctx, "seq", "123undefined");
  expectGlobalBoolean(ctx, "d4", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* yield 表达式接收 next(value) 传入值（GeneratorResume step 9）。 */
TEST(Generator, YieldReceivesNextValue) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() {"
      "  var a = yield 1;"
      "  globalThis.received = a;"
      "  yield a * 2;"
      "}"
      "var gen = g();"
      "gen.next();"
      "gen.next(10);");

  expectGlobalNumber(ctx, "received", 10.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* generator 函数 return 值：done=true 且 value=return 值。 */
TEST(Generator, ReturnValue) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() { yield 1; return 42; }"
      "var gen = g();"
      "gen.next();"
      "var r = gen.next();"
      "globalThis.rv = r.value;"
      "globalThis.rd = r.done;");

  expectGlobalNumber(ctx, "rv", 42.0);
  expectGlobalBoolean(ctx, "rd", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 完成后再 next：始终 {value:undefined, done:true}（§27.5.3.2 step 3）。 */
TEST(Generator, NextAfterCompletion) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() { yield 1; }"
      "var gen = g();"
      "gen.next();"
      "gen.next();"
      "var r3 = gen.next();"
      "var r4 = gen.next();"
      "globalThis.v3 = r3.value;"
      "globalThis.d3 = r3.done;"
      "globalThis.v4 = r4.value;"
      "globalThis.d4 = r4.done;");

  expectGlobalUndefined(ctx, "v3");
  expectGlobalBoolean(ctx, "d3", true);
  expectGlobalUndefined(ctx, "v4");
  expectGlobalBoolean(ctx, "d4", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.5a-3：throw/return 恢复路径                                               */
/* -------------------------------------------------------------------------- */

/* throw(exception) 在 yield 挂起点注入异常，可被 try/catch 捕获。 */
TEST(Generator, ThrowInjectsException) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() {"
      "  try { yield 1; }"
      "  catch (e) { globalThis.caught = e; }"
      "}"
      "var gen = g();"
      "gen.next();"
      "gen.throw('boom');");

  expectGlobalString(ctx, "caught", "boom");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* throw 在 completed 状态：直接抛出 value（§27.5.3.3 step 8.b）。
 * 用 catch 捕获 throw 调用本身的异常。 */
TEST(Generator, ThrowOnCompletedThrows) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() { yield 1; }"
      "var gen = g();"
      "gen.next();"
      "gen.next();"
      "try { gen.throw('late'); }"
      "catch (e) { globalThis.lateErr = e; }");

  expectGlobalString(ctx, "lateErr", "late");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* return(value) 强制完成：返回 {value, done:true}。
 * 简化实现：未做 try/finally 穿透（M5.5a-3 阶段）。 */
TEST(Generator, ReturnForcesCompletion) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() { yield 1; yield 2; }"
      "var gen = g();"
      "gen.next();"
      "var r = gen.return(99);"
      "globalThis.rv = r.value;"
      "globalThis.rd = r.done;"
      "var r2 = gen.next();"
      "globalThis.v2 = r2.value;"
      "globalThis.d2 = r2.done;");

  expectGlobalNumber(ctx, "rv", 99.0);
  expectGlobalBoolean(ctx, "rd", true);
  expectGlobalUndefined(ctx, "v2");
  expectGlobalBoolean(ctx, "d2", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* generator 闭包计数器：多次创建独立 generator，各自维护状态。 */
TEST(Generator, IndependentInstances) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* counter() {"
      "  var n = 0;"
      "  while (true) { yield n; n = n + 1; }"
      "}"
      "var g1 = counter();"
      "var g2 = counter();"
      "g1.next(); g1.next();"
      "globalThis.g1v = g1.next().value;"
      "globalThis.g2v = g2.next().value;");

  expectGlobalNumber(ctx, "g1v", 2.0);
  expectGlobalNumber(ctx, "g2v", 0.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.5a-4：yield* 委托（§14.4 / §27.5.3.3）                                    */
/* -------------------------------------------------------------------------- */

/* yield* 数组委托：外层 next() 依次转发给 inner 数组迭代器，inner 迭代完成后
 * 外层继续执行后续 yield。 */
TEST(Generator, YieldDelegateArray) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() {"
      "  yield 'before';"
      "  yield* [10, 20, 30];"
      "  yield 'after';"
      "}"
      "var gen = g();"
      "var r1 = gen.next();"
      "var r2 = gen.next();"
      "var r3 = gen.next();"
      "var r4 = gen.next();"
      "var r5 = gen.next();"
      "var r6 = gen.next();"
      "globalThis.seq = '' + r1.value + ',' + r2.value + ',' + r3.value + ','"
      "  + r4.value + ',' + r5.value + ',' + r6.value;"
      "globalThis.d6 = r6.done;");

  expectGlobalString(ctx, "seq", "before,10,20,30,after,undefined");
  expectGlobalBoolean(ctx, "d6", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* yield* generator 委托：inner 是另一个 generator 函数，外层 next() 转发给
 * inner.next()，inner 完成后外层续延。 */
TEST(Generator, YieldDelegateGenerator) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* inner() { yield 1; yield 2; return 99; }"
      "function* outer() {"
      "  var r = yield* inner();"
      "  yield r + 100;"
      "}"
      "var gen = outer();"
      "var r1 = gen.next();"
      "var r2 = gen.next();"
      "var r3 = gen.next();"
      "var r4 = gen.next();"
      "globalThis.seq = '' + r1.value + ',' + r2.value + ',' + r3.value + ','"
      "  + r4.value;"
      "globalThis.d4 = r4.done;");

  /* inner yield 1, 2；inner return 99 -> yield* 表达式值 99；外层 yield 199；
   * 再 next -> done:true undefined。 */
  expectGlobalString(ctx, "seq", "1,2,199,undefined");
  expectGlobalBoolean(ctx, "d4", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* yield* 委托 throw 转发：外层 gen.throw(err) 转发给 inner.throw(err)，inner
 * 无 throw 方法时透传 err 给外层（外层 try/catch 可捕）。 */
TEST(Generator, YieldDelegateThrowForwarded) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* inner() {"
      "  try { yield 1; yield 2; }"
      "  catch (e) { globalThis.innerCaught = e; }"
      "}"
      "function* outer() {"
      "  try { yield* inner(); }"
      "  catch (e) { globalThis.outerCaught = e; }"
      "}"
      "var gen = outer();"
      "var r1 = gen.next();"
      "gen.throw('injected');"
      "globalThis.r1v = r1.value;");

  /* inner 有 throw 方法（generator 默认）-> throw 注入 inner，inner try/catch
   * 捕获 'injected'。外层 outerCaught 不应被设置。 */
  expectGlobalNumber(ctx, "r1v", 1.0);
  expectGlobalString(ctx, "innerCaught", "injected");
  expectGlobalUndefined(ctx, "outerCaught");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* yield* 委托 return 转发：外层 gen.return(v) 转发给 inner.return(v)，inner
 * 完成 -> 外层完成，返回 {value:v, done:true}。 */
TEST(Generator, YieldDelegateReturnForwarded) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* inner() { yield 1; yield 2; }"
      "function* outer() { yield* inner(); yield 3; }"
      "var gen = outer();"
      "gen.next();"
      "var r = gen.return('done');"
      "globalThis.rv = r.value;"
      "globalThis.rd = r.done;"
      "var r2 = gen.next();"
      "globalThis.r2d = r2.done;");

  /* return 转发 inner.return -> inner 完成 -> 外层完成，{value:'done',done:true}。
   * 后续 next 仍 done:true。 */
  expectGlobalString(ctx, "rv", "done");
  expectGlobalBoolean(ctx, "rd", true);
  expectGlobalBoolean(ctx, "r2d", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 嵌套 yield*：三层 generator 委托链，next 逐层穿透。 */
TEST(Generator, YieldDelegateNested) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* a() { yield 1; yield 2; }"
      "function* b() { yield* a(); yield 3; }"
      "function* c() { yield* b(); yield 4; }"
      "var gen = c();"
      "var s = '';"
      "for (var i = 0; i < 5; i++) {"
      "  var r = gen.next();"
      "  s += r.value + (r.done ? '|' : ',');"
      "}"
      "globalThis.seq = s;");

  /* 1,2,3,4,undefined| */
  expectGlobalString(ctx, "seq", "1,2,3,4,undefined|");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
