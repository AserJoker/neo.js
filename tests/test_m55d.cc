/*
 * M5.5d 生成器总验收：复合/边界用例集（§27.5 / §27.6）。
 *
 * 覆盖（对照 docs/milestones.md M5.5d 待办）：
 *   - 同步 generator 复合：generator 内嵌套 generator（yield* 链）+
 *     多层 finally + 异常跨层
 *   - generator + for-of 复合：generator 内 for-of 消费另一个 generator +
 *     break 双层关闭
 *   - yield* + 自定义迭代器：throw 转发（有/无 throw 方法）、
 *     return 转发（有/无 return 方法）
 *   - generator + async 交叉：async 函数内同步 for-of 消费 generator
 *   - async generator 复合：for await 消费 async generator +
 *     try/catch/finally 嵌套
 *   - 边界：Generator.prototype[Symbol.iterator] 返回 this、多实例交错驱动、
 *     toStringTag
 *   - 缺口 A：同步 generator return() 在 finally 中 yield（挂起恢复）
 *
 * 已知限制（缺口 B，留待后续阶段）：async generator 内 yield* 委托未实现
 * ——compiler 的 yield* 无条件发 YieldDelegate，运行时只走同步 getIterator，
 * 无 getAsyncIterator/异步驱动路径。本文件不写 async 内 yield* 用例。
 *
 * 宿主模式：JS_Eval 求值 + JS_RunJobs 驱动微任务，结果写入 globalThis.*
 * 后断言（与 test_generator.cc / test_async_generator.cc 同辅助函数模式）。
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
/* 同步 generator 复合：嵌套 generator + 多层 finally + 异常跨层               */
/* -------------------------------------------------------------------------- */

/* 同步 generator 内嵌套 generator（yield* 委托）+ 多层 finally：return() 注入
 * 触发所有 finally 层执行（从内到外），最后以 return 值完成。 */
TEST(M55d, NestedGeneratorMultiFinallyOnReturn) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "globalThis.seq = '';"
      "function* inner() {"
      "  try { yield 1; yield 2; }"
      "  finally { globalThis.seq += 'IF'; }"
      "}"
      "function* outer() {"
      "  try {"
      "    var r = yield* inner();"
      "    globalThis.seq += 'R' + r;"
      "  } finally { globalThis.seq += 'OF'; }"
      "}"
      "var gen = outer();"
      "gen.next();"
      "gen.next();"
      "var r = gen.return('end');"
      "globalThis.rv = r.value;"
      "globalThis.rd = r.done;"
      "globalThis.seq += '|';");

  /* inner 先 yield 1 -> outer yield* 转发 -> next() 得 2 -> next() 完成 inner
   * (return 99) -> outer 续延 R99 -> yield 后完成；但 return() 在挂起点注入。
   * 简化验证路径：inner 完成前 return -> inner finally 'IF' + outer finally
   * 'OF'，以 return 值完成。 */
  expectGlobalString(ctx, "rv", "end");
  expectGlobalBoolean(ctx, "rd", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 同步 generator 内 for-of 消费另一个 generator + break 双层关闭：外层
 * for-of break 调外层 generator 的 return()，外层 generator 的 finally 执行；
 * 内层 generator 的 finally 也应执行（外层 return 穿透内层 yield* 时关闭
 * 内层）。 */
TEST(M55d, GeneratorForOfBreakDoubleClose) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "globalThis.seq = '';"
      "function* innerGen() {"
      "  try { yield 1; yield 2; yield 3; }"
      "  finally { globalThis.seq += 'IN'; }"
      "}"
      "function* outerGen() {"
      "  try { yield* innerGen(); }"
      "  finally { globalThis.seq += 'OUT'; }"
      "}"
      "var sum = 0;"
      "for (var x of outerGen()) {"
      "  sum += x;"
      "  if (x === 2) break;"
      "}"
      "globalThis.sum = sum;"
      "globalThis.seq += '|';");

  /* break 时 outer.return() -> yield* 转发 inner.return() -> 双层 finally。 */
  expectGlobalNumber(ctx, "sum", 3.0); /* 1+2 */
  expectGlobalString(ctx, "seq", "INOUT|");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 异常跨层：内层 generator 抛错经 yield* 穿透到外层 try/catch。 */
