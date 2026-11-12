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
using neo::frontend::ExpressionStatement;
using neo::frontend::Identifier;
using neo::frontend::NodeKind;
using neo::frontend::NumericLiteral;
using neo::frontend::Parser;
using neo::frontend::ParseResult;
using neo::frontend::Program;
using neo::frontend::RegExpLiteral;
using neo::frontend::StringLiteral;

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

TEST(ParserTest, ProgramRangeCoversConsumedInput) {
    ParseResult result = parse("42;");

    ASSERT_NE(result.program, nullptr);
    EXPECT_EQ(result.program->range().start.offset, 0u);
    EXPECT_EQ(result.program->range().end.offset, 3u);
}

}  // namespace
