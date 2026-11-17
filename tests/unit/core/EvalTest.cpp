// EvalTest.cpp - 全局 eval 端到端（ES2024 §19.2.1）
//
// 覆盖：间接 eval、直接 eval 读写调用方绑定、sloppy var 泄漏、
// 严格 eval 不外泄、完成值、非字符串原样返回、语法错误、eval 被遮蔽、
// eval 内定义的闭包捕获注入绑定。

#include "compiler/Compiler.h"
#include "core/IContext.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "frontend/Node.h"
#include "frontend/Parser.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"
#include "sema/Sema.h"
#include "vm/VM.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using neo::compiler::Compiler;
using neo::compiler::CompilerOptions;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::frontend::Parser;
using neo::frontend::ParserOptions;
using neo::frontend::Program;
using neo::frontend::SourceType;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;
using neo::sema::runSema;
using neo::vm::VM;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

struct Fixture {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    NullSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx = createContext(alloc.get(), &console);
};

// 编译并执行脚本；顶层 var 提升在 root scope，测试从 root 读取结果。
bool runScript(Fixture& f, const std::string& source) {
    Parser parser(source, ParserOptions{SourceType::Script});
    auto parsed = parser.parseProgram();
    EXPECT_TRUE(parsed.ok());
    if (!parsed.ok()) return false;

    auto sema = runSema(*parsed.program);
    EXPECT_TRUE(sema.ok());
    if (!sema.ok()) return false;

    Compiler compiler(static_cast<const Program&>(*parsed.program), sema,
                      CompilerOptions{"eval-test.js"});
    auto compiled = compiler.compile();
    EXPECT_TRUE(compiled.ok());
    if (!compiled.ok()) return false;

    VM machine(f.ctx.get(), compiled.code.get());
    Value* result = machine.run();
    EXPECT_FALSE(neo::core::detail::isErrorValue(result));
    return !neo::core::detail::isErrorValue(result);
}

Value* lookupOut(Fixture& f) { return f.ctx->root()->lookup("out"); }

double numberOf(Value* v) {
    EXPECT_EQ(v->type()->kind(), TypeKind::Number);
    return static_cast<NumberData*>(v->data())->value;
}

std::string stringOf(Value* v) {
    EXPECT_EQ(v->type()->kind(), TypeKind::String);
    return static_cast<StringData*>(v->data())->value;
}

TEST(EvalTest, IndirectEvalReturnsCompletionValue) {
    Fixture f;
    ASSERT_TRUE(runScript(f, "var out = eval('1 + 2');"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 3.0);
}

TEST(EvalTest, IndirectEvalReturnsNonStringAsIs) {
    Fixture f;
    ASSERT_TRUE(runScript(f, "var out = eval(42);"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 42.0);
}

TEST(EvalTest, IndirectEvalNoArgumentReturnsUndefined) {
    Fixture f;
    ASSERT_TRUE(runScript(f, "var out = eval();"));
    EXPECT_EQ(lookupOut(f)->type()->kind(), TypeKind::Undefined);
}

TEST(EvalTest, IndirectEvalVarGoesToGlobalEnvironment) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var e = eval;\n"
                          "var out = e('var gv = 7; gv + 1');\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 8.0);
    Value* gv = f.ctx->root()->lookup("gv");
    ASSERT_NE(gv, nullptr);
    EXPECT_DOUBLE_EQ(numberOf(gv), 7.0);
}

TEST(EvalTest, DirectEvalReadsAndWritesCallerBinding) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  var x = 10;\n"
                          "  eval('x = x + 1;');\n"
                          "  return x;\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 11.0);
}

TEST(EvalTest, SloppyDirectEvalVarLeaksToCaller) {
    Fixture f;
    f.ctx->global()->clearFlag("strict");
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  eval('var leaked = 5;');\n"
                          "  return leaked;\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 5.0);
}

TEST(EvalTest, StrictDirectEvalVarDoesNotLeak) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  eval('var strictLeak = 1;');\n"
                          "  try { return strictLeak; } catch (e) { return 'missing'; }\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "missing");
}

TEST(EvalTest, DirectEvalLetDoesNotLeak) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  eval('let local = 5;');\n"
                          "  try { return local; } catch (e) { return 'missing'; }\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "missing");
}

