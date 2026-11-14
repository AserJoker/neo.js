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

namespace {

using neo::compiler::CompileResult;
using neo::compiler::Compiler;
using neo::compiler::CompilerOptions;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::frontend::AssignmentExpression;
using neo::frontend::ArrayExpression;
using neo::frontend::AssignmentOperator;
using neo::frontend::BinaryExpression;
using neo::frontend::BinaryOperator;
using neo::frontend::BlockStatement;
using neo::frontend::BooleanLiteral;
using neo::frontend::CallExpression;
using neo::frontend::DebuggerStatement;
using neo::frontend::ExpressionStatement;
using neo::frontend::DoWhileStatement;
using neo::frontend::FunctionDeclaration;
using neo::frontend::Identifier;
using neo::frontend::IfStatement;
using neo::frontend::MemberExpression;
using neo::frontend::NodeList;
using neo::frontend::NumericLiteral;
using neo::frontend::ObjectExpression;
using neo::frontend::Parser;
using neo::frontend::Property;
using neo::frontend::ParserOptions;
using neo::frontend::Program;
using neo::frontend::ReturnStatement;
using neo::frontend::SourceRange;
using neo::frontend::StringLiteral;
using neo::frontend::SourceType;
using neo::frontend::UpdateExpression;
using neo::frontend::UpdateOperator;
using neo::frontend::VariableDeclaration;
using neo::frontend::VariableDeclarator;
using neo::frontend::VariableKind;
using neo::frontend::WhileStatement;
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

TEST(CompilerTest, CompilesFunctionDeclarationAndCall) {
    SourceRange range{};

    // function add(a, b) { return a + b; }  return add(1, 2);
    NodeList params;
    params.push_back(std::make_unique<Identifier>(range, "a"));
    params.push_back(std::make_unique<Identifier>(range, "b"));

    NodeList bodyStatements;
    bodyStatements.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<BinaryExpression>(
                   range, BinaryOperator::Add,
                   std::make_unique<Identifier>(range, "a"),
                   std::make_unique<Identifier>(range, "b"))));
    auto fnBody = std::make_unique<BlockStatement>(
        range, std::move(bodyStatements));

    NodeList programBody;
    programBody.push_back(std::make_unique<FunctionDeclaration>(
        range, std::make_unique<Identifier>(range, "add"), std::move(params),
        std::move(fnBody), false, false));

    NodeList args;
    args.push_back(std::make_unique<NumericLiteral>(range, 1.0));
    args.push_back(std::make_unique<NumericLiteral>(range, 2.0));
    programBody.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<CallExpression>(
                   range, std::make_unique<Identifier>(range, "add"),
                   std::move(args))));

    Program program(range, std::move(programBody), SourceType::Script);
    SemaResult sema;
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

namespace {

neo::frontend::NodePtr letDeclaration(const SourceRange& range,
                                      const char* name, double value) {
    NodeList declarations;
    declarations.push_back(std::make_unique<VariableDeclarator>(
        range, std::make_unique<Identifier>(range, name),
        std::make_unique<NumericLiteral>(range, value)));
    return std::make_unique<VariableDeclaration>(
        range, VariableKind::Let, std::move(declarations));
}

neo::frontend::NodePtr varDeclaration(const SourceRange& range,
                                      const char* name, double value) {
    NodeList declarations;
    declarations.push_back(std::make_unique<VariableDeclarator>(
        range, std::make_unique<Identifier>(range, name),
        std::make_unique<NumericLiteral>(range, value)));
    return std::make_unique<VariableDeclaration>(
        range, VariableKind::Var, std::move(declarations));
}

}  // namespace

TEST(CompilerTest, CompilesLetDeclarations) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "x", 1.0));
    body.push_back(letDeclaration(range, "y", 2.0));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<BinaryExpression>(
                   range, BinaryOperator::Add,
                   std::make_unique<Identifier>(range, "x"),
                   std::make_unique<Identifier>(range, "y"))));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
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

TEST(CompilerTest, LetReadBeforeDeclarationIsTdz) {
    SourceRange range{};
    NodeList body;
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "x")));
    body.push_back(letDeclaration(range, "x", 1.0));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::Exception);
}

TEST(CompilerTest, VarInBlockHoistsToProgramScope) {
    SourceRange range{};
    NodeList blockBody;
    blockBody.push_back(varDeclaration(range, "x", 7.0));
    NodeList body;
    body.push_back(std::make_unique<BlockStatement>(range,
                                                    std::move(blockBody)));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "x")));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 7.0);
}

