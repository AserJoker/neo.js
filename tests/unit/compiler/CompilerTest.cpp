// CompilerTest.cpp - Compiler 框架测试（ADR-061）
//
// 验证：
// - AST + SemaResult → Code 的纵向链路
// - 表达式语句/return/字面量/二元运算的字节码
// - SourceMap 记录节点源码位置
// - 未支持节点与 sema 错误不会静默生成可执行字节码

#include "compiler/Compiler.h"
#include "core/IContext.h"
#include "core/Scope.h"
#include "core/types/BigInt.h"
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

#include <string>

namespace {

using neo::compiler::CompileResult;
using neo::compiler::Compiler;
using neo::compiler::CompilerOptions;
using neo::core::IContext;
using neo::core::BooleanData;
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
using neo::frontend::BreakStatement;
using neo::frontend::CallExpression;
using neo::frontend::CatchClause;
using neo::frontend::ConditionalExpression;
using neo::frontend::ContinueStatement;
using neo::frontend::LabeledStatement;
using neo::frontend::LogicalExpression;
using neo::frontend::LogicalOperator;
using neo::frontend::SequenceExpression;
using neo::frontend::UnaryExpression;
using neo::frontend::UnaryOperator;
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
using neo::frontend::ThrowStatement;
using neo::frontend::TryStatement;
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

// 手工 AST：return <expr>;，编译执行后返回结果值（生命周期归 ctx）。
Value* runReturnExpression(Fixture& f, neo::frontend::NodePtr expression) {
    SourceRange range{};
    NodeList body;
    body.push_back(std::make_unique<ReturnStatement>(range,
                                                     std::move(expression)));
    Program program(range, std::move(body), SourceType::Script);

    SemaResult sema;
    Compiler compiler(program, sema);
    CompileResult result = compiler.compile();
    EXPECT_TRUE(result.ok());

    VM vm(f.ctx.get(), result.code.get());
    return vm.run();
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

TEST(CompilerTest, WhileBreakAndContinue) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "i", 0.0));

    NodeList loopBody;
    loopBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "i"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "i"),
                       std::make_unique<NumericLiteral>(range, 1.0)))));
    loopBody.push_back(std::make_unique<IfStatement>(
        range,
        std::make_unique<BinaryExpression>(
            range, BinaryOperator::LessThan,
            std::make_unique<Identifier>(range, "i"),
            std::make_unique<NumericLiteral>(range, 3.0)),
        std::make_unique<ContinueStatement>(range, ""), nullptr));
    loopBody.push_back(std::make_unique<BreakStatement>(range, ""));

    body.push_back(std::make_unique<WhileStatement>(
        range, std::make_unique<BooleanLiteral>(range, true),
        std::make_unique<BlockStatement>(range, std::move(loopBody))));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "i")));

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

TEST(CompilerTest, LabeledBreakExitsOuterLoop) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "n", 0.0));

    NodeList innerBody;
    innerBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "n"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "n"),
                       std::make_unique<NumericLiteral>(range, 1.0)))));
    innerBody.push_back(std::make_unique<BreakStatement>(range, "outer"));
    auto inner = std::make_unique<WhileStatement>(
        range, std::make_unique<BooleanLiteral>(range, true),
        std::make_unique<BlockStatement>(range, std::move(innerBody)));

    NodeList outerBody;
    outerBody.push_back(std::move(inner));
    auto outer = std::make_unique<WhileStatement>(
        range, std::make_unique<BooleanLiteral>(range, true),
        std::make_unique<BlockStatement>(range, std::move(outerBody)));
    body.push_back(std::make_unique<LabeledStatement>(
        range, "outer", std::move(outer)));
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

TEST(CompilerTest, LabeledContinueTargetsOuterLoop) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "i", 0.0));

    NodeList outerBody;
    outerBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "i"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "i"),
                       std::make_unique<NumericLiteral>(range, 1.0)))));
    NodeList innerBody;
    innerBody.push_back(std::make_unique<ContinueStatement>(range, "outer"));
    outerBody.push_back(std::make_unique<WhileStatement>(
        range, std::make_unique<BooleanLiteral>(range, true),
        std::make_unique<BlockStatement>(range, std::move(innerBody))));

    auto outer = std::make_unique<WhileStatement>(
        range,
        std::make_unique<BinaryExpression>(
            range, BinaryOperator::LessThan,
            std::make_unique<Identifier>(range, "i"),
            std::make_unique<NumericLiteral>(range, 3.0)),
        std::make_unique<BlockStatement>(range, std::move(outerBody)));
    body.push_back(std::make_unique<LabeledStatement>(
        range, "outer", std::move(outer)));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "i")));

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

TEST(CompilerTest, DoWhileContinueJumpsToTest) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "i", 0.0));

    NodeList loopBody;
    loopBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "i"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "i"),
                       std::make_unique<NumericLiteral>(range, 1.0)))));
    loopBody.push_back(std::make_unique<IfStatement>(
        range,
        std::make_unique<BinaryExpression>(
            range, BinaryOperator::LessThan,
            std::make_unique<Identifier>(range, "i"),
            std::make_unique<NumericLiteral>(range, 3.0)),
        std::make_unique<ContinueStatement>(range, ""), nullptr));

    body.push_back(std::make_unique<DoWhileStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(loopBody)),
        std::make_unique<BinaryExpression>(
            range, BinaryOperator::LessThan,
            std::make_unique<Identifier>(range, "i"),
            std::make_unique<NumericLiteral>(range, 3.0))));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "i")));

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

TEST(CompilerTest, BreakRestoresScopePastInnerBlock) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "x", 1.0));

    NodeList innerBlock;
    innerBlock.push_back(letDeclaration(range, "x", 2.0));
    innerBlock.push_back(std::make_unique<BreakStatement>(range, ""));

    NodeList loopBody;
    loopBody.push_back(std::make_unique<BlockStatement>(
        range, std::move(innerBlock)));
    body.push_back(std::make_unique<WhileStatement>(
        range, std::make_unique<BooleanLiteral>(range, true),
        std::make_unique<BlockStatement>(range, std::move(loopBody))));
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
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, CompilesTryCatch) {
    SourceRange range{};
    NodeList tryBody;
    tryBody.push_back(std::make_unique<ThrowStatement>(
        range, std::make_unique<NumericLiteral>(range, 1.0)));

    NodeList catchBody;
    catchBody.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "e")));
    auto handler = std::make_unique<CatchClause>(
        range, std::make_unique<Identifier>(range, "e"),
        std::make_unique<BlockStatement>(range, std::move(catchBody)));

    NodeList body;
    body.push_back(std::make_unique<TryStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(tryBody)),
        std::move(handler), nullptr));

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

