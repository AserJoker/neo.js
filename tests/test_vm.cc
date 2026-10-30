/*
 * vm 层端到端测试（M4：源码 → AST → 字节码 → VM 执行）。
 *
 * 流水线：parseSource → AST → compile_expression → Vm.run。
 *
 * M4 范围：仅表达式（无函数调用/语句）。
 *   - 字面量 / 标识符（变量加载）
 *   - 一元/二元/比较/位运算
 *   - 逻辑短路（&&/||/??）与条件表达式
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>

#include "neojs/core/context.h"
#include "neojs/core/data.h"
#include "neojs/frontend/diagnostics.h"
#include "neojs/frontend/frontend.h"
#include "neojs/vm/compiler.h"
#include "neojs/vm/vm.h"

namespace {

using neojs::core::BooleanData;
using neojs::core::Context;
using neojs::core::NumberData;
using neojs::core::StringData;
using neojs::core::Value;
using neojs::core::ValueKind;
using neojs::DiagnosticSink;

namespace vm = neojs::vm;

/* 编译并执行单个表达式源码，返回结果 value。 */
Value *eval_expr(Context &ctx, const char *source) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource(source, std::strlen(source), sink);
  if (ast == nullptr) {
    ADD_FAILURE() << "parse failed: " << source;
    return nullptr;
  }
  /* Program → children[0] = ExprStmt → children[0] = 表达式 */
  neojs::ASTNode *expr = nullptr;
  if (ast->root->children.size() >= 1) {
    neojs::ASTNode *stmt = ast->root->children[0];
    if (stmt->children.size() >= 1) expr = stmt->children[0];
  }
  if (expr == nullptr) {
    ADD_FAILURE() << "no expression in: " << source;
    delete ast;
    return nullptr;
  }

  neojs::vm::Bytecode bc;
  if (!vm::compile_expression(expr, bc)) {
    ADD_FAILURE() << "compile failed: " << source;
    delete ast;
    return nullptr;
  }

  neojs::vm::Vm vm(ctx);
  Value *result = vm.run(bc);
  delete ast;
  return result;
}

/* 数值结果断言 */
double numOf(const Value *v) {
  EXPECT_EQ(v->kind, ValueKind::Number);
  return static_cast<const NumberData *>(v->data)->number;
}

/* 字符串结果断言 */
std::string strOf(const Value *v) {
  EXPECT_EQ(v->kind, ValueKind::String);
  return static_cast<const StringData *>(v->data)->str;
}

/* 布尔结果断言 */
bool boolOf(const Value *v) {
  EXPECT_EQ(v->kind, ValueKind::Boolean);
  return static_cast<const BooleanData *>(v->data)->value;
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 字面量                                                                     */
/* -------------------------------------------------------------------------- */

TEST(Vm, Literals) {
  Context ctx;
  EXPECT_EQ(numOf(eval_expr(ctx, "42")), 42.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "3.14")), 3.14);
  EXPECT_EQ(strOf(eval_expr(ctx, "\"hello\"")), "hello");
  EXPECT_TRUE(boolOf(eval_expr(ctx, "true")));
  EXPECT_FALSE(boolOf(eval_expr(ctx, "false")));
  EXPECT_EQ(eval_expr(ctx, "null")->kind, ValueKind::Null);
  EXPECT_EQ(eval_expr(ctx, "undefined")->kind, ValueKind::Undefined);
}

/* -------------------------------------------------------------------------- */
/* 变量加载                                                                   */
/* -------------------------------------------------------------------------- */

TEST(Vm, LoadVar) {
  Context ctx;
  ctx.define("x", ctx.create_number(10));
  ctx.define("s", ctx.create_string("abc"));
  EXPECT_EQ(numOf(eval_expr(ctx, "x")), 10.0);
  EXPECT_EQ(strOf(eval_expr(ctx, "s")), "abc");
}

TEST(Vm, LoadVarUndefined) {
  Context ctx;
  Value *r = eval_expr(ctx, "missing");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->kind, ValueKind::Exception);
}

/* -------------------------------------------------------------------------- */
/* 算术                                                                       */
/* -------------------------------------------------------------------------- */