TEST(CompilerTest, FunctionDeclarationIsHoisted) {
    SourceRange range{};

    NodeList params;
    params.push_back(std::make_unique<Identifier>(range, "a"));
    params.push_back(std::make_unique<Identifier>(range, "b"));
    NodeList fnBody;
    fnBody.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<BinaryExpression>(
                   range, BinaryOperator::Add,
                   std::make_unique<Identifier>(range, "a"),
                   std::make_unique<Identifier>(range, "b"))));

    NodeList args;
    args.push_back(std::make_unique<NumericLiteral>(range, 1.0));
    args.push_back(std::make_unique<NumericLiteral>(range, 2.0));

    NodeList body;
    // 调用在声明之前：依赖函数声明提升。
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<CallExpression>(
                   range, std::make_unique<Identifier>(range, "add"),
                   std::move(args))));
    body.push_back(std::make_unique<FunctionDeclaration>(
        range, std::make_unique<Identifier>(range, "add"), std::move(params),
        std::make_unique<BlockStatement>(range, std::move(fnBody)), false,
        false));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
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

TEST(CompilerTest, AssignmentExpressionLeavesValue) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "x", 1.0));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<BinaryExpression>(
                   range, BinaryOperator::Add,
                   std::make_unique<AssignmentExpression>(
                       range, AssignmentOperator::Assign,
                       std::make_unique<Identifier>(range, "x"),
                       std::make_unique<NumericLiteral>(range, 2.0)),
                   std::make_unique<Identifier>(range, "x"))));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 4.0);  // (x=2) + x
}

TEST(CompilerTest, CompoundAssignmentUpdatesBinding) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "x", 1.0));
    body.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Add,
                   std::make_unique<Identifier>(range, "x"),
                   std::make_unique<NumericLiteral>(range, 2.0))));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "x")));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
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

namespace {

neo::frontend::NodePtr letFromExpression(const SourceRange& range,
                                         const char* name,
                                         neo::frontend::NodePtr init) {
    NodeList declarations;
    declarations.push_back(std::make_unique<VariableDeclarator>(
        range, std::make_unique<Identifier>(range, name), std::move(init)));
    return std::make_unique<VariableDeclaration>(
        range, VariableKind::Let, std::move(declarations));
}

}  // namespace

TEST(CompilerTest, PostfixUpdateYieldsOldValue) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "x", 1.0));
    body.push_back(letFromExpression(
        range, "y",
        std::make_unique<UpdateExpression>(
            range, UpdateOperator::Increment, false,
            std::make_unique<Identifier>(range, "x"))));
    // y * 10 + x  => 1 * 10 + 2 = 12
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<BinaryExpression>(
                   range, BinaryOperator::Add,
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Multiply,
                       std::make_unique<Identifier>(range, "y"),
                       std::make_unique<NumericLiteral>(range, 10.0)),
                   std::make_unique<Identifier>(range, "x"))));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, PrefixUpdateYieldsNewValue) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "x", 1.0));
    body.push_back(letFromExpression(
        range, "y",
        std::make_unique<UpdateExpression>(
            range, UpdateOperator::Increment, true,
            std::make_unique<Identifier>(range, "x"))));
    // y * 10 + x  => 2 * 10 + 2 = 22
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<BinaryExpression>(
                   range, BinaryOperator::Add,
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Multiply,
                       std::make_unique<Identifier>(range, "y"),
                       std::make_unique<NumericLiteral>(range, 10.0)),
                   std::make_unique<Identifier>(range, "x"))));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 22.0);
}

TEST(CompilerTest, CompilesIfElseBranch) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "x", 0.0));

    NodeList thenBody;
    thenBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "x"),
                   std::make_unique<NumericLiteral>(range, 3.0))));
    NodeList elseBody;
    elseBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "x"),
                   std::make_unique<NumericLiteral>(range, 4.0))));

    body.push_back(std::make_unique<IfStatement>(
        range,
        std::make_unique<BinaryExpression>(
            range, BinaryOperator::LessThan,
            std::make_unique<NumericLiteral>(range, 1.0),
            std::make_unique<NumericLiteral>(range, 2.0)),
        std::make_unique<BlockStatement>(range, std::move(thenBody)),
        std::make_unique<BlockStatement>(range, std::move(elseBody))));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "x")));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
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

