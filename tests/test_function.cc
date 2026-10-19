/*
 * E3 函数最小集单元测试（docs/architecture.md §5.3 + ECMA-262 §7.3.12）。
 * 验证：
 *   1. JS_NewCFunction 创建宿主 C 回调函数（kind=JS_OBJECT，可调用）
 *   2. JS_Call 调用：参数传递、返回值
 *   3. this 绑定（非严格模式 null/undefined -> globalObject）
 *   4. 非函数调用 -> TypeError（kind=JS_EXCEPTION）
 *   5. 回调抛 C++ 异常 -> 错误值（#3）
 *   6. GC 下函数/参数存活
 *
 * 注意：宿主回调收到的 self/argv[i] 是**宿主句柄**（G2），
 * 需经 JS_GetKind/JS_ToNumber 等 C API 解析。
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <stdexcept>

#include "neojs/neojs.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/function.h"

namespace {

/* 创建默认分配器 + ctx；调用方负责 JS_FreeContext 后 JS_FreeAllocator。 */
JSContext *makeCtx(JSAllocator **out_alloc) {
  JSAllocator *a = JS_NewAllocator(nullptr, nullptr, nullptr);
  EXPECT_NE(a, nullptr);
  JSContext *ctx = JS_NewContext(a);
  EXPECT_NE(ctx, nullptr);
  if (out_alloc) *out_alloc = a;
  return ctx;
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 宿主 C 回调（JSCFunction 签名，#6）                                        */
/* -------------------------------------------------------------------------- */

/* 求和回调：返回 argc 个参数的数值和（缺省视为 0）。
 * 注意：argv 实为宿主句柄数组（G2），按指针数组 reinterpret_cast 访问。 */
JSValue *sumFn(JSContext *ctx, JSValue *self, size_t argc, JSValue *argv) {
  (void)self;
  JSValue *const *args = reinterpret_cast<JSValue *const *>(argv);
  double total = 0.0;
  for (size_t i = 0; i < argc; ++i) {
    total += JS_ToNumber(ctx, args[i]);
  }
  return JS_CreateNumber(ctx, total, nullptr);
}

/* 返回 this 的 kind 描述的回调：验证 this 绑定。 */
JSValue *thisKindFn(JSContext *ctx, JSValue *self, size_t, JSValue *) {
  if (self == nullptr) return JS_CreateString(ctx, "null", 4, nullptr);
  switch (JS_GetKind(ctx, self)) {
    case JS_OBJECT:
      return JS_CreateString(ctx, "object", 6, nullptr);
    case JS_UNDEFINED:
      return JS_CreateString(ctx, "undefined", 9, nullptr);
    default:
      return JS_CreateString(ctx, "other", 5, nullptr);
  }
}

/* 抛 C++ 异常的回调：验证 #3（异常不跨 C ABI 边界）。 */
JSValue *throwFn(JSContext *, JSValue *, size_t, JSValue *) {
  throw std::runtime_error("boom");
}

/* -------------------------------------------------------------------------- */
/* 创建 + 调用                                                                */
/* -------------------------------------------------------------------------- */

TEST(Function, CreateAndCallHostFunction) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 创建宿主函数（临时值，无绑定） */
  JSValue *fn = JS_NewCFunction(ctx, sumFn, nullptr);
  ASSERT_NE(fn, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, fn), JS_OBJECT); /* 函数是可调用对象 */

  /* 传两个参数调用：1 + 2 = 3 */
  JSValue *x = JS_CreateNumber(ctx, 1, nullptr);
  JSValue *y = JS_CreateNumber(ctx, 2, nullptr);
  JSValue *args[] = {x, y};
  JSValue *rv = JS_Call(ctx, fn, nullptr, 2, args);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_NUMBER);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, rv), 3.0);

  JS_FreeValue(ctx, x);
  JS_FreeValue(ctx, y);
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, fn);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 空参数调用：argc=0 不崩，返回 0。 */
TEST(Function, CallWithNoArgs) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *fn = JS_NewCFunction(ctx, sumFn, nullptr);
  ASSERT_NE(fn, nullptr);
  JSValue *rv = JS_Call(ctx, fn, nullptr, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_NUMBER);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, rv), 0.0);

  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, fn);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* this 绑定（§10.2.5.6：非严格 null/undefined -> globalObject）              */
/* -------------------------------------------------------------------------- */

TEST(Function, ThisBindsToGlobalWhenNull) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *fn = JS_NewCFunction(ctx, thisKindFn, nullptr);
  ASSERT_NE(fn, nullptr);

  /* this=null（非严格模式）-> globalObject（kind=Object） */
  JSValue *rv = JS_Call(ctx, fn, nullptr, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, rv), "object");

  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, fn);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 显式传 this=对象：原样透传（不替换为 global）。 */