TEST(CompilerTest, FinallyRunsOnNormalCompletion) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "x", 0.0));

    NodeList tryBody;
    tryBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "x"),
                   std::make_unique<NumericLiteral>(range, 1.0))));
    NodeList finallyBody;
    finallyBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "x"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "x"),
                       std::make_unique<NumericLiteral>(range, 1.0)))));
    body.push_back(std::make_unique<TryStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(tryBody)),
        nullptr,
        std::make_unique<BlockStatement>(range, std::move(finallyBody))));
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
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, FinallyReturnOverridesTryReturn) {
    SourceRange range{};
    NodeList tryBody;
    tryBody.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<NumericLiteral>(range, 1.0)));
    NodeList finallyBody;
    finallyBody.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<NumericLiteral>(range, 2.0)));

    NodeList body;
    body.push_back(std::make_unique<TryStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(tryBody)),
        nullptr,
        std::make_unique<BlockStatement>(range, std::move(finallyBody))));

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

TEST(CompilerTest, FinallyReturnOverridesCatchThrow) {
    SourceRange range{};
    NodeList tryBody;
    tryBody.push_back(std::make_unique<ThrowStatement>(
        range, std::make_unique<NumericLiteral>(range, 1.0)));
    NodeList catchBody;
    catchBody.push_back(std::make_unique<ThrowStatement>(
        range, std::make_unique<NumericLiteral>(range, 2.0)));
    auto handler = std::make_unique<CatchClause>(
        range, std::make_unique<Identifier>(range, "e"),
        std::make_unique<BlockStatement>(range, std::move(catchBody)));
    NodeList finallyBody;
    finallyBody.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<NumericLiteral>(range, 3.0)));

    NodeList body;
    body.push_back(std::make_unique<TryStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(tryBody)),
        std::move(handler),
        std::make_unique<BlockStatement>(range, std::move(finallyBody))));

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

TEST(CompilerTest, CatchRethrowConsumesFinallyBeforeOuterCatch) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "x", 0.0));

    NodeList innerTryBody;
    innerTryBody.push_back(std::make_unique<ThrowStatement>(
        range, std::make_unique<NumericLiteral>(range, 1.0)));
    NodeList innerFinallyBody;
    innerFinallyBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "x"),
                   std::make_unique<NumericLiteral>(range, 5.0))));
    NodeList outerTryBody;
    outerTryBody.push_back(std::make_unique<TryStatement>(
        range,
        std::make_unique<BlockStatement>(range, std::move(innerTryBody)),
        nullptr,
        std::make_unique<BlockStatement>(range, std::move(innerFinallyBody))));

    NodeList catchBody;
    catchBody.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<BinaryExpression>(
                   range, BinaryOperator::Add,
                   std::make_unique<Identifier>(range, "e"),
                   std::make_unique<Identifier>(range, "x"))));
    auto handler = std::make_unique<CatchClause>(
        range, std::make_unique<Identifier>(range, "e"),
        std::make_unique<BlockStatement>(range, std::move(catchBody)));

    body.push_back(std::make_unique<TryStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(outerTryBody)),
        std::move(handler), nullptr));

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
    EXPECT_DOUBLE_EQ(numberValue(value), 6.0);
}

TEST(CompilerTest, CatchRethrowRunsOwnFinallyBeforeOuterCatch) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "x", 0.0));

    NodeList innerTryBody;
    innerTryBody.push_back(std::make_unique<ThrowStatement>(
        range, std::make_unique<NumericLiteral>(range, 1.0)));
    NodeList innerCatchBody;
    innerCatchBody.push_back(std::make_unique<ThrowStatement>(
        range, std::make_unique<NumericLiteral>(range, 2.0)));
    auto innerHandler = std::make_unique<CatchClause>(
        range, std::make_unique<Identifier>(range, "e"),
        std::make_unique<BlockStatement>(range, std::move(innerCatchBody)));
    NodeList innerFinallyBody;
    innerFinallyBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "x"),
                   std::make_unique<NumericLiteral>(range, 5.0))));
    NodeList outerTryBody;
    outerTryBody.push_back(std::make_unique<TryStatement>(
        range,
        std::make_unique<BlockStatement>(range, std::move(innerTryBody)),
        std::move(innerHandler),
        std::make_unique<BlockStatement>(range, std::move(innerFinallyBody))));

    NodeList catchBody;
    catchBody.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<BinaryExpression>(
                   range, BinaryOperator::Add,
                   std::make_unique<Identifier>(range, "e"),
                   std::make_unique<Identifier>(range, "x"))));
    auto handler = std::make_unique<CatchClause>(
        range, std::make_unique<Identifier>(range, "e"),
        std::make_unique<BlockStatement>(range, std::move(catchBody)));

    body.push_back(std::make_unique<TryStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(outerTryBody)),
        std::move(handler), nullptr));

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

TEST(CompilerTest, BreakCrossesFinally) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "i", 0.0));

    NodeList tryBody;
    tryBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "i"),
                   std::make_unique<NumericLiteral>(range, 1.0))));
    tryBody.push_back(std::make_unique<BreakStatement>(range, ""));
    NodeList finallyBody;
    finallyBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "i"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "i"),
                       std::make_unique<NumericLiteral>(range, 1.0)))));
    NodeList loopBody;
    loopBody.push_back(std::make_unique<TryStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(tryBody)),
        nullptr,
        std::make_unique<BlockStatement>(range, std::move(finallyBody))));

    body.push_back(std::make_unique<WhileStatement>(
        range, std::make_unique<BooleanLiteral>(range, true),
        std::make_unique<BlockStatement>(range, std::move(loopBody))));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "i")));

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

TEST(CompilerTest, ContinueCrossesFinally) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "i", 0.0));

    NodeList tryBody;
    tryBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "i"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "i"),
                       std::make_unique<NumericLiteral>(range, 1.0)))));
    tryBody.push_back(std::make_unique<ContinueStatement>(range, ""));
    NodeList finallyBody;
    finallyBody.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "i"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "i"),
                       std::make_unique<NumericLiteral>(range, 10.0)))));
    NodeList loopBody;
    loopBody.push_back(std::make_unique<TryStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(tryBody)),
        nullptr,
        std::make_unique<BlockStatement>(range, std::move(finallyBody))));

    body.push_back(std::make_unique<WhileStatement>(
        range,
        std::make_unique<BinaryExpression>(
            range, BinaryOperator::LessThan,
            std::make_unique<Identifier>(range, "i"),
            std::make_unique<NumericLiteral>(range, 2.0)),
        std::make_unique<BlockStatement>(range, std::move(loopBody))));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "i")));

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
    EXPECT_DOUBLE_EQ(numberValue(value), 11.0);
}