TEST(CompilerTest, CompilesWhileLoop) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "i", 0.0));
    body.push_back(letDeclaration(range, "sum", 0.0));

    NodeList loopBody;
    loopBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "sum"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "sum"),
                       std::make_unique<Identifier>(range, "i")))));
    loopBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "i"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "i"),
                       std::make_unique<NumericLiteral>(range, 1.0)))));

    body.push_back(std::make_unique<WhileStatement>(
        range,
        std::make_unique<BinaryExpression>(
            range, BinaryOperator::LessThan,
            std::make_unique<Identifier>(range, "i"),
            std::make_unique<NumericLiteral>(range, 5.0)),
        std::make_unique<BlockStatement>(range, std::move(loopBody))));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "sum")));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 10.0);
}

TEST(CompilerTest, CompilesDoWhileLoopAtLeastOnce) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "n", 0.0));

    NodeList loopBody;
    loopBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "n"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "n"),
                       std::make_unique<NumericLiteral>(range, 1.0)))));

    body.push_back(std::make_unique<DoWhileStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(loopBody)),
        std::make_unique<BooleanLiteral>(range, false)));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "n")));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

namespace {

neo::frontend::NodePtr arrayCall(const SourceRange& range) {
    NodeList args;
    args.push_back(std::make_unique<NumericLiteral>(range, 1.0));
    args.push_back(std::make_unique<NumericLiteral>(range, 2.0));
    return std::make_unique<CallExpression>(
        range, std::make_unique<Identifier>(range, "Array"), std::move(args));
}

}  // namespace

TEST(CompilerTest, CompilesMemberRead) {
    SourceRange range{};
    NodeList body;
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<MemberExpression>(
                   range, arrayCall(range),
                   std::make_unique<Identifier>(range, "length"), false)));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, CompilesMethodCallWithReceiverThis) {
    SourceRange range{};
    NodeList args;
    args.push_back(std::make_unique<StringLiteral>(range, "-"));
    NodeList body;
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<CallExpression>(
                   range,
                   std::make_unique<MemberExpression>(
                       range, arrayCall(range),
                       std::make_unique<Identifier>(range, "join"), false),
                   std::move(args))));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "1-2");
}

TEST(CompilerTest, CompilesComputedMemberAssignment) {
    SourceRange range{};
    NodeList body;
    body.push_back(letFromExpression(range, "a", arrayCall(range)));
    body.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<MemberExpression>(
                       range, std::make_unique<Identifier>(range, "a"),
                       std::make_unique<NumericLiteral>(range, 0.0), true),
                   std::make_unique<NumericLiteral>(range, 5.0))));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<MemberExpression>(
                   range, std::make_unique<Identifier>(range, "a"),
                   std::make_unique<NumericLiteral>(range, 0.0), true)));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, CompilesObjectLiteral) {
    SourceRange range{};
    NodeList properties;
    properties.push_back(std::make_unique<Property>(
        range, std::make_unique<Identifier>(range, "x"),
        std::make_unique<NumericLiteral>(range, 42.0), false, false));

    NodeList body;
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<MemberExpression>(
                   range,
                   std::make_unique<ObjectExpression>(range,
                                                      std::move(properties)),
                   std::make_unique<Identifier>(range, "x"), false)));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 42.0);
}

TEST(CompilerTest, CompilesArrayLiteral) {
    SourceRange range{};
    NodeList elements;
    elements.push_back(std::make_unique<NumericLiteral>(range, 1.0));
    elements.push_back(std::make_unique<NumericLiteral>(range, 2.0));
    elements.push_back(std::make_unique<NumericLiteral>(range, 3.0));

    NodeList body;
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<MemberExpression>(
                   range,
                   std::make_unique<ArrayExpression>(range, std::move(elements)),
                   std::make_unique<NumericLiteral>(range, 1.0), true)));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    ASSERT_TRUE(result.ok());

    Fixture f;
    VM vm(f.ctx.get(), result.code.get());
    Value* value = vm.run();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, ArrayElisionPreservesLength) {
    SourceRange range{};
    NodeList elements;
    elements.push_back(std::make_unique<NumericLiteral>(range, 1.0));
    elements.push_back(nullptr);
    elements.push_back(std::make_unique<NumericLiteral>(range, 3.0));

    NodeList body;
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<MemberExpression>(
                   range,
                   std::make_unique<ArrayExpression>(range, std::move(elements)),
                   std::make_unique<Identifier>(range, "length"), false)));

    Program program(range, std::move(body), SourceType::Script);
    SemaResult sema;
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
