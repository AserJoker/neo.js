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

#include <cmath>
#include <cstring>
#include <string>

#include "neojs/core/context.h"
#include "neojs/core/data.h"
#include "neojs/frontend/diagnostics.h"
#include "neojs/frontend/frontend.h"
#include "neojs/frontend/sema.h"
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
  /* M5-B：compile 前先跑 sema（变量提升/TDZ 消减在 sema 侧，§10.8） */
  neojs::sema::Result sema_result;
  if (!neojs::sema::analyze(ast->root, sema_result)) {
    ADD_FAILURE() << "sema failed: " << source;
    delete ast;
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

/* -------------------------------------------------------------------------- */
/* 函数定义 / 调用（M5-B：函数定义 + 调用编译 + vm 执行）                       */
/* -------------------------------------------------------------------------- */

TEST(Stmt, FuncDeclCall) {
  Context ctx;
  /* 函数声明 + 调用：add(2,3) = 5 */
  eval_program(ctx,
               "function add(a, b) { return a + b; }"
               "var r = add(2, 3);");
  EXPECT_EQ(numOf(varOf(ctx, "r")), 5.0);
}

TEST(Stmt, FuncNoExplicitReturn) {
  Context ctx;
  /* 无显式 return：默认 undefined */
  eval_program(ctx,
               "function noop() { var x = 1; }"
               "var r = noop();");
  EXPECT_EQ(varOf(ctx, "r")->kind, ValueKind::Undefined);
}

TEST(Stmt, FuncParamCountMismatch) {
  Context ctx;
  /* 实参不足 → undefined；多余 → 忽略 */
  eval_program(ctx,
               "function f(a, b) { return a + b; }"
               "var r1 = f(1);"
               "var r2 = f(1, 2, 3);");
  EXPECT_TRUE(std::isnan(numOf(varOf(ctx, "r1")))); /* 1 + undefined = NaN */
  EXPECT_EQ(numOf(varOf(ctx, "r2")), 3.0);
}

TEST(Stmt, FuncClosureCapture) {
  Context ctx;
  /* 闭包捕获外层变量：outer 内 inner 引用 x + y */
  eval_program(ctx,
               "var x = 100;"
               "function outer() {"
               "  var y = 5;"
               "  function inner() { return x + y; }"
               "  return inner();"
               "}"
               "var r = outer();");
  EXPECT_EQ(numOf(varOf(ctx, "r")), 105.0);
}

TEST(Stmt, FuncClosureFactory) {
  Context ctx;
  /* 工厂函数：make(10) 返回捕获 x 的闭包，f(5) = 15 */
  eval_program(ctx,
               "function make(x) { return function (y) { return x + y; }; }"
               "var f = make(10);"
               "var r = f(5);");
  EXPECT_EQ(numOf(varOf(ctx, "r")), 15.0);
}

TEST(Stmt, FuncRecursion) {
  Context ctx;
  /* 递归阶乘：fact(5) = 120 */
  eval_program(ctx,
               "function fact(n) { if (n <= 1) return 1; return n * fact(n - 1); }"
               "var r = fact(5);");
  EXPECT_EQ(numOf(varOf(ctx, "r")), 120.0);
}

TEST(Stmt, FuncMutateOuterVar) {
  Context ctx;
  /* 闭包修改外层变量（捕获共享同一 value，读写穿透） */
  eval_program(ctx,
               "var count = 0;"
               "function inc() { count = count + 1; }"
               "inc(); inc(); inc();");
  EXPECT_EQ(numOf(varOf(ctx, "count")), 3.0);
}

/* -------------------------------------------------------------------------- */
/* 运行时选项：directive 序言（"use strict"/自定义）→ 字节码 → Context 选项      */
/* -------------------------------------------------------------------------- */

TEST(Stmt, OptionUseStrictEnabled) {
  Context ctx;
  /* 顶层 "use strict" 指令生效：strict 期间赋值未声明标识符 → ReferenceError。
   * 选项是作用域属性：顶层选项挂 root_scope 常驻（脚本级严格语义），
   * 程序结束后仍为 true（随 ctx 销毁才消失）。 */
  eval_program(ctx, "\"use strict\"; var x = 1;");
  EXPECT_TRUE(ctx.get_option("use strict")); /* 脚本级严格：常驻 */
}

TEST(Stmt, OptionDisabledAfterProgram) {
  Context ctx;
  /* 无指令的程序：选项保持关闭；
   * 有指令的程序结束后选项也恢复关闭（作用域对称关闭）。 */
  eval_program(ctx, "var x = 1;");
  EXPECT_FALSE(ctx.get_option("use strict"));
}

TEST(Stmt, OptionCustomDirective) {
  Context ctx;
  /* 自定义指令（如 'use client'）：进作用域选项存储，随 scope 销毁自动消失。
   * 运行期语义由消费方（如隐式全局查 "use strict"）决定，此处验证
   * 编译器把指令编译进字节码（EnableOption）。 */
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("\"use client\"; var x = 1;",
                                       std::strlen("\"use client\"; var x = 1;"),
                                       sink);
  ASSERT_NE(ast, nullptr);
  neojs::sema::Result sr;
  ASSERT_TRUE(neojs::sema::analyze(ast->root, sr));
  neojs::vm::Bytecode bc;
  ASSERT_TRUE(vm::compile_program(ast->root, bc));
  std::string dump = neojs::vm::dumpBytecode(bc);
  EXPECT_NE(dump.find("EnableOption 'use client'"), std::string::npos);
  delete ast;
}

TEST(Stmt, ImplicitGlobalNonStrict) {
  Context ctx;
  /* 非严格模式：赋值给未声明标识符 → 隐式全局（挂 root_scope）。 */
  eval_program(ctx, "x = 5; x = x + 1;");
  EXPECT_EQ(numOf(varOf(ctx, "x")), 6.0);
}

TEST(Stmt, ImplicitGlobalBlockedByStrict) {
  Context ctx;
  /* 严格模式：赋值给未声明标识符 → ReferenceError。 */
  Value *r = eval_program(ctx, "\"use strict\"; x = 5;");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->kind, ValueKind::Exception);
}

TEST(Stmt, FunctionLevelUseStrict) {
  Context ctx;
  /* 函数体指令序言：函数内 "use strict" 仅影响该函数（入口 enable，出口 disable）。
   * 函数内隐式全局 → ReferenceError。 */
  Value *r = eval_program(ctx,
                          "function f() { \"use strict\"; x = 1; }"
                          "var r = f();");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->kind, ValueKind::Exception);
}

TEST(Stmt, FunctionLevelUseStrictRestored) {
  Context ctx;
  /* 函数返回后选项恢复：函数内启用 strict，函数外非严格不受影响。
   * 外层隐式全局仍允许。 */
  eval_program(ctx,
               "function f() { \"use strict\"; return 1; }"
               "var r = f();"
               "y = 7;");
  EXPECT_EQ(numOf(varOf(ctx, "y")), 7.0);
  EXPECT_EQ(numOf(varOf(ctx, "r")), 1.0);
  EXPECT_FALSE(ctx.get_option("use strict"));
}
