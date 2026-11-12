// frontend/Parser.cpp - 语法分析器框架实现
//
// 分派函数只路由，具体节点由 parseXxx 构造；每个 parseXxx 负责自己的
// 子节点、token 消费与 SourceRange。

#include "frontend/Parser.h"

#include <utility>

namespace neo::frontend {

Parser::Parser(std::string_view source) : lexer_(source) {
    // 程序起始位置是表达式起始位置，`/` 应优先按正则扫描。
    advance(LexicalGoal::RegExp);
}

ParseResult Parser::parseProgram() {
    const SourceLocation start = current_.start;
    NodeList body;
    bool inDirectivePrologue = true;

    while (!at(TokenKind::EndOfInput)) {
        NodePtr statement = parseStatement();
        if (statement == nullptr) {
            inDirectivePrologue = false;
            synchronize();
            continue;
        }

        if (inDirectivePrologue) {
            if (statement->kind() == NodeKind::ExpressionStatement) {
                auto* expressionStatement =
                    static_cast<ExpressionStatement*>(statement.get());
                if (expressionStatement->expression != nullptr &&
                    expressionStatement->expression->kind() ==
                        NodeKind::StringLiteral) {
                    auto* literal = static_cast<StringLiteral*>(
                        expressionStatement->expression.get());
                    if (literal->value == "use strict") {
                        strict_ = true;
                        // 当前 token 已按旧严格性扫描，重扫一次。
                        rescan(LexicalGoal::RegExp);
                    }
                } else {
                    inDirectivePrologue = false;
                }
            } else {
                inDirectivePrologue = false;
            }
        }

        body.push_back(std::move(statement));
    }

    ParseResult result;
    result.program = std::make_unique<Program>(
        SourceRange{start, previousEnd_}, std::move(body), strict_);
    result.errors = std::move(errors_);
    return result;
}

bool Parser::match(TokenKind kind) {
    if (!at(kind)) return false;
    advance();
    return true;
}

bool Parser::expect(TokenKind kind, std::string_view what) {
    if (match(kind)) return true;
    error(current_.start, "Expected " + std::string(what));
    return false;
}

bool Parser::consumeSemicolon() {
    if (match(TokenKind::Semicolon)) return true;
    if (at(TokenKind::RightBrace) || at(TokenKind::EndOfInput) ||
        current_.precededByLineTerminator) {
        return true;
    }
    error(current_.start, "Expected ';'");
    return false;
}

void Parser::advance(LexicalGoal goal) {
    previousEnd_ = current_.end;
    currentStart_ = lexer_.save();
    current_ = lexer_.next({goal, strict_});
    if (current_.kind == TokenKind::Illegal) {
        error(current_.start, current_.text);
    }
}

Token Parser::peek(LexicalGoal goal) {
    const LexerState state = lexer_.save();
    Token token = lexer_.next({goal, strict_});
    lexer_.restore(state);
    return token;
}

void Parser::rescan(LexicalGoal goal) {
    lexer_.restore(currentStart_);
    current_ = lexer_.next({goal, strict_});
    if (current_.kind == TokenKind::Illegal) {
        error(current_.start, current_.text);
    }
}

void Parser::error(SourceLocation location, std::string message) {
    errors_.push_back(ParseError{location, std::move(message)});
}

void Parser::synchronize() {
    while (!at(TokenKind::EndOfInput)) {
        if (at(TokenKind::Semicolon) || at(TokenKind::RightBrace)) {
            advance();
            return;
        }
        advance();
    }
}

// ---------------------------------------------------------------------------
// 语句
// ---------------------------------------------------------------------------

NodePtr Parser::parseStatement() {
    if (at(TokenKind::Semicolon)) return parseEmptyStatement();
    return parseExpressionStatement();
}

NodePtr Parser::parseEmptyStatement() {
    const SourceLocation start = current_.start;
    advance();
    return std::make_unique<EmptyStatement>(rangeFrom(start));
}

NodePtr Parser::parseExpressionStatement() {
    const SourceLocation start = current_.start;
    NodePtr expression = parseExpression();
    if (expression == nullptr) return nullptr;
    if (!consumeSemicolon()) return nullptr;
    return std::make_unique<ExpressionStatement>(rangeFrom(start),
                                                 std::move(expression));
}

// ---------------------------------------------------------------------------
// 表达式
// ---------------------------------------------------------------------------

NodePtr Parser::parseExpression() {
    // 后续在 primary 之上逐层加入一元、二元、赋值、条件等节点。
    return parsePrimaryExpression();
}

NodePtr Parser::parsePrimaryExpression() {
    switch (current_.kind) {
        case TokenKind::Identifier:
            return parseIdentifier();
        case TokenKind::NumericLiteral:
            return parseNumericLiteral();
        case TokenKind::BigIntLiteral:
            return parseBigIntLiteral();
        case TokenKind::StringLiteral:
            return parseStringLiteral();
        case TokenKind::True:
        case TokenKind::False:
            return parseBooleanLiteral();
        case TokenKind::Null:
            return parseNullLiteral();
        case TokenKind::RegularExpressionLiteral:
            return parseRegExpLiteral();
        case TokenKind::Slash:
        case TokenKind::SlashAssign:
            // 当前 `/` 是按除法目标扫出来的，进入表达式起始位置后重扫为正则。
            rescan(LexicalGoal::RegExp);
            if (at(TokenKind::RegularExpressionLiteral)) {
                return parseRegExpLiteral();
            }
            error(current_.start, "Expected regular expression literal");
            return nullptr;
        case TokenKind::LeftParen: {
            const SourceLocation start = current_.start;
            advance();
            NodePtr expression = parseExpression();
            if (expression == nullptr) return nullptr;
            if (!expect(TokenKind::RightParen, "')'")) return nullptr;
            expression->setRange(rangeFrom(start));
            return expression;
        }
        default:
            error(current_.start, "Unexpected token in expression");
            return nullptr;
    }
}

NodePtr Parser::parseIdentifier() {
    const SourceLocation start = current_.start;
    std::string name = current_.text;
    advance();
    return std::make_unique<Identifier>(rangeFrom(start), std::move(name));
}

NodePtr Parser::parseNumericLiteral() {
    const SourceLocation start = current_.start;
    const double value = current_.numberValue;
    advance();
    return std::make_unique<NumericLiteral>(rangeFrom(start), value);
}

NodePtr Parser::parseBigIntLiteral() {
    const SourceLocation start = current_.start;
    std::string digits = current_.text;
    const int radix = current_.numericRadix;
    advance();
    return std::make_unique<BigIntLiteral>(rangeFrom(start), std::move(digits),
                                           radix);
}

NodePtr Parser::parseStringLiteral() {
    const SourceLocation start = current_.start;
    std::string value = current_.text;
    advance();
    return std::make_unique<StringLiteral>(rangeFrom(start), std::move(value));
}

NodePtr Parser::parseBooleanLiteral() {
    const SourceLocation start = current_.start;
    const bool value = at(TokenKind::True);
    advance();
    return std::make_unique<BooleanLiteral>(rangeFrom(start), value);
}

NodePtr Parser::parseNullLiteral() {
    const SourceLocation start = current_.start;
    advance();
    return std::make_unique<NullLiteral>(rangeFrom(start));
}

NodePtr Parser::parseRegExpLiteral() {
    const SourceLocation start = current_.start;
    std::string pattern = current_.text;
    std::string flags = current_.regexFlags;
    advance();
    return std::make_unique<RegExpLiteral>(rangeFrom(start), std::move(pattern),
                                           std::move(flags));
}

}  // namespace neo::frontend
