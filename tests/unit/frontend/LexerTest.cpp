// LexerTest.cpp - 拉取式词法器
//
// 覆盖：trivia/ASI、标识符与 Unicode 转义、数字与 BigInt、字符串转义、
// 模板 head/middle/tail、正则 vs 除法、标点最长匹配、Illegal token。

#include "frontend/Lexer.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using neo::frontend::LexicalContext;
using neo::frontend::LexicalGoal;
using neo::frontend::Lexer;
using neo::frontend::Token;
using neo::frontend::TokenKind;

Token lexOne(std::string_view source, LexicalContext context = {}) {
    Lexer lexer(source);
    return lexer.next(context);
}

std::vector<Token> lexAll(std::string_view source, LexicalContext context = {}) {
    Lexer lexer(source);
    std::vector<Token> tokens;
    while (true) {
        Token token = lexer.next(context);
        const bool end = token.kind == TokenKind::EndOfInput;
        tokens.push_back(std::move(token));
        if (end) break;
    }
    return tokens;
}

TEST(LexerTest, EmptyInputProducesEndOfInput) {
    Token token = lexOne("");
    EXPECT_EQ(token.kind, TokenKind::EndOfInput);
    EXPECT_TRUE(token.lexeme.empty());
}

TEST(LexerTest, KeywordsAndIdentifiers) {
    const auto tokens = lexAll("if (foo) return null;");
    ASSERT_EQ(tokens.size(), 8u);
    EXPECT_EQ(tokens[0].kind, TokenKind::If);
    EXPECT_EQ(tokens[1].kind, TokenKind::LeftParen);
    EXPECT_EQ(tokens[2].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[2].text, "foo");
    EXPECT_EQ(tokens[3].kind, TokenKind::RightParen);
    EXPECT_EQ(tokens[4].kind, TokenKind::Return);
    EXPECT_EQ(tokens[5].kind, TokenKind::Null);
    EXPECT_EQ(tokens[6].kind, TokenKind::Semicolon);
}

TEST(LexerTest, ContextualKeywordsRemainIdentifiers) {
    const auto tokens = lexAll("let async await yield");
    ASSERT_EQ(tokens.size(), 5u);
    for (size_t index = 0; index < 4; ++index) {
        EXPECT_EQ(tokens[index].kind, TokenKind::Identifier);
    }
}

TEST(LexerTest, UnicodeIdentifierAndEscape) {
    Token token = lexOne("caf\u00E9");
    EXPECT_EQ(token.kind, TokenKind::Identifier);
    EXPECT_EQ(token.text, "caf\u00E9");

    token = lexOne("\\u0061bc");
    EXPECT_EQ(token.kind, TokenKind::Identifier);
    EXPECT_EQ(token.text, "abc");

    token = lexOne("a\\u{62}c");
    EXPECT_EQ(token.kind, TokenKind::Identifier);
    EXPECT_EQ(token.text, "abc");
}

TEST(LexerTest, PrivateIdentifier) {
    Token token = lexOne("#name");
    EXPECT_EQ(token.kind, TokenKind::PrivateIdentifier);
    EXPECT_EQ(token.text, "name");
}

TEST(LexerTest, NumericLiterals) {
    EXPECT_DOUBLE_EQ(lexOne("42").numberValue, 42.0);
    EXPECT_DOUBLE_EQ(lexOne("3.5").numberValue, 3.5);
    EXPECT_DOUBLE_EQ(lexOne(".5").numberValue, 0.5);
    EXPECT_DOUBLE_EQ(lexOne("1e3").numberValue, 1000.0);
    EXPECT_DOUBLE_EQ(lexOne("1E-2").numberValue, 0.01);
    EXPECT_DOUBLE_EQ(lexOne("0xFF").numberValue, 255.0);
    EXPECT_DOUBLE_EQ(lexOne("0o17").numberValue, 15.0);
    EXPECT_DOUBLE_EQ(lexOne("0b101").numberValue, 5.0);
    EXPECT_DOUBLE_EQ(lexOne("1_000_000").numberValue, 1000000.0);
}

TEST(LexerTest, BigIntLiterals) {
    Token token = lexOne("123n");
    EXPECT_EQ(token.kind, TokenKind::BigIntLiteral);
    EXPECT_EQ(token.text, "123");
    EXPECT_EQ(token.numericRadix, 10);

    token = lexOne("0x1Fn");
    EXPECT_EQ(token.kind, TokenKind::BigIntLiteral);
    EXPECT_EQ(token.text, "1F");
    EXPECT_EQ(token.numericRadix, 16);
}

TEST(LexerTest, LegacyOctal) {
    Token token = lexOne("077");
    EXPECT_EQ(token.kind, TokenKind::NumericLiteral);
    EXPECT_DOUBLE_EQ(token.numberValue, 63.0);

    token = lexOne("089");
    EXPECT_EQ(token.kind, TokenKind::NumericLiteral);
    EXPECT_DOUBLE_EQ(token.numberValue, 89.0);

    LexicalContext strict;
    strict.strict = true;
    token = lexOne("077", strict);
    EXPECT_EQ(token.kind, TokenKind::Illegal);
}