TEST(CompilerTest, NestedFinallyRunsInnerToOuter) {
    SourceRange range{};
    NodeList body;
    body.push_back(letDeclaration(range, "trace", 0.0));
    body.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "trace"),
                   std::make_unique<StringLiteral>(range, ""))));

    NodeList innerFinally;
    innerFinally.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "trace"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "trace"),
                       std::make_unique<StringLiteral>(range, "a")))));
    NodeList innerTry;
    innerTry.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<NumericLiteral>(range, 1.0)));
    auto inner = std::make_unique<TryStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(innerTry)),
        nullptr,
        std::make_unique<BlockStatement>(range, std::move(innerFinally)));

    NodeList outerFinally;
    outerFinally.push_back(std::make_unique<ExpressionStatement>(
        range, std::make_unique<AssignmentExpression>(
                   range, AssignmentOperator::Assign,
                   std::make_unique<Identifier>(range, "trace"),
                   std::make_unique<BinaryExpression>(
                       range, BinaryOperator::Add,
                       std::make_unique<Identifier>(range, "trace"),
                       std::make_unique<StringLiteral>(range, "b")))));
    outerFinally.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<Identifier>(range, "trace")));

    NodeList outerTry;
    outerTry.push_back(std::move(inner));
    body.push_back(std::make_unique<TryStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(outerTry)),
        nullptr,
        std::make_unique<BlockStatement>(range, std::move(outerFinally))));

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
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "ab");
}

TEST(CompilerTest, ThrowInFinallyOverridesPendingReturn) {
    SourceRange range{};
    NodeList tryBody;
    tryBody.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<NumericLiteral>(range, 1.0)));
    NodeList finallyBody;
    finallyBody.push_back(std::make_unique<ThrowStatement>(
        range, std::make_unique<NumericLiteral>(range, 2.0)));

    NodeList body;
    body.push_back(std::make_unique<TryStatement>(
        range, std::make_unique<BlockStatement>(range, std::move(tryBody)),
        nullptr,
        std::make_unique<BlockStatement>(range, std::move(finallyBody))));

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

TEST(CompilerTest, SourceMapRecordsNodePosition) {
    CompileResult result = compileSource("1 + 2;", "entry.js");
    ASSERT_TRUE(result.ok());

    const neo::core::SourcePosition* position = result.code->sourceMap.lookup(0);
    ASSERT_NE(position, nullptr);
    EXPECT_EQ(position->filename, "entry.js");
    EXPECT_EQ(position->line, 1u);
    EXPECT_EQ(position->column, 1u);
}