TEST(Vm, Arith) {
  Context ctx;
  EXPECT_EQ(numOf(eval_expr(ctx, "1 + 2")), 3.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "5 - 2")), 3.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "3 * 4")), 12.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "7 / 2")), 3.5);
  EXPECT_EQ(numOf(eval_expr(ctx, "7 % 3")), 1.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "2 ** 10")), 1024.0);
  /* 复合 */
  EXPECT_EQ(numOf(eval_expr(ctx, "1 + 2 * 3")), 7.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "(1 + 2) * 3")), 9.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "2 ** 3 ** 2")), 512.0); /* 右结合 ** */
}

TEST(Vm, ArithString) {
  Context ctx;
  EXPECT_EQ(strOf(eval_expr(ctx, "\"a\" + \"b\"")), "ab");
  EXPECT_EQ(strOf(eval_expr(ctx, "1 + \"b\"")), "1b");
  EXPECT_EQ(strOf(eval_expr(ctx, "\"a\" + 1")), "a1");
}

TEST(Vm, ArithConversion) {
  Context ctx;
  /* 字符串数字运算（ToNumber） */
  EXPECT_EQ(numOf(eval_expr(ctx, "\"5\" * 2")), 10.0);
  /* undefined → NaN */
  EXPECT_TRUE(std::isnan(numOf(eval_expr(ctx, "undefined + 1"))));
  /* 除零 */
  EXPECT_TRUE(std::isinf(numOf(eval_expr(ctx, "1 / 0"))));
}

/* -------------------------------------------------------------------------- */
/* 一元                                                                       */
/* -------------------------------------------------------------------------- */

TEST(Vm, Unary) {
  Context ctx;
  EXPECT_EQ(numOf(eval_expr(ctx, "-5")), -5.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "+\"7\"")), 7.0);
  EXPECT_TRUE(boolOf(eval_expr(ctx, "!0")));
  EXPECT_FALSE(boolOf(eval_expr(ctx, "!1")));
  EXPECT_EQ(numOf(eval_expr(ctx, "~5")), -6.0);
  EXPECT_EQ(strOf(eval_expr(ctx, "typeof 1")), "number");
  EXPECT_EQ(strOf(eval_expr(ctx, "typeof \"x\"")), "string");
  EXPECT_EQ(strOf(eval_expr(ctx, "typeof true")), "boolean");
  EXPECT_EQ(strOf(eval_expr(ctx, "typeof undefined")), "undefined");
  EXPECT_EQ(strOf(eval_expr(ctx, "typeof null")), "object");
  EXPECT_EQ(eval_expr(ctx, "void 0")->kind, ValueKind::Undefined);
}

/* -------------------------------------------------------------------------- */
/* 位运算                                                                     */
/* -------------------------------------------------------------------------- */

TEST(Vm, Bitwise) {
  Context ctx;
  EXPECT_EQ(numOf(eval_expr(ctx, "6 & 3")), 2.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "4 | 1")), 5.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "5 ^ 3")), 6.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "1 << 4")), 16.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "-16 >> 2")), -4.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "-1 >>> 1")), 2147483647.0);
}

/* -------------------------------------------------------------------------- */
/* 比较                                                                       */
/* -------------------------------------------------------------------------- */

TEST(Vm, Relational) {
  Context ctx;
  EXPECT_TRUE(boolOf(eval_expr(ctx, "1 < 2")));
  EXPECT_FALSE(boolOf(eval_expr(ctx, "2 < 1")));
  EXPECT_TRUE(boolOf(eval_expr(ctx, "2 <= 2")));
  EXPECT_TRUE(boolOf(eval_expr(ctx, "3 > 2")));
  EXPECT_TRUE(boolOf(eval_expr(ctx, "3 >= 3")));
  EXPECT_TRUE(boolOf(eval_expr(ctx, "\"a\" < \"b\"")));
  /* NaN 比较全 false */
  EXPECT_FALSE(boolOf(eval_expr(ctx, "undefined < 1")));
  EXPECT_FALSE(boolOf(eval_expr(ctx, "undefined <= 1")));
}

