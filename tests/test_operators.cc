/*
 * E4-3 运算符语义单元测试（ECMA-262 §7.2/§7.3/§13）。
 * 验证：
 *   1. 严格相等 ===（§7.2.14）：类型不同 false；NaN!=NaN；+0==-0
 *   2. 抽象相等 ==（§7.2.15）：null==undefined；Number/String/Boolean 转换
 *   3. 关系比较 <（§7.2.13）：数值/字符串码点/NaN->false
 *   4. 加法 +（§13.15.2）：数值相加 / 字符串拼接
 *   5. typeof（§13.5.3）：八种类型 + 可调用对象 -> "function"
 *   6. instanceof（§7.3.22）：原型链查找 + 非可调用 RHS -> TypeError
 *   7. in（§7.3.20）：HasProperty + 非对象 RHS -> TypeError
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

#include "neojs/neojs.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/operators.h"
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

/* C 句柄 -> 内部值指针（取 global 属性等 C API 返回的句柄时用）。 */
neojs::JSValue *unh(neojs::JSContext *c, JSValue *h) {
  return c->handles().get(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(h)));
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 严格相等 ===（§7.2.14）                                                    */
/* -------------------------------------------------------------------------- */

TEST(Operators, StrictEquals) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 类型不同 -> false */
  neojs::ConversionResult r =
      neojs::strictEquals(c, c->createValue(1.0),
                          c->createValue(std::string("1")));
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.value->toBoolean());

  /* 同类型值相等 */
  r = neojs::strictEquals(c, c->createValue(1.0), c->createValue(1.0));
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());
  r = neojs::strictEquals(c, c->createValue(std::string("a")),
                          c->createValue(std::string("a")));
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());

  /* NaN !== NaN */
  r = neojs::strictEquals(c, c->createValue(NAN), c->createValue(NAN));
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.value->toBoolean());

  /* +0 === -0 */
  r = neojs::strictEquals(c, c->createValue(0.0), c->createValue(-0.0));
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());

  /* null === null / undefined === undefined / null !== undefined */
  r = neojs::strictEquals(c, c->createValue(neojs::Kind::Null),
                          c->createValue(neojs::Kind::Null));
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());
  r = neojs::strictEquals(c, c->createValue(neojs::Kind::Null),
                          c->createValue(neojs::Kind::Undefined));
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.value->toBoolean());

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 抽象相等 ==（§7.2.15）                                                     */
/* -------------------------------------------------------------------------- */

TEST(Operators, AbstractEquals) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* null == undefined */
  neojs::ConversionResult r =
      neojs::abstractEquals(c, c->createValue(neojs::Kind::Null),
                            c->createValue(neojs::Kind::Undefined));
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());

  /* 数值与字符串 */
  r = neojs::abstractEquals(c, c->createValue(1.0),
                            c->createValue(std::string("1")));
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());
  r = neojs::abstractEquals(c, c->createValue(1.0),
                            c->createValue(std::string("2")));
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.value->toBoolean());

  /* Boolean 与数字：true == 1 */
  r = neojs::abstractEquals(c, c->createValue(true), c->createValue(1.0));
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());

  /* 不同类型不同值 */
  r = neojs::abstractEquals(c, c->createValue(std::string("a")),
                            c->createValue(1.0));
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.value->toBoolean());

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 关系比较 <（§7.2.13）                                                      */
/* -------------------------------------------------------------------------- */

TEST(Operators, IsLessThan) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 数值比较 */
  neojs::ConversionResult r =
      neojs::isLessThan(c, c->createValue(1.0), c->createValue(2.0));
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());
  r = neojs::isLessThan(c, c->createValue(2.0), c->createValue(1.0));
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.value->toBoolean());

  /* 字符串按码点 */
  r = neojs::isLessThan(c, c->createValue(std::string("abc")),
                        c->createValue(std::string("abd")));
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());

  /* NaN 参与 -> false */
  r = neojs::isLessThan(c, c->createValue(NAN), c->createValue(1.0));
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.value->toBoolean());

  /* 数字与字符串混合：字符串转数字 */
  r = neojs::isLessThan(c, c->createValue(2.0),
                        c->createValue(std::string("10")));
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 加法 +（§13.15.2）                                                          */
/* -------------------------------------------------------------------------- */

