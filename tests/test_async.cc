/*
 * M5.4 async/await 端到端测试。
 *
 * 覆盖：async 函数立即 resolve、await 挂起/恢复、await 非 Promise 值、
 * await rejected promise 异常传播 + try/catch 捕获、await thenable 接合、
 * 多个 await 串行、嵌套 async 调用、async 调用返回 promise 经 .then 链、
 * finally 跨 await、async 箭头函数、async 方法。
 *
 * 宿主模式：JS_Eval 求值（async 回调是 JS 函数），随后 JS_RunJobs 驱动
 * 微任务执行，再读取全局变量验证结果。
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
/* M5.4：async 函数调用与立即 resolve                                          */
/* -------------------------------------------------------------------------- */

/* async 函数无 await：同步体执行，返回 promise，随后 resolve 返回值。 */
TEST(Async, SyncBodyResolves) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "async function f() { return 42; }"
      "f().then(v => { globalThis.result = v; });");

  expectGlobalNumber(ctx, "result", 42.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* async 调用返回的是 promise（不立即执行 then 回调）。 */
TEST(Async, ReturnsPromise) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "async function f() { return 1; }"
      "var p = f();"
      "globalThis.isThenable = (typeof p.then === 'function');"
      "p.then(v => { globalThis.result = v; });");

  expectGlobalBoolean(ctx, "isThenable", true);
  expectGlobalNumber(ctx, "result", 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 无 return 的 async：resolve undefined。 */
TEST(Async, NoReturnResolvesUndefined) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "async function f() { let x = 1; }"
      "f().then(v => { globalThis.result = v; });");

  JSValue *v = globalProp(ctx, "result");
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, v), JS_UNDEFINED) << "await undefined result kind";
  JS_FreeValue(ctx, v);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* async 函数体内抛错 -> 返回的 promise reject。 */
TEST(Async, ThrowsRejects) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "async function f() { throw 'boom'; }"
      "f().catch(e => { globalThis.err = e; });");

  expectGlobalString(ctx, "err", "boom");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.4：await 挂起与恢复                                                       */
/* -------------------------------------------------------------------------- */

/* await 一个 pending promise：async 挂起，settle 后经微任务续延。 */
TEST(Async, AwaitPendingResume) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var resolveFn;"
      "var p = new Promise(res => { resolveFn = res; });"
      "async function f() {"
      "  var v = await p;"
      "  globalThis.result = v;"
      "}"
      "globalThis.order = 'before';"
      "f();"
      "globalThis.order += '-after-call';"
      "resolveFn(7);");

  /* await 挂起时 f() 立即返回，后续同步代码先执行，resolve 后续延。 */
  expectGlobalString(ctx, "order", "before-after-call");
  expectGlobalNumber(ctx, "result", 7.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* await 非 Promise 值：PromiseResolve 接合，立即（同步路径）继续。 */
TEST(Async, AwaitNonPromise) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "async function f() {"
      "  var a = await 10;"
      "  var b = await 'hi';"
      "  globalThis.result = a + ':' + b;"
      "}"
      "f();");

  expectGlobalString(ctx, "result", "10:hi");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* await 已 fulfilled 的 promise：不挂起，直接取结果。 */
TEST(Async, AwaitFulfilledPromise) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = Promise.resolve(5);"
      "async function f() {"
      "  var v = await p;"
      "  globalThis.result = v * 2;"
      "}"
      "f();");

  expectGlobalNumber(ctx, "result", 10.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* await rejected promise：rejection 值作为异常进入 async 帧。 */
TEST(Async, AwaitRejectedThrows) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = Promise.reject('nope');"
      "async function f() {"
      "  try { await p; }"
      "  catch (e) { globalThis.err = e; }"
      "}"
      "f();");

  expectGlobalString(ctx, "err", "nope");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* await rejected 无捕获：async 函数 reject（异常传播到返回 promise）。 */
TEST(Async, AwaitRejectedUncaughtRejects) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var p = Promise.reject('boom2');"
      "async function f() { var v = await p; return v; }"
      "f().catch(e => { globalThis.err = e; });");

  expectGlobalString(ctx, "err", "boom2");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 多个 await 串行：每个挂起/恢复依次执行。 */