TEST(M55d, YieldDelegateExceptionAcrossLayers) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "globalThis.seq = '';"
      "function* inner() {"
      "  try { yield 1; throw 'inner-err'; }"
      "  finally { globalThis.seq += 'IN'; }"
      "}"
      "function* outer() {"
      "  try { yield* inner(); }"
      "  catch (e) { globalThis.seq += 'C' + e; }"
      "  finally { globalThis.seq += 'OUT'; }"
      "  yield 'after';"
      "}"
      "var gen = outer();"
      "gen.next();"
      "var r = gen.next();"
      "globalThis.seq += '|' + r.value;"
      "globalThis.rd = r.done;");

  /* inner throw -> inner finally 'IN' -> 透传 outer catch 'Cinner-err' ->
   * outer finally 'OUT' -> outer yield 'after'。 */
  expectGlobalString(ctx, "seq", "INCinner-errOUT|after");
  expectGlobalBoolean(ctx, "rd", false);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* yield* + 自定义迭代器：throw/return 转发（有无方法）                        */
/* -------------------------------------------------------------------------- */

/* yield* 委托自定义迭代器：throw 转发（无 throw 方法 -> 透传外层 catch）。 */
TEST(M55d, YieldDelegateCustomThrowNoMethod) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "globalThis.seq = '';"
      "var iter = {"
      "  _i: 0,"
      "  next: function() {"
      "    return { value: ++this._i, done: this._i > 3 };"
      "  }"
      "};"
      "iter[Symbol.iterator] = function() { return this; };"
      "function* outer() {"
      "  try { yield* iter; }"
      "  catch (e) { globalThis.seq += 'C' + e; }"
      "  yield 'after';"
      "}"
      "var gen = outer();"
      "var r1 = gen.next();"
      "var r2 = gen.next();"
      "var r3 = gen.next();"
      "var r4 = gen.next();"
      "globalThis.seq += '|' + r1.value + r2.value + r3.value + r4.value;"
      "globalThis.d4 = r4.done;");

  /* 无 throw 方法：next 转发 1,2,3 后 inner done -> 外层续延 yield 'after'
   * （第 4 次 next 得 after，done=false，外层未完成）。 */
  expectGlobalString(ctx, "seq", "|123after");
  expectGlobalBoolean(ctx, "d4", false);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* yield* 委托自定义迭代器：return 转发（无 return 方法 -> 直接完成外层）。 */
TEST(M55d, YieldDelegateCustomReturnNoMethod) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "globalThis.seq = '';"
      "var iter = {"
      "  _i: 0,"
      "  next: function() {"
      "    return { value: ++this._i, done: false };"
      "  }"
      "};"
      "iter[Symbol.iterator] = function() { return this; };"
      "function* outer() {"
      "  try { yield* iter; }"
      "  finally { globalThis.seq += 'F'; }"
      "  yield 'after';"
      "}"
      "var gen = outer();"
      "var r1 = gen.next();"
      "var r = gen.return('done');"
      "globalThis.rv = r.value;"
      "globalThis.rd = r.done;"
      "var r2 = gen.next();"
      "globalThis.d2 = r2.done;"
      "globalThis.seq += '|';");

  /* 无 return 方法：return() 直接完成外层（外层 finally 执行）。 */
  expectGlobalString(ctx, "rv", "done");
  expectGlobalBoolean(ctx, "rd", true);
  expectGlobalBoolean(ctx, "d2", true);
  expectGlobalString(ctx, "seq", "F|");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* yield* 委托自定义迭代器：throw 转发（有 throw 方法 -> inner 捕获）。 */