TEST(CompilerTest, DebuggerStatementIsNoOp) {
    SourceRange range{};
    NodeList body;
    body.push_back(std::make_unique<DebuggerStatement>(range));
    body.push_back(std::make_unique<ReturnStatement>(
        range, std::make_unique<BinaryExpression>(
                   range, BinaryOperator::Add,
                   std::make_unique<NumericLiteral>(range, 1.0),
                   std::make_unique<NumericLiteral>(range, 2.0))));
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

TEST(CompilerTest, UnsupportedStatementRecordsError) {
    SourceRange range{};
    NodeList body;
    body.push_back(std::make_unique<Identifier>(range, "x"));
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

TEST(CompilerTest, CompilesUnaryOperators) {
    SourceRange range{};
    Fixture f;

    Value* negated = runReturnExpression(
        f, std::make_unique<UnaryExpression>(
               range, UnaryOperator::Minus,
               std::make_unique<NumericLiteral>(range, 5.0)));
    ASSERT_NE(negated, nullptr);
    ASSERT_EQ(negated->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(negated), -5.0);

    Value* notZero = runReturnExpression(
        f, std::make_unique<UnaryExpression>(
               range, UnaryOperator::LogicalNot,
               std::make_unique<NumericLiteral>(range, 0.0)));
    ASSERT_NE(notZero, nullptr);
    ASSERT_EQ(notZero->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(static_cast<BooleanData*>(notZero->data())->value);

    Value* bitNot = runReturnExpression(
        f, std::make_unique<UnaryExpression>(
               range, UnaryOperator::BitNot,
               std::make_unique<NumericLiteral>(range, 1.0)));
    ASSERT_NE(bitNot, nullptr);
    ASSERT_EQ(bitNot->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(bitNot), -2.0);

    Value* typeOf = runReturnExpression(
        f, std::make_unique<UnaryExpression>(
               range, UnaryOperator::TypeOf,
               std::make_unique<NumericLiteral>(range, 1.0)));
    ASSERT_NE(typeOf, nullptr);
    ASSERT_EQ(typeOf->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(typeOf->data())->value, "number");

    Value* voided = runReturnExpression(
        f, std::make_unique<UnaryExpression>(
               range, UnaryOperator::Void,
               std::make_unique<NumericLiteral>(range, 1.0)));
    ASSERT_NE(voided, nullptr);
    EXPECT_EQ(voided->type()->kind(), TypeKind::Undefined);
}

TEST(CompilerTest, CompilesLogicalOperatorsWithShortCircuit) {
    SourceRange range{};
    Fixture f;

    Value* andValue = runReturnExpression(
        f, std::make_unique<LogicalExpression>(
               range, LogicalOperator::And,
               std::make_unique<NumericLiteral>(range, 0.0),
               std::make_unique<NumericLiteral>(range, 1.0)));
    ASSERT_NE(andValue, nullptr);
    ASSERT_EQ(andValue->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(andValue), 0.0);

    Value* orValue = runReturnExpression(
        f, std::make_unique<LogicalExpression>(
               range, LogicalOperator::Or,
               std::make_unique<NumericLiteral>(range, 1.0),
               std::make_unique<NumericLiteral>(range, 2.0)));
    ASSERT_NE(orValue, nullptr);
    ASSERT_EQ(orValue->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(orValue), 1.0);

    Value* nullish = runReturnExpression(
        f, std::make_unique<LogicalExpression>(
               range, LogicalOperator::Nullish,
               std::make_unique<neo::frontend::NullLiteral>(range),
               std::make_unique<NumericLiteral>(range, 3.0)));
    ASSERT_NE(nullish, nullptr);
    ASSERT_EQ(nullish->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(nullish), 3.0);

    Value* notNullish = runReturnExpression(
        f, std::make_unique<LogicalExpression>(
               range, LogicalOperator::Nullish,
               std::make_unique<NumericLiteral>(range, 0.0),
               std::make_unique<NumericLiteral>(range, 3.0)));
    ASSERT_NE(notNullish, nullptr);
    ASSERT_EQ(notNullish->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(notNullish), 0.0);
}

TEST(CompilerTest, CompilesConditionalExpression) {
    SourceRange range{};
    Fixture f;

    Value* consequent = runReturnExpression(
        f, std::make_unique<ConditionalExpression>(
               range, std::make_unique<NumericLiteral>(range, 1.0),
               std::make_unique<NumericLiteral>(range, 2.0),
               std::make_unique<NumericLiteral>(range, 3.0)));
    ASSERT_NE(consequent, nullptr);
    ASSERT_EQ(consequent->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(consequent), 2.0);

    Value* alternate = runReturnExpression(
        f, std::make_unique<ConditionalExpression>(
               range, std::make_unique<NumericLiteral>(range, 0.0),
               std::make_unique<NumericLiteral>(range, 2.0),
               std::make_unique<NumericLiteral>(range, 3.0)));
    ASSERT_NE(alternate, nullptr);
    ASSERT_EQ(alternate->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(alternate), 3.0);
}

TEST(CompilerTest, CompilesSequenceExpression) {
    SourceRange range{};
    NodeList expressions;
    expressions.push_back(std::make_unique<NumericLiteral>(range, 1.0));
    expressions.push_back(std::make_unique<NumericLiteral>(range, 2.0));
    expressions.push_back(std::make_unique<NumericLiteral>(range, 3.0));

    Fixture f;
    Value* value = runReturnExpression(
        f, std::make_unique<SequenceExpression>(range, std::move(expressions)));
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

namespace {

// 执行脚本并从全局作用域读取 result（脚本以 `var result` 承载结果）。
Value* runScriptResult(Fixture& f, const std::string& source) {
    CompileResult result = compileSource(source);
    EXPECT_TRUE(result.ok());
    if (!result.ok()) return nullptr;
    VM vm(f.ctx.get(), result.code.get());
    (void)vm.run();
    return f.ctx->root()->lookup("result");
}

// 执行脚本并排空任务队列（Promise/async 结果需要微任务推进）。
Value* runScriptResultDrained(Fixture& f, const std::string& source) {
    CompileResult result = compileSource(source);
    EXPECT_TRUE(result.ok());
    if (!result.ok()) return nullptr;
    VM vm(f.ctx.get(), result.code.get());
    (void)vm.run();
    while (f.ctx->hasRunnableTask()) (void)f.ctx->runOneTask();
    return f.ctx->root()->lookup("result");
}

}  // namespace

TEST(CompilerTest, CompilesBigIntLiteral) {
    Fixture f;
    Value* value =
        runScriptResult(f, "var result = 12345678901234567890n;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::BigInt);
    auto* data = static_cast<neo::core::BigIntData*>(value->data());
    EXPECT_EQ(data->value.toString(10), "12345678901234567890");
}

TEST(CompilerTest, CompilesBigIntLiteralWithRadix) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var result = 0xffn + 0b101n + 0o17n;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::BigInt);
    auto* data = static_cast<neo::core::BigIntData*>(value->data());
    EXPECT_EQ(data->value.toString(10), "275");
}

TEST(CompilerTest, CompilesObjectAccessor) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var o = { _v: 1, get x() { return this._v; },"
        "          set x(v) { this._v = v; } };"
        "o.x = 5;"
        "var result = o.x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, CompilesGetterOnlyAccessor) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var o = { get x() { return 42; } }; var result = o.x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 42.0);
}

TEST(CompilerTest, CompilesComputedAccessor) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var k = 'x';"
        "var o = { get [k]() { return 7; } };"
        "var result = o.x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 7.0);
}

TEST(CompilerTest, CompilesWithStatementRead) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var o = { x: 5 };"
        "with (o) { result = x; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, CompilesWithStatementWrite) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var o = { x: 1 };"
        "with (o) { x = 7; }"
        "var result = o.x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 7.0);
}

TEST(CompilerTest, CompilesObjectLiteralProto) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var p = { a: 1 };"
        "var o = { __proto__: p };"
        "var result = o.a;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, CompilesObjectLiteralProtoNonObjectIsNoOp) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var o = { __proto__: 42 };"
        "var result = Object.getPrototypeOf(o) === Object.prototype;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(static_cast<BooleanData*>(value->data())->value);
}

TEST(CompilerTest, CompilesComputedProtoAsOwnProperty) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var o = { ['__proto__']: 1 };"
        "var result = o.__proto__;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, CompilesForLoopWithVar) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "for (var i = 0; i < 5; i = i + 1) { result = result + i; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 10.0);
}

TEST(CompilerTest, CompilesForLoopWithLet) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "for (let i = 0; i < 5; i = i + 1) { result = result + i; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 10.0);
}

TEST(CompilerTest, ForLoopContinueAndBreak) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "for (var i = 0; i < 10; i = i + 1) {"
        "  if (i < 3) continue;"
        "  if (i > 5) break;"
        "  result = result + i;"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, ForLoopLabeledContinue) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "outer: for (var i = 0; i < 3; i = i + 1) {"
        "  for (var j = 0; j < 3; j = j + 1) {"
        "    if (j > 1) continue outer;"
        "    result = result + 1;"
        "  }"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 6.0);
}

TEST(CompilerTest, ForLoopWithoutTestStopsOnBreak) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "for (var i = 0; ; i = i + 1) {"
        "  if (i > 2) break;"
        "  result = result + i;"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, ForInEnumeratesOwnKeys) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "var o = { a: 1, b: 2, c: 3 };"
        "for (var k in o) { result = result + o[k]; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 6.0);
}

TEST(CompilerTest, ForInIncludesInheritedKeysAndSkipsShadowed) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "var p = { a: 1 };"
        "var o = Object.create(p);"
        "o.a = 2;"
        "o.b = 3;"
        "for (var k in o) { result = result + 1; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, ForInSkipsDeletedKey) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var o = { a: 1, b: 2 };"
        "var result = 0;"
        "for (var k in o) {"
        "  result = result + 1;"
        "  Reflect.deleteProperty(o, 'b');"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, ForInSkipsKeyMadeNonEnumerable) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var o = { a: 1, b: 2 };"
        "var result = 0;"
        "for (var k in o) {"
        "  result = result + 1;"
        "  Object.defineProperty(o, 'b', { value: 2, enumerable: false });"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, ForInNonEnumerableOwnShadowsPrototype) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var p = { a: 1 };"
        "var o = Object.create(p);"
        "Object.defineProperty(o, 'a', { value: 2, enumerable: false });"
        "var result = 0;"
        "for (var k in o) { result = result + 1; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 0.0);
}

