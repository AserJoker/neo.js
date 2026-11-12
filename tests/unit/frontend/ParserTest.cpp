// ParserTest.cpp - 语法分析器框架与首批节点
//
// 覆盖：Program、空语句、表达式语句、标识符、各类字面量、ASI、
// 正则重扫、指令序言严格模式、错误收集。

#include "frontend/Parser.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using neo::frontend::BigIntLiteral;
using neo::frontend::BooleanLiteral;
using neo::frontend::CallExpression;
using neo::frontend::ExpressionStatement;
using neo::frontend::Identifier;
using neo::frontend::MemberExpression;
using neo::frontend::NewExpression;
using neo::frontend::NodeKind;
using neo::frontend::NumericLiteral;
using neo::frontend::Parser;
using neo::frontend::ParseResult;
using neo::frontend::Program;
using neo::frontend::RegExpLiteral;
using neo::frontend::StringLiteral;
using neo::frontend::UnaryExpression;
using neo::frontend::UnaryOperator;

ParseResult parse(std::string_view source) {
    Parser parser(source);
    return parser.parseProgram();
}

const Program& programOf(const ParseResult& result) {
    return *static_cast<const Program*>(result.program.get());
}

const ExpressionStatement& statementOf(const ParseResult& result,
                                       size_t index) {
    const auto& statement = programOf(result).body[index];
    return *static_cast<const ExpressionStatement*>(statement.get());
}

TEST(ParserTest, EmptyProgram) {
    ParseResult result = parse("");

    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.program, nullptr);
    EXPECT_EQ(result.program->kind(), NodeKind::Program);
    EXPECT_TRUE(programOf(result).body.empty());
    EXPECT_FALSE(programOf(result).strict);
}

TEST(ParserTest, TriviaOnlyProgram) {
    ParseResult result = parse(" // line\n /* block */ \n");

    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(programOf(result).body.empty());
}

TEST(ParserTest, EmptyStatements) {
    ParseResult result = parse(";;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::EmptyStatement);
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::EmptyStatement);
}

