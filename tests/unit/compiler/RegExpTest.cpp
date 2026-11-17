// RegExpTest.cpp - RegExp 类型/内建与 String 正则方法端到端测试（ADR-062 R4）

#include "compiler/Compiler.h"
#include "core/IContext.h"
#include "core/Scope.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "frontend/Parser.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"
#include "sema/Sema.h"
#include "vm/VM.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

namespace {

using neo::compiler::CompileResult;
using neo::compiler::Compiler;
using neo::compiler::CompilerOptions;
using neo::core::BooleanData;
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
using neo::sema::SemaResult;
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

CompileResult compileSource(const std::string& source) {
    Parser parser(source, ParserOptions{SourceType::Script});
    auto parsed = parser.parseProgram();
    EXPECT_TRUE(parsed.ok());
    SemaResult sema = runSema(*parsed.program);
    EXPECT_TRUE(sema.ok());
    Compiler compiler(static_cast<const Program&>(*parsed.program), sema,
                      CompilerOptions{"test.js"});
    return compiler.compile();
}

Value* runScript(Fixture& f, const std::string& source) {
    CompileResult result = compileSource(source);
    EXPECT_TRUE(result.ok())
        << (result.errors.empty() ? std::string() : result.errors[0].message);
    if (!result.ok()) return nullptr;
    VM vm(f.ctx.get(), result.code.get());
    (void)vm.run();
    return f.ctx->root()->lookup("result");
}

std::string stringValue(Value* value) {
    EXPECT_NE(value, nullptr);
    if (value == nullptr || value->type()->kind() != TypeKind::String) {
        return {};
    }
    return static_cast<StringData*>(value->data())->value;
}

double numberValue(Value* value) {
    EXPECT_NE(value, nullptr);
    if (value == nullptr || value->type()->kind() != TypeKind::Number) {
        return 0.0;
    }
    return static_cast<NumberData*>(value->data())->value;
}

bool boolValue(Value* value) {
    EXPECT_NE(value, nullptr);
    if (value == nullptr || value->type()->kind() != TypeKind::Boolean) {
        return false;
    }
    return static_cast<BooleanData*>(value->data())->value;
}

}  // namespace

TEST(RegExpTest, LiteralTestAndProperties) {
    Fixture f;
    Value* result = runScript(
        f,
        "var r = /ab+c/i;"
        "var result = r.test('xxABBC') + ':' + r.source + ':' + r.flags;");
    EXPECT_EQ(stringValue(result), "true:ab+c:i");
}

TEST(RegExpTest, ExecUpdatesLastIndexForGlobal) {
    Fixture f;
    Value* result = runScript(
        f,
        "var r = /a/g;"
        "var m = r.exec('aXa');"
        "var result = m[0] + ':' + m.index + ':' + r.lastIndex;");
    EXPECT_EQ(stringValue(result), "a:0:1");
}

TEST(RegExpTest, ConstructorAndCaptures) {
    Fixture f;
    Value* result = runScript(
        f,
        "var m = new RegExp('a(\\\\d+)').exec('xa12');"
        "var result = m[0] + ':' + m[1];");
    EXPECT_EQ(stringValue(result), "a12:12");
}

TEST(RegExpTest, NamedGroupsAndIndices) {
    Fixture f;
    Value* result = runScript(
        f,
        "var m = /(?<year>\\d{4})-(?<month>\\d{2})/d.exec('2026-11');"
        "var result = m.groups.year + ':' + m.groups.month + ':' +"
        " m.indices[1][0] + ':' + m.indices.groups.month[0];");
    EXPECT_EQ(stringValue(result), "2026:11:0:5");
}

TEST(RegExpTest, StringMatchGlobal) {
    Fixture f;
    Value* result = runScript(
        f,
        "var m = 'a1b2'.match(/\\d/g);"
        "var result = m[0] + ':' + m[1] + ':' + m.length;");
    EXPECT_EQ(stringValue(result), "1:2:2");
}

TEST(RegExpTest, StringReplaceWithCapture) {
    Fixture f;
    Value* result = runScript(
        f,
        "var result = 'a1b2'.replace(/(\\d)/g, '[$1]');");
    EXPECT_EQ(stringValue(result), "a[1]b[2]");
}

TEST(RegExpTest, StringReplaceAllStringPattern) {
    Fixture f;
    Value* result = runScript(
        f, "var result = 'aXbXc'.replaceAll('X', '-');");
    EXPECT_EQ(stringValue(result), "a-b-c");
}

TEST(RegExpTest, StringSearch) {
    Fixture f;
    Value* result = runScript(f, "var result = 'abc'.search(/b/);");
    EXPECT_DOUBLE_EQ(numberValue(result), 1.0);
}

TEST(RegExpTest, StringSplit) {
    Fixture f;
    Value* result = runScript(
        f,
        "var parts = 'a1b2c'.split(/\\d/);"
        "var result = parts.join(':');");
    EXPECT_EQ(stringValue(result), "a:b:c");
}

TEST(RegExpTest, MatchAllIterator) {
    Fixture f;
    Value* result = runScript(
        f,
        "var it = 'a1b2'.matchAll(/(\\d)/g);"
        "var a = it.next().value;"
        "var b = it.next().value;"
        "var result = a[0] + ':' + b[0];");
    EXPECT_EQ(stringValue(result), "1:2");
}

TEST(RegExpTest, SymbolMethods) {
    Fixture f;
    Value* result = runScript(
        f,
        "var result = /b/[Symbol.search]('abc');");
    EXPECT_DOUBLE_EQ(numberValue(result), 1.0);
}

TEST(RegExpTest, InvalidPatternThrowsSyntaxError) {
    Fixture f;
    Value* result = runScript(
        f,
        "var result = 'none';"
        "try { new RegExp('['); }"
        "catch (e) { result = e.name; }");
    EXPECT_EQ(stringValue(result), "SyntaxError");
}

TEST(RegExpTest, SymbolSpeciesReturnsThis) {
    Fixture f;
    Value* result = runScript(
        f,
        "var result = RegExp[Symbol.species] === RegExp;");
    EXPECT_TRUE(boolValue(result));
}

TEST(RegExpTest, VModeUnicodeSets) {
    Fixture f;
    Value* result = runScript(
        f,
        "var a = /[\\p{ASCII}&&\\p{Letter}]+/v.test('abc');"
        "var b = /[[a-z]--[aeiou]]+/v.test('bcdf');"
        "var result = a && b;");
    EXPECT_TRUE(boolValue(result));
}

TEST(RegExpTest, AnnexBLegacyOctalInLiteral) {
    Fixture f;
    Value* result = runScript(
        f,
        "var result = /^\\1$/.test('\\x01');");
    EXPECT_TRUE(boolValue(result));
}
