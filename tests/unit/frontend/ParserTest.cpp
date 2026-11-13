// ParserTest.cpp - 语法分析器框架与首批节点
//
// 覆盖：Program、空语句、表达式语句、标识符、各类字面量、ASI、
// 正则重扫、指令序言严格模式、错误收集。

#include "frontend/Parser.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using neo::frontend::ArrayExpression;
using neo::frontend::ArrowFunctionExpression;
using neo::frontend::AssignmentExpression;
using neo::frontend::AssignmentOperator;
using neo::frontend::BigIntLiteral;
using neo::frontend::BinaryExpression;
using neo::frontend::BlockStatement;
using neo::frontend::BreakStatement;
using neo::frontend::BinaryOperator;
using neo::frontend::BooleanLiteral;
using neo::frontend::CallExpression;
using neo::frontend::CatchClause;
using neo::frontend::ConditionalExpression;
using neo::frontend::ContinueStatement;
using neo::frontend::DebuggerStatement;
using neo::frontend::Directive;
using neo::frontend::DoWhileStatement;
using neo::frontend::ExportDefaultDeclaration;
using neo::frontend::ExportNamedDeclaration;
using neo::frontend::ExpressionStatement;
using neo::frontend::ForInStatement;
using neo::frontend::FunctionDeclaration;
using neo::frontend::ForOfStatement;
using neo::frontend::ForStatement;
using neo::frontend::Identifier;
using neo::frontend::IfStatement;
using neo::frontend::LabeledStatement;
using neo::frontend::LogicalExpression;
using neo::frontend::LogicalOperator;
using neo::frontend::MemberExpression;
using neo::frontend::NewExpression;
using neo::frontend::NodeKind;
using neo::frontend::NumericLiteral;
using neo::frontend::Parser;
using neo::frontend::ParseResult;
using neo::frontend::Program;
using neo::frontend::RegExpLiteral;
using neo::frontend::RestElement;
using neo::frontend::ReturnStatement;
using neo::frontend::SequenceExpression;
using neo::frontend::SpreadElement;
using neo::frontend::StringLiteral;
using neo::frontend::SwitchCase;
using neo::frontend::SwitchStatement;
using neo::frontend::ThrowStatement;
using neo::frontend::TryStatement;
using neo::frontend::UnaryExpression;
using neo::frontend::UnaryOperator;
using neo::frontend::UpdateExpression;
using neo::frontend::UpdateOperator;
using neo::frontend::VariableDeclaration;
using neo::frontend::VariableDeclarator;
using neo::frontend::VariableKind;
using neo::frontend::WhileStatement;
using neo::frontend::WithStatement;

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
    ParseResult result = parse("(\"hi\\n\");");

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

TEST(ParserTest, PostfixUpdateExpression) {
    struct Case {
        const char* source;
        UpdateOperator op;
    };
    const Case cases[] = {
        {"a++;", UpdateOperator::Increment},
        {"a--;", UpdateOperator::Decrement},
    };

    for (const Case& item : cases) {
        ParseResult result = parse(item.source);
        ASSERT_TRUE(result.ok()) << item.source;
        const auto& statement = statementOf(result, 0);
        ASSERT_NE(statement.expression, nullptr) << item.source;
        EXPECT_EQ(statement.expression->kind(), NodeKind::UpdateExpression)
            << item.source;

        const auto* update =
            static_cast<const UpdateExpression*>(statement.expression.get());
        EXPECT_EQ(update->op, item.op) << item.source;
        EXPECT_FALSE(update->prefix) << item.source;
        ASSERT_NE(update->argument, nullptr) << item.source;
        EXPECT_EQ(update->argument->kind(), NodeKind::Identifier)
            << item.source;
    }
}

TEST(ParserTest, PrefixUpdateExpression) {
    struct Case {
        const char* source;
        UpdateOperator op;
    };
    const Case cases[] = {
        {"++a;", UpdateOperator::Increment},
        {"--a;", UpdateOperator::Decrement},
    };

    for (const Case& item : cases) {
        ParseResult result = parse(item.source);
        ASSERT_TRUE(result.ok()) << item.source;
        const auto& statement = statementOf(result, 0);
        ASSERT_NE(statement.expression, nullptr) << item.source;
        EXPECT_EQ(statement.expression->kind(), NodeKind::UpdateExpression)
            << item.source;

        const auto* update =
            static_cast<const UpdateExpression*>(statement.expression.get());
        EXPECT_EQ(update->op, item.op) << item.source;
        EXPECT_TRUE(update->prefix) << item.source;
        ASSERT_NE(update->argument, nullptr) << item.source;
        EXPECT_EQ(update->argument->kind(), NodeKind::Identifier)
            << item.source;
    }
}