TEST(CompilerTest, ForInOverArray) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "var a = [10, 20];"
        "for (var k in a) { result = result + a[k]; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 30.0);
}

TEST(CompilerTest, ForInNullIsNoOp) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "for (var k in null) { result = 1; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 0.0);
}

TEST(CompilerTest, ForInLetCreatesPerIterationBinding) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "var g0;"
        "var g1;"
        "var n = 0;"
        "var o = { a: 1, b: 2 };"
        "for (let k in o) {"
        "  if (n < 1) { g0 = function () { return k; }; }"
        "  else { g1 = function () { return k; }; }"
        "  n = n + 1;"
        "}"
        "result = (g0() < g1()) ? 1 : 0;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, ForInMemberTarget) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "var box = {};"
        "var o = { a: 1, b: 2 };"
        "for (box.k in o) { result = result + 1; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, ForOfIteratesArray) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "for (var v of [1, 2, 3]) { result = result + v; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 6.0);
}

TEST(CompilerTest, ForOfIteratesString) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = '';"
        "for (var ch of 'ab') { result = result + ch; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "ab");
}

TEST(CompilerTest, ForOfStringIteratesCodePoints) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var count = 0;"
        "var astral = 0;"
        "for (var ch of 'a😀') {"
        "  count = count + 1;"
        "  if (ch === '😀') astral = 1;"
        "}"
        "var result = count * 10 + astral;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 21.0);
}

TEST(CompilerTest, StringIteratorNext) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var it = 'ab'[Symbol.iterator]();"
        "var a = it.next().value;"
        "var b = it.next().value;"
        "var done = it.next().done;"
        "var result = a + b + (done ? '1' : '0');");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "ab1");
}

TEST(CompilerTest, BreakThroughFinally) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "while (true) { try { break; } finally { result = 1; } }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, ForOfContinueOnArray) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "for (var v of [1, 2, 3]) { if (v < 2) continue; result = result + v; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, ForOfBreakOnArray) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "for (var v of [1, 2, 3]) { if (v > 1) break; result = result + v; }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, ForOfBreakClosesIterator) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "var closed = 0;"
        "var n = 0;"
        "var iterable = {};"
        "iterable[Symbol.iterator] = function () {"
        "  return {"
        "    next: function () { n = n + 1; return { done: false, value: n }; },"
        "    return: function () { closed = 1; return {}; }"
        "  };"
        "};"
        "for (var v of iterable) { if (v > 2) break; result = result + v; }"
        "result = result + closed * 100;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 103.0);
}

TEST(CompilerTest, ForOfNormalCompletionDoesNotCloseIterator) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "var closed = 0;"
        "var n = 0;"
        "var iterable = {};"
        "iterable[Symbol.iterator] = function () {"
        "  return {"
        "    next: function () {"
        "      n = n + 1;"
        "      if (n < 2) { return { done: false, value: n }; }"
        "      return { done: true };"
        "    },"
        "    return: function () { closed = 1; return {}; }"
        "  };"
        "};"
        "for (var v of iterable) { result = result + v; }"
        "result = result + closed * 100;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, ForOfReturnClosesIterator) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var closed = 0;"
        "var n = 0;"
        "var iterable = {};"
        "iterable[Symbol.iterator] = function () {"
        "  return {"
        "    next: function () { n = n + 1; return { done: false, value: n }; },"
        "    return: function () { closed = 1; return {}; }"
        "  };"
        "};"
        "function f() {"
        "  for (var v of iterable) { if (v > 1) return 5; }"
        "  return 0;"
        "}"
        "var result = f() + closed * 100;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 105.0);
}

TEST(CompilerTest, ForOfLetCreatesPerIterationBinding) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "var g0;"
        "var g1;"
        "var n = 0;"
        "for (let v of [10, 20]) {"
        "  if (n < 1) { g0 = function () { return v; }; }"
        "  else { g1 = function () { return v; }; }"
        "  n = n + 1;"
        "}"
        "result = g0() + g1();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 30.0);
}

TEST(CompilerTest, SwitchMatchesCaseAndBreaks) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "switch (2) {"
        "  case 1: result = 1; break;"
        "  case 2: result = 2; break;"
        "  default: result = 3;"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, SwitchFallsThrough) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "switch (1) {"
        "  case 1: result = result + 1;"
        "  case 2: result = result + 2; break;"
        "  case 3: result = result + 3;"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, SwitchDefaultInMiddleFallsThrough) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "switch (9) {"
        "  case 1: result = 1; break;"
        "  default: result = 2;"
        "  case 3: result = result + 3; break;"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, SwitchUsesStrictEquality) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "switch ('1') {"
        "  case 1: result = 1; break;"
        "  default: result = 2;"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, SwitchCaseHasSharedLexicalScope) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "switch (1) {"
        "  case 1: let x = 5; result = x; break;"
        "  default: result = 9;"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, SwitchLabeledBreak) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "a: switch (1) {"
        "  case 1: result = 1; break a;"
        "  default: result = 2;"
        "}"
        "result = result + 10;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 11.0);
}

TEST(CompilerTest, SwitchContinueTargetsEnclosingLoop) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "for (var i = 0; i < 3; i = i + 1) {"
        "  switch (i) { case 1: continue; }"
        "  result = result + i;"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, NewConstructsObject) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function C(x) { this.x = x; }"
        "var o = new C(7);"
        "var result = o.x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 7.0);
}

TEST(CompilerTest, NewWithoutArguments) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function C() { this.x = 3; }"
        "var result = (new C()).x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, NewObjectReturnOverridesInstance) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function C() { this.x = 1; return { x: 2 }; }"
        "var result = (new C()).x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, NewPrimitiveReturnIgnored) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function C() { this.x = 1; return 5; }"
        "var result = (new C()).x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, NewTargetInConstructorIsCallee) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var t;"
        "function F() { t = new.target; }"
        "new F();"
        "var result = t === F ? 1 : 0;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, NewTargetInPlainCallIsUndefined) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var t;"
        "function F() { t = new.target; }"
        "F();"
        "var result = t === void 0 ? 1 : 0;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, TemplateNoSubstitution) {
    Fixture f;
    Value* value = runScriptResult(f, "var result = `hello`;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "hello");
}

TEST(CompilerTest, TemplateInterpolation) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var x = 2; var result = `a${x}b${x + 1}`;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "a2b3");
}

