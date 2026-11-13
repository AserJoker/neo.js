// ParserTest.cpp - 语法分析器框架与首批节点
//
// 覆盖：Program、空语句、表达式语句、标识符、各类字面量、ASI、
// 正则重扫、指令序言严格模式、错误收集。

#include "frontend/Parser.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using neo::frontend::ArrayExpression;
using neo::frontend::ArrayPattern;
using neo::frontend::ArrowFunctionExpression;
using neo::frontend::AssignmentPattern;
using neo::frontend::AssignmentExpression;
using neo::frontend::AssignmentOperator;
using neo::frontend::AwaitExpression;
using neo::frontend::BigIntLiteral;
using neo::frontend::BinaryExpression;
using neo::frontend::BlockStatement;
using neo::frontend::BreakStatement;
using neo::frontend::BinaryOperator;
using neo::frontend::BooleanLiteral;
using neo::frontend::CallExpression;
using neo::frontend::CatchClause;
using neo::frontend::ChainExpression;
using neo::frontend::ClassBody;
using neo::frontend::ClassDeclaration;
using neo::frontend::ClassExpression;
using neo::frontend::ConditionalExpression;
using neo::frontend::ContinueStatement;
using neo::frontend::DebuggerStatement;
using neo::frontend::Directive;
using neo::frontend::DoWhileStatement;
using neo::frontend::ExportAllDeclaration;
using neo::frontend::ExportDefaultDeclaration;
using neo::frontend::ExportNamedDeclaration;
using neo::frontend::ExportSpecifier;
using neo::frontend::ExpressionStatement;
using neo::frontend::ForInStatement;
using neo::frontend::FunctionDeclaration;
using neo::frontend::ForOfStatement;
using neo::frontend::ForStatement;
using neo::frontend::FunctionExpression;
using neo::frontend::Identifier;
using neo::frontend::IfStatement;
using neo::frontend::ImportAttribute;
using neo::frontend::ImportDeclaration;
using neo::frontend::ImportDefaultSpecifier;
using neo::frontend::ImportExpression;
using neo::frontend::ImportNamespaceSpecifier;
using neo::frontend::ImportSpecifier;
using neo::frontend::LabeledStatement;
using neo::frontend::LogicalExpression;
using neo::frontend::LogicalOperator;
using neo::frontend::MemberExpression;
using neo::frontend::MetaProperty;
using neo::frontend::MethodDefinition;
using neo::frontend::MethodKind;
using neo::frontend::NewExpression;
using neo::frontend::NodeKind;
using neo::frontend::ObjectExpression;
using neo::frontend::ObjectPattern;
using neo::frontend::PrivateIdentifier;
using neo::frontend::Property;
using neo::frontend::PropertyDefinition;
using neo::frontend::PropertyKind;
using neo::frontend::NumericLiteral;
using neo::frontend::Parser;
using neo::frontend::ParseResult;
using neo::frontend::Program;
using neo::frontend::RegExpLiteral;
using neo::frontend::RestElement;
using neo::frontend::ReturnStatement;
using neo::frontend::SequenceExpression;
using neo::frontend::SpreadElement;
using neo::frontend::StaticBlock;
using neo::frontend::StringLiteral;
using neo::frontend::SuperExpression;
using neo::frontend::SwitchCase;
using neo::frontend::SwitchStatement;
using neo::frontend::TaggedTemplateExpression;
using neo::frontend::TemplateElement;
using neo::frontend::TemplateLiteral;
using neo::frontend::ThisExpression;
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
using neo::frontend::YieldExpression;

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

TEST(ParserTest, LegacyOctalIsRecordedForSema) {
    ParseResult result = parse("\"use strict\"; 077;");

    // Parser 不判定严格性：077 正常成树，并带上 legacy 标记。
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    const auto& statement = statementOf(result, 1);
    ASSERT_EQ(statement.expression->kind(), NodeKind::NumericLiteral);
    const auto& literal =
        static_cast<const NumericLiteral&>(*statement.expression);
    EXPECT_DOUBLE_EQ(literal.value, 63.0);
    EXPECT_TRUE(literal.legacyLeadingZero);
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

TEST(ParserTest, ForAwaitOfStatement) {
    ParseResult result = parse("async function f() { for await (x of arr) ; }");

    ASSERT_TRUE(result.ok());
    const auto& function = static_cast<const FunctionDeclaration&>(
        *programOf(result).body[0]);
    const auto& body = static_cast<const BlockStatement&>(*function.body);
    const auto& statement =
        static_cast<const ForOfStatement&>(*body.body[0]);
    EXPECT_TRUE(statement.await);
}

TEST(ParserTest, ForAwaitWithoutOfReportsError) {
    ParseResult result = parse("async function f() { for await (x in arr) ; }");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, PlainForOfIsNotAwait) {
    ParseResult result = parse("for (x of arr) ;");

    ASSERT_TRUE(result.ok());
    const auto& statement =
        static_cast<const ForOfStatement&>(*programOf(result).body[0]);
    EXPECT_FALSE(statement.await);
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

TEST(ParserTest, WithInStrictModeIsParsedForSema) {
    ParseResult result = parse("\"use strict\"; with (obj) { }");

    // 严格性检查属于 Sema；Parser 只负责成树。
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::WithStatement);
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
}

TEST(ParserTest, DirectivePrologueContinuesThroughOtherDirectives) {
    ParseResult result = parse("\"hello\"; \"use strict\";");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::Directive);
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::Directive);
}

TEST(ParserTest, EscapedUseStrictIsDirectiveButNotStrictMode) {
    ParseResult result = parse("\"use\\x20strict\";");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body[0]->kind(), NodeKind::Directive);

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
}

TEST(ParserTest, ParenthesizedStringIsNotDirective) {
    ParseResult result = parse("(\"use strict\");");

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, DirectivePrologueEndsAtFirstNonDirective) {
    ParseResult result = parse("x; \"use strict\";");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 2u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::ExpressionStatement);
    EXPECT_EQ(programOf(result).body[1]->kind(), NodeKind::ExpressionStatement);
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

TEST(ParserTest, DefaultParameter) {
    ParseResult result = parse("function f(a = 1) {}");

    ASSERT_TRUE(result.ok());
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    ASSERT_EQ(function.params.size(), 1u);
    ASSERT_EQ(function.params[0]->kind(), NodeKind::AssignmentPattern);
    const auto& parameter =
        static_cast<const AssignmentPattern&>(*function.params[0]);
    EXPECT_EQ(parameter.left->kind(), NodeKind::Identifier);
    EXPECT_EQ(parameter.right->kind(), NodeKind::NumericLiteral);
}