TEST(Async, MultipleAwaitSerial) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "async function f() {"
      "  var x = await Promise.resolve(1);"
      "  var y = await Promise.resolve(x + 1);"
      "  var z = await Promise.resolve(y + 1);"
      "  globalThis.result = x + y + z;"
      "}"
      "f();");

  expectGlobalNumber(ctx, "result", 6.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* await 返回值经表达式继续运算（await 是表达式，参与运算）。 */
TEST(Async, AwaitInExpression) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "async function f() {"
      "  var r = (await Promise.resolve(3)) * (await Promise.resolve(4));"
      "  globalThis.result = r;"
      "}"
      "f();");

  expectGlobalNumber(ctx, "result", 12.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.4：嵌套 async / 链式                                                     */
/* -------------------------------------------------------------------------- */

/* async 函数调用另一个 async 函数，await 其返回 promise。 */
TEST(Async, NestedAsyncAwait) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "async function inner() {"
      "  return await Promise.resolve(20);"
      "}"
      "async function outer() {"
      "  var v = await inner();"
      "  globalThis.result = v + 1;"
      "}"
      "outer();");

  expectGlobalNumber(ctx, "result", 21.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* async 调用返回的 promise 可再 then：await 值经 then 链继续。 */
TEST(Async, AsyncThenChain) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "async function f() { return 10; }"
      "f().then(v => v * 2).then(v => { globalThis.result = v; });");

  expectGlobalNumber(ctx, "result", 20.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* async 箭头函数。 */
TEST(Async, ArrowFunction) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var f = async () => { return await Promise.resolve(8); };"
      "f().then(v => { globalThis.result = v; });");

  expectGlobalNumber(ctx, "result", 8.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* async 箭头表达式体：返回值 resolve。 */
TEST(Async, ArrowExpressionBody) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var f = async () => 99;"
      "f().then(v => { globalThis.result = v; });");

  expectGlobalNumber(ctx, "result", 99.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* async 方法（对象字面量）。 */
TEST(Async, ObjectMethod) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var obj = {"
      "  async m() { return await Promise.resolve('meth'); }"
      "};"
      "obj.m().then(v => { globalThis.result = v; });");

  expectGlobalString(ctx, "result", "meth");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.4：finally / 异常跨 await                                                */
/* -------------------------------------------------------------------------- */

/* finally 跨 await：await 挂起后 finally 仍执行。 */
TEST(Async, FinallyAcrossAwait) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var resolveFn;"
      "var p = new Promise(res => { resolveFn = res; });"
      "async function f() {"
      "  try {"
      "    await p;"
      "  } finally {"
      "    globalThis.fin = 'done';"
      "  }"
      "}"
      "f();"
      "resolveFn(1);");

  expectGlobalString(ctx, "fin", "done");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* await rejected 后 finally 执行，异常继续向外传播。 */
TEST(Async, FinallyAfterRejectPropagates) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "async function f() {"
      "  try { await Promise.reject('x'); }"
      "  finally { globalThis.fin = 'done'; }"
      "}"
      "f().catch(e => { globalThis.err = e; });");

  expectGlobalString(ctx, "fin", "done");
  expectGlobalString(ctx, "err", "x");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* await 值在挂起期间被局部变量保留（栈副本 + env 恢复）。 */
TEST(Async, LocalVarsPreservedAcrossAwait) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var resolveFn;"
      "var p = new Promise(res => { resolveFn = res; });"
      "async function f() {"
      "  var a = 1;"
      "  var b = 'keep';"
      "  var c = await p;"
      "  globalThis.result = a + ':' + b + ':' + c;"
      "}"
      "f();"
      "resolveFn(3);");

  expectGlobalString(ctx, "result", "1:keep:3");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* await 一个 thenable（含 .then 方法的对象）：经 PromiseResolve 接合。 */
TEST(Async, AwaitThenable) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "var thenable = {"
      "  then(res, rej) { res('thenable-value'); }"
      "};"
      "async function f() {"
      "  var v = await thenable;"
      "  globalThis.result = v;"
      "}"
      "f();");

  expectGlobalString(ctx, "result", "thenable-value");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
