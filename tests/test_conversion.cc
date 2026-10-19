/*
 * E2 类型转换抽象运算单元测试（docs/architecture.md §5.5 + ECMA-262 §7.1）。
 * 验证：
 *   1. ToBoolean 全表（§7.1.2）
 *   2. ToNumber 各类型（表 7）+ Symbol -> TypeError
 *   3. ToString 各类型（表 8）+ Number::toString 最短表示（§6.1.6.1.20）
 *   4. StringToNumber（§7.1.4.1）：空白/±Infinity/十六进制/指数/非法 -> NaN
 *   5. SameValue / SameValueZero（§7.2.10/11）：±0 与 NaN 语义
 *   6. Symbol 创建、唯一性、description
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

#include "neojs/neojs.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/value.h"

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
/* ToBoolean（§7.1.2）：falsey 全集                                    */
/* -------------------------------------------------------------------------- */

TEST(Conversion, ToBooleanTruthiness) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* falsey：undefined / null / false / ±0 / NaN / 空串 */
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateUndefined(ctx, nullptr)), 0);
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateNull(ctx, nullptr)), 0);
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateBoolean(ctx, 0, nullptr)), 0);
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateNumber(ctx, 0.0, nullptr)), 0);
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateNumber(ctx, -0.0, nullptr)), 0);
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateNumber(ctx, NAN, nullptr)), 0);
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateString(ctx, "", 0, nullptr)), 0);

  /* truthy：true / 非零数字 / 非空串 / 对象 / Symbol */
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateBoolean(ctx, 1, nullptr)), 1);
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateNumber(ctx, 1.5, nullptr)), 1);
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateNumber(ctx, -0.1, nullptr)), 1);
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateString(ctx, "x", 1, nullptr)), 1);
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateObject(ctx, nullptr, nullptr)), 1);
  EXPECT_EQ(JS_ToBoolean(ctx, JS_CreateSymbol(ctx, "s", nullptr)), 1);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* ToNumber（§7.1.4 表 7）                                                    */
/* -------------------------------------------------------------------------- */

TEST(Conversion, ToNumberPrimitives) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* undefined -> NaN */
  EXPECT_TRUE(std::isnan(JS_ToNumber(ctx, JS_CreateUndefined(ctx, nullptr))));
  /* null -> +0 */
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateNull(ctx, nullptr)), 0.0);
  /* boolean -> 1/0 */
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateBoolean(ctx, 1, nullptr)), 1.0);
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateBoolean(ctx, 0, nullptr)), 0.0);
  /* number -> 原值 */
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateNumber(ctx, -3.14, nullptr)), -3.14);
  /* string -> 解析 */
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateString(ctx, "42", 2, nullptr)), 42.0);
EXPECT_EQ(JS_ToNumber(ctx, JS_CreateString(ctx, "  \t 7  ", std::strlen("  \t 7  "), nullptr)), 7.0);
  EXPECT_TRUE(std::isnan(JS_ToNumber(ctx, JS_CreateString(ctx, "abc", 3, nullptr))));
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateString(ctx, "", 0, nullptr)), 0.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Conversion, ToNumberStringEdgeCases) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* ±Infinity */
  EXPECT_TRUE(std::isinf(JS_ToNumber(ctx, JS_CreateString(ctx, "Infinity", 8, nullptr))));
  EXPECT_TRUE(std::isinf(JS_ToNumber(ctx, JS_CreateString(ctx, "-Infinity", 9, nullptr))));
  /* 十六进制 */
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateString(ctx, "0x1F", 4, nullptr)), 31.0);
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateString(ctx, "0X10", 4, nullptr)), 16.0);
  /* 指数 */
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateString(ctx, "1e3", 3, nullptr)), 1000.0);
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateString(ctx, "1.5e-2", 6, nullptr)), 0.015);
  /* 小数 */
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateString(ctx, ".5", 2, nullptr)), 0.5);
  EXPECT_EQ(JS_ToNumber(ctx, JS_CreateString(ctx, "3.", 2, nullptr)), 3.0);
  /* 非法 -> NaN（十六进制残留、无数字前缀） */
  EXPECT_TRUE(std::isnan(JS_ToNumber(ctx, JS_CreateString(ctx, "0x", 2, nullptr))));
  EXPECT_TRUE(std::isnan(JS_ToNumber(ctx, JS_CreateString(ctx, "0xG", 3, nullptr))));
  EXPECT_TRUE(std::isnan(JS_ToNumber(ctx, JS_CreateString(ctx, ".", 1, nullptr))));
  EXPECT_TRUE(std::isnan(JS_ToNumber(ctx, JS_CreateString(ctx, "1 2", 3, nullptr))));

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Conversion, ToNumberSymbolThrows) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* Symbol 转数字必须报 TypeError（§7.1.4 表 7 步骤 7）。 */
  JSValue *sym = JS_CreateSymbol(ctx, "s", nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSValue *vs = c->handles().get(static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(sym)));
  ASSERT_NE(vs, nullptr);
  neojs::ConversionResult r = neojs::toNumber(c, vs);
  EXPECT_FALSE(r.ok());
  EXPECT_NE(r.error.find("TypeError"), std::string::npos);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* ToString（§7.1.17 表 8）+ Number::toString（§6.1.6.1.20）                  */
/* -------------------------------------------------------------------------- */

