// CompilerTest.cpp - Compiler 框架测试（ADR-061）
//
// 验证：
// - AST + SemaResult → Code 的纵向链路
// - 表达式语句/return/字面量/二元运算的字节码
// - SourceMap 记录节点源码位置
// - 未支持节点与 sema 错误不会静默生成可执行字节码

#include "compiler/Compiler.h"
#include "core/IContext.h"
#include "core/types/Number.h"
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

using neo::compiler::CompileResult;
using neo::compiler::Compiler;
using neo::compiler::CompilerOptions;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::frontend::BinaryExpression;
using neo::frontend::BinaryOperator;
using neo::frontend::DebuggerStatement;
using neo::frontend::NodeList;
using neo::frontend::NumericLiteral;
using neo::frontend::Parser;
using neo::frontend::ParserOptions;
using neo::frontend::Program;
using neo::frontend::ReturnStatement;
using neo::frontend::SourceRange;
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

double numberValue(Value* value) {
    return static_cast<NumberData*>(value->data())->value;
}

// 解析 + Sema，返回编译结果。source 必须能通过 Sema。
CompileResult compileSource(const std::string& source,
                            const std::string& filename = "test.js") {
    Parser parser(source, ParserOptions{SourceType::Script});
    auto parsed = parser.parseProgram();
    EXPECT_TRUE(parsed.ok());
    SemaResult sema = runSema(*parsed.program);
    EXPECT_TRUE(sema.ok());
    Compiler compiler(static_cast<const Program&>(*parsed.program), sema,
                      CompilerOptions{filename});
    return compiler.compile();
}

}  // namespace

TEST(CompilerTest, CompilesExpressionStatementToBytecode) {
    CompileResult result = compileSource("1 + 2;");
    ASSERT_TRUE(result.ok());

    // push_number(6) + push_number(6) + add(2) + pop(2) + halt(2) = 18
    ASSERT_EQ(result.code->bytecode.size(), 18u);
    ASSERT_EQ(result.code->numbers.size(), 2u);
    EXPECT_DOUBLE_EQ(result.code->numbers[0], 1.0);
    EXPECT_DOUBLE_EQ(result.code->numbers[1], 2.0);

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::Undefined);
}

TEST(CompilerTest, CompilesReturnBinaryExpression) {
    SourceRange range{};
    NodeList body;
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<BinaryExpression>(
                   range, BinaryOperator::Add,
                   std::make_unique<NumericLiteral>(range, 1.0),
                   std::make_unique<NumericLiteral>(range, 2.0))));
    Program program(range, std::move(body), SourceType::Script);

    SemaResult sema;  // 手工 AST：无标注、无错误
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, SourceMapRecordsNodePosition) {
    CompileResult result = compileSource("1 + 2;", "entry.js");
    ASSERT_TRUE(result.ok());

    const neo::core::SourcePosition* position = result.code->sourceMap.lookup(0);
    ASSERT_NE(position, nullptr);
    EXPECT_EQ(position->filename, "entry.js");
    EXPECT_EQ(position->line, 1u);
    EXPECT_EQ(position->column, 1u);
}

TEST(CompilerTest, UnsupportedStatementRecordsError) {
    SourceRange range{};
    NodeList body;
    body.push_back(std::make_unique<DebuggerStatement>(range));
    Program program(range, std::move(body), SourceType::Script);

    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();

    EXPECT_FALSE(result.ok());
    ASSERT_EQ(result.errors.size(), 1u);
    EXPECT_NE(result.errors[0].message.find("unsupported statement"),
              std::string::npos);
}

TEST(CompilerTest, SemaErrorsPreventCompilation) {
    SourceRange range{};
    Program program(range, NodeList{}, SourceType::Script);

    SemaResult sema;
    sema.errors.push_back({range.start, "sema failure"});
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();

    EXPECT_FALSE(result.ok());
    ASSERT_EQ(result.errors.size(), 1u);
    EXPECT_NE(result.errors[0].message.find("sema reported errors"),
              std::string::npos);
}