TEST(EvalTest, DirectEvalCompletionValue) {
    Fixture f;
    ASSERT_TRUE(runScript(f, "var out = eval('1; 2; 3;');"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 3.0);
}

TEST(EvalTest, CompletionValuePropagatesThroughBlockAndIf) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var out = eval('if (true) { 1; 2; }');\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 2.0);
}

TEST(EvalTest, CompletionValueNotClobberedByNestedFunctionBody) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var out = eval('function g() { 9; } 5;');\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 5.0);
}

TEST(EvalTest, CompletionValueFromLoopBody) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var out = eval('for (var i = 0; i < 3; i = i + 1) i;');\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 2.0);
}

TEST(EvalTest, EvalSyntaxErrorBecomesSyntaxErrorException) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var out;\n"
                          "try { eval('var ='); } catch (e) { out = e.name; }\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "SyntaxError");
}

TEST(EvalTest, ShadowedEvalIsOrdinaryCall) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f(eval) { return eval('1 + 2'); }\n"
                          "var out = f(function(s) { return 'shadow:' + s; });\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "shadow:1 + 2");
}

TEST(EvalTest, DirectEvalInheritsNewTarget) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function F() { this.v = eval('new.target === F ? 1 : 2'); }\n"
                          "var out = (new F()).v;\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 1.0);
}

TEST(EvalTest, DirectEvalInMethodAllowsSuperProperty) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "class A { m() { return 1; } }\n"
                          "class B extends A { m() { return eval('super.m()'); } }\n"
                          "var out = new B().m();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 1.0);
}

TEST(EvalTest, DirectEvalInDerivedConstructorAllowsSuperCall) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "class A { constructor() { this.x = 1; } }\n"
                          "class B extends A { constructor() { eval('super()'); } }\n"
                          "var out = new B().x;\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 1.0);
}

TEST(EvalTest, SloppyEvalVarConflictsWithCallerLexicalBinding) {
    Fixture f;
    f.ctx->global()->clearFlag("strict");
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  let x = 1;\n"
                          "  try { eval('var x = 2;'); return 'no'; }\n"
                          "  catch (e) { return e.name; }\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "SyntaxError");
}

TEST(EvalTest, DirectEvalWithSpreadUsesCallerScope) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  var x = 1;\n"
                          "  return eval(...['x + 1']);\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 2.0);
}

TEST(EvalTest, EvalIsNotConstructor) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var out;\n"
                          "try { new eval(); } catch (e) { out = e.name; }\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "TypeError");
}

TEST(EvalTest, EvalHasNoPrototype) {
    Fixture f;
    ASSERT_TRUE(runScript(f, "var out = eval.prototype;"));
    EXPECT_EQ(lookupOut(f)->type()->kind(), TypeKind::Undefined);
}

TEST(EvalTest, SloppyEvalVarInCatchAssignsCatchParameter) {
    Fixture f;
    f.ctx->global()->clearFlag("strict");
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  try { throw 1; } catch (e) {\n"
                          "    eval('var e = 9;');\n"
                          "    return e;\n"
                          "  }\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 9.0);
}

TEST(EvalTest, SloppyEvalVarInCatchDoesNotLeakValue) {
    Fixture f;
    f.ctx->global()->clearFlag("strict");
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  try { throw 1; } catch (e) { eval('var e = 9;'); }\n"
                          "  return e;\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_EQ(lookupOut(f)->type()->kind(), TypeKind::Undefined);
}

TEST(EvalTest, EvalVarInDestructuringCatchIsSyntaxError) {
    Fixture f;
    f.ctx->global()->clearFlag("strict");
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  try { throw { e: 1 }; } catch ({ e }) {\n"
                          "    try { eval('var e = 2;'); return 'no'; }\n"
                          "    catch (err) { return err.name; }\n"
                          "  }\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "SyntaxError");
}

TEST(EvalTest, DirectEvalInMethodSeesPrivateField) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "class A {\n"
                          "  #x = 5;\n"
                          "  m() { return eval('this.#x'); }\n"
                          "}\n"
                          "var out = new A().m();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 5.0);
}

TEST(EvalTest, DirectEvalCanWritePrivateField) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "class A {\n"
                          "  #x = 1;\n"
                          "  m() { eval('this.#x = 7;'); return this.#x; }\n"
                          "}\n"
                          "var out = new A().m();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 7.0);
}