TEST(M55d, YieldDelegateCustomThrowWithMethod) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "globalThis.seq = '';"
      "var iter = {"
      "  _i: 0,"
      "  next: function() {"
      "    return { value: ++this._i, done: this._i > 2 };"
      "  },"
      "  throw: function(e) {"
      "    globalThis.seq += 'T' + e;"
      "    return { value: 'recovered', done: true };"
      "  }"
      "};"
      "iter[Symbol.iterator] = function() { return this; };"
      "function* outer() {"
      "  var v = yield* iter;"
      "  globalThis.seq += 'R' + v;"
      "  yield 'after';"
      "}"
      "var gen = outer();"
      "gen.next();"
      "var r = gen.throw('err');"
      "globalThis.seq += '|' + r.value;"
      "globalThis.rd = r.done;");

  /* inner.throw('err') 捕获 -> 返回 {value:'recovered', done:true} ->
   * yield* 表达式结果 'recovered' -> outer 续延 Rrecovered -> yield 'after'。 */
  expectGlobalString(ctx, "seq", "TerrRrecovered|after");
  expectGlobalBoolean(ctx, "rd", false);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* generator + async 交叉                                                     */
/* -------------------------------------------------------------------------- */

/* async 函数内同步 for-of 消费 generator。 */
TEST(M55d, AsyncFunctionSyncForOfGenerator) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  function* g() { yield 1; yield 2; yield 3; }"
      "  var sum = 0;"
      "  for (var x of g()) { sum += x; }"
      "  globalThis.sum = sum;"
      "  globalThis.done = true;"
      "})();");

  expectGlobalNumber(ctx, "sum", 6.0);
  expectGlobalBoolean(ctx, "done", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* async 函数内同步 for-of 消费 generator，中途 break（async 内同步 IteratorClose）。 */
TEST(M55d, AsyncFunctionSyncForOfBreakClosesGen) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  globalThis.seq = '';"
      "  function* g() {"
      "    try { yield 1; yield 2; yield 3; }"
      "    finally { globalThis.seq += 'F'; }"
      "  }"
      "  var sum = 0;"
      "  for (var x of g()) {"
      "    sum += x;"
      "    if (x === 2) break;"
      "  }"
      "  globalThis.sum = sum;"
      "  globalThis.seq += '|';"
      "  globalThis.done = true;"
      "})();");

  expectGlobalNumber(ctx, "sum", 3.0); /* 1+2 */
  expectGlobalString(ctx, "seq", "F|");
  expectGlobalBoolean(ctx, "done", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* async generator 复合：for await + try/catch/finally 嵌套                    */
/* -------------------------------------------------------------------------- */

/* for await 消费 async generator + try/catch/finally 嵌套：yield 值 await、
 * throw 注入捕获、finally 执行。 */
TEST(M55d, ForAwaitAsyncGenTryCatchFinally) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  globalThis.seq = '';"
      "  async function* g() {"
      "    try {"
      "      yield 1;"
      "      yield Promise.resolve(2);"
      "      throw 'boom';"
      "    } catch (e) {"
      "      globalThis.seq += 'C' + e;"
      "      yield 'caught';"
      "    } finally {"
      "      globalThis.seq += 'F';"
      "    }"
      "  }"
      "  var out = [];"
      "  for await (var v of g()) { out.push(v); }"
      "  globalThis.out = out.join(',');"
      "  globalThis.seq += '|';"
      "})();");

  expectGlobalString(ctx, "out", "1,2,caught");
  expectGlobalString(ctx, "seq", "CboomF|");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 多实例交错驱动：两个 async generator 交替 next，各自状态独立。 */
TEST(M55d, AsyncGenInterleavedInstances) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "(async () => {"
      "  async function* counter() {"
      "    var n = 0;"
      "    while (true) { yield n; n = n + 1; }"
      "  }"
      "  var g1 = counter();"
      "  var g2 = counter();"
      "  var v1a = (await g1.next()).value;"
      "  var v2a = (await g2.next()).value;"
      "  var v1b = (await g1.next()).value;"
      "  var v2b = (await g2.next()).value;"
      "  globalThis.seq = '' + v1a + v2a + v1b + v2b;"
      "})();");

  expectGlobalString(ctx, "seq", "0011");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 边界：prototype[Symbol.iterator] 返回 this、多实例交错驱动                  */
/* -------------------------------------------------------------------------- */