TEST(Conversion, ToStringPrimitives) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  EXPECT_STREQ(JS_ToString(ctx, JS_CreateUndefined(ctx, nullptr)), "undefined");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNull(ctx, nullptr)), "null");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateBoolean(ctx, 1, nullptr)), "true");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateBoolean(ctx, 0, nullptr)), "false");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateString(ctx, "hi", 2, nullptr)), "hi");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Conversion, NumberToStringShortest) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 整数 */
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, 42.0, nullptr)), "42");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, -7.0, nullptr)), "-7");
  /* 小数 */
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, 1.5, nullptr)), "1.5");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, 0.5, nullptr)), "0.5");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, 0.125, nullptr)), "0.125");
  /* 特殊值 */
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, NAN, nullptr)), "NaN");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, INFINITY, nullptr)), "Infinity");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, -INFINITY, nullptr)), "-Infinity");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, 0.0, nullptr)), "0");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, -0.0, nullptr)), "0");
  /* 指数记数法 */
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, 1e21, nullptr)), "1e+21");
  EXPECT_STREQ(JS_ToString(ctx, JS_CreateNumber(ctx, 1e-7, nullptr)), "1e-7");
  /* 大数：IEEE 754 实际值（double 字面量 12345678901234567890.0 的精确二进制
   * 值是 12345678901234567168，规范要求输出该精确值）。 */
  EXPECT_STREQ(
      JS_ToString(ctx, JS_CreateNumber(ctx, 12345678901234567890.0, nullptr)),
      "12345678901234567168");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Conversion, ToStringSymbolThrows) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *sym = JS_CreateSymbol(ctx, "s", nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSValue *vs = c->handles().get(static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(sym)));
  ASSERT_NE(vs, nullptr);
  neojs::ConversionResult r = neojs::toString(c, vs);
  EXPECT_FALSE(r.ok());
  EXPECT_NE(r.error.find("TypeError"), std::string::npos);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* ToPrimitive / ToPropertyKey（§7.1.1 / §7.1.19）                            */
/* -------------------------------------------------------------------------- */

TEST(Conversion, ToPrimitivePrimitiveIdentity) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 原始值 ToPrimitive 返回自身（同一内部指针，无拷贝）。 */
  neojs::JSValue *num = c->createValue(42.0);
  neojs::ConversionResult r = neojs::toPrimitive(c, num, neojs::Hint::Default);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value, num); /* 同一指针 */

  neojs::JSValue *str = c->createValue(std::string("x"));
  r = neojs::toPrimitive(c, str, neojs::Hint::String);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value, str);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Conversion, ToPropertyKeySemantics) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 字符串属性键：原样返回。 */
  neojs::JSValue *key = c->createValue(std::string("foo"));
  neojs::ConversionResult r = neojs::toPropertyKey(c, key);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::String);
  EXPECT_EQ(r.value->toString(), "foo");

  /* 数字属性键：转为字符串（"42"）。 */
  neojs::JSValue *num = c->createValue(42.0);
  r = neojs::toPropertyKey(c, num);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::String);
  EXPECT_EQ(r.value->toString(), "42");

  /* Symbol 属性键：保留为 Symbol（不转字符串）。 */
  JSValue *symh = JS_CreateSymbol(ctx, "desc", nullptr);
  neojs::JSValue *sym = c->handles().get(static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(symh)));
  ASSERT_NE(sym, nullptr);
  r = neojs::toPropertyKey(c, sym);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::Symbol);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* SameValue / SameValueZero（§7.2.10/11）                                    */
/* -------------------------------------------------------------------------- */

TEST(Conversion, SameValueZeroSemantics) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  neojs::JSValue *zero = c->createValue(0.0);
  neojs::JSValue *nzero = c->createValue(-0.0);
  neojs::JSValue *nan1 = c->createValue(NAN);
  neojs::JSValue *nan2 = c->createValue(NAN);
  neojs::JSValue *one = c->createValue(1.0);
  neojs::JSValue *strA = c->createValue(std::string("a"));
  neojs::JSValue *strB = c->createValue(std::string("a"));

  /* SameValueZero：+0 == -0（与 SameValue 的区别） */
  EXPECT_TRUE(neojs::sameValueZero(zero, nzero));
  EXPECT_FALSE(neojs::sameValue(zero, nzero));
  /* NaN == NaN */
  EXPECT_TRUE(neojs::sameValueZero(nan1, nan2));
  EXPECT_TRUE(neojs::sameValue(nan1, nan2));
  /* 不同类型不等 */
  EXPECT_FALSE(neojs::sameValueZero(one, strA));
  /* 字符串按值 */
  EXPECT_TRUE(neojs::sameValueZero(strA, strB));
  /* 不同值 */
  EXPECT_FALSE(neojs::sameValueZero(one, c->createValue(2.0)));

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* Symbol：创建 / 唯一性 / description                                         */
/* -------------------------------------------------------------------------- */

TEST(Conversion, SymbolCreateAndUniqueness) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  JSValue *s1 = JS_CreateSymbol(ctx, "desc", nullptr);
  JSValue *s2 = JS_CreateSymbol(ctx, "desc", nullptr);
  ASSERT_NE(s1, nullptr);
  ASSERT_NE(s2, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, s1), JS_SYMBOL);
  EXPECT_EQ(JS_GetKind(ctx, s2), JS_SYMBOL);

  /* 相同 description 的 Symbol 不相等（每次创建唯一）。 */
  neojs::JSValue *v1 = c->handles().get(static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(s1)));
  neojs::JSValue *v2 = c->handles().get(static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(s2)));
  ASSERT_NE(v1, nullptr);
  ASSERT_NE(v2, nullptr);
  EXPECT_FALSE(neojs::sameValue(v1, v2));
  EXPECT_EQ(v1->symbolDescription(), "desc");
  EXPECT_EQ(v2->symbolDescription(), "desc");

  /* 同一句柄两次解引用同一内部值（SameValue 自反）。 */
  EXPECT_TRUE(neojs::sameValue(v1, v1));

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