TEST(CompilerTest, TemplateSubstitutionUsesStringHint) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var o = { toString: function () { return 's'; },"
        "          valueOf: function () { return 1; } };"
        "var result = `${o}`;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "s");
}

TEST(CompilerTest, TaggedTemplateReceivesCookedAndRaw) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function tag(strings) {"
        "  return strings.length * 10 + strings.raw.length;"
        "}"
        "var result = tag`a${1}b`;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 22.0);
}

TEST(CompilerTest, TaggedTemplateRawPreservesEscapes) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function tag(strings) { return strings.raw[0]; }"
        "var result = tag`a\\nb`;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "a\\nb");
}

TEST(CompilerTest, TaggedTemplateInvalidEscapeHasUndefinedCooked) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function tag(strings) { return strings[0] === void 0 ? 1 : 0; }"
        "var result = tag`\\8`;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, TaggedTemplateInvalidEscapeKeepsRaw) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function tag(strings) { return strings.raw[0]; }"
        "var result = tag`\\8`;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "\\8");
}

TEST(CompilerTest, TaggedTemplateObjectIsCachedPerSite) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function tag(strings) { return strings; }"
        "function get() { return tag`x`; }"
        "var a = get();"
        "var b = get();"
        "var result = a === b ? 1 : 0;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, TaggedTemplateMemberUsesReceiverAsThis) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var obj = { name: 'o',"
        "            tag: function (strings) { return this.name + strings[0]; } };"
        "var result = obj.tag`!`;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "o!");
}

TEST(CompilerTest, StringRawUsesRawSegments) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var result = String.raw`a\\nb`;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "a\\nb");
}

TEST(CompilerTest, StringRawInterleavesSubstitutions) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var result = String.raw`a${1}b${2}c`;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "a1b2c");
}

TEST(CompilerTest, ArraySpread) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var a = [1, 2];"
        "var b = [...a, 3];"
        "var result = b.length * 10 + b[2];");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 33.0);
}

TEST(CompilerTest, ArraySpreadString) {
    Fixture f;
    Value* value = runScriptResult(f, "var result = [...'ab'].length;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, ArraySpreadWithElision) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var result = [1, , ...['x']].length;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, ObjectSpread) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var a = { x: 1 };"
        "var b = { ...a, y: 2 };"
        "var result = b.x * 10 + b.y;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, ObjectSpreadLaterPropertyWins) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var result = ({ x: 1, ...{ x: 2 } }).x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, ObjectSpreadNullIsIgnored) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var result = ({ ...null, x: 1 }).x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, CallSpread) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function f(a, b, c) { return a + b + c; }"
        "var args = [1, 2, 3];"
        "var result = f(...args);");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 6.0);
}

TEST(CompilerTest, CallMethodSpreadKeepsReceiver) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var o = { base: 10, add: function (a, b) { return this.base + a + b; } };"
        "var args = [1, 2];"
        "var result = o.add(...args);");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 13.0);
}

TEST(CompilerTest, NewSpread) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function C(a, b) { this.v = a + b; }"
        "var args = [1, 2];"
        "var result = (new C(...args)).v;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, ArrayPatternDeclaration) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var [a, b] = [1, 2]; var result = a * 10 + b;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, ArrayPatternDefaults) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var [a = 5, b = 6] = [1]; var result = a * 10 + b;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 16.0);
}

TEST(CompilerTest, ArrayPatternRest) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var [a, ...rest] = [1, 2, 3];"
        "var result = a * 10 + rest.length * 10 + rest[0];");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 32.0);
}

TEST(CompilerTest, ArrayPatternElision) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var [, b] = [1, 2]; var result = b;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, ObjectPatternDeclaration) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var { x, y } = { x: 1, y: 2 }; var result = x * 10 + y;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, ObjectPatternRename) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var { x: a } = { x: 7 }; var result = a;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 7.0);
}

TEST(CompilerTest, ObjectPatternDefault) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var { x = 5 } = {}; var result = x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, ObjectPatternRest) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var { x, ...rest } = { x: 1, y: 2, z: 3 };"
        "var result = x * 100 + rest.y * 10 + rest.z;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 123.0);
}

TEST(CompilerTest, NestedPatternDeclaration) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var { a: [b] } = { a: [9] }; var result = b;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 9.0);
}

TEST(CompilerTest, LetPatternDeclaration) {
    Fixture f;
    Value* value = runScriptResult(
        f, "let [a, b] = [1, 2]; var result = a + b;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, ArrayDestructuringAssignment) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var a, b; [a, b] = [1, 2]; var result = a * 10 + b;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, ObjectDestructuringAssignment) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var a, b; ({ a, b } = { a: 1, b: 2 }); var result = a * 10 + b;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, DestructuringAssignmentValueIsRhs) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var a; var result = ([a] = [7])[0];");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 7.0);
}

TEST(CompilerTest, DestructuringAssignmentToMember) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var o = {}; [o.x] = [5]; var result = o.x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, ArrayPatternParameter) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function f([a, b]) { return a * 10 + b; }"
        "var result = f([1, 2]);");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, ObjectPatternParameter) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function f({ x, y }) { return x * 10 + y; }"
        "var result = f({ x: 1, y: 2 });");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, DefaultParameter) {
    Fixture f;
    Value* value = runScriptResult(
        f, "function f(a = 5) { return a; } var result = f();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, PatternParameterDefault) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function f([a] = [9]) { return a; }"
        "var result = f();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 9.0);
}

TEST(CompilerTest, AsyncGeneratorYield) {
    Fixture f;
    Value* value = runScriptResultDrained(
        f,
        "async function* g() { yield 1; yield 2; }"
        "var it = g();"
        "var result = 0;"
        "it.next().then(function (r) { result = r.value; });");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, AsyncGeneratorAwait) {
    Fixture f;
    Value* value = runScriptResultDrained(
        f,
        "async function* g() { var x = await 5; yield x; }"
        "var it = g();"
        "var result = 0;"
        "it.next().then(function (r) { result = r.value; });");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, AsyncGeneratorSequentialNext) {
    Fixture f;
    Value* value = runScriptResultDrained(
        f,
        "async function* g() { yield 1; yield 2; }"
        "var it = g();"
        "var result = 0;"
        "it.next().then(function (r) { result = r.value * 10; return it.next(); })"
        ".then(function (r) { result = result + r.value; });");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, AsyncGeneratorReturn) {
    Fixture f;
    Value* value = runScriptResultDrained(
        f,
        "async function* g() { yield 1; }"
        "var it = g();"
        "var result = 0;"
        "it.return(9).then(function (r) { result = (r.done ? 10 : 0) + r.value; });");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 19.0);
}

