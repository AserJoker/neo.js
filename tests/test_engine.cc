/*
 * engine 层里程碑测试（M3：基于 Context 的 value 运算模块）。
 *
 * 架构（与 vm/编译器解耦）：
 *   - engine 提供几乎完整语言特性，核心 = Context + Value。
 *   - 所有运算首参 Context&，操作/返回 core::Value*。
 *   - 运算结果挂 ctx 内部 scope（scope_ref=1），由 ctx/GC 管理。
 *   - 错误返回 ValueKind::Exception 的 value（不抛 C++ 异常）。
 *
 * M3 覆盖：创建 + 类型转换 + 一元/二元运算 + 比较 + 逻辑 + 异常通道。
 */

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "neojs/engine/engine.h"

namespace {

using neojs::core::BooleanData;
using neojs::core::Context;
using neojs::core::NumberData;
using neojs::core::StringData;
using neojs::core::Value;
using neojs::core::ValueKind;

namespace eng = neojs::engine;

/* 取数值 */
double numOf(const Value *v) {
  EXPECT_EQ(v->kind, ValueKind::Number);
  return static_cast<const NumberData *>(v->data)->number;
}

/* 取字符串 */
std::string strOf(const Value *v) {
  EXPECT_EQ(v->kind, ValueKind::String);
  return static_cast<const StringData *>(v->data)->str;
}

/* 取布尔 */
bool boolOf(const Value *v) {
  EXPECT_EQ(v->kind, ValueKind::Boolean);
  return static_cast<const BooleanData *>(v->data)->value;
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 创建：结果挂 ctx 内部 scope（scope_ref=1）                                  */
/* -------------------------------------------------------------------------- */

TEST(Engine, CreateBasics) {
  Context ctx;
  Value *n = eng::create_number(ctx, 42.5);
  Value *b = eng::create_boolean(ctx, true);
  Value *s = eng::create_string(ctx, "hi");
  Value *u = eng::create_undefined(ctx);
  Value *nu = eng::create_null(ctx);
  Value *ui = eng::create_uninitialized(ctx);

  ASSERT_NE(n, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(s, nullptr);
  ASSERT_NE(u, nullptr);
  ASSERT_NE(nu, nullptr);
  ASSERT_NE(ui, nullptr);

  EXPECT_EQ(n->kind, ValueKind::Number);
  EXPECT_EQ(numOf(n), 42.5);
  EXPECT_EQ(b->kind, ValueKind::Boolean);
  EXPECT_TRUE(boolOf(b));
  EXPECT_EQ(s->kind, ValueKind::String);
  EXPECT_EQ(strOf(s), "hi");
  EXPECT_EQ(u->kind, ValueKind::Undefined);
  EXPECT_EQ(nu->kind, ValueKind::Null);
  EXPECT_EQ(ui->kind, ValueKind::Uninitialized);

  /* 全部挂到 root_scope（scope_ref=1） */
  EXPECT_EQ(n->scope_ref, 1);
  EXPECT_EQ(b->scope_ref, 1);
  EXPECT_EQ(s->scope_ref, 1);
  EXPECT_EQ(u->scope_ref, 1);
  EXPECT_EQ(nu->scope_ref, 1);
  EXPECT_EQ(ui->scope_ref, 1);
}

/* -------------------------------------------------------------------------- */
/* 类型转换                                                                   */
/* -------------------------------------------------------------------------- */

TEST(Engine, ToNumber) {
  Context ctx;
  EXPECT_EQ(numOf(eng::to_number(ctx, eng::create_number(ctx, 3.5))), 3.5);
  EXPECT_EQ(numOf(eng::to_number(ctx, eng::create_boolean(ctx, true))), 1.0);
  EXPECT_EQ(numOf(eng::to_number(ctx, eng::create_boolean(ctx, false))), 0.0);
  EXPECT_EQ(numOf(eng::to_number(ctx, eng::create_null(ctx))), 0.0);
  EXPECT_TRUE(std::isnan(numOf(eng::to_number(ctx, eng::create_undefined(ctx)))));
  EXPECT_EQ(numOf(eng::to_number(ctx, eng::create_string(ctx, "  42  "))), 42.0);
  EXPECT_EQ(numOf(eng::to_number(ctx, eng::create_string(ctx, ""))), 0.0);
  EXPECT_TRUE(std::isnan(numOf(eng::to_number(ctx, eng::create_string(ctx, "abc")))));
}

TEST(Engine, ToBoolean) {
  Context ctx;
  EXPECT_FALSE(boolOf(eng::to_boolean(ctx, eng::create_number(ctx, 0.0))));
  EXPECT_FALSE(boolOf(eng::to_boolean(ctx, eng::create_number(ctx, -0.0))));
  EXPECT_FALSE(boolOf(eng::to_boolean(ctx, eng::create_number(ctx, std::nan("")))));
  EXPECT_TRUE(boolOf(eng::to_boolean(ctx, eng::create_number(ctx, 1.0))));
  EXPECT_TRUE(boolOf(eng::to_boolean(ctx, eng::create_number(ctx, -5.0))));
  EXPECT_FALSE(boolOf(eng::to_boolean(ctx, eng::create_string(ctx, ""))));
  EXPECT_TRUE(boolOf(eng::to_boolean(ctx, eng::create_string(ctx, "x"))));
  EXPECT_FALSE(boolOf(eng::to_boolean(ctx, eng::create_null(ctx))));
  EXPECT_FALSE(boolOf(eng::to_boolean(ctx, eng::create_undefined(ctx))));
  EXPECT_TRUE(boolOf(eng::to_boolean(ctx, eng::create_boolean(ctx, true))));
}

TEST(Engine, ToString) {
  Context ctx;
  EXPECT_EQ(strOf(eng::to_string(ctx, eng::create_string(ctx, "abc"))), "abc");
  EXPECT_EQ(strOf(eng::to_string(ctx, eng::create_number(ctx, 42.0))), "42");
  EXPECT_EQ(strOf(eng::to_string(ctx, eng::create_number(ctx, 3.14))), "3.1400000000000001");
  EXPECT_EQ(strOf(eng::to_string(ctx, eng::create_number(ctx, -0.0))), "0");
  EXPECT_EQ(strOf(eng::to_string(ctx, eng::create_boolean(ctx, true))), "true");
  EXPECT_EQ(strOf(eng::to_string(ctx, eng::create_boolean(ctx, false))), "false");
  EXPECT_EQ(strOf(eng::to_string(ctx, eng::create_null(ctx))), "null");
  EXPECT_EQ(strOf(eng::to_string(ctx, eng::create_undefined(ctx))), "undefined");
}

/* -------------------------------------------------------------------------- */
/* 一元运算                                                                   */
/* -------------------------------------------------------------------------- */

TEST(Engine, Unary) {
  Context ctx;
  /* 一元 +：ToNumber */
  EXPECT_EQ(numOf(eng::unary_plus(ctx, eng::create_string(ctx, "7"))), 7.0);
  /* 一元 -：ToNumber 取负 */
  EXPECT_EQ(numOf(eng::unary_minus(ctx, eng::create_number(ctx, 5.0))), -5.0);
  EXPECT_EQ(numOf(eng::unary_minus(ctx, eng::create_boolean(ctx, true))), -1.0);
  /* ! */
  EXPECT_TRUE(boolOf(eng::logical_not(ctx, eng::create_number(ctx, 0.0))));
  EXPECT_FALSE(boolOf(eng::logical_not(ctx, eng::create_number(ctx, 3.0))));
  /* ~ */
  EXPECT_EQ(numOf(eng::bitwise_not(ctx, eng::create_number(ctx, 5.0))), -6.0);
  EXPECT_EQ(numOf(eng::bitwise_not(ctx, eng::create_number(ctx, -1.0))), 0.0);
  /* typeof */
  EXPECT_EQ(strOf(eng::typeof_value(ctx, eng::create_number(ctx, 1))), "number");
  EXPECT_EQ(strOf(eng::typeof_value(ctx, eng::create_string(ctx, "x"))), "string");
  EXPECT_EQ(strOf(eng::typeof_value(ctx, eng::create_boolean(ctx, true))), "boolean");
  EXPECT_EQ(strOf(eng::typeof_value(ctx, eng::create_undefined(ctx))), "undefined");
  EXPECT_EQ(strOf(eng::typeof_value(ctx, eng::create_null(ctx))), "object");
  /* void */
  EXPECT_EQ(eng::void_value(ctx, eng::create_number(ctx, 1))->kind, ValueKind::Undefined);
}

/* -------------------------------------------------------------------------- */
/* 二元运算                                                                   */
/* -------------------------------------------------------------------------- */

TEST(Engine, Arith) {
  Context ctx;
  EXPECT_EQ(numOf(eng::add(ctx, eng::create_number(ctx, 1), eng::create_number(ctx, 2))), 3.0);
  EXPECT_EQ(numOf(eng::sub(ctx, eng::create_number(ctx, 5), eng::create_number(ctx, 2))), 3.0);
  EXPECT_EQ(numOf(eng::mul(ctx, eng::create_number(ctx, 3), eng::create_number(ctx, 4))), 12.0);
  EXPECT_EQ(numOf(eng::div(ctx, eng::create_number(ctx, 7), eng::create_number(ctx, 2))), 3.5);
  EXPECT_EQ(numOf(eng::mod(ctx, eng::create_number(ctx, 7), eng::create_number(ctx, 3))), 1.0);
  EXPECT_EQ(numOf(eng::pow(ctx, eng::create_number(ctx, 2), eng::create_number(ctx, 10))), 1024.0);
  /* 字符串加法 → 拼接 */
  EXPECT_EQ(strOf(eng::add(ctx, eng::create_string(ctx, "a"), eng::create_string(ctx, "b"))), "ab");
  EXPECT_EQ(strOf(eng::add(ctx, eng::create_number(ctx, 1), eng::create_string(ctx, "b"))), "1b");
  /* 除零：1/0=Inf */
  EXPECT_TRUE(std::isinf(numOf(eng::div(ctx, eng::create_number(ctx, 1), eng::create_number(ctx, 0)))));
}

TEST(Engine, Bitwise) {
  Context ctx;
  EXPECT_EQ(numOf(eng::bit_and(ctx, eng::create_number(ctx, 6), eng::create_number(ctx, 3))), 2.0);
  EXPECT_EQ(numOf(eng::bit_or(ctx, eng::create_number(ctx, 4), eng::create_number(ctx, 1))), 5.0);
  EXPECT_EQ(numOf(eng::bit_xor(ctx, eng::create_number(ctx, 5), eng::create_number(ctx, 3))), 6.0);
  EXPECT_EQ(numOf(eng::lshift(ctx, eng::create_number(ctx, 1), eng::create_number(ctx, 4))), 16.0);
  EXPECT_EQ(numOf(eng::rshift(ctx, eng::create_number(ctx, -16), eng::create_number(ctx, 2))), -4.0);
  EXPECT_EQ(numOf(eng::urshift(ctx, eng::create_number(ctx, -1), eng::create_number(ctx, 1))), 2147483647.0);
}

/* -------------------------------------------------------------------------- */
/* 比较                                                                       */
/* -------------------------------------------------------------------------- */

TEST(Engine, Relational) {
  Context ctx;
  EXPECT_TRUE(boolOf(eng::lt(ctx, eng::create_number(ctx, 1), eng::create_number(ctx, 2))));
  EXPECT_FALSE(boolOf(eng::lt(ctx, eng::create_number(ctx, 2), eng::create_number(ctx, 1))));
  EXPECT_TRUE(boolOf(eng::le(ctx, eng::create_number(ctx, 2), eng::create_number(ctx, 2))));
  EXPECT_TRUE(boolOf(eng::gt(ctx, eng::create_number(ctx, 3), eng::create_number(ctx, 2))));
  EXPECT_TRUE(boolOf(eng::ge(ctx, eng::create_number(ctx, 3), eng::create_number(ctx, 3))));
  /* 字符串字典序 */
  EXPECT_TRUE(boolOf(eng::lt(ctx, eng::create_string(ctx, "a"), eng::create_string(ctx, "b"))));
  /* NaN 比较全 false */
  EXPECT_FALSE(boolOf(eng::lt(ctx, eng::create_number(ctx, std::nan("")), eng::create_number(ctx, 1))));
}

TEST(Engine, Equality) {
  Context ctx;
  EXPECT_TRUE(boolOf(eng::strict_eq(ctx, eng::create_number(ctx, 1), eng::create_number(ctx, 1))));
  EXPECT_FALSE(boolOf(eng::strict_eq(ctx, eng::create_number(ctx, 1), eng::create_string(ctx, "1"))));
  EXPECT_FALSE(boolOf(eng::strict_eq(ctx, eng::create_number(ctx, std::nan("")), eng::create_number(ctx, std::nan("")))));
  EXPECT_TRUE(boolOf(eng::eq(ctx, eng::create_number(ctx, 1), eng::create_string(ctx, "1"))));
  EXPECT_TRUE(boolOf(eng::eq(ctx, eng::create_boolean(ctx, true), eng::create_number(ctx, 1))));
  EXPECT_TRUE(boolOf(eng::eq(ctx, eng::create_null(ctx), eng::create_undefined(ctx))));
  EXPECT_FALSE(boolOf(eng::eq(ctx, eng::create_number(ctx, 1), eng::create_number(ctx, 2))));
  EXPECT_TRUE(boolOf(eng::ne(ctx, eng::create_number(ctx, 1), eng::create_number(ctx, 2))));
  EXPECT_TRUE(boolOf(eng::strict_ne(ctx, eng::create_number(ctx, 1), eng::create_string(ctx, "1"))));
}

/* -------------------------------------------------------------------------- */
/* 逻辑运算                                                                   */
/* -------------------------------------------------------------------------- */

TEST(Engine, Logical) {
  Context ctx;
  Value *zero = eng::create_number(ctx, 0);
  Value *one = eng::create_number(ctx, 1);
  Value *two = eng::create_number(ctx, 2);
  /* && */
  EXPECT_EQ(eng::logical_and(ctx, zero, two), zero);
  EXPECT_EQ(eng::logical_and(ctx, one, two), two);
  /* || */
  EXPECT_EQ(eng::logical_or(ctx, zero, two), two);
  EXPECT_EQ(eng::logical_or(ctx, one, two), one);
  /* ?? */
  Value *u = eng::create_undefined(ctx);
  Value *nul = eng::create_null(ctx);
  EXPECT_EQ(eng::nullish(ctx, u, two), two);
  EXPECT_EQ(eng::nullish(ctx, nul, two), two);
  EXPECT_EQ(eng::nullish(ctx, one, two), one);
}

/* -------------------------------------------------------------------------- */
/* 异常通道                                                                   */
/* -------------------------------------------------------------------------- */

TEST(Engine, ExceptionChannel) {
  Context ctx;
  Value *e = eng::create_exception(ctx, "boom");
  ASSERT_NE(e, nullptr);
  EXPECT_TRUE(eng::is_exception(e));
  EXPECT_EQ(e->kind, ValueKind::Exception);
  /* 异常消息存于 StringData（kind 仍是 Exception） */
  EXPECT_EQ(static_cast<StringData *>(e->data)->str, "boom");

  /* 正常值不是异常 */
  EXPECT_FALSE(eng::is_exception(eng::create_number(ctx, 1)));
}
