/*
 * vm 层语句端到端测试（M5-A：源码 → AST → 字节码 → VM 执行）。
 *
 * 流水线：parseSource → AST → compile_program → Vm.run。
 *
 * M5-A 范围：
 *   - 变量声明 var/let/const（完整块作用域 + TDZ）
 *   - 表达式语句（含赋值）
 *   - if/else、while/do-while/for 控制流
 *   - return
 */

#include <gtest/gtest.h>

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

/* 编译并执行整段程序，返回 run 结果。 */
Value *eval_program(Context &ctx, const char *source) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource(source, std::strlen(source), sink);
  if (ast == nullptr) {
    ADD_FAILURE() << "parse failed: " << source;
    return nullptr;
  }
  neojs::vm::Bytecode bc;
  if (!vm::compile_program(ast->root, bc)) {
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

/* 从 ctx 读变量值 */
Value *varOf(Context &ctx, const char *name) { return ctx.load(name); }

}  // namespace

/* -------------------------------------------------------------------------- */
/* 变量声明 / 赋值                                                            */
/* -------------------------------------------------------------------------- */

TEST(Stmt, VarDecl) {
  Context ctx;
  eval_program(ctx, "var x = 10; var y = 20; x = x + y;");
  EXPECT_EQ(numOf(varOf(ctx, "x")), 30.0);
  EXPECT_EQ(numOf(varOf(ctx, "y")), 20.0);
}

TEST(Stmt, VarNoInit) {
  Context ctx;
  eval_program(ctx, "var x;");
  EXPECT_EQ(varOf(ctx, "x")->kind, ValueKind::Undefined);
}

TEST(Stmt, LetConst) {
  Context ctx;
  eval_program(ctx, "let x = 5; const y = 7; x = x + 1;");
  EXPECT_EQ(numOf(varOf(ctx, "x")), 6.0);
  EXPECT_EQ(numOf(varOf(ctx, "y")), 7.0);
}

TEST(Stmt, LetBlockScope) {
  Context ctx;
  /* 块内 let 遮蔽块外；块结束恢复 */
  eval_program(ctx, "let x = 1; { let x = 2; }");
  EXPECT_EQ(numOf(varOf(ctx, "x")), 1.0);
}

TEST(Stmt, VarFunctionScope) {
  Context ctx;
  /* var 无块作用域：块内 var 声明提升到全局（顶层=函数级=全局） */
  eval_program(ctx, "var x = 1; { var x = 2; }");
  EXPECT_EQ(numOf(varOf(ctx, "x")), 2.0);
}

TEST(Stmt, TdzRead) {
  Context ctx;
  /* 块内自引用：let x = x，初始值读取已预声明的 TDZ 哨兵 → ReferenceError */
  Value *r = eval_program(ctx, "{ let x = x; }");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->kind, ValueKind::Exception);
}

TEST(Stmt, TdzForwardRef) {
  Context ctx;
  /* 块内前向引用：var y = x 时 let x 尚未初始化 → ReferenceError */
  Value *r = eval_program(ctx, "{ var y = x; let x = 1; }");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->kind, ValueKind::Exception);
}

/* -------------------------------------------------------------------------- */
/* if / else                                                                  */
/* -------------------------------------------------------------------------- */

TEST(Stmt, IfElse) {
  Context ctx;
  eval_program(ctx, "var x = 5; if (x > 3) x = 1; else x = 2;");
  EXPECT_EQ(numOf(varOf(ctx, "x")), 1.0);
}

TEST(Stmt, IfElseTakeElse) {
  Context ctx;
  eval_program(ctx, "var x = 1; if (x > 3) x = 1; else x = 2;");
  EXPECT_EQ(numOf(varOf(ctx, "x")), 2.0);
}

TEST(Stmt, IfNoElse) {
  Context ctx;
  eval_program(ctx, "var x = 1; if (x > 0) x = 9;");
  EXPECT_EQ(numOf(varOf(ctx, "x")), 9.0);
}

TEST(Stmt, IfNested) {
  Context ctx;
  eval_program(ctx, "var x = 0; if (1) { if (1) x = 42; }");
  EXPECT_EQ(numOf(varOf(ctx, "x")), 42.0);
}

/* -------------------------------------------------------------------------- */
/* 循环                                                                       */
/* -------------------------------------------------------------------------- */

TEST(Stmt, While) {
  Context ctx;
  /* 循环累加：1+2+...+10 = 55 */
  eval_program(ctx,
               "var i = 0; var s = 0; while (i < 10) { i = i + 1; s = s + i; }");
  EXPECT_EQ(numOf(varOf(ctx, "s")), 55.0);
}

TEST(Stmt, WhileZero) {
  Context ctx;
  eval_program(ctx, "var x = 5; while (x > 10) { x = x - 1; }");
  EXPECT_EQ(numOf(varOf(ctx, "x")), 5.0);
}

TEST(Stmt, DoWhile) {
  Context ctx;
  /* do-while 至少执行一次 */
  eval_program(ctx, "var x = 5; do { x = x + 1; } while (x < 3);");
  EXPECT_EQ(numOf(varOf(ctx, "x")), 6.0);
}

TEST(Stmt, For) {
  Context ctx;
  /* 经典 for：1+2+3+4+5 = 15 */
  eval_program(ctx,
               "var s = 0; for (var i = 1; i <= 5; i = i + 1) { s = s + i; }");
  EXPECT_EQ(numOf(varOf(ctx, "s")), 15.0);
}

TEST(Stmt, ForLetScope) {
  Context ctx;
  /* for 的 let 声明在循环块作用域内 */
  eval_program(ctx, "var s = 0; for (let i = 0; i < 3; i = i + 1) { s = s + i; }");
  EXPECT_EQ(numOf(varOf(ctx, "s")), 3.0);
}

/* -------------------------------------------------------------------------- */
/* 综合脚本                                                                   */
/* -------------------------------------------------------------------------- */

TEST(Stmt, FizzBuzz) {
  Context ctx;
  /* 1..15 FizzBuzz 统计：Fizz(3/6/9/12)=4，Buzz(5/10)=2，FizzBuzz(15)=1 */
  eval_program(ctx,
               "var fizz = 0; var buzz = 0; var fb = 0;"
               "for (var i = 1; i <= 15; i = i + 1) {"
               "  if (i % 15 == 0) fb = fb + 1;"
               "  else if (i % 3 == 0) fizz = fizz + 1;"
               "  else if (i % 5 == 0) buzz = buzz + 1;"
               "}");
  EXPECT_EQ(numOf(varOf(ctx, "fizz")), 4.0);
  EXPECT_EQ(numOf(varOf(ctx, "buzz")), 2.0);
  EXPECT_EQ(numOf(varOf(ctx, "fb")), 1.0);
}

TEST(Stmt, Factorial) {
  Context ctx;
  /* 迭代阶乘：6! = 720 */
  eval_program(ctx,
               "var n = 6; var r = 1; while (n > 1) { r = r * n; n = n - 1; }");
  EXPECT_EQ(numOf(varOf(ctx, "r")), 720.0);
}
