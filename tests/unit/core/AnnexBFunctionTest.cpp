// AnnexBFunctionTest.cpp - 块级函数声明 Annex B.3.3
//
// 非严格模式下，块/if 子句/标签中的函数声明同时建立外层 var 绑定，
// 并在对应位置执行时同步赋值；严格模式保持块级作用域。

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

bool runScript(Fixture& f, const std::string& source) {
    Parser parser(source, ParserOptions{SourceType::Script});
    auto parsed = parser.parseProgram();
    EXPECT_TRUE(parsed.ok());
    if (!parsed.ok()) return false;

    auto sema = runSema(*parsed.program);
    EXPECT_TRUE(sema.ok());
    if (!sema.ok()) return false;

    Compiler compiler(static_cast<const Program&>(*parsed.program), sema,
                      CompilerOptions{"annexb-test.js"});
    auto compiled = compiler.compile();
    EXPECT_TRUE(compiled.ok());
    if (!compiled.ok()) return false;

    VM machine(f.ctx.get(), compiled.code.get());
    Value* result = machine.run();
    EXPECT_FALSE(neo::core::detail::isErrorValue(result));
    return !neo::core::detail::isErrorValue(result);
}

Value* lookupOut(Fixture& f) { return f.ctx->root()->lookupChain("out"); }

double numberOf(Value* v) {
    EXPECT_EQ(v->type()->kind(), TypeKind::Number);
    return static_cast<NumberData*>(v->data())->value;
}

std::string stringOf(Value* v) {
    EXPECT_EQ(v->type()->kind(), TypeKind::String);
    return static_cast<StringData*>(v->data())->value;
}

TEST(AnnexBFunctionTest, BlockFunctionLeaksToFunctionScope) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  { function g() { return 1; } }\n"
                          "  return g();\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 1.0);
}

TEST(AnnexBFunctionTest, StrictBlockFunctionDoesNotLeak) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  'use strict';\n"
                          "  { function g() { return 1; } }\n"
                          "  try { return g(); } catch (e) { return e.name; }\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "ReferenceError");
}

TEST(AnnexBFunctionTest, IfClauseFunctionAssignedWhenBranchRuns) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f(x) {\n"
                          "  if (x) function g() { return 2; }\n"
                          "  return typeof g;\n"
                          "}\n"
                          "var out = f(true);\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "function");
}

TEST(AnnexBFunctionTest, IfClauseFunctionUnassignedWhenBranchSkipped) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f(x) {\n"
                          "  if (x) function g() { return 2; }\n"
                          "  return typeof g;\n"
                          "}\n"
                          "var out = f(false);\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "undefined");
}

TEST(AnnexBFunctionTest, LabelledFunctionIsVarScoped) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  lbl: function g() { return 3; }\n"
                          "  return g();\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 3.0);
}

TEST(AnnexBFunctionTest, SloppyEvalBlockFunctionLeaksToCaller) {
    Fixture f;
    f.ctx->global()->clearFlag("strict");
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  eval('{ function g() { return 4; } }');\n"
                          "  return g();\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 4.0);
}

TEST(AnnexBFunctionTest, StrictEvalBlockFunctionDoesNotLeak) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  eval('\"use strict\"; { function g() { return 4; } }');\n"
                          "  try { return g(); } catch (e) { return e.name; }\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "ReferenceError");
}

}  // namespace