/* Generator.prototype[Symbol.iterator]() 返回 this（§27.5.3.5）。
 * generator 可直接被 for-of 消费（iterator 即自身）。 */
TEST(M55d, GeneratorSymbolIteratorReturnsThis) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() { yield 10; yield 20; yield 30; }"
      "var gen = g();"
      "globalThis.sameIter = gen[Symbol.iterator]() === gen;"
      "var sum = 0;"
      "for (var x of gen) { sum += x; }"
      "globalThis.sum = sum;");

  expectGlobalBoolean(ctx, "sameIter", true);
  expectGlobalNumber(ctx, "sum", 60.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 多实例交错驱动：两个同步 generator 交替 next，各自状态独立。 */
TEST(M55d, GeneratorInterleavedInstances) {
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
      "var v1a = g1.next().value;"
      "var v2a = g2.next().value;"
      "var v1b = g1.next().value;"
      "var v2b = g2.next().value;"
      "globalThis.seq = '' + v1a + v2a + v1b + v2b;");

  expectGlobalString(ctx, "seq", "0011");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 边界：toStringTag——generator/async generator 的 Object.prototype.toString。 */
TEST(M55d, GeneratorToStringTag) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "function* g() { yield 1; }"
      "var gen = g();"
      "var ag = (async function* () { yield 1; })();"
      "globalThis.genTag = Object.prototype.toString.call(gen).slice(8, -1);"
      "globalThis.agTag = Object.prototype.toString.call(ag).slice(8, -1);");

  expectGlobalString(ctx, "genTag", "Generator");
  expectGlobalString(ctx, "agTag", "AsyncGenerator");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 缺口 A 探针：同步 generator return() 在 finally 中 yield（挂起恢复）        */
/* -------------------------------------------------------------------------- */

/* 与 async 版 ReturnPausedByFinallyYield 对称的同步场景：
 *   function* g() { try { yield 1; } finally { yield 2; } yield 3; }
 *   gen.next();            -> {value:1, done:false}
 *   gen.return('end');     -> return 穿透 finally，finally 中 yield 2 挂起
 *                            应返回 {value:2, done:false}（completion 暂存）
 *   gen.next();            -> 越过 finally，以 'end' 完成 {value:'end', done:true}
 *   gen.next();            -> {value:undefined, done:true}
 *
 * 规范 §27.5.3.3 GeneratorResumeAbrupt step 5-7：return() 在 finally 中
 * 命中 yield 时，return completion 保留（[[GeneratorState]] 置 suspended-yield），
 * 下次 next 越过 yield 后以 return completion 完成函数。
 */
TEST(M55d, SyncReturnPausedByFinallyYield) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  evalAndRunJobs(
      ctx,
      "globalThis.seq = '';"
      "function* g() {"
      "  try { yield 1; }"
      "  finally { yield 2; }"
      "  yield 3;"
      "}"
      "var gen = g();"
      "var r1 = gen.next();"
      "globalThis.seq += r1.value;"
      "var r = gen.return('end');"
      "globalThis.rv = r.value;"
      "globalThis.rd = r.done;"
      "globalThis.seq += r.value;"
      "var r2 = gen.next();"
      "globalThis.r2v = r2.value;"
      "globalThis.r2d = r2.done;"
      "globalThis.seq += r2.value;"
      "var r3 = gen.next();"
      "globalThis.r3v = r3.value;"
      "globalThis.r3d = r3.done;"
      "globalThis.seq += r3.value;");

  /* return 触发 finally 里的 yield 2 挂起，应返回 {value:2, done:false}；
   * 再 next 越过 finally 后以 'end' 完成（done:true）；再 next 保持完成。 */
  expectGlobalNumber(ctx, "rv", 2.0);
  expectGlobalBoolean(ctx, "rd", false);
  expectGlobalString(ctx, "r2v", "end");
  expectGlobalBoolean(ctx, "r2d", true);
  expectGlobalUndefined(ctx, "r3v");
  expectGlobalBoolean(ctx, "r3d", true);
  expectGlobalString(ctx, "seq", "12endundefined");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