TEST(ParserTest, UpdateOnMemberExpression) {
    ParseResult result = parse("a.b++;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* update =
        static_cast<const UpdateExpression*>(statement.expression.get());
    EXPECT_EQ(update->kind(), NodeKind::UpdateExpression);
    EXPECT_FALSE(update->prefix);
    ASSERT_NE(update->argument, nullptr);
    EXPECT_EQ(update->argument->kind(), NodeKind::MemberExpression);
}

TEST(ParserTest, PostfixUpdateStopsAtLineTerminator) {
    ParseResult result = parse("a\n++b;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);

    const auto& first = statementOf(result, 0);
    EXPECT_EQ(first.expression->kind(), NodeKind::Identifier);

    const auto& second = statementOf(result, 1);
    ASSERT_NE(second.expression, nullptr);
    EXPECT_EQ(second.expression->kind(), NodeKind::UpdateExpression);
    const auto* update =
        static_cast<const UpdateExpression*>(second.expression.get());
    EXPECT_TRUE(update->prefix);
}

TEST(ParserTest, PrefixUpdateAllowsLineTerminator) {
    ParseResult result = parse("++\na;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::UpdateExpression);
    EXPECT_TRUE(
        static_cast<const UpdateExpression*>(statement.expression.get())
            ->prefix);
}

TEST(ParserTest, BinaryOperatorPrecedence) {
    ParseResult result = parse("a + b * c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* add =
        static_cast<const BinaryExpression*>(statement.expression.get());
    EXPECT_EQ(add->kind(), NodeKind::BinaryExpression);
    EXPECT_EQ(add->op, BinaryOperator::Add);
    ASSERT_NE(add->right, nullptr);
    EXPECT_EQ(add->right->kind(), NodeKind::BinaryExpression);
    EXPECT_EQ(static_cast<const BinaryExpression*>(add->right.get())->op,
              BinaryOperator::Multiply);
}

TEST(ParserTest, BinaryLeftAssociativity) {
    ParseResult result = parse("a - b - c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const BinaryExpression*>(statement.expression.get());
    EXPECT_EQ(outer->op, BinaryOperator::Subtract);
    ASSERT_NE(outer->left, nullptr);
    EXPECT_EQ(outer->left->kind(), NodeKind::BinaryExpression);
    EXPECT_EQ(static_cast<const BinaryExpression*>(outer->left.get())->op,
              BinaryOperator::Subtract);
}

TEST(ParserTest, ExponentiateIsRightAssociative) {
    ParseResult result = parse("a ** b ** c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const BinaryExpression*>(statement.expression.get());
    EXPECT_EQ(outer->op, BinaryOperator::Exponentiate);
    ASSERT_NE(outer->right, nullptr);
    EXPECT_EQ(outer->right->kind(), NodeKind::BinaryExpression);
    EXPECT_EQ(static_cast<const BinaryExpression*>(outer->right.get())->op,
              BinaryOperator::Exponentiate);
}

TEST(ParserTest, RelationalBindsTighterThanEquality) {
    ParseResult result = parse("a < b == c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const BinaryExpression*>(statement.expression.get());
    EXPECT_EQ(outer->op, BinaryOperator::Equal);
    ASSERT_NE(outer->left, nullptr);
    EXPECT_EQ(outer->left->kind(), NodeKind::BinaryExpression);
    EXPECT_EQ(static_cast<const BinaryExpression*>(outer->left.get())->op,
              BinaryOperator::LessThan);
}

TEST(ParserTest, BitwisePrecedence) {
    ParseResult result = parse("a | b & c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const BinaryExpression*>(statement.expression.get());
    EXPECT_EQ(outer->op, BinaryOperator::BitOr);
    ASSERT_NE(outer->right, nullptr);
    EXPECT_EQ(outer->right->kind(), NodeKind::BinaryExpression);
    EXPECT_EQ(static_cast<const BinaryExpression*>(outer->right.get())->op,
              BinaryOperator::BitAnd);
}

TEST(ParserTest, ShiftBindsLooserThanAdditive) {
    ParseResult result = parse("a << b + c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const BinaryExpression*>(statement.expression.get());
    EXPECT_EQ(outer->op, BinaryOperator::LeftShift);
    ASSERT_NE(outer->right, nullptr);
    EXPECT_EQ(outer->right->kind(), NodeKind::BinaryExpression);
    EXPECT_EQ(static_cast<const BinaryExpression*>(outer->right.get())->op,
              BinaryOperator::Add);
}

TEST(ParserTest, RelationalKeywords) {
    struct Case {
        const char* source;
        BinaryOperator op;
    };
    const Case cases[] = {
        {"a instanceof b;", BinaryOperator::InstanceOf},
        {"a in b;", BinaryOperator::In},
    };

    for (const Case& item : cases) {
        ParseResult result = parse(item.source);
        ASSERT_TRUE(result.ok()) << item.source;
        const auto& statement = statementOf(result, 0);
        const auto* binary =
            static_cast<const BinaryExpression*>(statement.expression.get());
        EXPECT_EQ(binary->kind(), NodeKind::BinaryExpression) << item.source;
        EXPECT_EQ(binary->op, item.op) << item.source;
    }
}

TEST(ParserTest, BinaryOperandsIncludeUnaryAndUpdate) {
    ParseResult result = parse("typeof a + b++;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* add =
        static_cast<const BinaryExpression*>(statement.expression.get());
    EXPECT_EQ(add->op, BinaryOperator::Add);
    ASSERT_NE(add->left, nullptr);
    EXPECT_EQ(add->left->kind(), NodeKind::UnaryExpression);
    ASSERT_NE(add->right, nullptr);
    EXPECT_EQ(add->right->kind(), NodeKind::UpdateExpression);
}

TEST(ParserTest, DivisionAndRegExpOperand) {
    ParseResult result = parse("a / b;");

    ASSERT_TRUE(result.ok());
    const auto& division = statementOf(result, 0);
    const auto* binary =
        static_cast<const BinaryExpression*>(division.expression.get());
    EXPECT_EQ(binary->op, BinaryOperator::Divide);

    ParseResult regexpResult = parse("a + /b/;");
    ASSERT_TRUE(regexpResult.ok());
    const auto& statement = statementOf(regexpResult, 0);
    const auto* add =
        static_cast<const BinaryExpression*>(statement.expression.get());
    EXPECT_EQ(add->op, BinaryOperator::Add);
    ASSERT_NE(add->right, nullptr);
    EXPECT_EQ(add->right->kind(), NodeKind::RegExpLiteral);
}

TEST(ParserTest, BinaryMissingOperandReportsError) {
    ParseResult result = parse("a +;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
}

TEST(ParserTest, LogicalOperators) {
    struct Case {
        const char* source;
        LogicalOperator op;
    };
    const Case cases[] = {
        {"a && b;", LogicalOperator::And},
        {"a || b;", LogicalOperator::Or},
        {"a ?? b;", LogicalOperator::Nullish},
    };

    for (const Case& item : cases) {
        ParseResult result = parse(item.source);
        ASSERT_TRUE(result.ok()) << item.source;
        const auto& statement = statementOf(result, 0);
        ASSERT_NE(statement.expression, nullptr) << item.source;
        EXPECT_EQ(statement.expression->kind(), NodeKind::LogicalExpression)
            << item.source;
        const auto* logical =
            static_cast<const LogicalExpression*>(statement.expression.get());
        EXPECT_EQ(logical->op, item.op) << item.source;
    }
}

TEST(ParserTest, LogicalPrecedence) {
    ParseResult result = parse("a || b && c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const LogicalExpression*>(statement.expression.get());
    EXPECT_EQ(outer->op, LogicalOperator::Or);
    ASSERT_NE(outer->right, nullptr);
    EXPECT_EQ(outer->right->kind(), NodeKind::LogicalExpression);
    EXPECT_EQ(static_cast<const LogicalExpression*>(outer->right.get())->op,
              LogicalOperator::And);
}

TEST(ParserTest, LogicalBindsLooserThanBinary) {
    ParseResult result = parse("a + b && c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const LogicalExpression*>(statement.expression.get());
    EXPECT_EQ(outer->op, LogicalOperator::And);
    ASSERT_NE(outer->left, nullptr);
    EXPECT_EQ(outer->left->kind(), NodeKind::BinaryExpression);
}

TEST(ParserTest, NullishIsLeftAssociative) {
    ParseResult result = parse("a ?? b ?? c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const LogicalExpression*>(statement.expression.get());
    EXPECT_EQ(outer->op, LogicalOperator::Nullish);
    ASSERT_NE(outer->left, nullptr);
    EXPECT_EQ(outer->left->kind(), NodeKind::LogicalExpression);
    EXPECT_EQ(static_cast<const LogicalExpression*>(outer->left.get())->op,
              LogicalOperator::Nullish);
}

TEST(ParserTest, NullishMixingWithoutParenthesesIsError) {
    const char* sources[] = {
        "a || b ?? c;",
        "a ?? b || c;",
        "a && b ?? c;",
        "a ?? b && c;",
    };

    for (const char* source : sources) {
        ParseResult result = parse(source);
        EXPECT_FALSE(result.ok()) << source;
        ASSERT_FALSE(result.errors.empty()) << source;
        EXPECT_NE(result.errors[0].message.find("Cannot mix"),
                  std::string::npos)
            << source;
    }
}

TEST(ParserTest, NullishMixingWithParenthesesIsAllowed) {
    const char* sources[] = {
        "(a || b) ?? c;",
        "a ?? (b && c);",
    };

    for (const char* source : sources) {
        ParseResult result = parse(source);
        EXPECT_TRUE(result.ok()) << source;
    }
}

TEST(ParserTest, ConditionalExpression) {
    ParseResult result = parse("a ? b : c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::ConditionalExpression);

    const auto* conditional =
        static_cast<const ConditionalExpression*>(statement.expression.get());
    ASSERT_NE(conditional->test, nullptr);
    ASSERT_NE(conditional->consequent, nullptr);
    ASSERT_NE(conditional->alternate, nullptr);
    EXPECT_EQ(conditional->test->kind(), NodeKind::Identifier);
    EXPECT_EQ(conditional->consequent->kind(), NodeKind::Identifier);
    EXPECT_EQ(conditional->alternate->kind(), NodeKind::Identifier);
}

TEST(ParserTest, ConditionalIsRightAssociative) {
    ParseResult result = parse("a ? b : c ? d : e;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const ConditionalExpression*>(statement.expression.get());
    ASSERT_NE(outer->alternate, nullptr);
    EXPECT_EQ(outer->alternate->kind(), NodeKind::ConditionalExpression);
}

TEST(ParserTest, NestedConditionalInConsequent) {
    ParseResult result = parse("a ? b ? c : d : e;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const ConditionalExpression*>(statement.expression.get());
    ASSERT_NE(outer->consequent, nullptr);
    EXPECT_EQ(outer->consequent->kind(), NodeKind::ConditionalExpression);
}

TEST(ParserTest, ConditionalTestAndBranchesUseExpressionLayers) {
    ParseResult result = parse("a || b ? c + d : e;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* conditional =
        static_cast<const ConditionalExpression*>(statement.expression.get());
    ASSERT_NE(conditional->test, nullptr);
    EXPECT_EQ(conditional->test->kind(), NodeKind::LogicalExpression);
    ASSERT_NE(conditional->consequent, nullptr);
    EXPECT_EQ(conditional->consequent->kind(), NodeKind::BinaryExpression);
}

TEST(ParserTest, ConditionalMissingColonReportsError) {
    ParseResult result = parse("a ? b;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ':'"), std::string::npos);
}

TEST(ParserTest, ConditionalMissingAlternateReportsError) {
    ParseResult result = parse("a ? b :;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
}

TEST(ParserTest, AssignmentOperators) {
    struct Case {
        const char* source;
        AssignmentOperator op;
    };
    const Case cases[] = {
        {"a = b;", AssignmentOperator::Assign},
        {"a += b;", AssignmentOperator::Add},
        {"a -= b;", AssignmentOperator::Subtract},
        {"a *= b;", AssignmentOperator::Multiply},
        {"a /= b;", AssignmentOperator::Divide},
        {"a %= b;", AssignmentOperator::Remainder},
        {"a **= b;", AssignmentOperator::Exponentiate},
        {"a <<= b;", AssignmentOperator::LeftShift},
        {"a >>= b;", AssignmentOperator::RightShift},
        {"a >>>= b;", AssignmentOperator::UnsignedRightShift},
        {"a &= b;", AssignmentOperator::BitAnd},
        {"a |= b;", AssignmentOperator::BitOr},
        {"a ^= b;", AssignmentOperator::BitXor},
        {"a &&= b;", AssignmentOperator::LogicalAnd},
        {"a ||= b;", AssignmentOperator::LogicalOr},
        {"a ?" "?= b;", AssignmentOperator::Nullish},
    };

    for (const Case& item : cases) {
        ParseResult result = parse(item.source);
        ASSERT_TRUE(result.ok()) << item.source;
        const auto& statement = statementOf(result, 0);
        ASSERT_NE(statement.expression, nullptr) << item.source;
        EXPECT_EQ(statement.expression->kind(), NodeKind::AssignmentExpression)
            << item.source;
        const auto* assignment =
            static_cast<const AssignmentExpression*>(statement.expression.get());
        EXPECT_EQ(assignment->op, item.op) << item.source;
    }
}

TEST(ParserTest, AssignmentIsRightAssociative) {
    ParseResult result = parse("a = b = c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const AssignmentExpression*>(statement.expression.get());
    EXPECT_EQ(outer->op, AssignmentOperator::Assign);
    ASSERT_NE(outer->right, nullptr);
    EXPECT_EQ(outer->right->kind(), NodeKind::AssignmentExpression);
}

TEST(ParserTest, AssignmentRhsCanBeConditional) {
    ParseResult result = parse("a = b ? c : d;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* assignment =
        static_cast<const AssignmentExpression*>(statement.expression.get());
    ASSERT_NE(assignment->right, nullptr);
    EXPECT_EQ(assignment->right->kind(), NodeKind::ConditionalExpression);
}

TEST(ParserTest, AssignmentInConditionalBranch) {
    ParseResult result = parse("a ? b = c : d;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* conditional =
        static_cast<const ConditionalExpression*>(statement.expression.get());
    ASSERT_NE(conditional->consequent, nullptr);
    EXPECT_EQ(conditional->consequent->kind(), NodeKind::AssignmentExpression);
}

TEST(ParserTest, AssignmentTargets) {
    const char* sources[] = {
        "a.b = c;",
        "a[0] += 1;",
    };

    for (const char* source : sources) {
        ParseResult result = parse(source);
        ASSERT_TRUE(result.ok()) << source;
        const auto& statement = statementOf(result, 0);
        const auto* assignment =
            static_cast<const AssignmentExpression*>(statement.expression.get());
        EXPECT_EQ(assignment->left->kind(), NodeKind::MemberExpression)
            << source;
    }
}

TEST(ParserTest, InvalidAssignmentTargetReportsError) {
    const char* sources[] = {
        "a + b = c;",
        "1 = 2;",
        "a() = b;",
    };

    for (const char* source : sources) {
        ParseResult result = parse(source);
        EXPECT_FALSE(result.ok()) << source;
        ASSERT_FALSE(result.errors.empty()) << source;
        EXPECT_NE(result.errors[0].message.find("Invalid assignment target"),
                  std::string::npos)
            << source;
    }
}

TEST(ParserTest, AssignmentMissingRhsReportsError) {
    ParseResult result = parse("a =;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
}

TEST(ParserTest, SequenceExpression) {
    ParseResult result = parse("a, b, c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    ASSERT_NE(statement.expression, nullptr);
    EXPECT_EQ(statement.expression->kind(), NodeKind::SequenceExpression);

    const auto* sequence =
        static_cast<const SequenceExpression*>(statement.expression.get());
    ASSERT_EQ(sequence->expressions.size(), 3u);
    for (const auto& expression : sequence->expressions) {
        EXPECT_EQ(expression->kind(), NodeKind::Identifier);
    }
}

TEST(ParserTest, SequenceBindsLooserThanAssignment) {
    ParseResult result = parse("a = b, c;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* sequence =
        static_cast<const SequenceExpression*>(statement.expression.get());
    ASSERT_EQ(sequence->expressions.size(), 2u);
    EXPECT_EQ(sequence->expressions[0]->kind(),
              NodeKind::AssignmentExpression);
    EXPECT_EQ(sequence->expressions[1]->kind(), NodeKind::Identifier);
}

TEST(ParserTest, ArgumentCommasAreNotSequence) {
    ParseResult result = parse("f(a, b);");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* call =
        static_cast<const CallExpression*>(statement.expression.get());
    EXPECT_EQ(call->kind(), NodeKind::CallExpression);
    ASSERT_EQ(call->arguments.size(), 2u);
    EXPECT_EQ(call->arguments[0]->kind(), NodeKind::Identifier);
    EXPECT_EQ(call->arguments[1]->kind(), NodeKind::Identifier);
}

TEST(ParserTest, ParenthesizedSequenceAsArgument) {
    ParseResult result = parse("f((a, b));");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* call =
        static_cast<const CallExpression*>(statement.expression.get());
    ASSERT_EQ(call->arguments.size(), 1u);
    EXPECT_EQ(call->arguments[0]->kind(), NodeKind::SequenceExpression);
}

TEST(ParserTest, SequenceWithConditionalFirst) {
    ParseResult result = parse("a ? b : c, d;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* sequence =
        static_cast<const SequenceExpression*>(statement.expression.get());
    ASSERT_EQ(sequence->expressions.size(), 2u);
    EXPECT_EQ(sequence->expressions[0]->kind(),
              NodeKind::ConditionalExpression);
}

TEST(ParserTest, SequenceMissingOperandReportsError) {
    ParseResult result = parse("a, ;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
}

TEST(ParserTest, ExpressionAsiKeepsValidContinuations) {
    struct Case {
        const char* source;
        NodeKind kind;
    };
    const Case cases[] = {
        {"a\n+b;", NodeKind::BinaryExpression},
        {"a\n(b);", NodeKind::CallExpression},
        {"a\n[0];", NodeKind::MemberExpression},
        {"a\n? b : c;", NodeKind::ConditionalExpression},
        {"a\n= b;", NodeKind::AssignmentExpression},
        {"a\n, b;", NodeKind::SequenceExpression},
        {"a\ninstanceof b;", NodeKind::BinaryExpression},
    };

    for (const Case& item : cases) {
        ParseResult result = parse(item.source);
        ASSERT_TRUE(result.ok()) << item.source;
        ASSERT_EQ(programOf(result).body.size(), 1u) << item.source;
        const auto& statement = statementOf(result, 0);
        ASSERT_NE(statement.expression, nullptr) << item.source;
        EXPECT_EQ(statement.expression->kind(), item.kind) << item.source;
    }
}

TEST(ParserTest, ExpressionAsiInsertsBetweenStatements) {
    ParseResult result = parse("a\nb;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(statementOf(result, 0).expression->kind(), NodeKind::Identifier);
    EXPECT_EQ(statementOf(result, 1).expression->kind(), NodeKind::Identifier);
}

TEST(ParserTest, ExpressionAsiDivisionAcrossLineTerminator) {
    ParseResult result = parse("a\n/b/g;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    const auto* outer =
        static_cast<const BinaryExpression*>(statement.expression.get());
    EXPECT_EQ(outer->kind(), NodeKind::BinaryExpression);
    EXPECT_EQ(outer->op, BinaryOperator::Divide);
    ASSERT_NE(outer->left, nullptr);
    EXPECT_EQ(outer->left->kind(), NodeKind::BinaryExpression);
    EXPECT_EQ(static_cast<const BinaryExpression*>(outer->left.get())->op,
              BinaryOperator::Divide);
}

TEST(ParserTest, BlockStatement) {
    ParseResult result = parse("{ 1; 2; }");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::BlockStatement);

    const auto* block =
        static_cast<const BlockStatement*>(programOf(result).body[0].get());
    ASSERT_EQ(block->body.size(), 2u);
    EXPECT_EQ(block->body[0]->kind(), NodeKind::ExpressionStatement);
    EXPECT_EQ(block->body[1]->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, EmptyBlockStatement) {
    ParseResult result = parse("{}");

    ASSERT_TRUE(result.ok());
    const auto* block =
        static_cast<const BlockStatement*>(programOf(result).body[0].get());
    EXPECT_TRUE(block->body.empty());
}

TEST(ParserTest, NestedBlockStatement) {
    ParseResult result = parse("{ { } }");

    ASSERT_TRUE(result.ok());
    const auto* outer =
        static_cast<const BlockStatement*>(programOf(result).body[0].get());
    ASSERT_EQ(outer->body.size(), 1u);
    EXPECT_EQ(outer->body[0]->kind(), NodeKind::BlockStatement);
}

TEST(ParserTest, BlockStatementAllowsAsiBeforeBrace) {
    ParseResult result = parse("{ 1 }");

    ASSERT_TRUE(result.ok());
    const auto* block =
        static_cast<const BlockStatement*>(programOf(result).body[0].get());
    ASSERT_EQ(block->body.size(), 1u);
    EXPECT_EQ(block->body[0]->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, BlockStatementFollowedByStatement) {
    ParseResult result = parse("{ } 1;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::BlockStatement);
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, MissingClosingBraceReportsError) {
    ParseResult result = parse("{ 1;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected '}'"), std::string::npos);
}

TEST(ParserTest, BlockErrorRecoveryStopsAtBrace) {
    ParseResult result = parse("{ 1 2; }");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::BlockStatement);
}

TEST(ParserTest, VariableDeclarationKinds) {
    struct Case {
        const char* source;
        VariableKind kind;
    };
    const Case cases[] = {
        {"var x;", VariableKind::Var},
        {"let x;", VariableKind::Let},
        {"const x = 1;", VariableKind::Const},
    };

    for (const Case& item : cases) {
        ParseResult result = parse(item.source);
        ASSERT_TRUE(result.ok()) << item.source;
        ASSERT_EQ(programOf(result).body.size(), 1u) << item.source;
        const auto* declaration = static_cast<const VariableDeclaration*>(
            programOf(result).body[0].get());
        EXPECT_EQ(declaration->kind(), NodeKind::VariableDeclaration)
            << item.source;
        EXPECT_EQ(declaration->variableKind, item.kind) << item.source;
        ASSERT_EQ(declaration->declarations.size(), 1u) << item.source;
    }
}

TEST(ParserTest, VariableDeclarators) {
    ParseResult result = parse("var x = 1, y = 2;");

    ASSERT_TRUE(result.ok());
    const auto* declaration = static_cast<const VariableDeclaration*>(
        programOf(result).body[0].get());
    ASSERT_EQ(declaration->declarations.size(), 2u);

    const auto* first = static_cast<const VariableDeclarator*>(
        declaration->declarations[0].get());
    ASSERT_NE(first->id, nullptr);
    EXPECT_EQ(static_cast<const Identifier*>(first->id.get())->name, "x");
    ASSERT_NE(first->init, nullptr);
    EXPECT_EQ(first->init->kind(), NodeKind::NumericLiteral);

    const auto* second = static_cast<const VariableDeclarator*>(
        declaration->declarations[1].get());
    EXPECT_EQ(static_cast<const Identifier*>(second->id.get())->name, "y");
    ASSERT_NE(second->init, nullptr);
}

TEST(ParserTest, DeclaratorWithoutInitializer) {
    ParseResult result = parse("var x, y = 1;");

    ASSERT_TRUE(result.ok());
    const auto* declaration = static_cast<const VariableDeclaration*>(
        programOf(result).body[0].get());
    ASSERT_EQ(declaration->declarations.size(), 2u);
    const auto* first = static_cast<const VariableDeclarator*>(
        declaration->declarations[0].get());
    EXPECT_EQ(first->init, nullptr);
}

TEST(ParserTest, DeclaratorInitStopsAtComma) {
    ParseResult result = parse("var x = a, b;");

    ASSERT_TRUE(result.ok());
    const auto* declaration = static_cast<const VariableDeclaration*>(
        programOf(result).body[0].get());
    ASSERT_EQ(declaration->declarations.size(), 2u);
    const auto* first = static_cast<const VariableDeclarator*>(
        declaration->declarations[0].get());
    ASSERT_NE(first->init, nullptr);
    EXPECT_EQ(first->init->kind(), NodeKind::Identifier);
    const auto* second = static_cast<const VariableDeclarator*>(
        declaration->declarations[1].get());
    EXPECT_EQ(second->init, nullptr);
}

TEST(ParserTest, ParenthesizedSequenceAsInitializer) {
    ParseResult result = parse("var x = (a, b);");

    ASSERT_TRUE(result.ok());
    const auto* declaration = static_cast<const VariableDeclaration*>(
        programOf(result).body[0].get());
    const auto* declarator = static_cast<const VariableDeclarator*>(
        declaration->declarations[0].get());
    ASSERT_NE(declarator->init, nullptr);
    EXPECT_EQ(declarator->init->kind(), NodeKind::SequenceExpression);
}

TEST(ParserTest, ConstRequiresInitializer) {
    ParseResult result = parse("const x;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Missing initializer"),
              std::string::npos);
}

TEST(ParserTest, LetAsIdentifier) {
    ParseResult assignment = parse("let = 1;");
    ASSERT_TRUE(assignment.ok());
    EXPECT_EQ(statementOf(assignment, 0).expression->kind(),
              NodeKind::AssignmentExpression);

    ParseResult identifier = parse("let;");
    ASSERT_TRUE(identifier.ok());
    EXPECT_EQ(statementOf(identifier, 0).expression->kind(),
              NodeKind::Identifier);
}

TEST(ParserTest, VariableDeclarationsAcrossLines) {
    ParseResult result = parse("var x = 1\nvar y = 2;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::VariableDeclaration);
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::VariableDeclaration);
}

TEST(ParserTest, MissingSemicolonBetweenDeclarationsReportsError) {
    ParseResult result = parse("var x = 1 var y = 2;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ';'"), std::string::npos);
}

TEST(ParserTest, ReservedWordAsBindingReportsError) {
    ParseResult result = parse("var if;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected identifier"),
              std::string::npos);
}

TEST(ParserTest, IfStatement) {
    ParseResult result = parse("if (a) b;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::IfStatement);

    const auto* statement =
        static_cast<const IfStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->test, nullptr);
    EXPECT_EQ(statement->test->kind(), NodeKind::Identifier);
    ASSERT_NE(statement->consequent, nullptr);
    EXPECT_EQ(statement->consequent->kind(), NodeKind::ExpressionStatement);
    EXPECT_EQ(statement->alternate, nullptr);
}

TEST(ParserTest, IfElseStatement) {
    ParseResult result = parse("if (a) b; else c;");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const IfStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->alternate, nullptr);
    EXPECT_EQ(statement->alternate->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, DanglingElseBindsToInnerIf) {
    ParseResult result = parse("if (a) if (b) c; else d;");

    ASSERT_TRUE(result.ok());
    const auto* outer =
        static_cast<const IfStatement*>(programOf(result).body[0].get());
    EXPECT_EQ(outer->alternate, nullptr);
    ASSERT_NE(outer->consequent, nullptr);
    ASSERT_EQ(outer->consequent->kind(), NodeKind::IfStatement);

    const auto* inner =
        static_cast<const IfStatement*>(outer->consequent.get());
    ASSERT_NE(inner->alternate, nullptr);
}

TEST(ParserTest, IfWithBlockBranches) {
    ParseResult result = parse("if (a) { } else { }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const IfStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->consequent, nullptr);
    EXPECT_EQ(statement->consequent->kind(), NodeKind::BlockStatement);
    ASSERT_NE(statement->alternate, nullptr);
    EXPECT_EQ(statement->alternate->kind(), NodeKind::BlockStatement);
}

TEST(ParserTest, IfWithEmptyStatement) {
    ParseResult result = parse("if (a) ;");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const IfStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->consequent, nullptr);
    EXPECT_EQ(statement->consequent->kind(), NodeKind::EmptyStatement);
}

TEST(ParserTest, IfMissingRightParenReportsError) {
    ParseResult result = parse("if (a b;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ')'"), std::string::npos);
}

TEST(ParserTest, IfElseWithoutAsiReportsError) {
    ParseResult result = parse("if (a) b else c;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
}

TEST(ParserTest, WhileStatement) {
    ParseResult result = parse("while (a) b;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::WhileStatement);

    const auto* statement =
        static_cast<const WhileStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->test, nullptr);
    EXPECT_EQ(statement->test->kind(), NodeKind::Identifier);
    ASSERT_NE(statement->body, nullptr);
    EXPECT_EQ(statement->body->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, WhileWithBlockBody) {
    ParseResult result = parse("while (a) { }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const WhileStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->body, nullptr);
    EXPECT_EQ(statement->body->kind(), NodeKind::BlockStatement);
}

TEST(ParserTest, WhileWithEmptyBody) {
    ParseResult result = parse("while (a);");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const WhileStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->body, nullptr);
    EXPECT_EQ(statement->body->kind(), NodeKind::EmptyStatement);
}

TEST(ParserTest, NestedWhileStatement) {
    ParseResult result = parse("while (a) while (b) c;");

    ASSERT_TRUE(result.ok());
    const auto* outer =
        static_cast<const WhileStatement*>(programOf(result).body[0].get());
    ASSERT_NE(outer->body, nullptr);
    EXPECT_EQ(outer->body->kind(), NodeKind::WhileStatement);
}

TEST(ParserTest, WhileMissingRightParenReportsError) {
    ParseResult result = parse("while (a b;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ')'"), std::string::npos);
}

TEST(ParserTest, DoWhileStatement) {
    ParseResult result = parse("do a; while (b);");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::DoWhileStatement);

    const auto* statement =
        static_cast<const DoWhileStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->body, nullptr);
    EXPECT_EQ(statement->body->kind(), NodeKind::ExpressionStatement);
    ASSERT_NE(statement->test, nullptr);
    EXPECT_EQ(statement->test->kind(), NodeKind::Identifier);
}

TEST(ParserTest, DoWhileWithBlockBody) {
    ParseResult result = parse("do { } while (a)");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const DoWhileStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->body, nullptr);
    EXPECT_EQ(statement->body->kind(), NodeKind::BlockStatement);
}

TEST(ParserTest, DoWhileSemicolonIsOptional) {
    ParseResult result = parse("do a; while (b) c;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::DoWhileStatement);
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, DoWhileMissingWhileReportsError) {
    ParseResult result = parse("do a; b;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected 'while'"),
              std::string::npos);
}

TEST(ParserTest, DoWhileMissingRightParenReportsError) {
    ParseResult result = parse("do a; while (b;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ')'"), std::string::npos);
}

TEST(ParserTest, ForStatementEmptyClauses) {
    ParseResult result = parse("for (;;) ;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::ForStatement);

    const auto* statement =
        static_cast<const ForStatement*>(programOf(result).body[0].get());
    EXPECT_EQ(statement->init, nullptr);
    EXPECT_EQ(statement->test, nullptr);
    EXPECT_EQ(statement->update, nullptr);
    ASSERT_NE(statement->body, nullptr);
    EXPECT_EQ(statement->body->kind(), NodeKind::EmptyStatement);
}

TEST(ParserTest, ForStatementWithExpressionClauses) {
    ParseResult result = parse("for (i = 0; i < 10; i++) { }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const ForStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->init, nullptr);
    EXPECT_EQ(statement->init->kind(), NodeKind::AssignmentExpression);
    ASSERT_NE(statement->test, nullptr);
    EXPECT_EQ(statement->test->kind(), NodeKind::BinaryExpression);
    ASSERT_NE(statement->update, nullptr);
    EXPECT_EQ(statement->update->kind(), NodeKind::UpdateExpression);
    ASSERT_NE(statement->body, nullptr);
    EXPECT_EQ(statement->body->kind(), NodeKind::BlockStatement);
}

TEST(ParserTest, ForStatementWithDeclarationInit) {
    struct Case {
        const char* source;
        VariableKind kind;
    };
    const Case cases[] = {
        {"for (var i = 0; i < 10; i++);", VariableKind::Var},
        {"for (let i = 0; i < 10; i++);", VariableKind::Let},
        {"for (const i = 0; i < 10; i++);", VariableKind::Const},
    };

    for (const Case& item : cases) {
        ParseResult result = parse(item.source);
        ASSERT_TRUE(result.ok()) << item.source;
        const auto* statement =
            static_cast<const ForStatement*>(programOf(result).body[0].get());
        ASSERT_NE(statement->init, nullptr) << item.source;
        EXPECT_EQ(statement->init->kind(), NodeKind::VariableDeclaration)
            << item.source;
        const auto* declaration =
            static_cast<const VariableDeclaration*>(statement->init.get());
        EXPECT_EQ(declaration->variableKind, item.kind) << item.source;
    }
}

TEST(ParserTest, ForStatementInitCanBeSequence) {
    ParseResult result = parse("for (i = 0, j = 0; i < 10; i++);");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const ForStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->init, nullptr);
    EXPECT_EQ(statement->init->kind(), NodeKind::SequenceExpression);
}

TEST(ParserTest, ForStatementOptionalTestAndUpdate) {
    ParseResult result = parse("for (; i < 10;);");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const ForStatement*>(programOf(result).body[0].get());
    EXPECT_EQ(statement->init, nullptr);
    ASSERT_NE(statement->test, nullptr);
    EXPECT_EQ(statement->test->kind(), NodeKind::BinaryExpression);
    EXPECT_EQ(statement->update, nullptr);
}

TEST(ParserTest, ForStatementMissingRightParenReportsError) {
    ParseResult result = parse("for (;; i++");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ')'"), std::string::npos);
}

TEST(ParserTest, ForStatementMissingClauseSemicolonReportsError) {
    ParseResult result = parse("for (var i = 0 i < 10; i++);");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ';'"), std::string::npos);
}

TEST(ParserTest, ForInStatement) {
    ParseResult result = parse("for (x in obj) ;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::ForInStatement);

    const auto* statement =
        static_cast<const ForInStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->left, nullptr);
    EXPECT_EQ(statement->left->kind(), NodeKind::Identifier);
    ASSERT_NE(statement->right, nullptr);
    EXPECT_EQ(statement->right->kind(), NodeKind::Identifier);
    ASSERT_NE(statement->body, nullptr);
    EXPECT_EQ(statement->body->kind(), NodeKind::EmptyStatement);
}

TEST(ParserTest, ForInWithDeclaration) {
    const char* sources[] = {
        "for (var x in obj) ;",
        "for (let x in obj) ;",
    };

    for (const char* source : sources) {
        ParseResult result = parse(source);
        ASSERT_TRUE(result.ok()) << source;
        const auto* statement =
            static_cast<const ForInStatement*>(programOf(result).body[0].get());
        ASSERT_NE(statement->left, nullptr) << source;
        EXPECT_EQ(statement->left->kind(), NodeKind::VariableDeclaration)
            << source;
    }
}

TEST(ParserTest, ForInLeftCanBeMember) {
    ParseResult result = parse("for (x.y in obj) ;");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const ForInStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->left, nullptr);
    EXPECT_EQ(statement->left->kind(), NodeKind::MemberExpression);
}

TEST(ParserTest, ForInRightCanBeSequence) {
    ParseResult result = parse("for (x in a, b) ;");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const ForInStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->right, nullptr);
    EXPECT_EQ(statement->right->kind(), NodeKind::SequenceExpression);
}

TEST(ParserTest, ForOfStatement) {
    ParseResult result = parse("for (x of arr) ;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::ForOfStatement);

    const auto* statement =
        static_cast<const ForOfStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->left, nullptr);
    EXPECT_EQ(statement->left->kind(), NodeKind::Identifier);
    ASSERT_NE(statement->right, nullptr);
    EXPECT_EQ(statement->right->kind(), NodeKind::Identifier);
}

TEST(ParserTest, ForOfWithDeclaration) {
    ParseResult result = parse("for (var x of arr) ;");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const ForOfStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->left, nullptr);
    EXPECT_EQ(statement->left->kind(), NodeKind::VariableDeclaration);
}

TEST(ParserTest, ForOfRightStopsAtComma) {
    ParseResult result = parse("for (x of a, b) ;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
}

TEST(ParserTest, ForInInvalidLeftReportsError) {
    ParseResult result = parse("for (a + b in obj) ;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Invalid left-hand side"),
              std::string::npos);
}

TEST(ParserTest, ForInMissingRightParenReportsError) {
    ParseResult result = parse("for (x in obj ;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ')'"), std::string::npos);
}

TEST(ParserTest, ParenthesesResetNoInRestriction) {
    ParseResult result = parse("for ((x in y); ; ) ;");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const ForStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->init, nullptr);
    EXPECT_EQ(statement->init->kind(), NodeKind::BinaryExpression);
    EXPECT_EQ(static_cast<const BinaryExpression*>(statement->init.get())->op,
              BinaryOperator::In);
}

TEST(ParserTest, InOperatorNotAllowedInForInitWithoutParens) {
    ParseResult result = parse("for (x in y; ; ) ;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
}

TEST(ParserTest, ReturnStatementWithoutArgument) {
    ParseResult result = parse("return;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::ReturnStatement);

    const auto* statement =
        static_cast<const ReturnStatement*>(programOf(result).body[0].get());
    EXPECT_EQ(statement->argument, nullptr);
}

TEST(ParserTest, ReturnStatementWithExpression) {
    ParseResult result = parse("return x + y;");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const ReturnStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->argument, nullptr);
    EXPECT_EQ(statement->argument->kind(), NodeKind::BinaryExpression);
}

TEST(ParserTest, ReturnLineTerminatorEndsStatement) {
    ParseResult result = parse("return\nx;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);

    const auto* statement =
        static_cast<const ReturnStatement*>(programOf(result).body[0].get());
    EXPECT_EQ(statement->argument, nullptr);
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, ReturnAtEndOfInput) {
    ParseResult result = parse("return");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const ReturnStatement*>(programOf(result).body[0].get());
    EXPECT_EQ(statement->argument, nullptr);
}

TEST(ParserTest, ReturnInsideBlock) {
    ParseResult result = parse("{ return; }");

    ASSERT_TRUE(result.ok());
    const auto* block =
        static_cast<const BlockStatement*>(programOf(result).body[0].get());
    ASSERT_EQ(block->body.size(), 1u);
    EXPECT_EQ(block->body[0]->kind(), NodeKind::ReturnStatement);
}

TEST(ParserTest, ReturnMissingSemicolonReportsError) {
    ParseResult result = parse("return x y;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ';'"), std::string::npos);
}

TEST(ParserTest, ThrowStatement) {
    ParseResult result = parse("throw x + y;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::ThrowStatement);

    const auto* statement =
        static_cast<const ThrowStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->argument, nullptr);
    EXPECT_EQ(statement->argument->kind(), NodeKind::BinaryExpression);
}

TEST(ParserTest, ThrowLineTerminatorReportsError) {
    ParseResult result = parse("throw\nx;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Illegal newline after throw"),
              std::string::npos);
}

TEST(ParserTest, ThrowWithoutArgumentReportsError) {
    ParseResult result = parse("throw;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
}

TEST(ParserTest, ThrowMissingSemicolonReportsError) {
    ParseResult result = parse("throw x y;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ';'"), std::string::npos);
}

TEST(ParserTest, ThrowInsideBlock) {
    ParseResult result = parse("{ throw x; }");

    ASSERT_TRUE(result.ok());
    const auto* block =
        static_cast<const BlockStatement*>(programOf(result).body[0].get());
    ASSERT_EQ(block->body.size(), 1u);
    EXPECT_EQ(block->body[0]->kind(), NodeKind::ThrowStatement);
}

TEST(ParserTest, BreakStatement) {
    ParseResult plain = parse("break;");
    ASSERT_TRUE(plain.ok());
    ASSERT_EQ(programOf(plain).body[0]->kind(), NodeKind::BreakStatement);
    const auto* noLabel =
        static_cast<const BreakStatement*>(programOf(plain).body[0].get());
    EXPECT_TRUE(noLabel->label.empty());

    ParseResult labeled = parse("break outer;");
    ASSERT_TRUE(labeled.ok());
    const auto* withLabel =
        static_cast<const BreakStatement*>(programOf(labeled).body[0].get());
    EXPECT_EQ(withLabel->label, "outer");
}

TEST(ParserTest, BreakLabelStopsAtLineTerminator) {
    ParseResult result = parse("break\nouter;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    const auto* statement =
        static_cast<const BreakStatement*>(programOf(result).body[0].get());
    EXPECT_TRUE(statement->label.empty());
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, ContinueStatement) {
    ParseResult plain = parse("continue;");
    ASSERT_TRUE(plain.ok());
    ASSERT_EQ(programOf(plain).body[0]->kind(), NodeKind::ContinueStatement);
    const auto* noLabel =
        static_cast<const ContinueStatement*>(programOf(plain).body[0].get());
    EXPECT_TRUE(noLabel->label.empty());

    ParseResult labeled = parse("continue outer;");
    ASSERT_TRUE(labeled.ok());
    const auto* withLabel =
        static_cast<const ContinueStatement*>(programOf(labeled).body[0].get());
    EXPECT_EQ(withLabel->label, "outer");
}

TEST(ParserTest, ContinueLabelStopsAtLineTerminator) {
    ParseResult result = parse("continue\nouter;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    const auto* statement =
        static_cast<const ContinueStatement*>(programOf(result).body[0].get());
    EXPECT_TRUE(statement->label.empty());
}

TEST(ParserTest, BreakMissingSemicolonReportsError) {
    ParseResult result = parse("break outer inner;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ';'"), std::string::npos);
}

TEST(ParserTest, TryCatchStatement) {
    ParseResult result = parse("try { } catch (e) { }");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::TryStatement);

    const auto* statement =
        static_cast<const TryStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->block, nullptr);
    EXPECT_EQ(statement->block->kind(), NodeKind::BlockStatement);
    ASSERT_NE(statement->handler, nullptr);
    EXPECT_EQ(statement->handler->kind(), NodeKind::CatchClause);
    EXPECT_EQ(statement->finalizer, nullptr);

    const auto* handler =
        static_cast<const CatchClause*>(statement->handler.get());
    ASSERT_NE(handler->param, nullptr);
    EXPECT_EQ(handler->param->kind(), NodeKind::Identifier);
    ASSERT_NE(handler->body, nullptr);
    EXPECT_EQ(handler->body->kind(), NodeKind::BlockStatement);
}

TEST(ParserTest, TryFinallyStatement) {
    ParseResult result = parse("try { } finally { }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const TryStatement*>(programOf(result).body[0].get());
    EXPECT_EQ(statement->handler, nullptr);
    ASSERT_NE(statement->finalizer, nullptr);
    EXPECT_EQ(statement->finalizer->kind(), NodeKind::BlockStatement);
}

TEST(ParserTest, TryCatchFinallyStatement) {
    ParseResult result = parse("try { } catch (e) { } finally { }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const TryStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->handler, nullptr);
    ASSERT_NE(statement->finalizer, nullptr);
}

TEST(ParserTest, CatchWithoutBinding) {
    ParseResult result = parse("try { } catch { }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const TryStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->handler, nullptr);
    const auto* handler =
        static_cast<const CatchClause*>(statement->handler.get());
    EXPECT_EQ(handler->param, nullptr);
}

TEST(ParserTest, TryWithoutCatchOrFinallyReportsError) {
    ParseResult result = parse("try { }");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Missing catch or finally"),
              std::string::npos);
}

TEST(ParserTest, CatchEmptyParenthesesReportsError) {
    ParseResult result = parse("try { } catch () { }");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected catch parameter"),
              std::string::npos);
}

TEST(ParserTest, CatchMissingBlockReportsError) {
    ParseResult result = parse("try { } catch e { }");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
}

TEST(ParserTest, EmptySwitchStatement) {
    ParseResult result = parse("switch (x) { }");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::SwitchStatement);

    const auto* statement =
        static_cast<const SwitchStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->discriminant, nullptr);
    EXPECT_EQ(statement->discriminant->kind(), NodeKind::Identifier);
    EXPECT_TRUE(statement->cases.empty());
}

TEST(ParserTest, SwitchCases) {
    ParseResult result = parse("switch (x) { case 1: a; case 2: b; }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const SwitchStatement*>(programOf(result).body[0].get());
    ASSERT_EQ(statement->cases.size(), 2u);

    const auto* first =
        static_cast<const SwitchCase*>(statement->cases[0].get());
    ASSERT_NE(first->test, nullptr);
    EXPECT_EQ(first->test->kind(), NodeKind::NumericLiteral);
    ASSERT_EQ(first->consequent.size(), 1u);

    const auto* second =
        static_cast<const SwitchCase*>(statement->cases[1].get());
    ASSERT_NE(second->test, nullptr);
    ASSERT_EQ(second->consequent.size(), 1u);
}

TEST(ParserTest, SwitchDefaultCase) {
    ParseResult result = parse("switch (x) { default: a; }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const SwitchStatement*>(programOf(result).body[0].get());
    ASSERT_EQ(statement->cases.size(), 1u);
    const auto* clause =
        static_cast<const SwitchCase*>(statement->cases[0].get());
    EXPECT_EQ(clause->test, nullptr);
    ASSERT_EQ(clause->consequent.size(), 1u);
}

TEST(ParserTest, SwitchFallthroughCase) {
    ParseResult result = parse("switch (x) { case 1: case 2: a; }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const SwitchStatement*>(programOf(result).body[0].get());
    ASSERT_EQ(statement->cases.size(), 2u);
    const auto* first =
        static_cast<const SwitchCase*>(statement->cases[0].get());
    EXPECT_TRUE(first->consequent.empty());
    const auto* second =
        static_cast<const SwitchCase*>(statement->cases[1].get());
    ASSERT_EQ(second->consequent.size(), 1u);
}

TEST(ParserTest, SwitchDefaultInMiddle) {
    ParseResult result = parse(
        "switch (x) { case 1: a; default: b; case 2: c; }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const SwitchStatement*>(programOf(result).body[0].get());
    ASSERT_EQ(statement->cases.size(), 3u);
    EXPECT_EQ(static_cast<const SwitchCase*>(statement->cases[1].get())->test,
              nullptr);
}

TEST(ParserTest, SwitchCaseExpressionCanBeSequence) {
    ParseResult result = parse("switch (x) { case a, b: c; }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const SwitchStatement*>(programOf(result).body[0].get());
    const auto* clause =
        static_cast<const SwitchCase*>(statement->cases[0].get());
    ASSERT_NE(clause->test, nullptr);
    EXPECT_EQ(clause->test->kind(), NodeKind::SequenceExpression);
}

TEST(ParserTest, SwitchMissingRightParenReportsError) {
    ParseResult result = parse("switch (x { }");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ')'"), std::string::npos);
}

TEST(ParserTest, SwitchInvalidClauseReportsError) {
    ParseResult result = parse("switch (x) { foo }");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected 'case' or 'default'"),
              std::string::npos);
}

TEST(ParserTest, LabeledStatement) {
    ParseResult result = parse("foo: bar;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::LabeledStatement);

    const auto* statement =
        static_cast<const LabeledStatement*>(programOf(result).body[0].get());
    EXPECT_EQ(statement->label, "foo");
    ASSERT_NE(statement->body, nullptr);
    EXPECT_EQ(statement->body->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, NestedLabeledStatement) {
    ParseResult result = parse("a: b: c;");

    ASSERT_TRUE(result.ok());
    const auto* outer =
        static_cast<const LabeledStatement*>(programOf(result).body[0].get());
    EXPECT_EQ(outer->label, "a");
    ASSERT_NE(outer->body, nullptr);
    EXPECT_EQ(outer->body->kind(), NodeKind::LabeledStatement);

    const auto* inner =
        static_cast<const LabeledStatement*>(outer->body.get());
    EXPECT_EQ(inner->label, "b");
}

TEST(ParserTest, LabeledBlockStatement) {
    ParseResult result = parse("foo: { }");

    ASSERT_TRUE(result.ok());
    const auto* statement =
        static_cast<const LabeledStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->body, nullptr);
    EXPECT_EQ(statement->body->kind(), NodeKind::BlockStatement);
}

TEST(ParserTest, ConditionalIsNotLabeledStatement) {
    ParseResult result = parse("foo ? a : b;");

    ASSERT_TRUE(result.ok());
    const auto& statement = statementOf(result, 0);
    EXPECT_EQ(statement.expression->kind(), NodeKind::ConditionalExpression);
}

TEST(ParserTest, LabelWithoutBodyReportsError) {
    ParseResult result = parse("foo:");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
}

TEST(ParserTest, WithStatement) {
    ParseResult result = parse("with (obj) { }");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::WithStatement);

    const auto* statement =
        static_cast<const WithStatement*>(programOf(result).body[0].get());
    ASSERT_NE(statement->object, nullptr);
    EXPECT_EQ(statement->object->kind(), NodeKind::Identifier);
    ASSERT_NE(statement->body, nullptr);
    EXPECT_EQ(statement->body->kind(), NodeKind::BlockStatement);
}

TEST(ParserTest, WithInStrictModeReportsError) {
    ParseResult result = parse("\"use strict\"; with (obj) { }");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Strict mode"), std::string::npos);
}

TEST(ParserTest, DebuggerStatement) {
    ParseResult result = parse("debugger;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::DebuggerStatement);
}

TEST(ParserTest, DebuggerAtEndOfInput) {
    ParseResult result = parse("debugger");

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::DebuggerStatement);
}

TEST(ParserTest, DebuggerAllowsAsi) {
    ParseResult result = parse("debugger\nx;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::DebuggerStatement);
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, DebuggerMissingSemicolonReportsError) {
    ParseResult result = parse("debugger x;");

    ASSERT_NE(result.program, nullptr);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].message.find("Expected ';'"), std::string::npos);
}

TEST(ParserTest, UseStrictDirectiveNode) {
    ParseResult result = parse("\"use strict\";");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::Directive);
    EXPECT_TRUE(programOf(result).strict);

    const auto* directive =
        static_cast<const Directive*>(programOf(result).body[0].get());
    EXPECT_EQ(directive->value, "use strict");
    EXPECT_EQ(directive->raw, "\"use strict\"");
    EXPECT_TRUE(directive->isUseStrict());
}

TEST(ParserTest, SingleQuotedUseStrictDirective) {
    ParseResult result = parse("'use strict';");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::Directive);
    EXPECT_TRUE(programOf(result).strict);
}

TEST(ParserTest, DirectivePrologueContinuesThroughOtherDirectives) {
    ParseResult result = parse("\"hello\"; \"use strict\";");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::Directive);
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::Directive);
    EXPECT_TRUE(programOf(result).strict);
}

TEST(ParserTest, EscapedUseStrictIsDirectiveButNotStrictMode) {
    ParseResult result = parse("\"use\\x20strict\";");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::Directive);
    EXPECT_FALSE(programOf(result).strict);

    const auto* directive =
        static_cast<const Directive*>(programOf(result).body[0].get());
    EXPECT_EQ(directive->value, "use strict");
    EXPECT_FALSE(directive->isUseStrict());
}

TEST(ParserTest, StringWithContinuationIsNotDirective) {
    ParseResult result = parse("\"use strict\" + x;");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::ExpressionStatement);
    EXPECT_FALSE(programOf(result).strict);
}

TEST(ParserTest, ParenthesizedStringIsNotDirective) {
    ParseResult result = parse("(\"use strict\");");

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::ExpressionStatement);
    EXPECT_FALSE(programOf(result).strict);
}

TEST(ParserTest, DirectivePrologueEndsAtFirstNonDirective) {
    ParseResult result = parse("x; \"use strict\";");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::ExpressionStatement);
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::ExpressionStatement);
    EXPECT_FALSE(programOf(result).strict);
}

TEST(ParserTest, ProgramRangeCoversConsumedInput) {
    ParseResult result = parse("42;");

    ASSERT_NE(result.program, nullptr);
    EXPECT_EQ(result.program->range().start.offset, 0u);
    EXPECT_EQ(result.program->range().end.offset, 3u);
}

// ---------------------------------------------------------------------------
// 函数声明
// ---------------------------------------------------------------------------

TEST(ParserTest, BasicFunctionDeclaration) {
    ParseResult result = parse("function add(a, b) { return a + b; }");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    const auto& node = *programOf(result).body[0];
    ASSERT_EQ(node.kind(), NodeKind::FunctionDeclaration);

    const auto& function = static_cast<const FunctionDeclaration&>(node);
    ASSERT_NE(function.id, nullptr);
    EXPECT_EQ(static_cast<const Identifier&>(*function.id).name, "add");
    EXPECT_FALSE(function.async);
    EXPECT_FALSE(function.generator);

    ASSERT_EQ(function.params.size(), 2u);
    EXPECT_EQ(static_cast<const Identifier&>(*function.params[0]).name, "a");
    EXPECT_EQ(static_cast<const Identifier&>(*function.params[1]).name, "b");

    ASSERT_NE(function.body, nullptr);
    ASSERT_EQ(function.body->kind(), NodeKind::BlockStatement);
    const auto& body = static_cast<const BlockStatement&>(*function.body);
    ASSERT_EQ(body.body.size(), 1u);
    EXPECT_EQ(body.body[0]->kind(), NodeKind::ReturnStatement);
}

TEST(ParserTest, FunctionDeclarationWithEmptyParams) {
    ParseResult result = parse("function f() {}");

    ASSERT_TRUE(result.ok());
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    EXPECT_TRUE(function.params.empty());
}

TEST(ParserTest, FunctionDeclarationAllowsTrailingComma) {
    ParseResult result = parse("function f(a,) {}");

    ASSERT_TRUE(result.ok());
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    ASSERT_EQ(function.params.size(), 1u);
}

TEST(ParserTest, AsyncFunctionDeclaration) {
    ParseResult result = parse("async function f() {}");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    EXPECT_TRUE(function.async);
    EXPECT_FALSE(function.generator);
}

TEST(ParserTest, GeneratorFunctionDeclaration) {
    ParseResult result = parse("function* gen() {}");

    ASSERT_TRUE(result.ok());
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    EXPECT_FALSE(function.async);
    EXPECT_TRUE(function.generator);
    EXPECT_EQ(static_cast<const Identifier&>(*function.id).name, "gen");
}

TEST(ParserTest, AsyncGeneratorFunctionDeclaration) {
    ParseResult result = parse("async function* gen() {}");

    ASSERT_TRUE(result.ok());
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    EXPECT_TRUE(function.async);
    EXPECT_TRUE(function.generator);
}

TEST(ParserTest, AsyncWithoutLineTerminatorIsDeclaration) {
    ParseResult result = parse("async function f() {}");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    EXPECT_EQ(programOf(result).body[0]->kind(),
              NodeKind::FunctionDeclaration);
}

TEST(ParserTest, AsyncFollowedByNewlineIsNotAsyncFunction) {
    ParseResult result = parse("async\nfunction f() {}");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::ExpressionStatement);
    EXPECT_EQ(programOf(result).body[1]->kind(),
              NodeKind::FunctionDeclaration);
}

TEST(ParserTest, RestParameter) {
    ParseResult result = parse("function f(a, ...rest) {}");

    ASSERT_TRUE(result.ok());
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    ASSERT_EQ(function.params.size(), 2u);
    ASSERT_EQ(function.params[1]->kind(), NodeKind::RestElement);
    const auto& rest = static_cast<const RestElement&>(*function.params[1]);
    ASSERT_NE(rest.argument, nullptr);
    EXPECT_EQ(static_cast<const Identifier&>(*rest.argument).name, "rest");
}

TEST(ParserTest, RestParameterMustBeLast) {
    ParseResult result = parse("function f(...rest, b) {}");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, MissingFunctionNameIsLeftToSema) {
    ParseResult result = parse("function () {}");

    ASSERT_TRUE(result.ok());
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    EXPECT_EQ(function.id, nullptr);
}

TEST(ParserTest, ExportNamedAnonymousFunctionIsLeftToSema) {
    ParseResult result = parse("export function () {}");

    ASSERT_TRUE(result.ok());
    const auto& exportNode = static_cast<const ExportNamedDeclaration&>(
        *programOf(result).body[0]);
    const auto& function =
        static_cast<const FunctionDeclaration&>(*exportNode.declaration);
    EXPECT_EQ(function.id, nullptr);
}

TEST(ParserTest, DefaultParameterReportsError) {
    ParseResult result = parse("function f(a = 1) {}");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, FunctionBodyUseStrictDoesNotLeak) {
    ParseResult result = parse("function f() { \"use strict\"; } var x = 1;");

    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(programOf(result).strict);
    ASSERT_EQ(programOf(result).body.size(), 2u);

    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    const auto& body = static_cast<const BlockStatement&>(*function.body);
    ASSERT_EQ(body.body.size(), 1u);
    EXPECT_EQ(body.body[0]->kind(), NodeKind::Directive);
}

TEST(ParserTest, FunctionDeclarationInsideBlock) {
    ParseResult result = parse("{ function f() {} }");

    ASSERT_TRUE(result.ok());
    const auto& block =
        static_cast<const BlockStatement&>(*programOf(result).body[0]);
    ASSERT_EQ(block.body.size(), 1u);
    EXPECT_EQ(block.body[0]->kind(), NodeKind::FunctionDeclaration);
}

// ---------------------------------------------------------------------------
// 顶层 export
// ---------------------------------------------------------------------------

TEST(ParserTest, ExportNamedFunctionDeclaration) {
    ParseResult result = parse("export function f(a) {}");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    const auto& node = *programOf(result).body[0];
    ASSERT_EQ(node.kind(), NodeKind::ExportNamedDeclaration);

    const auto& exportNode =
        static_cast<const ExportNamedDeclaration&>(node);
    ASSERT_NE(exportNode.declaration, nullptr);
    EXPECT_EQ(exportNode.declaration->kind(),
              NodeKind::FunctionDeclaration);
    const auto& function =
        static_cast<const FunctionDeclaration&>(*exportNode.declaration);
    EXPECT_EQ(static_cast<const Identifier&>(*function.id).name, "f");
}

TEST(ParserTest, ExportNamedAsyncGeneratorFunction) {
    ParseResult result = parse("export async function* gen() {}");

    ASSERT_TRUE(result.ok());
    const auto& exportNode = static_cast<const ExportNamedDeclaration&>(
        *programOf(result).body[0]);
    const auto& function =
        static_cast<const FunctionDeclaration&>(*exportNode.declaration);
    EXPECT_TRUE(function.async);
    EXPECT_TRUE(function.generator);
}

TEST(ParserTest, ExportDefaultNamedFunction) {
    ParseResult result = parse("export default function f() {}");

    ASSERT_TRUE(result.ok());
    const auto& node = *programOf(result).body[0];
    ASSERT_EQ(node.kind(), NodeKind::ExportDefaultDeclaration);

    const auto& exportNode =
        static_cast<const ExportDefaultDeclaration&>(node);
    ASSERT_NE(exportNode.declaration, nullptr);
    const auto& function =
        static_cast<const FunctionDeclaration&>(*exportNode.declaration);
    ASSERT_NE(function.id, nullptr);
    EXPECT_EQ(static_cast<const Identifier&>(*function.id).name, "f");
}

TEST(ParserTest, ExportDefaultAnonymousFunction) {
    ParseResult result = parse("export default function () {}");

    ASSERT_TRUE(result.ok());
    const auto& exportNode = static_cast<const ExportDefaultDeclaration&>(
        *programOf(result).body[0]);
    const auto& function =
        static_cast<const FunctionDeclaration&>(*exportNode.declaration);
    EXPECT_EQ(function.id, nullptr);
}

TEST(ParserTest, ExportDefaultAnonymousAsyncGenerator) {
    ParseResult result = parse("export default async function* () {}");

    ASSERT_TRUE(result.ok());
    const auto& exportNode = static_cast<const ExportDefaultDeclaration&>(
        *programOf(result).body[0]);
    const auto& function =
        static_cast<const FunctionDeclaration&>(*exportNode.declaration);
    EXPECT_EQ(function.id, nullptr);
    EXPECT_TRUE(function.async);
    EXPECT_TRUE(function.generator);
}

TEST(ParserTest, ExportNonFunctionReportsError) {
    ParseResult result = parse("export const x = 1;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ExportDefaultNonFunctionReportsError) {
    ParseResult result = parse("export default 1;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ExportInsideBlockReportsError) {
    ParseResult result = parse("{ export function f() {} }");

    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// 箭头函数
// ---------------------------------------------------------------------------

TEST(ParserTest, SingleParameterArrowFunction) {
    ParseResult result = parse("x => x;");

    ASSERT_TRUE(result.ok());
    const auto& expression = statementOf(result, 0).expression;
    ASSERT_NE(expression, nullptr);
    ASSERT_EQ(expression->kind(), NodeKind::ArrowFunctionExpression);

    const auto& arrow =
        static_cast<const ArrowFunctionExpression&>(*expression);
    EXPECT_FALSE(arrow.async);
    EXPECT_TRUE(arrow.expression);
    ASSERT_EQ(arrow.params.size(), 1u);
    EXPECT_EQ(static_cast<const Identifier&>(*arrow.params[0]).name, "x");
    ASSERT_NE(arrow.body, nullptr);
    EXPECT_EQ(arrow.body->kind(), NodeKind::Identifier);
}

TEST(ParserTest, ParenthesizedParametersArrowFunction) {
    ParseResult result = parse("(a, b) => a + b;");

    ASSERT_TRUE(result.ok());
    const auto& arrow = static_cast<const ArrowFunctionExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(arrow.params.size(), 2u);
    EXPECT_EQ(static_cast<const Identifier&>(*arrow.params[0]).name, "a");
    EXPECT_EQ(static_cast<const Identifier&>(*arrow.params[1]).name, "b");
    EXPECT_TRUE(arrow.expression);
    EXPECT_EQ(arrow.body->kind(), NodeKind::BinaryExpression);
}

TEST(ParserTest, EmptyParametersArrowFunction) {
    ParseResult result = parse("() => 42;");

    ASSERT_TRUE(result.ok());
    const auto& arrow = static_cast<const ArrowFunctionExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_TRUE(arrow.params.empty());
    EXPECT_TRUE(arrow.expression);
    EXPECT_EQ(arrow.body->kind(), NodeKind::NumericLiteral);
}

TEST(ParserTest, ArrowFunctionWithBlockBody) {
    ParseResult result = parse("(a) => { return a; };");

    ASSERT_TRUE(result.ok());
    const auto& arrow = static_cast<const ArrowFunctionExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_FALSE(arrow.expression);
    ASSERT_NE(arrow.body, nullptr);
    ASSERT_EQ(arrow.body->kind(), NodeKind::BlockStatement);
    const auto& body = static_cast<const BlockStatement&>(*arrow.body);
    ASSERT_EQ(body.body.size(), 1u);
    EXPECT_EQ(body.body[0]->kind(), NodeKind::ReturnStatement);
}

TEST(ParserTest, ArrowFunctionWithRestParameter) {
    ParseResult result = parse("(...rest) => rest;");

    ASSERT_TRUE(result.ok());
    const auto& arrow = static_cast<const ArrowFunctionExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(arrow.params.size(), 1u);
    EXPECT_EQ(arrow.params[0]->kind(), NodeKind::RestElement);
}

TEST(ParserTest, AsyncSingleParameterArrowFunction) {
    ParseResult result = parse("async x => x;");

    ASSERT_TRUE(result.ok());
    const auto& arrow = static_cast<const ArrowFunctionExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_TRUE(arrow.async);
    ASSERT_EQ(arrow.params.size(), 1u);
}

TEST(ParserTest, AsyncParenthesizedArrowFunction) {
    ParseResult result = parse("async (a, b) => a;");

    ASSERT_TRUE(result.ok());
    const auto& arrow = static_cast<const ArrowFunctionExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_TRUE(arrow.async);
    ASSERT_EQ(arrow.params.size(), 2u);
}

TEST(ParserTest, ParenthesizedExpressionIsNotArrowFunction) {
    ParseResult result = parse("(a);");

    ASSERT_TRUE(result.ok());
    const auto& expression = statementOf(result, 0).expression;
    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(expression->kind(), NodeKind::Identifier);
    EXPECT_TRUE(expression->isParenthesized());
}

TEST(ParserTest, ParenthesizedSequenceIsNotArrowFunction) {
    ParseResult result = parse("(a, b);");

    ASSERT_TRUE(result.ok());
    const auto& expression = statementOf(result, 0).expression;
    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(expression->kind(), NodeKind::SequenceExpression);
}

TEST(ParserTest, AsyncCallIsNotArrowFunction) {
    ParseResult result = parse("async(x);");

    ASSERT_TRUE(result.ok());
    const auto& expression = statementOf(result, 0).expression;
    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(expression->kind(), NodeKind::CallExpression);
}

TEST(ParserTest, LineTerminatorBeforeArrowReportsError) {
    ParseResult result = parse("x\n=> x;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ParenthesizedLineTerminatorBeforeArrowReportsError) {
    ParseResult result = parse("(x)\n=> x;");

    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// 数组字面量
// ---------------------------------------------------------------------------

TEST(ParserTest, EmptyArrayLiteral) {
    ParseResult result = parse("[];");

    ASSERT_TRUE(result.ok());
    const auto& array = static_cast<const ArrayExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_TRUE(array.elements.empty());
}

TEST(ParserTest, ArrayLiteralElements) {
    ParseResult result = parse("[1, \"two\", x];");

    ASSERT_TRUE(result.ok());
    const auto& array = static_cast<const ArrayExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(array.elements.size(), 3u);
    EXPECT_EQ(array.elements[0]->kind(), NodeKind::NumericLiteral);
    EXPECT_EQ(array.elements[1]->kind(), NodeKind::StringLiteral);
    EXPECT_EQ(array.elements[2]->kind(), NodeKind::Identifier);
}

TEST(ParserTest, ArrayLiteralElision) {
    ParseResult result = parse("[1, , 3];");

    ASSERT_TRUE(result.ok());
    const auto& array = static_cast<const ArrayExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(array.elements.size(), 3u);
    EXPECT_NE(array.elements[0], nullptr);
    EXPECT_EQ(array.elements[1], nullptr);
    EXPECT_NE(array.elements[2], nullptr);
}

TEST(ParserTest, ArrayLiteralLeadingElision) {
    ParseResult result = parse("[, 1];");

    ASSERT_TRUE(result.ok());
    const auto& array = static_cast<const ArrayExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(array.elements.size(), 2u);
    EXPECT_EQ(array.elements[0], nullptr);
    EXPECT_NE(array.elements[1], nullptr);
}

TEST(ParserTest, ArrayLiteralSingleElision) {
    ParseResult result = parse("[,];");

    ASSERT_TRUE(result.ok());
    const auto& array = static_cast<const ArrayExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(array.elements.size(), 1u);
    EXPECT_EQ(array.elements[0], nullptr);
}

TEST(ParserTest, ArrayLiteralTrailingCommaIsNotElision) {
    ParseResult result = parse("[1, 2,];");

    ASSERT_TRUE(result.ok());
    const auto& array = static_cast<const ArrayExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(array.elements.size(), 2u);
    EXPECT_NE(array.elements[0], nullptr);
    EXPECT_NE(array.elements[1], nullptr);
}

TEST(ParserTest, NestedArrayLiteral) {
    ParseResult result = parse("[[1], [2, 3]];");

    ASSERT_TRUE(result.ok());
    const auto& array = static_cast<const ArrayExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(array.elements.size(), 2u);
    EXPECT_EQ(array.elements[0]->kind(), NodeKind::ArrayExpression);
    EXPECT_EQ(array.elements[1]->kind(), NodeKind::ArrayExpression);
}

TEST(ParserTest, ArrayLiteralInMemberExpression) {
    ParseResult result = parse("[1, 2].length;");

    ASSERT_TRUE(result.ok());
    const auto& member = static_cast<const MemberExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_NE(member.object, nullptr);
    EXPECT_EQ(member.object->kind(), NodeKind::ArrayExpression);
}

TEST(ParserTest, ArrayLiteralAsArgument) {
    ParseResult result = parse("f([1, 2]);");

    ASSERT_TRUE(result.ok());
    const auto& call = static_cast<const CallExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(call.arguments.size(), 1u);
    EXPECT_EQ(call.arguments[0]->kind(), NodeKind::ArrayExpression);
}

TEST(ParserTest, ArrayLiteralMissingBracketReportsError) {
    ParseResult result = parse("[1, 2;");

    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// SpreadElement
// ---------------------------------------------------------------------------

TEST(ParserTest, ArrayLiteralSpreadElement) {
    ParseResult result = parse("[...a];");

    ASSERT_TRUE(result.ok());
    const auto& array = static_cast<const ArrayExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(array.elements.size(), 1u);
    ASSERT_EQ(array.elements[0]->kind(), NodeKind::SpreadElement);
    const auto& spread =
        static_cast<const SpreadElement&>(*array.elements[0]);
    ASSERT_NE(spread.argument, nullptr);
    EXPECT_EQ(spread.argument->kind(), NodeKind::Identifier);
}

TEST(ParserTest, ArrayLiteralSpreadAmongElements) {
    ParseResult result = parse("[1, ...a, 2];");

    ASSERT_TRUE(result.ok());
    const auto& array = static_cast<const ArrayExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(array.elements.size(), 3u);
    EXPECT_EQ(array.elements[0]->kind(), NodeKind::NumericLiteral);
    EXPECT_EQ(array.elements[1]->kind(), NodeKind::SpreadElement);
    EXPECT_EQ(array.elements[2]->kind(), NodeKind::NumericLiteral);
}

TEST(ParserTest, ArrayLiteralMultipleSpreadElements) {
    ParseResult result = parse("[...a, ...b];");

    ASSERT_TRUE(result.ok());
    const auto& array = static_cast<const ArrayExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(array.elements.size(), 2u);
    EXPECT_EQ(array.elements[0]->kind(), NodeKind::SpreadElement);
    EXPECT_EQ(array.elements[1]->kind(), NodeKind::SpreadElement);
}

TEST(ParserTest, ArrayLiteralSpreadAllowsTrailingComma) {
    ParseResult result = parse("[...a,];");

    ASSERT_TRUE(result.ok());
    const auto& array = static_cast<const ArrayExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(array.elements.size(), 1u);
    EXPECT_EQ(array.elements[0]->kind(), NodeKind::SpreadElement);
}

TEST(ParserTest, CallSpreadArguments) {
    ParseResult result = parse("f(a, ...b, c);");

    ASSERT_TRUE(result.ok());
    const auto& call = static_cast<const CallExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(call.arguments.size(), 3u);
    EXPECT_EQ(call.arguments[0]->kind(), NodeKind::Identifier);
    EXPECT_EQ(call.arguments[1]->kind(), NodeKind::SpreadElement);
    EXPECT_EQ(call.arguments[2]->kind(), NodeKind::Identifier);
}

TEST(ParserTest, NewSpreadArguments) {
    ParseResult result = parse("new F(...args);");

    ASSERT_TRUE(result.ok());
    const auto& call = static_cast<const NewExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(call.arguments.size(), 1u);
    EXPECT_EQ(call.arguments[0]->kind(), NodeKind::SpreadElement);
}

TEST(ParserTest, SpreadArgumentIsAssignmentExpression) {
    ParseResult result = parse("f(...a + b);");

    ASSERT_TRUE(result.ok());
    const auto& call = static_cast<const CallExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(call.arguments.size(), 1u);
    ASSERT_EQ(call.arguments[0]->kind(), NodeKind::SpreadElement);
    const auto& spread =
        static_cast<const SpreadElement&>(*call.arguments[0]);
    ASSERT_NE(spread.argument, nullptr);
    EXPECT_EQ(spread.argument->kind(), NodeKind::BinaryExpression);
}

}  // namespace