TEST(ParserTest, IdentifierExpressionStatement) {
    ParseResult result = parse("foo;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::Identifier);

    const auto* identifier =
        static_cast<const Identifier*>(statement.expression.get());
    EXPECT_EQ(identifier->name, "foo");
}

TEST(ParserTest, NumericLiteral) {
    ParseResult result = parse("42;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::NumericLiteral);
    EXPECT_DOUBLE_EQ(
        static_cast<const NumericLiteral*>(statement.expression.get())->value,
        42.0);
}

TEST(ParserTest, StringLiteral) {
    ParseResult result = parse("\"hi\\n\";");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::StringLiteral);
    EXPECT_EQ(
        static_cast<const StringLiteral*>(statement.expression.get())->value,
        "hi\n");
}

TEST(ParserTest, BooleanAndNullLiterals) {
    ParseResult result = parse("true; false; null;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 3u);

    const auto& first = statementOf(result, 0);
    ASSERT_NE(first.expression, nullptr);
    EXPECT_EQ(first.expression->kind(), NodeKind::BooleanLiteral);
    EXPECT_TRUE(
        static_cast<const BooleanLiteral*>(first.expression.get())->value);

    const auto& second = statementOf(result, 1);
    ASSERT_NE(second.expression, nullptr);
    EXPECT_EQ(second.expression->kind(), NodeKind::BooleanLiteral);
    EXPECT_FALSE(
        static_cast<const BooleanLiteral*>(second.expression.get())->value);

    const auto& third = statementOf(result, 2);
    ASSERT_NE(third.expression, nullptr);
    EXPECT_EQ(third.expression->kind(), NodeKind::NullLiteral);
}

TEST(ParserTest, BigIntLiteral) {
    ParseResult result = parse("0x1Fn;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::BigIntLiteral);

    const auto* literal =
        static_cast<const BigIntLiteral*>(statement.expression.get());
    EXPECT_EQ(literal->digits, "1F");
    EXPECT_EQ(literal->radix, 16);
}

TEST(ParserTest, RegExpLiteralAtProgramStart) {
    ParseResult result = parse("/ab+c/gi;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::RegExpLiteral);

    const auto* literal =
        static_cast<const RegExpLiteral*>(statement.expression.get());
    EXPECT_EQ(literal->pattern, "ab+c");
    EXPECT_EQ(literal->flags, "gi");
}

TEST(ParserTest, RegExpRescanAfterDivGoal) {
    // 分号后的 `/` 先按除法扫成 Slash，进入表达式起始位置后重扫为正则。
    ParseResult result = parse("0; /ab/;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    const auto& statement = statementOf(result, 1);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::RegExpLiteral);
}

TEST(ParserTest, ParenthesizedExpression) {
    ParseResult result = parse("(42);");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::NumericLiteral);
    EXPECT_EQ(statement.expression->range().start.offset, 0u);
    EXPECT_EQ(statement.expression->range().end.offset, 4u);
}

TEST(ParserTest, AutomaticSemicolonInsertion) {
    ParseResult result = parse("42\n43");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(statementOf(result, 0).expression->kind(),
              NodeKind::NumericLiteral);
    EXPECT_EQ(statementOf(result, 1).expression->kind(),
              NodeKind::NumericLiteral);
}

TEST(ParserTest, MissingSemicolonReportsError) {
    ParseResult result = parse("42 43");

    ASSERT_NE(result.program, nullptr);
    ASSERT_EQ(result.errors.size(), 1u);
    EXPECT_NE(result.errors[0].message.find("Expected ';'"), std::string::npos);
}

TEST(ParserTest, IllegalTokenReportsError) {
    ParseResult result = parse("@");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Unexpected character"),
              std::string::npos);
}

TEST(ParserTest, UseStrictDirectiveAffectsFollowingTokens) {
    ParseResult result = parse("\"use strict\"; 077;");

    ASSERT_NE(result.program, nullptr);
    EXPECT_TRUE(programOf(result).strict);
    ASSERT_FALSE(result.errors.empty());

    bool sawOctalError = false;
    for (const auto& error : result.errors) {
        if (error.message.find("Legacy octal") != std::string::npos) {
            sawOctalError = true;
        }
    }
    EXPECT_TRUE(sawOctalError);
}

TEST(ParserTest, MemberExpression) {
    ParseResult result = parse("a.b;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::MemberExpression);

    const auto* member =
        static_cast<const MemberExpression*>(statement.expression.get());
    EXPECT_FALSE(member->computed);
    ASSERT_NE(member->object, nullptr);
    ASSERT_NE(member->property, nullptr);
    EXPECT_EQ(member->object->kind(), NodeKind::Identifier);
    EXPECT_EQ(member->property->kind(), NodeKind::Identifier);
    EXPECT_EQ(static_cast<const Identifier*>(member->property.get())->name, "b");
}

TEST(ParserTest, ComputedMemberExpression) {
    ParseResult result = parse("a[0];");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::MemberExpression);

    const auto* member =
        static_cast<const MemberExpression*>(statement.expression.get());
    EXPECT_TRUE(member->computed);
    ASSERT_NE(member->property, nullptr);
    EXPECT_EQ(member->property->kind(), NodeKind::NumericLiteral);
}

TEST(ParserTest, ChainedMemberExpression) {
    ParseResult result = parse("a.b.c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const MemberExpression*>(statement.expression.get());
    ASSERT_NE(outer->object, nullptr);
    EXPECT_EQ(outer->object->kind(), NodeKind::MemberExpression);
}

TEST(ParserTest, KeywordAsPropertyName) {
    ParseResult result = parse("obj.if;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* member =
        static_cast<const MemberExpression*>(statement.expression.get());
    ASSERT_NE(member->property, nullptr);
    EXPECT_EQ(static_cast<const Identifier*>(member->property.get())->name,
              "if");
}

TEST(ParserTest, CallExpression) {
    ParseResult result = parse("a();");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::CallExpression);

    const auto* call =
        static_cast<const CallExpression*>(statement.expression.get());
    ASSERT_NE(call->callee, nullptr);
    EXPECT_EQ(call->callee->kind(), NodeKind::Identifier);
    EXPECT_TRUE(call->arguments.empty());
}

TEST(ParserTest, CallArguments) {
    ParseResult result = parse("a(1, \"x\",);");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* call =
        static_cast<const CallExpression*>(statement.expression.get());
    ASSERT_EQ(call->arguments.size(), 2u);
    EXPECT_EQ(call->arguments[0]->kind(), NodeKind::NumericLiteral);
    EXPECT_EQ(call->arguments[1]->kind(), NodeKind::StringLiteral);
}

TEST(ParserTest, CallOnMemberExpression) {
    ParseResult result = parse("a.b(c);");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* call =
        static_cast<const CallExpression*>(statement.expression.get());
    EXPECT_EQ(call->kind(), NodeKind::CallExpression);
    ASSERT_NE(call->callee, nullptr);
    EXPECT_EQ(call->callee->kind(), NodeKind::MemberExpression);
}

TEST(ParserTest, MemberOnCallExpression) {
    ParseResult result = parse("a().b;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* member =
        static_cast<const MemberExpression*>(statement.expression.get());
    EXPECT_EQ(member->kind(), NodeKind::MemberExpression);
    ASSERT_NE(member->object, nullptr);
    EXPECT_EQ(member->object->kind(), NodeKind::CallExpression);
}

TEST(ParserTest, NewExpressionWithoutArguments) {
    ParseResult result = parse("new Foo;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::NewExpression);

    const auto* node =
        static_cast<const NewExpression*>(statement.expression.get());
    ASSERT_NE(node->callee, nullptr);
    EXPECT_EQ(node->callee->kind(), NodeKind::Identifier);
    EXPECT_TRUE(node->arguments.empty());
}

TEST(ParserTest, NewExpressionWithArguments) {
    ParseResult result = parse("new Foo(1);");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* node =
        static_cast<const NewExpression*>(statement.expression.get());
    EXPECT_EQ(node->kind(), NodeKind::NewExpression);
    ASSERT_EQ(node->arguments.size(), 1u);
    EXPECT_EQ(node->arguments[0]->kind(), NodeKind::NumericLiteral);
}

TEST(ParserTest, NewExpressionCalleeIsMember) {
    ParseResult result = parse("new Foo.bar();");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* node =
        static_cast<const NewExpression*>(statement.expression.get());
    EXPECT_EQ(node->kind(), NodeKind::NewExpression);
    ASSERT_NE(node->callee, nullptr);
    EXPECT_EQ(node->callee->kind(), NodeKind::MemberExpression);
}

TEST(ParserTest, MemberOnNewExpression) {
    ParseResult result = parse("new Foo().bar;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* member =
        static_cast<const MemberExpression*>(statement.expression.get());
    EXPECT_EQ(member->kind(), NodeKind::MemberExpression);
    ASSERT_NE(member->object, nullptr);
    EXPECT_EQ(member->object->kind(), NodeKind::NewExpression);
}

TEST(ParserTest, NestedNewExpression) {
    ParseResult result = parse("new new Foo();");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const NewExpression*>(statement.expression.get());
    EXPECT_EQ(outer->kind(), NodeKind::NewExpression);
    ASSERT_NE(outer->callee, nullptr);
    EXPECT_EQ(outer->callee->kind(), NodeKind::NewExpression);
}

TEST(ParserTest, MixedMemberCallChain) {
    ParseResult result = parse("a.b[c](d).e;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const MemberExpression*>(statement.expression.get());
    EXPECT_EQ(outer->kind(), NodeKind::MemberExpression);
    ASSERT_NE(outer->object, nullptr);
    EXPECT_EQ(outer->object->kind(), NodeKind::CallExpression);

    const auto* call =
        static_cast<const CallExpression*>(outer->object.get());
    ASSERT_NE(call->callee, nullptr);
    EXPECT_EQ(call->callee->kind(), NodeKind::MemberExpression);
}

TEST(ParserTest, MissingPropertyNameReportsError) {
    ParseResult result = parse("a.;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("property name"), std::string::npos);
}

TEST(ParserTest, UnaryOperators) {
    struct Case {
        const char* source;
        UnaryOperator op;
    };
    const Case cases[] = {
        {"!a;", UnaryOperator::LogicalNot},
        {"~a;", UnaryOperator::BitNot},
        {"+a;", UnaryOperator::Plus},
        {"-a;", UnaryOperator::Minus},
        {"void a;", UnaryOperator::Void},
        {"typeof a;", UnaryOperator::TypeOf},
        {"delete a;", UnaryOperator::Delete},
    };

    for (const Case& item : cases) {
        ParseResult result = parse(item.source);
        ASSERT_TRUE(result.ok()) << item.source;
        const auto& statement = statementOf(result, 0);
        ASSERT_NE(statement.expression, nullptr) << item.source;
        EXPECT_EQ(statement.expression->kind(), NodeKind::UnaryExpression)
            << item.source;
        const auto* unary =
            static_cast<const UnaryExpression*>(statement.expression.get());
        EXPECT_EQ(unary->op, item.op) << item.source;
        ASSERT_NE(unary->argument, nullptr) << item.source;
        EXPECT_EQ(unary->argument->kind(), NodeKind::Identifier)
            << item.source;
    }
}

TEST(ParserTest, UnaryOnMemberCall) {
    ParseResult result = parse("typeof a.b();");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* unary =
        static_cast<const UnaryExpression*>(statement.expression.get());
    EXPECT_EQ(unary->op, UnaryOperator::TypeOf);
    ASSERT_NE(unary->argument, nullptr);
    EXPECT_EQ(unary->argument->kind(), NodeKind::CallExpression);
}

TEST(ParserTest, NestedUnaryOperators) {
    ParseResult result = parse("!!a;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const UnaryExpression*>(statement.expression.get());
    EXPECT_EQ(outer->op, UnaryOperator::LogicalNot);
    ASSERT_NE(outer->argument, nullptr);
    EXPECT_EQ(outer->argument->kind(), NodeKind::UnaryExpression);

    const auto* inner =
        static_cast<const UnaryExpression*>(outer->argument.get());
    EXPECT_EQ(inner->op, UnaryOperator::LogicalNot);
    ASSERT_NE(inner->argument, nullptr);
    EXPECT_EQ(inner->argument->kind(), NodeKind::Identifier);
}

TEST(ParserTest, UnaryMinusIsNotPartOfNumericLiteral) {
    ParseResult result = parse("-1;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* unary =
        static_cast<const UnaryExpression*>(statement.expression.get());
    EXPECT_EQ(unary->op, UnaryOperator::Minus);
    ASSERT_NE(unary->argument, nullptr);
    EXPECT_EQ(unary->argument->kind(), NodeKind::NumericLiteral);
    EXPECT_DOUBLE_EQ(
        static_cast<const NumericLiteral*>(unary->argument.get())->value, 1.0);
}

TEST(ParserTest, UnaryMissingOperandReportsError) {
    ParseResult result = parse("!;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
}

TEST(ParserTest, ProgramRangeCoversConsumedInput) {
    ParseResult result = parse("42;");

    ASSERT_NE(result.program, nullptr);
    EXPECT_EQ(result.program->range().start.offset, 0u);
    EXPECT_EQ(result.program->range().end.offset, 3u);
}

}  // namespace
