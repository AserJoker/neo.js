/*
 * C 对外接口（neojs.h）单元测试。
 * 验证规范 #2（命名/不透明指针）、#4（JSValue 一等公民 + kind）、
 * #5（内存管理/生命周期）、#9（一切从 ctx 出发）。
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "neojs/neojs.h"

/* -------------------------------------------------------------------------- */
/* 生命周期                                                                    */
/* -------------------------------------------------------------------------- */

TEST(CApi, NewAndFreeContext) {
  JSContext *ctx = JS_NewContext(nullptr, nullptr, nullptr);
  ASSERT_NE(ctx, nullptr);
  JS_FreeContext(ctx);
}

TEST(CApi, CustomAllocatorIsUsed) {
  static int alloc_calls = 0;
  static int free_calls = 0;
  alloc_calls = 0;
  free_calls = 0;

  auto my_alloc = [](size_t size, void *) -> void * {
    ++alloc_calls;
    return std::malloc(size);
  };
  auto my_free = [](void *ptr, void *) {
    ++free_calls;
    std::free(ptr);
  };

  JSContext *ctx = JS_NewContext(my_alloc, my_free, nullptr);
  ASSERT_NE(ctx, nullptr);
  ASSERT_GT(alloc_calls, 0);

  JSValue *v = JS_CreateNumber(ctx, 3.14);
  ASSERT_NE(v, nullptr);
  JS_FreeValue(ctx, v);

  JS_FreeContext(ctx);
  ASSERT_GT(free_calls, 0);
}

/* -------------------------------------------------------------------------- */
/* 值构造与 kind（#4）                                                         */
/* -------------------------------------------------------------------------- */

TEST(CApi, CreateValuesAndKinds) {
  JSContext *ctx = JS_NewContext(nullptr, nullptr, nullptr);
  ASSERT_NE(ctx, nullptr);

  JSValue *u = JS_CreateUndefined(ctx);
  ASSERT_NE(u, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, u), JS_UNDEFINED);

  JSValue *n = JS_CreateNull(ctx);
  ASSERT_NE(n, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, n), JS_NULL);

  JSValue *b = JS_CreateBoolean(ctx, 1);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, b), JS_BOOLEAN);
  EXPECT_EQ(JS_ToBoolean(ctx, b), 1);

  JSValue *num = JS_CreateNumber(ctx, 42.0);
  ASSERT_NE(num, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, num), JS_NUMBER);
  EXPECT_EQ(JS_ToNumber(ctx, num), 42.0);
  EXPECT_EQ(JS_ToInteger(ctx, num), 42);

  JS_FreeValue(ctx, num);
  JS_FreeValue(ctx, b);
  JS_FreeValue(ctx, n);
  JS_FreeValue(ctx, u);
  JS_FreeContext(ctx);
}

TEST(CApi, CreateString) {
  JSContext *ctx = JS_NewContext(nullptr, nullptr, nullptr);
  ASSERT_NE(ctx, nullptr);

  JSValue *s = JS_CreateString(ctx, "hello", 5);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, s), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, s), "hello");

  JS_FreeValue(ctx, s);
  JS_FreeContext(ctx);
}

/* -------------------------------------------------------------------------- */
/* 错误通道（#4：kind = JS_EXCEPTION）                                        */
/* -------------------------------------------------------------------------- */

TEST(CApi, EvalReturnsExceptionInM0) {
  JSContext *ctx = JS_NewContext(nullptr, nullptr, nullptr);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "1 + 1", 5);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION);

  JS_FreeValue(ctx, r);
  JS_FreeContext(ctx);
}

TEST(CApi, CreateError) {
  JSContext *ctx = JS_NewContext(nullptr, nullptr, nullptr);
  ASSERT_NE(ctx, nullptr);

  JSValue *err = JS_CreateError(ctx, "boom");
  ASSERT_NE(err, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, err), JS_EXCEPTION);

  JS_FreeValue(ctx, err);
  JS_FreeContext(ctx);
}

/* -------------------------------------------------------------------------- */
/* 空指针健壮性                                                                */
/* -------------------------------------------------------------------------- */

TEST(CApi, NullHandling) {
  JSContext *ctx = JS_NewContext(nullptr, nullptr, nullptr);
  ASSERT_NE(ctx, nullptr);

  EXPECT_EQ(JS_GetKind(ctx, nullptr), JS_UNDEFINED);
  EXPECT_EQ(JS_ToBoolean(ctx, nullptr), 0);
  EXPECT_EQ(JS_ToNumber(ctx, nullptr), 0.0);
  EXPECT_EQ(JS_ToInteger(ctx, nullptr), 0);
  EXPECT_EQ(JS_ToString(ctx, nullptr), nullptr);

  JS_FreeContext(ctx);
}