TEST(LexerTest, StringEscapes) {
    Token token = lexOne("\"a\\n\\t\\x41\\u0042\"");
    EXPECT_EQ(token.kind, TokenKind::StringLiteral);
    EXPECT_EQ(token.text, "a\n\tAB");

    token = lexOne("'\\u{1F600}'");
    EXPECT_EQ(token.kind, TokenKind::StringLiteral);
    EXPECT_EQ(token.text, "\xF0\x9F\x98\x80");

    token = lexOne("'a\\\nb'");
    EXPECT_EQ(token.kind, TokenKind::StringLiteral);
    EXPECT_EQ(token.text, "ab");
}

TEST(LexerTest, UnterminatedStringIsIllegal) {
    EXPECT_EQ(lexOne("\"abc").kind, TokenKind::Illegal);
    EXPECT_EQ(lexOne("\"abc\n\"").kind, TokenKind::Illegal);
}

TEST(LexerTest, TemplateHeadAndTail) {
    Lexer lexer("`a${b}c`");
    Token head = lexer.next();
    EXPECT_EQ(head.kind, TokenKind::TemplateHead);
    EXPECT_EQ(head.text, "a");

    Token name = lexer.next();
    EXPECT_EQ(name.kind, TokenKind::Identifier);
    EXPECT_EQ(name.text, "b");

    LexicalContext continuation;
    continuation.goal = LexicalGoal::TemplateContinuation;
    Token tail = lexer.next(continuation);
    EXPECT_EQ(tail.kind, TokenKind::TemplateTail);
    EXPECT_EQ(tail.text, "c");
}

TEST(LexerTest, NoSubstitutionTemplateNormalizesNewlines) {
    Token token = lexOne("`a\r\nb\rc`");
    EXPECT_EQ(token.kind, TokenKind::NoSubstitutionTemplate);
    EXPECT_EQ(token.text, "a\nb\nc");
}

TEST(LexerTest, TemplateMiddle) {
    Lexer lexer("`a${b}c${d}e`");
    EXPECT_EQ(lexer.next().kind, TokenKind::TemplateHead);
    EXPECT_EQ(lexer.next().kind, TokenKind::Identifier);

    LexicalContext continuation;
    continuation.goal = LexicalGoal::TemplateContinuation;
    Token middle = lexer.next(continuation);
    EXPECT_EQ(middle.kind, TokenKind::TemplateMiddle);
    EXPECT_EQ(middle.text, "c");

    EXPECT_EQ(lexer.next().kind, TokenKind::Identifier);
    Token tail = lexer.next(continuation);
    EXPECT_EQ(tail.kind, TokenKind::TemplateTail);
    EXPECT_EQ(tail.text, "e");
}

TEST(LexerTest, RegExpVersusDivision) {
    LexicalContext regexp;
    regexp.goal = LexicalGoal::RegExp;
    Token token = lexOne("/ab+c/gi", regexp);
    EXPECT_EQ(token.kind, TokenKind::RegularExpressionLiteral);
    EXPECT_EQ(token.text, "ab+c");
    EXPECT_EQ(token.regexFlags, "gi");

    token = lexOne("/[/]/", regexp);
    EXPECT_EQ(token.kind, TokenKind::RegularExpressionLiteral);
    EXPECT_EQ(token.text, "[/]");

    token = lexOne("/a/");
    EXPECT_EQ(token.kind, TokenKind::Slash);
}

TEST(LexerTest, PunctuatorLongestMatch) {
    const auto tokens = lexAll("a>>>=b?c:...d");
    ASSERT_EQ(tokens.size(), 9u);
    EXPECT_EQ(tokens[1].kind, TokenKind::UnsignedRightShiftAssign);
    EXPECT_EQ(tokens[3].kind, TokenKind::Question);
    EXPECT_EQ(tokens[5].kind, TokenKind::Colon);
    EXPECT_EQ(tokens[6].kind, TokenKind::Ellipsis);
}

TEST(LexerTest, OptionalChainingDoesNotConsumeNumber) {
    const auto tokens = lexAll("a?.5");
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[1].kind, TokenKind::Question);
    EXPECT_EQ(tokens[2].kind, TokenKind::NumericLiteral);
    EXPECT_DOUBLE_EQ(tokens[2].numberValue, 0.5);
}

TEST(LexerTest, TriviaAndLineTerminatorFlag) {
    const auto tokens = lexAll("a /* x\ny */ b // z\nc");
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_FALSE(tokens[0].precededByLineTerminator);
    EXPECT_TRUE(tokens[1].precededByLineTerminator);
    EXPECT_TRUE(tokens[2].precededByLineTerminator);
    EXPECT_EQ(tokens[1].text, "b");
    EXPECT_EQ(tokens[2].text, "c");
}

TEST(LexerTest, Hashbang) {
    const auto tokens = lexAll("#!/usr/bin/env node\na");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, "a");
}

TEST(LexerTest, UnterminatedCommentIsIllegal) {
    Token token = lexOne("/* abc");
    EXPECT_EQ(token.kind, TokenKind::Illegal);
    EXPECT_EQ(token.text, "Unterminated comment");
}

TEST(LexerTest, SaveRestoreLookahead) {
    Lexer lexer("a b");
    const auto state = lexer.save();
    EXPECT_EQ(lexer.next().text, "a");
    EXPECT_EQ(lexer.next().text, "b");
    lexer.restore(state);
    EXPECT_EQ(lexer.next().text, "a");
}

}  // namespace