TEST(Operators, Add) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 数值相加 */
  neojs::ConversionResult r =
      neojs::add(c, c->createValue(1.5), c->createValue(2.5));
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::Number);
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 4.0);

  /* 字符串拼接 */
  r = neojs::add(c, c->createValue(std::string("foo")),
                 c->createValue(std::string("bar")));
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::String);
  EXPECT_EQ(r.value->toString(), "foobar");

  /* 数字 + 字符串 -> 拼接 */
  r = neojs::add(c, c->createValue(42.0), c->createValue(std::string("px")));
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::String);
  EXPECT_EQ(r.value->toString(), "42px");

  /* 字符串 + 数字 -> 拼接 */
  r = neojs::add(c, c->createValue(std::string("n=")), c->createValue(7.0));
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->toString(), "n=7");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* typeof（§13.5.3）                                                           */
/* -------------------------------------------------------------------------- */

TEST(Operators, TypeOf) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 六种原始类型 + null -> "object"（历史遗留）。 */
  EXPECT_EQ(neojs::typeOf(c, c->createValue(neojs::Kind::Undefined))
                .value->toString(),
            "undefined");
  EXPECT_EQ(neojs::typeOf(c, c->createValue(neojs::Kind::Null)).value->toString(),
            "object");
  EXPECT_EQ(neojs::typeOf(c, c->createValue(true)).value->toString(), "boolean");
  EXPECT_EQ(neojs::typeOf(c, c->createValue(1.0)).value->toString(), "number");
  EXPECT_EQ(neojs::typeOf(c, c->createValue(std::string("x"))).value->toString(),
            "string");
  EXPECT_EQ(
      neojs::typeOf(c, c->createValue(neojs::Kind::Symbol)).value->toString(),
      "symbol");

  /* 普通对象 -> "object"。 */
  neojs::JSValue *obj = c->createObject(nullptr);
  EXPECT_EQ(neojs::typeOf(c, obj).value->toString(), "object");

  /* 可调用对象 -> "function"（global 注册的内置函数）。 */
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JSValue *symbolCtor = JS_GetProperty(ctx, global, "Symbol");
  ASSERT_NE(symbolCtor, nullptr);
  neojs::JSValue *fn = unh(c, symbolCtor);
  EXPECT_EQ(neojs::typeOf(c, fn).value->toString(), "function");

  JS_FreeValue(ctx, symbolCtor);
  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* instanceof（§7.3.22）                                                       */
/* -------------------------------------------------------------------------- */

TEST(Operators, InstanceOf) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);

  /* Number 构造器：普通对象不在其原型链上 -> false。 */
  JSValue *numCtor = JS_GetProperty(ctx, global, "Number");
  ASSERT_NE(numCtor, nullptr);
  neojs::JSValue *obj = c->createObject(nullptr);
  neojs::ConversionResult r = neojs::instanceOf(c, obj, unh(c, numCtor));
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::Boolean);
  EXPECT_FALSE(r.value->toBoolean());

  /* LHS 非对象 -> false。 */
  r = neojs::instanceOf(c, c->createValue(1.0), unh(c, numCtor));
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.value->toBoolean());

  /* RHS 非可调用 -> TypeError。 */
  r = neojs::instanceOf(c, obj, c->createValue(1.0));
  ASSERT_FALSE(r.ok());
  EXPECT_NE(r.error.find("TypeError"), std::string::npos);

  /* RHS 无 prototype 属性 -> TypeError。 */
  neojs::JSValue *plainFn = c->createObject(nullptr);
  r = neojs::instanceOf(c, obj, plainFn);
  ASSERT_FALSE(r.ok());

  JS_FreeValue(ctx, numCtor);
  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* in 运算符（§7.3.20 HasProperty）                                            */
/* -------------------------------------------------------------------------- */

TEST(Operators, InOperator) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 对象有属性 -> true；无 -> false。 */
  neojs::JSValue *obj = c->createObject(nullptr);
  obj->object()->setProperty("foo", c->createValue(1.0));
  neojs::ConversionResult r =
      neojs::inOperator(c, c->createValue(std::string("foo")), obj);
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());

  r = neojs::inOperator(c, c->createValue(std::string("bar")), obj);
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.value->toBoolean());

  /* RHS 非对象 -> TypeError。 */
  r = neojs::inOperator(c, c->createValue(std::string("x")), c->createValue(1.0));
  ASSERT_FALSE(r.ok());
  EXPECT_NE(r.error.find("TypeError"), std::string::npos);

  /* 原型链查找。 */
  neojs::JSValue *child = c->createObject(obj->object());
  r = neojs::inOperator(c, c->createValue(std::string("foo")), child);
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* E4-4 C API 暴露（JS_StrictEqual / JS_Equal / ... 纯 C 层验证）             */
/* -------------------------------------------------------------------------- */

