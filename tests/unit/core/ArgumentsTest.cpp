// ArgumentsTest.cpp - arguments 对象端到端（ES2024 §10.4.4）
//
// 覆盖：length/索引、mapped 别名（双向）、严格与非简单形参 unmapped、
// callee、箭头继承外层 arguments、@@iterator、逃逸后映射存活、delete 解除映射。

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
                      CompilerOptions{"arguments-test.js"});
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

TEST(ArgumentsTest, LengthAndIndexedAccess) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f(a, b) {\n"
                          "  return arguments.length + ':' + arguments[0] + ':' + arguments[2];\n"
                          "}\n"
                          "var out = f(1, 2, 3);\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "3:1:3");
}

TEST(ArgumentsTest, MappedWriteUpdatesParameter) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f(a) { arguments[0] = 5; return a; }\n"
                          "var out = f(1);\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 5.0);
}

TEST(ArgumentsTest, MappedParameterWriteUpdatesArguments) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f(a) { a = 7; return arguments[0]; }\n"
                          "var out = f(1);\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 7.0);
}

TEST(ArgumentsTest, StrictArgumentsAreUnmapped) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f(a) { 'use strict'; arguments[0] = 5; return a; }\n"
                          "var out = f(1);\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 1.0);
}

TEST(ArgumentsTest, NonSimpleParametersAreUnmapped) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f(a = 1) { arguments[0] = 5; return a; }\n"
                          "var out = f(1);\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 1.0);
}

TEST(ArgumentsTest, MappedCalleeIsFunction) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() { return arguments.callee === f; }\n"
                          "var out = f();\n"));
    EXPECT_EQ(lookupOut(f)->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(neo::core::detail::toBoolean(lookupOut(f)));
}

TEST(ArgumentsTest, StrictCalleeThrowsTypeError) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  'use strict';\n"
                          "  try { return arguments.callee; } catch (e) { return e.name; }\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "TypeError");
}

TEST(ArgumentsTest, ArrowInheritsOuterArguments) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() { var g = () => arguments[0]; return g(); }\n"
                          "var out = f(9);\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 9.0);
}

TEST(ArgumentsTest, ArgumentsIsIterable) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  var s = 0;\n"
                          "  for (var v of arguments) s = s + v;\n"
                          "  return s;\n"
                          "}\n"
                          "var out = f(1, 2, 3);\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 6.0);
}

TEST(ArgumentsTest, EscapedMappedArgumentsKeepBinding) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f(a) { return arguments; }\n"
                          "var args = f(1);\n"
                          "args[0] = 2;\n"
                          "var out = args[0];\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 2.0);
}

TEST(ArgumentsTest, ClassMethodAndConstructorHaveArguments) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "class C {\n"
                          "  constructor(a) { this.v = arguments[0]; }\n"
                          "  m(a) { return arguments[0]; }\n"
                          "}\n"
                          "var c = new C(3);\n"
                          "var out = c.v + c.m(4);\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 7.0);
}

TEST(ArgumentsTest, GeneratorArgumentsMappedWhileSuspended) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var saved;\n"
                          "function* g(a) { saved = arguments; yield 1; }\n"
                          "var it = g(1);\n"
                          "it.next();\n"
                          "saved[0] = 2;\n"
                          "var out = saved[0];\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 2.0);
}

TEST(ArgumentsTest, VarArgumentsIsNoOp) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f(a) { var arguments; return arguments.length; }\n"
                          "var out = f(1, 2);\n"));
    EXPECT_DOUBLE_EQ(numberOf(lookupOut(f)), 2.0);
}

TEST(ArgumentsTest, DeleteRemovesMapping) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f(a) {\n"
                          "  delete arguments[0];\n"
                          "  a = 9;\n"
                          "  return arguments[0];\n"
                          "}\n"
                          "var out = f(1);\n"));
    EXPECT_EQ(lookupOut(f)->type()->kind(), TypeKind::Undefined);
}

}  // namespace