TEST(Vm, Equality) {
  Context ctx;
  EXPECT_TRUE(boolOf(eval_expr(ctx, "1 === 1")));
  EXPECT_FALSE(boolOf(eval_expr(ctx, "1 === \"1\"")));
  EXPECT_TRUE(boolOf(eval_expr(ctx, "1 == \"1\"")));
  EXPECT_TRUE(boolOf(eval_expr(ctx, "true == 1")));
  EXPECT_TRUE(boolOf(eval_expr(ctx, "null == undefined")));
  EXPECT_FALSE(boolOf(eval_expr(ctx, "1 != 1")));
  EXPECT_TRUE(boolOf(eval_expr(ctx, "1 !== \"1\"")));
}

/* -------------------------------------------------------------------------- */
/* 逻辑短路与条件                                                             */
/* -------------------------------------------------------------------------- */

TEST(Vm, LogicalShortCircuit) {
  Context ctx;
  /* && 返回假值或右操作数 */
  EXPECT_EQ(numOf(eval_expr(ctx, "0 && 5")), 0.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "1 && 5")), 5.0);
  /* || 返回真值或右操作数 */
  EXPECT_EQ(numOf(eval_expr(ctx, "0 || 5")), 5.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "1 || 5")), 1.0);
  /* ?? 返回非空值或右操作数 */
  EXPECT_EQ(numOf(eval_expr(ctx, "null ?? 5")), 5.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "undefined ?? 5")), 5.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "0 ?? 5")), 0.0);
  /* 混合 */
  EXPECT_EQ(numOf(eval_expr(ctx, "1 && 2 || 3")), 2.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "0 && 2 || 3")), 3.0);
}

TEST(Vm, Conditional) {
  Context ctx;
  EXPECT_EQ(numOf(eval_expr(ctx, "true ? 1 : 2")), 1.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "false ? 1 : 2")), 2.0);
  EXPECT_EQ(strOf(eval_expr(ctx, "1 < 2 ? \"a\" : \"b\"")), "a");
  EXPECT_EQ(numOf(eval_expr(ctx, "0 ? 1 : 2")), 2.0);
}

/* -------------------------------------------------------------------------- */
/* 综合表达式                                                                 */
/* -------------------------------------------------------------------------- */

TEST(Vm, Composite) {
  Context ctx;
  ctx.define("x", ctx.create_number(5));
  ctx.define("y", ctx.create_number(3));
  EXPECT_EQ(numOf(eval_expr(ctx, "x + y * 2")), 11.0);
  EXPECT_EQ(numOf(eval_expr(ctx, "(x - y) * 3")), 6.0);
  /* && 返回右操作数：x > y 为真 → 返回 (x - y == 2) 的布尔值 */
  EXPECT_TRUE(boolOf(eval_expr(ctx, "x > y && x - y == 2")));
  /* 复合条件 */
  EXPECT_TRUE(boolOf(eval_expr(ctx, "x > y && x - y == 2 || x == 0")));
  EXPECT_EQ(numOf(eval_expr(ctx, "(x > y ? x : y) * 2")), 10.0);
}

/* -------------------------------------------------------------------------- */
/* Ret 指令（直接构造字节码；compiler 当前不发射 Ret）                          */
/* -------------------------------------------------------------------------- */

TEST(Vm, RetUnwrap) {
  Context ctx;
  neojs::vm::Bytecode bc;
  bc.emit(neojs::vm::Opcode::PushConst, bc.add_number(42));
  bc.emit(neojs::vm::Opcode::Ret);
  neojs::vm::Vm vm(ctx);
  /* Ret 产生 interrupt(return, 42)，run 解包返回 42 */
  EXPECT_EQ(numOf(vm.run(bc)), 42.0);
}

TEST(Vm, RetUnwrapString) {
  Context ctx;
  neojs::vm::Bytecode bc;
  bc.emit(neojs::vm::Opcode::PushConst, bc.add_string("done"));
  bc.emit(neojs::vm::Opcode::Ret);
  neojs::vm::Vm vm(ctx);
  EXPECT_EQ(strOf(vm.run(bc)), "done");
}