TEST(Operators, CAPIStrictEqual) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *n1 = JS_CreateNumber(ctx, 1, nullptr);
  JSValue *n1b = JS_CreateNumber(ctx, 1, nullptr);
  JSValue *s1 = JS_CreateString(ctx, "1", 1, nullptr);
  ASSERT_NE(n1, nullptr);
  ASSERT_NE(n1b, nullptr);
  ASSERT_NE(s1, nullptr);

  /* 1 === 1 -> 1；1 === "1" -> 0 */
  EXPECT_EQ(JS_StrictEqual(ctx, n1, n1b), 1);
  EXPECT_EQ(JS_StrictEqual(ctx, n1, s1), 0);

  JS_FreeValue(ctx, n1);
  JS_FreeValue(ctx, n1b);
  JS_FreeValue(ctx, s1);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Operators, CAPIEqualAndAdd) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *n1 = JS_CreateNumber(ctx, 1, nullptr);
  JSValue *s1 = JS_CreateString(ctx, "1", 1, nullptr);
  ASSERT_NE(n1, nullptr);
  ASSERT_NE(s1, nullptr);

  /* 1 == "1" -> true */
  JSValue *rv = JS_Equal(ctx, n1, s1);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_BOOLEAN);
  EXPECT_EQ(JS_ToBoolean(ctx, rv), 1);
  JS_FreeValue(ctx, rv);

  /* 1 + "px" -> "1px" */
  JSValue *px = JS_CreateString(ctx, "px", 2, nullptr);
  rv = JS_Add(ctx, n1, px);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, rv), "1px");
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, px);

  JS_FreeValue(ctx, n1);
  JS_FreeValue(ctx, s1);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Operators, CAPIRelationalAndTypeof) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *n2 = JS_CreateNumber(ctx, 2, nullptr);
  JSValue *n10 = JS_CreateNumber(ctx, 10, nullptr);
  ASSERT_NE(n2, nullptr);
  ASSERT_NE(n10, nullptr);

  /* 2 < 10 -> true */
  JSValue *rv = JS_RelationalCompare(ctx, n2, n10);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_BOOLEAN);
  EXPECT_EQ(JS_ToBoolean(ctx, rv), 1);
  JS_FreeValue(ctx, rv);

  /* typeof 2 -> "number" */
  rv = JS_Typeof(ctx, n2);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, rv), "number");
  JS_FreeValue(ctx, rv);

  JS_FreeValue(ctx, n2);
  JS_FreeValue(ctx, n10);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Operators, CAPIInstanceofAndIn) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JSValue *numCtor = JS_GetProperty(ctx, global, "Number");
  ASSERT_NE(numCtor, nullptr);

  /* 普通对象 instanceof Number -> false（非构造产物）。 */
  JSValue *obj = JS_CreateObject(ctx, nullptr, nullptr);
  JSValue *rv = JS_Instanceof(ctx, obj, numCtor);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_BOOLEAN);
  EXPECT_EQ(JS_ToBoolean(ctx, rv), 0);
  JS_FreeValue(ctx, rv);

  /* RHS 非可调用 -> Exception。 */
  rv = JS_Instanceof(ctx, obj, obj);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_EXCEPTION);
  JS_FreeValue(ctx, rv);

  /* obj 加属性后 in 检查。 */
  JSValue *one = JS_CreateNumber(ctx, 1, nullptr);
  JS_SetProperty(ctx, obj, "foo", one);
  JSValue *key = JS_CreateString(ctx, "foo", 3, nullptr);
  rv = JS_In(ctx, key, obj);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_BOOLEAN);
  EXPECT_EQ(JS_ToBoolean(ctx, rv), 1);
  JS_FreeValue(ctx, rv);

  /* RHS 非对象 -> Exception。 */
  rv = JS_In(ctx, key, one);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_EXCEPTION);
  JS_FreeValue(ctx, rv);

  JS_FreeValue(ctx, key);
  JS_FreeValue(ctx, one);
  JS_FreeValue(ctx, obj);
  JS_FreeValue(ctx, numCtor);
  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