TEST(CompilerTest, YieldDelegateArray) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function* g() { yield* [1, 2, 3]; }"
        "var it = g();"
        "var a = it.next().value;"
        "var b = it.next().value;"
        "var c = it.next().value;"
        "var result = a * 100 + b * 10 + c;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 123.0);
}

TEST(CompilerTest, YieldDelegateNested) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function* inner() { yield 1; yield 2; }"
        "function* outer() { yield 0; yield* inner(); yield 3; }"
        "var it = outer();"
        "var result = 0;"
        "var step = it.next();"
        "while (!step.done) { result = result * 10 + step.value; step = it.next(); }");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 123.0);
}

TEST(CompilerTest, YieldDelegateForwardsSentValue) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function* inner() { var x = yield 1; yield x; }"
        "function* outer() { yield* inner(); }"
        "var it = outer();"
        "it.next();"
        "var result = it.next(42).value;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 42.0);
}

TEST(CompilerTest, YieldDelegateCompletionValue) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function* g() { var x = yield* [1, 2]; return x; }"
        "var it = g();"
        "it.next();"
        "it.next();"
        "var result = it.next().done ? 1 : 0;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, YieldDelegateForwardsThrow) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function* inner() { try { yield 1; } catch (e) { yield e; } }"
        "function* outer() { yield* inner(); }"
        "var it = outer();"
        "it.next();"
        "var result = it.throw(7).value;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 7.0);
}

TEST(CompilerTest, YieldDelegateForwardsReturn) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var log = 0;"
        "function* inner() { try { yield 1; } finally { log = 1; } }"
        "function* outer() { yield* inner(); }"
        "var it = outer();"
        "it.next();"
        "var r = it.return(9);"
        "var result = log * 10 + (r.done ? 1 : 0);");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 11.0);
}

TEST(CompilerTest, ClassMethodCall) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "class A { m() { return 5; } }"
        "var result = new A().m();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, ClassConstructor) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "class A { constructor(x) { this.x = x; } }"
        "var result = new A(7).x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 7.0);
}

TEST(CompilerTest, ClassFields) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "class A { x = 2; y = this.x + 1; }"
        "var result = new A().y;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, ClassStaticMembers) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "class A { static m() { return 4; } static x = 6; }"
        "var result = A.m() * 10 + A.x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 46.0);
}

TEST(CompilerTest, ClassGetter) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "class A { get x() { return 8; } }"
        "var result = new A().x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 8.0);
}

TEST(CompilerTest, ClassStaticBlock) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "class A { static x; static { A.x = 11; } }"
        "var result = A.x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 11.0);
}

TEST(CompilerTest, ClassExpression) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var C = class { m() { return 1; } };"
        "var result = new C().m();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, ClassExtendsInheritsMethod) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "class B { m() { return 2; } }"
        "class A extends B { n() { return this.m() + 1; } }"
        "var result = new A().n();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, ClassSuperConstructor) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "class B { constructor(x) { this.x = x; } }"
        "class A extends B { constructor() { super(9); } }"
        "var result = new A().x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 9.0);
}

TEST(CompilerTest, ClassDefaultDerivedConstructor) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "class B { constructor(x) { this.x = x; } }"
        "class A extends B {}"
        "var result = new A(5).x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, ClassSuperMethod) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "class B { m() { return 3; } }"
        "class A extends B { m() { return super.m() + 1; } }"
        "var result = new A().m();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 4.0);
}

TEST(CompilerTest, ClassDerivedFields) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "class B { constructor() { this.a = 1; } }"
        "class A extends B { b = this.a + 1; }"
        "var result = new A().b;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 2.0);
}

TEST(CompilerTest, OptionalMemberShortCircuits) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var o = null; var result = o?.x;");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::Undefined);
}

TEST(CompilerTest, OptionalMemberReadsWhenPresent) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var o = { x: 7 }; var result = o?.x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 7.0);
}

TEST(CompilerTest, OptionalChainSkipsRemainingSuffixes) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var o = null; var result = o?.x.y.z;");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::Undefined);
}

TEST(CompilerTest, OptionalComputedMember) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var o = null; var result = o?.['x'];");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::Undefined);
}

TEST(CompilerTest, OptionalCallShortCircuits) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var fn = null; var result = fn?.();");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::Undefined);
}

TEST(CompilerTest, OptionalCallSkipsArguments) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var n = 0;"
        "function bump() { n = n + 1; }"
        "var fn = null;"
        "var skipped = fn?.(bump());"
        "var result = n;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 0.0);
}

TEST(CompilerTest, OptionalCallInvokesWhenPresent) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var fn = function () { return 5; };"
        "var result = fn?.();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, OptionalMethodCallKeepsReceiver) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var o = { x: 3, m: function () { return this.x; } };"
        "var result = o.m?.();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, OptionalMethodCallShortCircuits) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var o = {}; var result = o.m?.();");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::Undefined);
}

TEST(CompilerTest, OptionalMemberCallShortCircuits) {
    Fixture f;
    Value* value = runScriptResult(
        f, "var o = null; var result = o?.m();");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::Undefined);
}

TEST(CompilerTest, RestParameter) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function f(...args) { return args.length * 10 + args[0]; }"
        "var result = f(3, 4, 5);");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 33.0);
}

TEST(CompilerTest, RestParameterEmpty) {
    Fixture f;
    Value* value = runScriptResult(
        f, "function f(...args) { return args.length; } var result = f();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 0.0);
}

TEST(CompilerTest, RestParameterWithFixed) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function f(a, ...rest) { return a * 100 + rest.length * 10 + rest[0]; }"
        "var result = f(1, 2, 3);");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 122.0);
}

TEST(CompilerTest, RestParameterPattern) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "function f(...[a, b]) { return a * 10 + b; }"
        "var result = f(1, 2);");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, ForLetCreatesPerIterationBinding) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "var g0;"
        "var g1;"
        "for (let i = 0; i < 2; i = i + 1) {"
        "  if (i < 1) { g0 = function () { return i; }; }"
        "  else { g1 = function () { return i; }; }"
        "}"
        "result = g0() * 10 + g1();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, ForLetMultipleLabelsContinue) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "a: b: for (let i = 0; i < 3; i = i + 1) {"
        "  if (i < 1) continue a;"
        "  result = result + i;"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, ForLetMultipleLabelsBreak) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "a: b: for (let i = 0; i < 10; i = i + 1) {"
        "  if (i > 2) break a;"
        "  result = result + i;"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, NestedForLetLabeledContinueOuter) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "outer: for (let i = 0; i < 3; i = i + 1) {"
        "  for (let j = 0; j < 3; j = j + 1) {"
        "    if (j > 1) continue outer;"
        "    result = result + 1;"
        "  }"
        "}");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 6.0);
}