TEST(Function, ExplicitThisObjectPassedThrough) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *fn = JS_NewCFunction(ctx, thisKindFn, nullptr);
  JSValue *obj = JS_CreateObject(ctx, nullptr, nullptr);
  ASSERT_NE(fn, nullptr);
  ASSERT_NE(obj, nullptr);

  JSValue *rv = JS_Call(ctx, fn, obj, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, rv), "object");

  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, obj);
  JS_FreeValue(ctx, fn);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 错误路径                                                                    */
/* -------------------------------------------------------------------------- */

/* 非函数调用 -> TypeError（kind=JS_EXCEPTION，#4）。 */
TEST(Function, CallNonFunctionReturnsTypeError) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *num = JS_CreateNumber(ctx, 42, nullptr);
  ASSERT_NE(num, nullptr);

  JSValue *rv = JS_Call(ctx, num, nullptr, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_EXCEPTION);

  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, num);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 回调抛 C++ 异常 -> 错误值（#3：不跨 C ABI 边界）。 */
TEST(Function, HostExceptionBecomesErrorValue) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *fn = JS_NewCFunction(ctx, throwFn, nullptr);
  ASSERT_NE(fn, nullptr);
  JSValue *rv = JS_Call(ctx, fn, nullptr, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_EXCEPTION);

  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, fn);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 空函数指针（JS_NewCFunction 传 null）-> 调用返回 undefined 占位。 */
TEST(Function, NullFunctionPointerCallsUndefined) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *fn = JS_NewCFunction(ctx, nullptr, nullptr);
  ASSERT_NE(fn, nullptr);
  JSValue *rv = JS_Call(ctx, fn, nullptr, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_UNDEFINED);

  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, fn);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 具名函数绑定 + GC 存活                                                    */
/* -------------------------------------------------------------------------- */

/* 具名函数绑定到当前作用域，句柄释放后值仍经绑定存活（无孤立值原则）。 */
TEST(Function, NamedFunctionSurvivesHandleFree) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *fn = JS_NewCFunction(ctx, sumFn, "sum");
  ASSERT_NE(fn, nullptr);
  JS_FreeValue(ctx, fn); /* 释放句柄，值由作用域绑定持有 */

  JSValue *loaded = JS_LoadValue(ctx, "sum");
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, loaded), JS_OBJECT);

  /* 经绑定加载的函数仍可调用 */
  JSValue *x = JS_CreateNumber(ctx, 5, nullptr);
  JSValue *y = JS_CreateNumber(ctx, 7, nullptr);
  JSValue *args[] = {x, y};
  JSValue *rv = JS_Call(ctx, loaded, nullptr, 2, args);
  ASSERT_NE(rv, nullptr);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, rv), 12.0);

  JS_FreeValue(ctx, x);
  JS_FreeValue(ctx, y);
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, loaded);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 函数值在 GC 后仍可调用（函数对象经句柄/绑定可达，G2）。 */
TEST(Function, FunctionSurvivesGc) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *fn = JS_NewCFunction(ctx, sumFn, nullptr);
  ASSERT_NE(fn, nullptr);

  /* 触发多次 GC：函数值必须存活（宿主句柄持有，根集） */
  JS_GC(ctx);
  JS_GC(ctx);

  JSValue *x = JS_CreateNumber(ctx, 10, nullptr);
  JSValue *y = JS_CreateNumber(ctx, 32, nullptr);
  JSValue *args[] = {x, y};
  JSValue *rv = JS_Call(ctx, fn, nullptr, 2, args);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_NUMBER);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, rv), 42.0);

  JS_FreeValue(ctx, x);
  JS_FreeValue(ctx, y);
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, fn);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 参数值在回调执行期间受临时句柄保护（防 GC 回收）。
 * 触发 GC 后参数仍可被回调读取。 */
TEST(Function, ArgsProtectedDuringCall) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *fn = JS_NewCFunction(ctx, sumFn, nullptr);
  ASSERT_NE(fn, nullptr);

  JSValue *x = JS_CreateNumber(ctx, 3, nullptr);
  JSValue *y = JS_CreateNumber(ctx, 4, nullptr);
  JSValue *args[] = {x, y};

  JS_GC(ctx); /* 调用前 GC：x/y 由宿主句柄持有，存活 */
  JSValue *rv = JS_Call(ctx, fn, nullptr, 2, args);
  ASSERT_NE(rv, nullptr);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, rv), 7.0);

  JS_FreeValue(ctx, x);
  JS_FreeValue(ctx, y);
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, fn);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