TEST(ParserTest, FunctionBodyUseStrictDoesNotLeak) {
    ParseResult result = parse("function f() { \"use strict\"; } var x = 1;");

    ASSERT_TRUE(result.ok());
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

TEST(ParserTest, ExportNonDeclarationReportsError) {
    ParseResult result = parse("export 1;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ExportDefaultExpression) {
    ParseResult result = parse("export default 1;");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportDefaultDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_NE(declaration.declaration, nullptr);
    EXPECT_EQ(declaration.declaration->kind(), NodeKind::NumericLiteral);
}

TEST(ParserTest, ExportDefaultBinaryExpression) {
    ParseResult result = parse("export default a + b;");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportDefaultDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_NE(declaration.declaration, nullptr);
    EXPECT_EQ(declaration.declaration->kind(), NodeKind::BinaryExpression);
}

TEST(ParserTest, ExportDefaultMissingExpressionReportsError) {
    ParseResult result = parse("export default ;");

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

// ---------------------------------------------------------------------------
// 对象字面量
// ---------------------------------------------------------------------------

TEST(ParserTest, EmptyObjectLiteral) {
    ParseResult result = parse("({});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_TRUE(object.properties.empty());
}

TEST(ParserTest, ObjectLiteralProperties) {
    ParseResult result = parse("({a: 1, b: 2});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(object.properties.size(), 2u);

    const auto& first = static_cast<const Property&>(*object.properties[0]);
    EXPECT_EQ(first.key->kind(), NodeKind::Identifier);
    EXPECT_EQ(static_cast<const Identifier&>(*first.key).name, "a");
    EXPECT_EQ(first.value->kind(), NodeKind::NumericLiteral);
    EXPECT_FALSE(first.computed);
    EXPECT_FALSE(first.shorthand);
}

TEST(ParserTest, ObjectLiteralShorthand) {
    ParseResult result = parse("({a, b});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(object.properties.size(), 2u);

    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.shorthand);
    EXPECT_FALSE(property.computed);
    EXPECT_EQ(property.key->kind(), NodeKind::Identifier);
    ASSERT_NE(property.value, nullptr);
    EXPECT_EQ(static_cast<const Identifier&>(*property.value).name, "a");
}

TEST(ParserTest, ObjectLiteralComputedKey) {
    ParseResult result = parse("({[k]: v});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(object.properties.size(), 1u);

    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.computed);
    EXPECT_FALSE(property.shorthand);
    EXPECT_EQ(property.key->kind(), NodeKind::Identifier);
    EXPECT_EQ(property.value->kind(), NodeKind::Identifier);
}

TEST(ParserTest, ObjectLiteralLiteralKeys) {
    ParseResult result = parse("({\"s\": 1, 42: 2, 1n: 3});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(object.properties.size(), 3u);
    EXPECT_EQ(static_cast<const Property&>(*object.properties[0]).key->kind(),
              NodeKind::StringLiteral);
    EXPECT_EQ(static_cast<const Property&>(*object.properties[1]).key->kind(),
              NodeKind::NumericLiteral);
    EXPECT_EQ(static_cast<const Property&>(*object.properties[2]).key->kind(),
              NodeKind::BigIntLiteral);
}

TEST(ParserTest, ObjectLiteralReservedWordKey) {
    ParseResult result = parse("({default: 1, if: 2});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(object.properties.size(), 2u);
    EXPECT_EQ(static_cast<const Identifier&>(
                  *static_cast<const Property&>(*object.properties[0]).key)
                  .name,
              "default");
}

TEST(ParserTest, ObjectLiteralSpread) {
    ParseResult result = parse("({...base, a: 1});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(object.properties.size(), 2u);
    EXPECT_EQ(object.properties[0]->kind(), NodeKind::SpreadElement);
    EXPECT_EQ(object.properties[1]->kind(), NodeKind::Property);
}

TEST(ParserTest, ObjectLiteralTrailingComma) {
    ParseResult result = parse("({a: 1,});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(object.properties.size(), 1u);
}

TEST(ParserTest, NestedObjectLiteral) {
    ParseResult result = parse("({a: {b: 1}});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_EQ(property.value->kind(), NodeKind::ObjectExpression);
}

TEST(ParserTest, ObjectLiteralInCallAndMember) {
    ParseResult result = parse("f({a: 1}).a;");

    ASSERT_TRUE(result.ok());
    const auto& member = static_cast<const MemberExpression&>(
        *statementOf(result, 0).expression);
    const auto& call =
        static_cast<const CallExpression&>(*member.object);
    ASSERT_EQ(call.arguments.size(), 1u);
    EXPECT_EQ(call.arguments[0]->kind(), NodeKind::ObjectExpression);
}

TEST(ParserTest, ObjectLiteralMissingColonReportsError) {
    ParseResult result = parse("({a 1});");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ObjectLiteralReservedWordShorthandReportsError) {
    ParseResult result = parse("({if});");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, BraceAtStatementStartIsBlock) {
    ParseResult result = parse("{a: 1}");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(programOf(result).body.size(), 1u);
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::BlockStatement);
}

// ---------------------------------------------------------------------------
// 解构模式
// ---------------------------------------------------------------------------

namespace {

const VariableDeclarator& declaratorOf(const ParseResult& result) {
    const auto& declaration = static_cast<const VariableDeclaration&>(
        *programOf(result).body[0]);
    return static_cast<const VariableDeclarator&>(*declaration.declarations[0]);
}

}  // namespace

TEST(ParserTest, ArrayBindingPattern) {
    ParseResult result = parse("var [a, b] = arr;");

    ASSERT_TRUE(result.ok());
    const auto& declarator = declaratorOf(result);
    ASSERT_EQ(declarator.id->kind(), NodeKind::ArrayPattern);
    const auto& pattern = static_cast<const ArrayPattern&>(*declarator.id);
    ASSERT_EQ(pattern.elements.size(), 2u);
    EXPECT_EQ(static_cast<const Identifier&>(*pattern.elements[0]).name, "a");
    EXPECT_EQ(static_cast<const Identifier&>(*pattern.elements[1]).name, "b");
}

TEST(ParserTest, ArrayBindingPatternElision) {
    ParseResult result = parse("var [, a] = arr;");

    ASSERT_TRUE(result.ok());
    const auto& pattern = static_cast<const ArrayPattern&>(*declaratorOf(result).id);
    ASSERT_EQ(pattern.elements.size(), 2u);
    EXPECT_EQ(pattern.elements[0], nullptr);
    EXPECT_NE(pattern.elements[1], nullptr);
}

TEST(ParserTest, ArrayBindingPatternRest) {
    ParseResult result = parse("var [a, ...rest] = arr;");

    ASSERT_TRUE(result.ok());
    const auto& pattern = static_cast<const ArrayPattern&>(*declaratorOf(result).id);
    ASSERT_EQ(pattern.elements.size(), 2u);
    EXPECT_EQ(pattern.elements[1]->kind(), NodeKind::RestElement);
}

TEST(ParserTest, ObjectBindingPattern) {
    ParseResult result = parse("var {a, b: c} = obj;");

    ASSERT_TRUE(result.ok());
    const auto& pattern = static_cast<const ObjectPattern&>(*declaratorOf(result).id);
    ASSERT_EQ(pattern.properties.size(), 2u);

    const auto& shorthand = static_cast<const Property&>(*pattern.properties[0]);
    EXPECT_TRUE(shorthand.shorthand);
    EXPECT_EQ(static_cast<const Identifier&>(*shorthand.value).name, "a");

    const auto& renamed = static_cast<const Property&>(*pattern.properties[1]);
    EXPECT_FALSE(renamed.shorthand);
    EXPECT_EQ(static_cast<const Identifier&>(*renamed.key).name, "b");
    EXPECT_EQ(static_cast<const Identifier&>(*renamed.value).name, "c");
}

TEST(ParserTest, ObjectBindingPatternDefault) {
    ParseResult result = parse("var {a = 1} = obj;");

    ASSERT_TRUE(result.ok());
    const auto& pattern = static_cast<const ObjectPattern&>(*declaratorOf(result).id);
    ASSERT_EQ(pattern.properties.size(), 1u);
    const auto& property = static_cast<const Property&>(*pattern.properties[0]);
    ASSERT_EQ(property.value->kind(), NodeKind::AssignmentPattern);
    const auto& assignment =
        static_cast<const AssignmentPattern&>(*property.value);
    EXPECT_EQ(assignment.left->kind(), NodeKind::Identifier);
    EXPECT_EQ(assignment.right->kind(), NodeKind::NumericLiteral);
}

TEST(ParserTest, ObjectBindingPatternRenamedDefault) {
    ParseResult result = parse("var {a: b = 1} = obj;");

    ASSERT_TRUE(result.ok());
    const auto& pattern = static_cast<const ObjectPattern&>(*declaratorOf(result).id);
    const auto& property = static_cast<const Property&>(*pattern.properties[0]);
    ASSERT_EQ(property.value->kind(), NodeKind::AssignmentPattern);
}

TEST(ParserTest, ObjectBindingPatternRest) {
    ParseResult result = parse("var {a, ...rest} = obj;");

    ASSERT_TRUE(result.ok());
    const auto& pattern = static_cast<const ObjectPattern&>(*declaratorOf(result).id);
    ASSERT_EQ(pattern.properties.size(), 2u);
    EXPECT_EQ(pattern.properties[1]->kind(), NodeKind::RestElement);
}

TEST(ParserTest, NestedBindingPattern) {
    ParseResult result = parse("var {a: [b, {c}]} = obj;");

    ASSERT_TRUE(result.ok());
    const auto& pattern = static_cast<const ObjectPattern&>(*declaratorOf(result).id);
    const auto& property = static_cast<const Property&>(*pattern.properties[0]);
    ASSERT_EQ(property.value->kind(), NodeKind::ArrayPattern);
    const auto& array = static_cast<const ArrayPattern&>(*property.value);
    ASSERT_EQ(array.elements.size(), 2u);
    EXPECT_EQ(array.elements[1]->kind(), NodeKind::ObjectPattern);
}

TEST(ParserTest, FunctionParametersCanBePatterns) {
    ParseResult result = parse("function f({a}, [b]) {}");

    ASSERT_TRUE(result.ok());
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    ASSERT_EQ(function.params.size(), 2u);
    EXPECT_EQ(function.params[0]->kind(), NodeKind::ObjectPattern);
    EXPECT_EQ(function.params[1]->kind(), NodeKind::ArrayPattern);
}

TEST(ParserTest, ArrowParametersCanBePatterns) {
    ParseResult result = parse("({a}, [b]) => a;");

    ASSERT_TRUE(result.ok());
    const auto& arrow = static_cast<const ArrowFunctionExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(arrow.params.size(), 2u);
    EXPECT_EQ(arrow.params[0]->kind(), NodeKind::ObjectPattern);
    EXPECT_EQ(arrow.params[1]->kind(), NodeKind::ArrayPattern);
}

TEST(ParserTest, CatchParameterCanBePattern) {
    ParseResult result = parse("try {} catch ({message}) {}");

    ASSERT_TRUE(result.ok());
    const auto& statement = static_cast<const TryStatement&>(
        *programOf(result).body[0]);
    ASSERT_NE(statement.handler, nullptr);
    const auto& clause = static_cast<const CatchClause&>(*statement.handler);
    ASSERT_NE(clause.param, nullptr);
    EXPECT_EQ(clause.param->kind(), NodeKind::ObjectPattern);
}

TEST(ParserTest, ForOfVariableDeclarationPattern) {
    ParseResult result = parse("for (const [k, v] of entries) {}");

    ASSERT_TRUE(result.ok());
    const auto& statement = static_cast<const ForOfStatement&>(
        *programOf(result).body[0]);
    const auto& declaration =
        static_cast<const VariableDeclaration&>(*statement.left);
    const auto& declarator =
        static_cast<const VariableDeclarator&>(*declaration.declarations[0]);
    EXPECT_EQ(declarator.id->kind(), NodeKind::ArrayPattern);
}

TEST(ParserTest, ForOfConstWithoutInitializer) {
    ParseResult result = parse("for (const x of entries) {}");

    ASSERT_TRUE(result.ok());
    const auto& statement = static_cast<const ForOfStatement&>(
        *programOf(result).body[0]);
    EXPECT_EQ(statement.left->kind(), NodeKind::VariableDeclaration);
}

TEST(ParserTest, ForInConstWithoutInitializer) {
    ParseResult result = parse("for (const x in obj) {}");

    ASSERT_TRUE(result.ok());
}

TEST(ParserTest, ForStatementConstRequiresInitializer) {
    ParseResult result = parse("for (const x;;) {}");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ForOfExpressionPattern) {
    ParseResult result = parse("for ([a, b] of entries) {}");

    ASSERT_TRUE(result.ok());
    const auto& statement = static_cast<const ForOfStatement&>(
        *programOf(result).body[0]);
    EXPECT_EQ(statement.left->kind(), NodeKind::ArrayPattern);
}

TEST(ParserTest, ArrayDestructuringAssignment) {
    ParseResult result = parse("[a, b] = arr;");

    ASSERT_TRUE(result.ok());
    const auto& assignment = static_cast<const AssignmentExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_EQ(assignment.left->kind(), NodeKind::ArrayPattern);
}

TEST(ParserTest, ObjectDestructuringAssignment) {
    ParseResult result = parse("({a, b: c} = obj);");

    ASSERT_TRUE(result.ok());
    const auto& assignment = static_cast<const AssignmentExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_EQ(assignment.left->kind(), NodeKind::ObjectPattern);
}

TEST(ParserTest, DestructuringAssignmentDefault) {
    ParseResult result = parse("[a = 1] = arr;");

    ASSERT_TRUE(result.ok());
    const auto& assignment = static_cast<const AssignmentExpression&>(
        *statementOf(result, 0).expression);
    const auto& pattern = static_cast<const ArrayPattern&>(*assignment.left);
    ASSERT_EQ(pattern.elements.size(), 1u);
    EXPECT_EQ(pattern.elements[0]->kind(), NodeKind::AssignmentPattern);
}

TEST(ParserTest, ObjectDestructuringAssignmentDefault) {
    ParseResult result = parse("({a = 1} = obj);");

    ASSERT_TRUE(result.ok());
    const auto& assignment = static_cast<const AssignmentExpression&>(
        *statementOf(result, 0).expression);
    const auto& pattern = static_cast<const ObjectPattern&>(*assignment.left);
    ASSERT_EQ(pattern.properties.size(), 1u);
    const auto& property = static_cast<const Property&>(*pattern.properties[0]);
    EXPECT_EQ(property.value->kind(), NodeKind::AssignmentPattern);
}

TEST(ParserTest, DestructuringAssignmentRest) {
    ParseResult result = parse("[a, ...rest] = arr;");

    ASSERT_TRUE(result.ok());
    const auto& assignment = static_cast<const AssignmentExpression&>(
        *statementOf(result, 0).expression);
    const auto& pattern = static_cast<const ArrayPattern&>(*assignment.left);
    ASSERT_EQ(pattern.elements.size(), 2u);
    EXPECT_EQ(pattern.elements[1]->kind(), NodeKind::RestElement);
}

TEST(ParserTest, DestructuringAssignmentMemberTarget) {
    ParseResult result = parse("[a.b] = arr;");

    ASSERT_TRUE(result.ok());
    const auto& assignment = static_cast<const AssignmentExpression&>(
        *statementOf(result, 0).expression);
    const auto& pattern = static_cast<const ArrayPattern&>(*assignment.left);
    ASSERT_EQ(pattern.elements.size(), 1u);
    EXPECT_EQ(pattern.elements[0]->kind(), NodeKind::MemberExpression);
}

TEST(ParserTest, InvalidDestructuringAssignmentTargetReportsError) {
    ParseResult result = parse("[a + b] = arr;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ArrayRestMustBeLastInAssignment) {
    ParseResult result = parse("[...a, b] = arr;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ObjectRestMustBeLastInAssignment) {
    ParseResult result = parse("({...a, b} = obj);");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ObjectPatternRestMustBeIdentifier) {
    ParseResult result = parse("var {...{a}} = obj;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ObjectLiteralShorthandDefaultIsCoverGrammar) {
    // `{a = 1}` 只在模式中合法；表达式位置先按 cover grammar 接受，
    // 早期错误留给 Sema。
    ParseResult result = parse("({a = 1});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.shorthand);
    EXPECT_EQ(property.value->kind(), NodeKind::AssignmentExpression);
}

// ---------------------------------------------------------------------------
// this 与函数表达式
// ---------------------------------------------------------------------------

TEST(ParserTest, ThisExpression) {
    ParseResult result = parse("this;");

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(statementOf(result, 0).expression->kind(),
              NodeKind::ThisExpression);
}

TEST(ParserTest, ThisInMemberExpression) {
    ParseResult result = parse("this.x;");

    ASSERT_TRUE(result.ok());
    const auto& member = static_cast<const MemberExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_NE(member.object, nullptr);
    EXPECT_EQ(member.object->kind(), NodeKind::ThisExpression);
}

TEST(ParserTest, AnonymousFunctionExpression) {
    ParseResult result = parse("(function () {});");

    ASSERT_TRUE(result.ok());
    const auto& expression = statementOf(result, 0).expression;
    ASSERT_EQ(expression->kind(), NodeKind::FunctionExpression);
    const auto& function =
        static_cast<const FunctionExpression&>(*expression);
    EXPECT_EQ(function.id, nullptr);
    EXPECT_FALSE(function.async);
    EXPECT_FALSE(function.generator);
    ASSERT_NE(function.body, nullptr);
    EXPECT_EQ(function.body->kind(), NodeKind::BlockStatement);
}

TEST(ParserTest, NamedFunctionExpression) {
    ParseResult result = parse("(function f(a) { return a; });");

    ASSERT_TRUE(result.ok());
    const auto& function = static_cast<const FunctionExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_NE(function.id, nullptr);
    EXPECT_EQ(static_cast<const Identifier&>(*function.id).name, "f");
    ASSERT_EQ(function.params.size(), 1u);
}

TEST(ParserTest, AsyncGeneratorFunctionExpression) {
    ParseResult result = parse("(async function* gen() {});");

    ASSERT_TRUE(result.ok());
    const auto& function = static_cast<const FunctionExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_TRUE(function.async);
    EXPECT_TRUE(function.generator);
    ASSERT_NE(function.id, nullptr);
}

TEST(ParserTest, FunctionExpressionAsCallArgument) {
    ParseResult result = parse("f(function () {});");

    ASSERT_TRUE(result.ok());
    const auto& call = static_cast<const CallExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(call.arguments.size(), 1u);
    EXPECT_EQ(call.arguments[0]->kind(), NodeKind::FunctionExpression);
}

// ---------------------------------------------------------------------------
// 对象方法 / 访问器
// ---------------------------------------------------------------------------

TEST(ParserTest, ObjectMethodShorthand) {
    ParseResult result = parse("({f(a) { return a; }});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.method);
    EXPECT_EQ(property.propertyKind, PropertyKind::Init);
    ASSERT_NE(property.value, nullptr);
    ASSERT_EQ(property.value->kind(), NodeKind::FunctionExpression);
    const auto& function =
        static_cast<const FunctionExpression&>(*property.value);
    ASSERT_EQ(function.params.size(), 1u);
}

TEST(ParserTest, ObjectGeneratorMethod) {
    ParseResult result = parse("({*g() {}});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.method);
    const auto& function =
        static_cast<const FunctionExpression&>(*property.value);
    EXPECT_TRUE(function.generator);
    EXPECT_FALSE(function.async);
}

TEST(ParserTest, ObjectAsyncMethod) {
    ParseResult result = parse("({async f() {}});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.method);
    const auto& function =
        static_cast<const FunctionExpression&>(*property.value);
    EXPECT_TRUE(function.async);
    EXPECT_FALSE(function.generator);
}

TEST(ParserTest, ObjectAsyncGeneratorMethod) {
    ParseResult result = parse("({async *g() {}});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& function = static_cast<const FunctionExpression&>(
        *static_cast<const Property&>(*object.properties[0]).value);
    EXPECT_TRUE(function.async);
    EXPECT_TRUE(function.generator);
}

TEST(ParserTest, ObjectComputedMethod) {
    ParseResult result = parse("({[k]() {}});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.method);
    EXPECT_TRUE(property.computed);
}

TEST(ParserTest, ObjectGetter) {
    ParseResult result = parse("({get a() { return 1; }});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.method);
    EXPECT_EQ(property.propertyKind, PropertyKind::Get);
    EXPECT_EQ(static_cast<const Identifier&>(*property.key).name, "a");
}

TEST(ParserTest, ObjectSetter) {
    ParseResult result = parse("({set a(v) {}});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.method);
    EXPECT_EQ(property.propertyKind, PropertyKind::Set);
    const auto& function =
        static_cast<const FunctionExpression&>(*property.value);
    ASSERT_EQ(function.params.size(), 1u);
}

TEST(ParserTest, MethodNamedGet) {
    ParseResult result = parse("({get() {}});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.method);
    EXPECT_EQ(property.propertyKind, PropertyKind::Init);
    EXPECT_EQ(static_cast<const Identifier&>(*property.key).name, "get");
}

TEST(ParserTest, PropertyNamedGet) {
    ParseResult result = parse("({get: 1});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_FALSE(property.method);
    EXPECT_EQ(property.value->kind(), NodeKind::NumericLiteral);
}

TEST(ParserTest, ShorthandNamedGet) {
    ParseResult result = parse("({get});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.shorthand);
}

TEST(ParserTest, PropertyNamedAsync) {
    ParseResult result = parse("({async: 1});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_FALSE(property.method);
}

TEST(ParserTest, MethodNamedAsync) {
    ParseResult result = parse("({async() {}});");

    ASSERT_TRUE(result.ok());
    const auto& object = static_cast<const ObjectExpression&>(
        *statementOf(result, 0).expression);
    const auto& property = static_cast<const Property&>(*object.properties[0]);
    EXPECT_TRUE(property.method);
    const auto& function =
        static_cast<const FunctionExpression&>(*property.value);
    EXPECT_FALSE(function.async);
}

TEST(ParserTest, MethodInDestructuringAssignmentReportsError) {
    ParseResult result = parse("({f() {}} = obj);");

    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// class
// ---------------------------------------------------------------------------

namespace {

const ClassBody& classBodyOf(const ParseResult& result, size_t index = 0) {
    const auto& declaration = static_cast<const ClassDeclaration&>(
        *programOf(result).body[index]);
    return static_cast<const ClassBody&>(*declaration.body);
}

}  // namespace

TEST(ParserTest, EmptyClassDeclaration) {
    ParseResult result = parse("class A {}");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ClassDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_NE(declaration.id, nullptr);
    EXPECT_EQ(static_cast<const Identifier&>(*declaration.id).name, "A");
    EXPECT_EQ(declaration.superClass, nullptr);
    ASSERT_NE(declaration.body, nullptr);
    EXPECT_EQ(declaration.body->kind(), NodeKind::ClassBody);
    EXPECT_TRUE(classBodyOf(result).elements.empty());
}

TEST(ParserTest, ClassExtends) {
    ParseResult result = parse("class A extends B.C {}");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ClassDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_NE(declaration.superClass, nullptr);
    EXPECT_EQ(declaration.superClass->kind(), NodeKind::MemberExpression);
}

TEST(ParserTest, AnonymousClassExpression) {
    ParseResult result = parse("(class {});");

    ASSERT_TRUE(result.ok());
    const auto& expression = statementOf(result, 0).expression;
    ASSERT_EQ(expression->kind(), NodeKind::ClassExpression);
    const auto& classExpression =
        static_cast<const ClassExpression&>(*expression);
    EXPECT_EQ(classExpression.id, nullptr);
}

TEST(ParserTest, NamedClassExpression) {
    ParseResult result = parse("(class A {});");

    ASSERT_TRUE(result.ok());
    const auto& classExpression = static_cast<const ClassExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_NE(classExpression.id, nullptr);
    EXPECT_EQ(static_cast<const Identifier&>(*classExpression.id).name, "A");
}

TEST(ParserTest, ClassConstructor) {
    ParseResult result = parse("class A { constructor(x) { this.x = x; } }");

    ASSERT_TRUE(result.ok());
    const auto& body = classBodyOf(result);
    ASSERT_EQ(body.elements.size(), 1u);
    ASSERT_EQ(body.elements[0]->kind(), NodeKind::MethodDefinition);
    const auto& method = static_cast<const MethodDefinition&>(*body.elements[0]);
    EXPECT_EQ(method.methodKind, MethodKind::Constructor);
    EXPECT_FALSE(method.isStatic);
    const auto& function =
        static_cast<const FunctionExpression&>(*method.value);
    ASSERT_EQ(function.params.size(), 1u);
}

TEST(ParserTest, ClassMethods) {
    ParseResult result = parse(
        "class A { m() {} static s() {} get g() {} set s2(v) {} *gen() {} "
        "async a() {} async *ag() {} [k]() {} }");

    ASSERT_TRUE(result.ok());
    const auto& body = classBodyOf(result);
    ASSERT_EQ(body.elements.size(), 8u);

    EXPECT_EQ(static_cast<const MethodDefinition&>(*body.elements[0]).methodKind,
              MethodKind::Method);
    EXPECT_TRUE(static_cast<const MethodDefinition&>(*body.elements[1]).isStatic);
    EXPECT_EQ(static_cast<const MethodDefinition&>(*body.elements[2]).methodKind,
              MethodKind::Get);
    EXPECT_EQ(static_cast<const MethodDefinition&>(*body.elements[3]).methodKind,
              MethodKind::Set);
    EXPECT_TRUE(static_cast<const FunctionExpression&>(
                    *static_cast<const MethodDefinition&>(*body.elements[4])
                         .value)
                    .generator);
    EXPECT_TRUE(static_cast<const FunctionExpression&>(
                    *static_cast<const MethodDefinition&>(*body.elements[5])
                         .value)
                    .async);
    EXPECT_TRUE(static_cast<const FunctionExpression&>(
                    *static_cast<const MethodDefinition&>(*body.elements[6])
                         .value)
                    .generator);
    EXPECT_TRUE(
        static_cast<const MethodDefinition&>(*body.elements[7]).computed);
}

TEST(ParserTest, ClassFields) {
    ParseResult result = parse("class A { x; y = 1; static z = 2; }");

    ASSERT_TRUE(result.ok());
    const auto& body = classBodyOf(result);
    ASSERT_EQ(body.elements.size(), 3u);

    const auto& first = static_cast<const PropertyDefinition&>(*body.elements[0]);
    EXPECT_EQ(first.value, nullptr);
    EXPECT_FALSE(first.isStatic);

    const auto& second =
        static_cast<const PropertyDefinition&>(*body.elements[1]);
    ASSERT_NE(second.value, nullptr);
    EXPECT_EQ(second.value->kind(), NodeKind::NumericLiteral);

    const auto& third = static_cast<const PropertyDefinition&>(*body.elements[2]);
    EXPECT_TRUE(third.isStatic);
}

TEST(ParserTest, ClassPrivateMembers) {
    ParseResult result = parse("class A { #x = 1; #m() {} }");

    ASSERT_TRUE(result.ok());
    const auto& body = classBodyOf(result);
    ASSERT_EQ(body.elements.size(), 2u);

    const auto& field = static_cast<const PropertyDefinition&>(*body.elements[0]);
    ASSERT_EQ(field.key->kind(), NodeKind::PrivateIdentifier);
    EXPECT_EQ(static_cast<const PrivateIdentifier&>(*field.key).name, "x");

    const auto& method = static_cast<const MethodDefinition&>(*body.elements[1]);
    ASSERT_EQ(method.key->kind(), NodeKind::PrivateIdentifier);
    EXPECT_EQ(static_cast<const PrivateIdentifier&>(*method.key).name, "m");
}

TEST(ParserTest, EmptyStaticBlock) {
    ParseResult result = parse("class A { static {} }");

    ASSERT_TRUE(result.ok());
    const auto& body = classBodyOf(result);
    ASSERT_EQ(body.elements.size(), 1u);
    ASSERT_EQ(body.elements[0]->kind(), NodeKind::StaticBlock);
    const auto& block = static_cast<const StaticBlock&>(*body.elements[0]);
    EXPECT_TRUE(block.body.empty());
}

TEST(ParserTest, StaticBlockWithStatements) {
    ParseResult result = parse("class A { static { this.x = 1; } }");

    ASSERT_TRUE(result.ok());
    const auto& body = classBodyOf(result);
    const auto& block = static_cast<const StaticBlock&>(*body.elements[0]);
    ASSERT_EQ(block.body.size(), 1u);
    EXPECT_EQ(block.body[0]->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, StaticBlockDoesNotConsumeFollowingElement) {
    ParseResult result = parse("class A { static {} x; }");

    ASSERT_TRUE(result.ok());
    const auto& body = classBodyOf(result);
    ASSERT_EQ(body.elements.size(), 2u);
    EXPECT_EQ(body.elements[0]->kind(), NodeKind::StaticBlock);
    EXPECT_EQ(body.elements[1]->kind(), NodeKind::PropertyDefinition);
}

TEST(ParserTest, ClassMethodNamedStatic) {
    ParseResult result = parse("class A { static() {} static = 1; }");

    ASSERT_TRUE(result.ok());
    const auto& body = classBodyOf(result);
    ASSERT_EQ(body.elements.size(), 2u);

    const auto& method = static_cast<const MethodDefinition&>(*body.elements[0]);
    EXPECT_FALSE(method.isStatic);
    EXPECT_EQ(static_cast<const Identifier&>(*method.key).name, "static");

    const auto& field = static_cast<const PropertyDefinition&>(*body.elements[1]);
    EXPECT_FALSE(field.isStatic);
    EXPECT_EQ(static_cast<const Identifier&>(*field.key).name, "static");
}

TEST(ParserTest, ClassBodyLegacyOctalIsRecordedForSema) {
    ParseResult result = parse("class A { m() { 010; } }");

    // class 体恒 strict 由 Sema 判定；Parser 只记录 legacy 形态。
    ASSERT_TRUE(result.ok());
    const auto& body = classBodyOf(result);
    const auto& method = static_cast<const MethodDefinition&>(*body.elements[0]);
    const auto& function =
        static_cast<const FunctionExpression&>(*method.value);
    const auto& block = static_cast<const BlockStatement&>(*function.body);
    const auto& statement =
        static_cast<const ExpressionStatement&>(*block.body[0]);
    const auto& literal =
        static_cast<const NumericLiteral&>(*statement.expression);
    EXPECT_TRUE(literal.legacyLeadingZero);
}

TEST(ParserTest, ExportClassDeclaration) {
    ParseResult result = parse("export class A {}");

    ASSERT_TRUE(result.ok());
    const auto& exportNode = static_cast<const ExportNamedDeclaration&>(
        *programOf(result).body[0]);
    EXPECT_EQ(exportNode.declaration->kind(), NodeKind::ClassDeclaration);
}

TEST(ParserTest, ExportDefaultAnonymousClass) {
    ParseResult result = parse("export default class {}");

    ASSERT_TRUE(result.ok());
    const auto& exportNode = static_cast<const ExportDefaultDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_EQ(exportNode.declaration->kind(), NodeKind::ClassDeclaration);
    const auto& declaration =
        static_cast<const ClassDeclaration&>(*exportNode.declaration);
    EXPECT_EQ(declaration.id, nullptr);
}

TEST(ParserTest, SuperCallAndMember) {
    ParseResult result = parse("class A extends B { constructor() { super(); } }");

    ASSERT_TRUE(result.ok());
    const auto& body = classBodyOf(result);
    const auto& method = static_cast<const MethodDefinition&>(*body.elements[0]);
    const auto& function =
        static_cast<const FunctionExpression&>(*method.value);
    const auto& block = static_cast<const BlockStatement&>(*function.body);
    const auto& statement =
        static_cast<const ExpressionStatement&>(*block.body[0]);
    ASSERT_EQ(statement.expression->kind(), NodeKind::CallExpression);
    const auto& call =
        static_cast<const CallExpression&>(*statement.expression);
    ASSERT_NE(call.callee, nullptr);
    EXPECT_EQ(call.callee->kind(), NodeKind::SuperExpression);
}

// ---------------------------------------------------------------------------
// 模板字面量
// ---------------------------------------------------------------------------

TEST(ParserTest, NoSubstitutionTemplateLiteral) {
    ParseResult result = parse("`abc`;");

    ASSERT_TRUE(result.ok());
    const auto& literal = static_cast<const TemplateLiteral&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(literal.quasis.size(), 1u);
    EXPECT_TRUE(literal.expressions.empty());
    const auto& element =
        static_cast<const TemplateElement&>(*literal.quasis[0]);
    EXPECT_EQ(element.cooked, "abc");
}

TEST(ParserTest, TemplateLiteralWithSubstitution) {
    ParseResult result = parse("`a${b}c`;");

    ASSERT_TRUE(result.ok());
    const auto& literal = static_cast<const TemplateLiteral&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(literal.quasis.size(), 2u);
    ASSERT_EQ(literal.expressions.size(), 1u);
    EXPECT_EQ(static_cast<const TemplateElement&>(*literal.quasis[0]).cooked,
              "a");
    EXPECT_EQ(static_cast<const TemplateElement&>(*literal.quasis[1]).cooked,
              "c");
    EXPECT_EQ(literal.expressions[0]->kind(), NodeKind::Identifier);
}

TEST(ParserTest, TemplateLiteralWithMultipleSubstitutions) {
    ParseResult result = parse("`a${b}c${d}e`;");

    ASSERT_TRUE(result.ok());
    const auto& literal = static_cast<const TemplateLiteral&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(literal.quasis.size(), 3u);
    ASSERT_EQ(literal.expressions.size(), 2u);
    EXPECT_EQ(static_cast<const TemplateElement&>(*literal.quasis[1]).cooked,
              "c");
    EXPECT_EQ(static_cast<const TemplateElement&>(*literal.quasis[2]).cooked,
              "e");
}

TEST(ParserTest, NestedTemplateLiteral) {
    ParseResult result = parse("`a${`b${c}d`}e`;");

    ASSERT_TRUE(result.ok());
    const auto& outer = static_cast<const TemplateLiteral&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(outer.expressions.size(), 1u);
    ASSERT_EQ(outer.expressions[0]->kind(), NodeKind::TemplateLiteral);
    const auto& inner =
        static_cast<const TemplateLiteral&>(*outer.expressions[0]);
    ASSERT_EQ(inner.expressions.size(), 1u);
}

TEST(ParserTest, TemplateSubstitutionAllowsObjectLiteral) {
    ParseResult result = parse("`${{a: 1}.a}`;");

    ASSERT_TRUE(result.ok());
    const auto& literal = static_cast<const TemplateLiteral&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(literal.expressions.size(), 1u);
    EXPECT_EQ(literal.expressions[0]->kind(), NodeKind::MemberExpression);
}

TEST(ParserTest, TemplateLiteralInCall) {
    ParseResult result = parse("f(`a${b}`);");

    ASSERT_TRUE(result.ok());
    const auto& call = static_cast<const CallExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(call.arguments.size(), 1u);
    EXPECT_EQ(call.arguments[0]->kind(), NodeKind::TemplateLiteral);
}

TEST(ParserTest, UnterminatedTemplateReportsError) {
    ParseResult result = parse("`abc");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, TaggedTemplateExpression) {
    ParseResult result = parse("tag`abc`;");

    ASSERT_TRUE(result.ok());
    const auto& expression = statementOf(result, 0).expression;
    ASSERT_EQ(expression->kind(), NodeKind::TaggedTemplateExpression);
    const auto& tagged =
        static_cast<const TaggedTemplateExpression&>(*expression);
    EXPECT_EQ(tagged.tag->kind(), NodeKind::Identifier);
    ASSERT_NE(tagged.quasi, nullptr);
    EXPECT_EQ(tagged.quasi->kind(), NodeKind::TemplateLiteral);
}

TEST(ParserTest, TaggedTemplateWithMemberTag) {
    ParseResult result = parse("obj.tag`a${b}`;");

    ASSERT_TRUE(result.ok());
    const auto& tagged = static_cast<const TaggedTemplateExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_EQ(tagged.tag->kind(), NodeKind::MemberExpression);
    const auto& quasi =
        static_cast<const TemplateLiteral&>(*tagged.quasi);
    ASSERT_EQ(quasi.expressions.size(), 1u);
}

TEST(ParserTest, TaggedTemplateFollowedByMember) {
    ParseResult result = parse("tag`abc`.x;");

    ASSERT_TRUE(result.ok());
    const auto& member = static_cast<const MemberExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_EQ(member.object->kind(), NodeKind::TaggedTemplateExpression);
}

TEST(ParserTest, TaggedTemplateFollowedByCall) {
    ParseResult result = parse("tag`abc`(x);");

    ASSERT_TRUE(result.ok());
    const auto& call = static_cast<const CallExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_EQ(call.callee->kind(), NodeKind::TaggedTemplateExpression);
}

// ---------------------------------------------------------------------------
// 可选链
// ---------------------------------------------------------------------------

TEST(ParserTest, OptionalMemberExpression) {
    ParseResult result = parse("a?.b;");

    ASSERT_TRUE(result.ok());
    const auto& chain = static_cast<const ChainExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_NE(chain.expression, nullptr);
    ASSERT_EQ(chain.expression->kind(), NodeKind::MemberExpression);
    const auto& member =
        static_cast<const MemberExpression&>(*chain.expression);
    EXPECT_TRUE(member.optional);
    EXPECT_FALSE(member.computed);
}

TEST(ParserTest, OptionalComputedMemberExpression) {
    ParseResult result = parse("a?.[k];");

    ASSERT_TRUE(result.ok());
    const auto& chain = static_cast<const ChainExpression&>(
        *statementOf(result, 0).expression);
    const auto& member =
        static_cast<const MemberExpression&>(*chain.expression);
    EXPECT_TRUE(member.optional);
    EXPECT_TRUE(member.computed);
}

TEST(ParserTest, OptionalCallExpression) {
    ParseResult result = parse("f?.();");

    ASSERT_TRUE(result.ok());
    const auto& chain = static_cast<const ChainExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(chain.expression->kind(), NodeKind::CallExpression);
    const auto& call = static_cast<const CallExpression&>(*chain.expression);
    EXPECT_TRUE(call.optional);
}

TEST(ParserTest, OptionalChainCoversFollowingSegments) {
    ParseResult result = parse("a?.b.c;");

    ASSERT_TRUE(result.ok());
    const auto& chain = static_cast<const ChainExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(chain.expression->kind(), NodeKind::MemberExpression);
    const auto& outer =
        static_cast<const MemberExpression&>(*chain.expression);
    EXPECT_FALSE(outer.optional);
    ASSERT_EQ(outer.object->kind(), NodeKind::MemberExpression);
    EXPECT_TRUE(static_cast<const MemberExpression&>(*outer.object).optional);
}

TEST(ParserTest, OptionalCallAfterOptionalMember) {
    ParseResult result = parse("a?.b?.();");

    ASSERT_TRUE(result.ok());
    const auto& chain = static_cast<const ChainExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(chain.expression->kind(), NodeKind::CallExpression);
    const auto& call = static_cast<const CallExpression&>(*chain.expression);
    EXPECT_TRUE(call.optional);
    ASSERT_EQ(call.callee->kind(), NodeKind::MemberExpression);
    EXPECT_TRUE(static_cast<const MemberExpression&>(*call.callee).optional);
}

TEST(ParserTest, NormalCallAfterOptionalMember) {
    ParseResult result = parse("a?.b(c);");

    ASSERT_TRUE(result.ok());
    const auto& chain = static_cast<const ChainExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(chain.expression->kind(), NodeKind::CallExpression);
    const auto& call = static_cast<const CallExpression&>(*chain.expression);
    EXPECT_FALSE(call.optional);
    EXPECT_TRUE(static_cast<const MemberExpression&>(*call.callee).optional);
}

TEST(ParserTest, OptionalChainIsNotAssignmentTarget) {
    ParseResult result = parse("a?.b = c;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, OptionalChainInNewCalleeReportsError) {
    ParseResult result = parse("new a?.b();");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, OptionalChainAfterNewExpression) {
    ParseResult result = parse("new a()?.b;");

    ASSERT_TRUE(result.ok());
    const auto& chain = static_cast<const ChainExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(chain.expression->kind(), NodeKind::MemberExpression);
    EXPECT_EQ(static_cast<const MemberExpression&>(*chain.expression)
                  .object->kind(),
              NodeKind::NewExpression);
}

// ---------------------------------------------------------------------------
// MetaProperty
// ---------------------------------------------------------------------------

TEST(ParserTest, NewTarget) {
    ParseResult result = parse("new.target;");

    ASSERT_TRUE(result.ok());
    const auto& expression = statementOf(result, 0).expression;
    ASSERT_EQ(expression->kind(), NodeKind::MetaProperty);
    const auto& meta = static_cast<const MetaProperty&>(*expression);
    EXPECT_EQ(static_cast<const Identifier&>(*meta.meta).name, "new");
    EXPECT_EQ(static_cast<const Identifier&>(*meta.property).name, "target");
}

TEST(ParserTest, ImportMeta) {
    ParseResult result = parse("import.meta;");

    ASSERT_TRUE(result.ok());
    const auto& meta = static_cast<const MetaProperty&>(
        *statementOf(result, 0).expression);
    EXPECT_EQ(static_cast<const Identifier&>(*meta.meta).name, "import");
    EXPECT_EQ(static_cast<const Identifier&>(*meta.property).name, "meta");
}

TEST(ParserTest, NewTargetMember) {
    ParseResult result = parse("new.target.x;");

    ASSERT_TRUE(result.ok());
    const auto& member = static_cast<const MemberExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_EQ(member.object->kind(), NodeKind::MetaProperty);
}

TEST(ParserTest, InvalidNewPropertyReportsError) {
    ParseResult result = parse("new.foo;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, InvalidImportPropertyReportsError) {
    ParseResult result = parse("import.foo;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ImportExpression) {
    ParseResult result = parse("import(\"mod\");");

    ASSERT_TRUE(result.ok());
    const auto& expression = statementOf(result, 0).expression;
    ASSERT_EQ(expression->kind(), NodeKind::ImportExpression);
    const auto& import = static_cast<const ImportExpression&>(*expression);
    ASSERT_NE(import.source, nullptr);
    EXPECT_EQ(import.source->kind(), NodeKind::StringLiteral);
}

TEST(ParserTest, ImportExpressionWithoutOptions) {
    ParseResult result = parse("import(\"mod\");");

    ASSERT_TRUE(result.ok());
    const auto& import = static_cast<const ImportExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_EQ(import.options, nullptr);
}

TEST(ParserTest, ImportExpressionWithOptions) {
    ParseResult result = parse("import(\"mod\", { with: { type: \"json\" } });");

    ASSERT_TRUE(result.ok());
    const auto& import = static_cast<const ImportExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_NE(import.options, nullptr);
    EXPECT_EQ(import.options->kind(), NodeKind::ObjectExpression);
}

TEST(ParserTest, ImportExpressionWithBinarySource) {
    ParseResult result = parse("import(a + b);");

    ASSERT_TRUE(result.ok());
    const auto& import = static_cast<const ImportExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_EQ(import.source->kind(), NodeKind::BinaryExpression);
}

TEST(ParserTest, ImportExpressionFollowedByMember) {
    ParseResult result = parse("import(\"mod\").then(f);");

    ASSERT_TRUE(result.ok());
    const auto& call = static_cast<const CallExpression&>(
        *statementOf(result, 0).expression);
    ASSERT_EQ(call.callee->kind(), NodeKind::MemberExpression);
    const auto& member =
        static_cast<const MemberExpression&>(*call.callee);
    EXPECT_EQ(member.object->kind(), NodeKind::ImportExpression);
}

// ---------------------------------------------------------------------------
// import 声明
// ---------------------------------------------------------------------------

TEST(ParserTest, SideEffectImportDeclaration) {
    ParseResult result = parse("import \"mod\";");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ImportDeclaration&>(
        *programOf(result).body[0]);
    EXPECT_TRUE(declaration.specifiers.empty());
    ASSERT_NE(declaration.source, nullptr);
    EXPECT_EQ(declaration.source->kind(), NodeKind::StringLiteral);
}

TEST(ParserTest, DefaultImportDeclaration) {
    ParseResult result = parse("import d from \"mod\";");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ImportDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_EQ(declaration.specifiers.size(), 1u);
    ASSERT_EQ(declaration.specifiers[0]->kind(),
              NodeKind::ImportDefaultSpecifier);
    const auto& specifier = static_cast<const ImportDefaultSpecifier&>(
        *declaration.specifiers[0]);
    EXPECT_EQ(static_cast<const Identifier&>(*specifier.local).name, "d");
}

TEST(ParserTest, NamespaceImportDeclaration) {
    ParseResult result = parse("import * as ns from \"mod\";");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ImportDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_EQ(declaration.specifiers.size(), 1u);
    ASSERT_EQ(declaration.specifiers[0]->kind(),
              NodeKind::ImportNamespaceSpecifier);
    const auto& specifier = static_cast<const ImportNamespaceSpecifier&>(
        *declaration.specifiers[0]);
    EXPECT_EQ(static_cast<const Identifier&>(*specifier.local).name, "ns");
}

TEST(ParserTest, NamedImportDeclaration) {
    ParseResult result = parse("import { a, b as c } from \"mod\";");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ImportDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_EQ(declaration.specifiers.size(), 2u);

    const auto& first = static_cast<const ImportSpecifier&>(
        *declaration.specifiers[0]);
    EXPECT_EQ(static_cast<const Identifier&>(*first.imported).name, "a");
    EXPECT_EQ(static_cast<const Identifier&>(*first.local).name, "a");

    const auto& second = static_cast<const ImportSpecifier&>(
        *declaration.specifiers[1]);
    EXPECT_EQ(static_cast<const Identifier&>(*second.imported).name, "b");
    EXPECT_EQ(static_cast<const Identifier&>(*second.local).name, "c");
}

TEST(ParserTest, ReservedNamedImportRequiresAs) {
    ParseResult result = parse("import { default as d } from \"mod\";");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ImportDeclaration&>(
        *programOf(result).body[0]);
    const auto& specifier = static_cast<const ImportSpecifier&>(
        *declaration.specifiers[0]);
    EXPECT_EQ(static_cast<const Identifier&>(*specifier.imported).name,
              "default");
    EXPECT_EQ(static_cast<const Identifier&>(*specifier.local).name, "d");
}

TEST(ParserTest, ImportDeclarationWithAttributes) {
    ParseResult result = parse("import d from \"mod\" with { type: \"json\" };");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ImportDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_EQ(declaration.attributes.size(), 1u);
    const auto& attribute = static_cast<const ImportAttribute&>(
        *declaration.attributes[0]);
    EXPECT_EQ(static_cast<const Identifier&>(*attribute.key).name, "type");
    EXPECT_EQ(static_cast<const StringLiteral&>(*attribute.value).value,
              "json");
}

TEST(ParserTest, SideEffectImportWithAttributes) {
    ParseResult result = parse("import \"mod\" with { type: \"json\" };");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ImportDeclaration&>(
        *programOf(result).body[0]);
    EXPECT_TRUE(declaration.specifiers.empty());
    ASSERT_EQ(declaration.attributes.size(), 1u);
}

TEST(ParserTest, ImportAttributeWithStringKey) {
    ParseResult result = parse("import \"mod\" with { \"type\": \"json\" };");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ImportDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_EQ(declaration.attributes.size(), 1u);
    const auto& attribute = static_cast<const ImportAttribute&>(
        *declaration.attributes[0]);
    EXPECT_EQ(attribute.key->kind(), NodeKind::StringLiteral);
}

TEST(ParserTest, ImportAttributeNonStringValueReportsError) {
    ParseResult result = parse("import \"mod\" with { type: json };");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ReservedNamedImportWithoutAsReportsError) {
    ParseResult result = parse("import { if } from \"mod\";");

    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// export 声明
// ---------------------------------------------------------------------------

TEST(ParserTest, ExportAllDeclaration) {
    ParseResult result = parse("export * from \"mod\";");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportAllDeclaration&>(
        *programOf(result).body[0]);
    EXPECT_EQ(declaration.exported, nullptr);
    ASSERT_NE(declaration.source, nullptr);
    EXPECT_EQ(declaration.source->kind(), NodeKind::StringLiteral);
}

TEST(ParserTest, ExportAllAsNamespace) {
    ParseResult result = parse("export * as ns from \"mod\";");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportAllDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_NE(declaration.exported, nullptr);
    EXPECT_EQ(static_cast<const Identifier&>(*declaration.exported).name, "ns");
    ASSERT_NE(declaration.source, nullptr);
    EXPECT_EQ(declaration.source->kind(), NodeKind::StringLiteral);
}

TEST(ParserTest, ExportAllWithoutFromReportsError) {
    ParseResult result = parse("export * \"mod\";");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ExportAllWithoutSourceReportsError) {
    ParseResult result = parse("export * from;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ExportNamedSpecifiers) {
    ParseResult result = parse("export { a, b as c };");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportNamedDeclaration&>(
        *programOf(result).body[0]);
    EXPECT_EQ(declaration.declaration, nullptr);
    EXPECT_EQ(declaration.source, nullptr);
    ASSERT_EQ(declaration.specifiers.size(), 2u);

    const auto& first = static_cast<const ExportSpecifier&>(
        *declaration.specifiers[0]);
    EXPECT_EQ(static_cast<const Identifier&>(*first.local).name, "a");
    EXPECT_EQ(static_cast<const Identifier&>(*first.exported).name, "a");

    const auto& second = static_cast<const ExportSpecifier&>(
        *declaration.specifiers[1]);
    EXPECT_EQ(static_cast<const Identifier&>(*second.local).name, "b");
    EXPECT_EQ(static_cast<const Identifier&>(*second.exported).name, "c");
}

TEST(ParserTest, ExportNamedSpecifiersWithSource) {
    ParseResult result = parse("export { a, b as c } from \"mod\";");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportNamedDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_EQ(declaration.specifiers.size(), 2u);
    ASSERT_NE(declaration.source, nullptr);
    EXPECT_EQ(declaration.source->kind(), NodeKind::StringLiteral);
}

TEST(ParserTest, EmptyExportSpecifiers) {
    ParseResult result = parse("export {};");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportNamedDeclaration&>(
        *programOf(result).body[0]);
    EXPECT_TRUE(declaration.specifiers.empty());
    EXPECT_EQ(declaration.declaration, nullptr);
}

TEST(ParserTest, ExportAllWithAttributes) {
    ParseResult result = parse("export * from \"mod\" with { type: \"json\" };");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportAllDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_EQ(declaration.attributes.size(), 1u);
}

TEST(ParserTest, ExportNamedWithSourceAndAttributes) {
    ParseResult result =
        parse("export { a } from \"mod\" with { type: \"json\" };");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportNamedDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_NE(declaration.source, nullptr);
    ASSERT_EQ(declaration.attributes.size(), 1u);
}

TEST(ParserTest, ExportNamedSpecifiersWithoutSourceReportsError) {
    ParseResult result = parse("export { a } from;");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ExportVariableDeclaration) {
    ParseResult result = parse("export var x = 1;");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportNamedDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_NE(declaration.declaration, nullptr);
    EXPECT_EQ(declaration.declaration->kind(), NodeKind::VariableDeclaration);
}

TEST(ParserTest, ExportLetDeclaration) {
    ParseResult result = parse("export let x = 1;");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportNamedDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_NE(declaration.declaration, nullptr);
    EXPECT_EQ(declaration.declaration->kind(), NodeKind::VariableDeclaration);
}

TEST(ParserTest, ExportConstDeclaration) {
    ParseResult result = parse("export const x = 1;");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ExportNamedDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_NE(declaration.declaration, nullptr);
    EXPECT_EQ(declaration.declaration->kind(), NodeKind::VariableDeclaration);
}

TEST(ParserTest, DefaultAndNamedImportDeclaration) {
    ParseResult result = parse("import d, { a } from \"mod\";");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ImportDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_EQ(declaration.specifiers.size(), 2u);
    EXPECT_EQ(declaration.specifiers[0]->kind(),
              NodeKind::ImportDefaultSpecifier);
    EXPECT_EQ(declaration.specifiers[1]->kind(), NodeKind::ImportSpecifier);
}

TEST(ParserTest, DefaultAndNamespaceImportDeclaration) {
    ParseResult result = parse("import d, * as ns from \"mod\";");

    ASSERT_TRUE(result.ok());
    const auto& declaration = static_cast<const ImportDeclaration&>(
        *programOf(result).body[0]);
    ASSERT_EQ(declaration.specifiers.size(), 2u);
    EXPECT_EQ(declaration.specifiers[0]->kind(),
              NodeKind::ImportDefaultSpecifier);
    EXPECT_EQ(declaration.specifiers[1]->kind(),
              NodeKind::ImportNamespaceSpecifier);
}

TEST(ParserTest, ImportDeclarationMissingFromReportsError) {
    ParseResult result = parse("import d \"mod\";");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, ImportDeclarationInsideBlockReportsError) {
    ParseResult result = parse("{ import \"mod\"; }");

    EXPECT_FALSE(result.errors.empty());
}

TEST(ParserTest, DynamicImportIsNotDeclaration) {
    ParseResult result = parse("import(\"mod\");");

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(programOf(result).body[0]->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, ImportExpressionWithoutArgumentReportsError) {
    ParseResult result = parse("import();");

    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// await
// ---------------------------------------------------------------------------

TEST(ParserTest, AwaitInAsyncFunction) {
    ParseResult result = parse("async function f() { await x; }");

    ASSERT_TRUE(result.ok());
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    const auto& body = static_cast<const BlockStatement&>(*function.body);
    ASSERT_EQ(body.body.size(), 1u);
    const auto& statement =
        static_cast<const ExpressionStatement&>(*body.body[0]);
    ASSERT_EQ(statement.expression->kind(), NodeKind::AwaitExpression);
    const auto& await =
        static_cast<const AwaitExpression&>(*statement.expression);
    EXPECT_EQ(await.argument->kind(), NodeKind::Identifier);
}

TEST(ParserTest, AwaitInAsyncArrowExpressionBody) {
    ParseResult result = parse("async () => await x;");

    ASSERT_TRUE(result.ok());
    const auto& arrow = static_cast<const ArrowFunctionExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_TRUE(arrow.expression);
    EXPECT_EQ(arrow.body->kind(), NodeKind::AwaitExpression);
}

TEST(ParserTest, AwaitBindsTighterThanBinary) {
    ParseResult result = parse("async function f() { await a + b; }");

    ASSERT_TRUE(result.ok());
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    const auto& body = static_cast<const BlockStatement&>(*function.body);
    const auto& statement =
        static_cast<const ExpressionStatement&>(*body.body[0]);
    ASSERT_EQ(statement.expression->kind(), NodeKind::BinaryExpression);
    const auto& binary =
        static_cast<const BinaryExpression&>(*statement.expression);
    EXPECT_EQ(binary.left->kind(), NodeKind::AwaitExpression);
}

TEST(ParserTest, AwaitIsIdentifierOutsideAsync) {
    ParseResult result = parse("await;");

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(statementOf(result, 0).expression->kind(),
              NodeKind::Identifier);
}

// ---------------------------------------------------------------------------
// yield
// ---------------------------------------------------------------------------

namespace {

const BlockStatement& generatorBodyOf(const ParseResult& result) {
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    return static_cast<const BlockStatement&>(*function.body);
}

}  // namespace

TEST(ParserTest, YieldExpression) {
    ParseResult result = parse("function* g() { yield x; }");

    ASSERT_TRUE(result.ok());
    const auto& body = generatorBodyOf(result);
    ASSERT_EQ(body.body.size(), 1u);
    const auto& statement =
        static_cast<const ExpressionStatement&>(*body.body[0]);
    ASSERT_EQ(statement.expression->kind(), NodeKind::YieldExpression);
    const auto& yield =
        static_cast<const YieldExpression&>(*statement.expression);
    EXPECT_FALSE(yield.delegate);
    ASSERT_NE(yield.argument, nullptr);
    EXPECT_EQ(yield.argument->kind(), NodeKind::Identifier);
}

TEST(ParserTest, YieldWithoutArgument) {
    ParseResult result = parse("function* g() { yield; }");

    ASSERT_TRUE(result.ok());
    const auto& body = generatorBodyOf(result);
    const auto& statement =
        static_cast<const ExpressionStatement&>(*body.body[0]);
    const auto& yield =
        static_cast<const YieldExpression&>(*statement.expression);
    EXPECT_FALSE(yield.delegate);
    EXPECT_EQ(yield.argument, nullptr);
}

TEST(ParserTest, YieldDelegate) {
    ParseResult result = parse("function* g() { yield* inner; }");

    ASSERT_TRUE(result.ok());
    const auto& body = generatorBodyOf(result);
    const auto& statement =
        static_cast<const ExpressionStatement&>(*body.body[0]);
    const auto& yield =
        static_cast<const YieldExpression&>(*statement.expression);
    EXPECT_TRUE(yield.delegate);
    ASSERT_NE(yield.argument, nullptr);
    EXPECT_EQ(yield.argument->kind(), NodeKind::Identifier);
}

TEST(ParserTest, YieldStopsAtLineTerminator) {
    ParseResult result = parse("function* g() { yield\nx; }");

    ASSERT_TRUE(result.ok());
    const auto& body = generatorBodyOf(result);
    ASSERT_EQ(body.body.size(), 2u);
    const auto& first =
        static_cast<const ExpressionStatement&>(*body.body[0]);
    const auto& yield =
        static_cast<const YieldExpression&>(*first.expression);
    EXPECT_EQ(yield.argument, nullptr);
    EXPECT_EQ(body.body[1]->kind(), NodeKind::ExpressionStatement);
}

TEST(ParserTest, YieldIsIdentifierOutsideGenerator) {
    ParseResult result = parse("yield;");

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(statementOf(result, 0).expression->kind(),
              NodeKind::Identifier);
}

TEST(ParserTest, NestedFunctionResetsGeneratorContext) {
    ParseResult result = parse(
        "function* g() { function f() { yield; } }");

    ASSERT_TRUE(result.ok());
    const auto& body = generatorBodyOf(result);
    const auto& inner =
        static_cast<const FunctionDeclaration&>(*body.body[0]);
    const auto& innerBody = static_cast<const BlockStatement&>(*inner.body);
    const auto& statement =
        static_cast<const ExpressionStatement&>(*innerBody.body[0]);
    EXPECT_EQ(statement.expression->kind(), NodeKind::Identifier);
}

TEST(ParserTest, AsyncGeneratorYieldAwait) {
    ParseResult result = parse("async function* g() { yield await x; }");

    ASSERT_TRUE(result.ok());
    const auto& body = generatorBodyOf(result);
    const auto& statement =
        static_cast<const ExpressionStatement&>(*body.body[0]);
    const auto& yield =
        static_cast<const YieldExpression&>(*statement.expression);
    ASSERT_NE(yield.argument, nullptr);
    EXPECT_EQ(yield.argument->kind(), NodeKind::AwaitExpression);
}

TEST(ParserTest, NestedFunctionResetsAsyncContext) {
    ParseResult result = parse(
        "async function f() { function g() { await; } }");

    ASSERT_TRUE(result.ok());
    const auto& function =
        static_cast<const FunctionDeclaration&>(*programOf(result).body[0]);
    const auto& body = static_cast<const BlockStatement&>(*function.body);
    const auto& inner =
        static_cast<const FunctionDeclaration&>(*body.body[0]);
    const auto& innerBody = static_cast<const BlockStatement&>(*inner.body);
    const auto& statement =
        static_cast<const ExpressionStatement&>(*innerBody.body[0]);
    EXPECT_EQ(statement.expression->kind(), NodeKind::Identifier);
}

TEST(ParserTest, DeleteOptionalChain) {
    ParseResult result = parse("delete a?.b;");

    ASSERT_TRUE(result.ok());
    const auto& unary = static_cast<const UnaryExpression&>(
        *statementOf(result, 0).expression);
    EXPECT_EQ(unary.op, UnaryOperator::Delete);
    EXPECT_EQ(unary.argument->kind(), NodeKind::ChainExpression);
}

TEST(ParserTest, SuperMemberExpression) {
    ParseResult result = parse("class A extends B { m() { super.x; } }");

    ASSERT_TRUE(result.ok());
    const auto& body = classBodyOf(result);
    const auto& method = static_cast<const MethodDefinition&>(*body.elements[0]);
    const auto& function =
        static_cast<const FunctionExpression&>(*method.value);
    const auto& block = static_cast<const BlockStatement&>(*function.body);
    const auto& statement =
        static_cast<const ExpressionStatement&>(*block.body[0]);
    ASSERT_EQ(statement.expression->kind(), NodeKind::MemberExpression);
    const auto& member =
        static_cast<const MemberExpression&>(*statement.expression);
    EXPECT_EQ(member.object->kind(), NodeKind::SuperExpression);
}

}  // namespace
