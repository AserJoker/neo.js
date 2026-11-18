// TypeOfTest.cpp - typeof 未声明标识符（ES2024 §13.5.3）
//
// typeof 对完全未解析的裸标识符返回 "undefined"，不抛 ReferenceError；
// 已声明但处于 TDZ 的绑定仍抛 ReferenceError。

#include "compiler/Compiler.h"
#include "core/IContext.h"
#include "core/Scope.h"
#include "core/Value.h"
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
                      CompilerOptions("typeof-test.js"));
    auto compiled = compiler.compile();
    EXPECT_TRUE(compiled.ok());
    if (!compiled.ok()) return false;

    VM machine(f.ctx.get(), compiled.code.get());
    Value* result = machine.run();
    EXPECT_FALSE(neo::core::detail::isErrorValue(result));
    return !neo::core::detail::isErrorValue(result);
}

Value* lookupOut(Fixture& f) { return f.ctx->root()->lookupChain("out"); }

std::string stringOf(Value* v) {
    EXPECT_EQ(v->type()->kind(), TypeKind::String);
    return static_cast<StringData*>(v->data())->value;
}

TEST(TypeOfTest, UndeclaredIdentifierIsUndefined) {
    Fixture f;
    ASSERT_TRUE(runScript(f, "var out = typeof missingName;\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "undefined");
}

TEST(TypeOfTest, DeclaredUndefinedIsUndefined) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "var declared;\n"
                          "var out = typeof declared;\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "undefined");
}

TEST(TypeOfTest, FunctionIsFunction) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function g() {}\n"
                          "var out = typeof g;\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "function");
}

TEST(TypeOfTest, TdzBindingThrowsReferenceError) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  try { return typeof x; }\n"
                          "  catch (e) { return e.name; }\n"
                          "  let x = 1;\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "ReferenceError");
}

TEST(TypeOfTest, UnresolvedMemberBaseStillThrows) {
    Fixture f;
    ASSERT_TRUE(runScript(f,
                          "function f() {\n"
                          "  try { return typeof missingName.prop; }\n"
                          "  catch (e) { return e.name; }\n"
                          "}\n"
                          "var out = f();\n"));
    EXPECT_EQ(stringOf(lookupOut(f)), "ReferenceError");
}

}  // namespace