TEST(EvalTest, DirectEvalPrivateInOperator) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "class A {\n"
                          "  #x = 1;\n"
                          "  m() { return eval('#x in this'); }\n"
                          "}\n"
                          "var out = new A().m();\n"));
    EXPECT_TRUE(neo::core::detail::toBoolean(lookupOut(f)));
}

TEST(EvalTest, DirectEvalCallsPrivateMethod) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "class A {\n"
                          "  #v = 3;\n"
                          "  #m() { return this.#v; }\n"
                          "  run() { return eval('this.#m()'); }\n"
                          "}\n"
                          "var out = new A().run();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 3.0);
}

TEST(EvalTest, ClosureCreatedByEvalCapturesInjectedBinding) {
    Fixture f;
    f.ctx->global()->clearFlag("strict");
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  return eval('var injected = 42; function g() { return injected; } g');\n"
                          "}\n"
                          "var out = f()();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 42.0);
}

}  // namespace

TEST(EvalTest, IndirectEvalSeesScriptVar) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var sv = 3;\n"
                          "var e = eval;\n"
                          "var out = e('sv');\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 3.0);
}

TEST(EvalTest, IndirectEvalSeesScriptLet) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "let sl = 4;\n"
                          "var e = eval;\n"
                          "var out = e('sl');\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 4.0);
}

TEST(EvalTest, IndirectEvalVarConflictsWithScriptLet) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "let x = 1;\n"
                          "var e = eval;\n"
                          "try { e('var x = 2;'); var out = 'no error'; }\n"
                          "catch (err) { var out = err.name; }\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "SyntaxError");
}

TEST(EvalTest, IndirectEvalVarReusesScriptVar) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var x = 1;\n"
                          "var e = eval;\n"
                          "try { e('var x = 2;'); var out = x; }\n"
                          "catch (err) { var out = err.name; }\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 2.0);
}

TEST(EvalTest, DirectEvalVarReusesCallerVar) {
    Fixture f;
    f.ctx->global()->clearFlag("strict");
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  var x = 1;\n"
                          "  try { eval('var x = 2;'); return x; }\n"
                          "  catch (err) { return err.name; }\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 2.0);
}

TEST(EvalTest, TryFinallyDiscardsFinallyValue) {
    Fixture f;
    ASSERT_TRUE(runScript(f, "var out = eval('try { 1; } finally { 2; }');\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 1.0);
}

TEST(EvalTest, TryFinallyEmptyTryIsUndefined) {
    Fixture f;
    ASSERT_TRUE(runScript(f, "var out = eval('try { } finally { 2; }');\n"));
    EXPECT_EQ(lookupOut(f)->type()->kind(), TypeKind::Undefined);
}

TEST(EvalTest, CatchEmptyCompletionIsUndefined) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var out = eval('try { 1; throw 2; } catch (e) { }');\n"));
    EXPECT_EQ(lookupOut(f)->type()->kind(), TypeKind::Undefined);
}

TEST(EvalTest, CatchValueWinsOverTryValue) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var out = eval('try { 1; throw 2; } catch (e) { 3; }');\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 3.0);
}

TEST(EvalTest, BreakThroughFinallyClearsCompletion) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var out = eval('for (;;) { try { 1; break; } finally { 2; } }');\n"));
    EXPECT_EQ(lookupOut(f)->type()->kind(), TypeKind::Undefined);
}

TEST(EvalTest, BreakInsideFinallyClearsCompletion) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var out = eval('for (;;) { try { 1; } finally { 2; break; } }');\n"));
    EXPECT_EQ(lookupOut(f)->type()->kind(), TypeKind::Undefined);
}

TEST(EvalTest, NestedFinallyKeepsOuterTryValue) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var out = eval('try { 1; } finally { try { 2; } finally { 3; } }');\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 1.0);
}

TEST(EvalTest, FinallyValueIgnoredBeforeLaterStatement) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var out = eval('try { 1; } finally { 2; } 3;');\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 3.0);
}