TEST(CompilerTest, ForLetPerIterationSurvivesContinue) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result = 0;"
        "var g0;"
        "var g1;"
        "for (let i = 0; i < 3; i = i + 1) {"
        "  if (i < 1) { g0 = function () { return i; }; continue; }"
        "  if (i < 2) { g1 = function () { return i; }; }"
        "}"
        "result = g0() * 10 + g1();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, CompoundMemberAssignment) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var o = { x: 2 };"
        "o.x += 3;"
        "result = o.x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, CompoundComputedMemberEvaluatesKeyOnce) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var n = 0;"
        "var o = { x: 1 };"
        "function k() { n = n + 1; return 'x'; }"
        "o[k()] += 2;"
        "result = o.x * 10 + n;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 31.0);
}

TEST(CompilerTest, CompoundSuperMemberAssignment) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "class A { get x() { return 1; } set x(v) { this._x = v; } }"
        "class B extends A { bump() { super.x += 2; return this._x; } }"
        "result = new B().bump();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, MemberPostfixUpdateReturnsOldValue) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var o = { x: 1 };"
        "result = o.x++ + o.x * 10;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 21.0);
}

TEST(CompilerTest, MemberPrefixUpdateReturnsNewValue) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var o = { x: 1 };"
        "result = ++o.x + o.x * 10;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 22.0);
}

TEST(CompilerTest, ComputedMemberUpdateEvaluatesKeyOnce) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var n = 0;"
        "var o = { x: 1 };"
        "function k() { n = n + 1; return 'x'; }"
        "o[k()]++;"
        "result = n * 10 + o.x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 12.0);
}

TEST(CompilerTest, SuperPostfixUpdateReturnsOldValue) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "class A { get x() { return 1; } set x(v) { this._x = v; } }"
        "class B extends A { post() { return super.x++ + this._x * 10; } }"
        "result = new B().post();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 21.0);
}

TEST(CompilerTest, DeleteMemberProperty) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var o = { x: 1 };"
        "var r = delete o.x;"
        "result = (r ? 10 : 0) + (o.x === void 0 ? 1 : 0);");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 11.0);
}

TEST(CompilerTest, DeleteComputedMemberProperty) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var o = { x: 1 };"
        "result = delete o['x'] ? 1 : 0;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, DeleteNonReferenceReturnsTrue) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "result = delete (1 + 2) ? 1 : 0;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, DeleteIdentifierReturnsFalseAndKeepsBinding) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var x = 1;"
        "var r = delete x;"
        "result = (r ? 10 : 0) + x;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, ArrayDestructuringClosesIteratorOnEarlyExit) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var closed = false;"
        "var iterable = {};"
        "iterable[Symbol.iterator] = function () {"
        "  var i = 0;"
        "  return {"
        "    next: function () { return { value: i++, done: false }; },"
        "    return: function () { closed = true; return { done: true }; }"
        "  };"
        "};"
        "var [a] = iterable;"
        "result = (closed ? 10 : 0) + a;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 10.0);
}

TEST(CompilerTest, ArrayDestructuringClosesIteratorOnBindingThrow) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var closed = false;"
        "var iterable = {};"
        "iterable[Symbol.iterator] = function () {"
        "  return {"
        "    next: function () { return { value: 1, done: false }; },"
        "    return: function () { closed = true; return { done: true }; }"
        "  };"
        "};"
        "function boom() { throw 1; }"
        "try { var [a = boom()] = iterable; } catch (e) {}"
        "result = closed ? 1 : 0;");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 1.0);
}

TEST(CompilerTest, ArrayDestructuringDoesNotCloseExhaustedIterator) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "var closed = false;"
        "var iterable = {};"
        "iterable[Symbol.iterator] = function () {"
        "  var i = 0;"
        "  return {"
        "    next: function () {"
        "      if (i < 2) return { value: i++, done: false };"
        "      return { value: void 0, done: true };"
        "    },"
        "    return: function () { closed = true; return { done: true }; }"
        "  };"
        "};"
        "var [a, b, c] = iterable;"
        "result = (closed ? 100 : 0) + a + b * 10 + (c === void 0 ? 1 : 0);");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 11.0);
}

TEST(CompilerTest, PrivateFieldReadWrite) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "class A {"
        "  #x = 1;"
        "  get() { return this.#x; }"
        "  set(v) { this.#x = v; }"
        "}"
        "var a = new A();"
        "a.set(5);"
        "result = a.get();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 5.0);
}

TEST(CompilerTest, PrivateMethodCall) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "class A {"
        "  #m() { return 3; }"
        "  call() { return this.#m(); }"
        "}"
        "result = new A().call();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, PrivateAccessorReadWrite) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "class A {"
        "  #v = 2;"
        "  get #x() { return this.#v; }"
        "  set #x(v) { this.#v = v; }"
        "  run() { this.#x = 7; return this.#x; }"
        "}"
        "result = new A().run();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 7.0);
}

TEST(CompilerTest, StaticPrivateFieldAndMethod) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "class A {"
        "  static #x = 4;"
        "  static #m() { return this.#x; }"
        "  static run() { return A.#m(); }"
        "}"
        "result = A.run();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 4.0);
}

TEST(CompilerTest, PrivateCompoundAndUpdate) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "class A {"
        "  #x = 1;"
        "  run() { this.#x += 2; this.#x++; return this.#x; }"
        "}"
        "result = new A().run();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 4.0);
}

TEST(CompilerTest, PrivateFieldInitializerReadsPrivate) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "class A {"
        "  #a = 2;"
        "  #b = this.#a + 1;"
        "  get() { return this.#b; }"
        "}"
        "result = new A().get();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 3.0);
}

TEST(CompilerTest, StaticBlockAccessesPrivate) {
    Fixture f;
    Value* value = runScriptResult(
        f,
        "var result;"
        "class A {"
        "  static #x = 3;"
        "  static { A.#x = A.#x + 1; }"
        "  static get() { return A.#x; }"
        "}"
        "result = A.get();");
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(value), 4.0);
}

TEST(CompilerTest, PrivateAccessOutsideClassIsRejected) {
    Fixture f;
    CompileResult result = compileSource(
        "class A { #x = 1; }"
        "var a = new A();"
        "result = a.#x;");
    EXPECT_FALSE(result.ok());
}
