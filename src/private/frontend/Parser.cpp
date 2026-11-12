// frontend/Parser.cpp - 语法分析器框架实现
//
// 分派函数只路由，具体节点由 parseXxx 构造；每个 parseXxx 负责自己的
// 子节点、token 消费与 SourceRange。

#include "frontend/Parser.h"

#include <utility>

namespace neo::frontend {

namespace {

// 属性名可以是任意 IdentifierName，包括保留字（obj.default）。
// TokenKind 中 Break..False 是连续的固定保留字区间。
bool isIdentifierName(TokenKind kind) {
    if (kind == TokenKind::Identifier) return true;
    const int value = static_cast<int>(kind);
    return value >= static_cast<int>(TokenKind::Break) &&
           value <= static_cast<int>(TokenKind::False);
}

struct BinaryOperatorInfo {
    BinaryOperator op;
    int precedence;
    bool rightAssociative;
};

bool binaryOperator(TokenKind kind, BinaryOperatorInfo& info) {
    switch (kind) {
        case TokenKind::BitOr:
            info = {BinaryOperator::BitOr, 3, false};
            return true;
        case TokenKind::BitXor:
            info = {BinaryOperator::BitXor, 4, false};
            return true;
        case TokenKind::BitAnd:
            info = {BinaryOperator::BitAnd, 5, false};
            return true;
        case TokenKind::EqualEqual:
            info = {BinaryOperator::Equal, 6, false};
            return true;
        case TokenKind::NotEqual:
            info = {BinaryOperator::NotEqual, 6, false};
            return true;
        case TokenKind::StrictEqual:
            info = {BinaryOperator::StrictEqual, 6, false};
            return true;
        case TokenKind::StrictNotEqual:
            info = {BinaryOperator::StrictNotEqual, 6, false};
            return true;
        case TokenKind::LessThan:
            info = {BinaryOperator::LessThan, 7, false};
            return true;
        case TokenKind::GreaterThan:
            info = {BinaryOperator::GreaterThan, 7, false};
            return true;
        case TokenKind::LessThanEqual:
            info = {BinaryOperator::LessThanEqual, 7, false};
            return true;
        case TokenKind::GreaterThanEqual:
            info = {BinaryOperator::GreaterThanEqual, 7, false};
            return true;
        case TokenKind::In:
            info = {BinaryOperator::In, 7, false};
            return true;
        case TokenKind::InstanceOf:
            info = {BinaryOperator::InstanceOf, 7, false};
            return true;
        case TokenKind::LeftShift:
            info = {BinaryOperator::LeftShift, 8, false};
            return true;
        case TokenKind::RightShift:
            info = {BinaryOperator::RightShift, 8, false};
            return true;
        case TokenKind::UnsignedRightShift:
            info = {BinaryOperator::UnsignedRightShift, 8, false};
            return true;
        case TokenKind::Plus:
            info = {BinaryOperator::Add, 9, false};
            return true;
        case TokenKind::Minus:
            info = {BinaryOperator::Subtract, 9, false};
            return true;
        case TokenKind::Star:
            info = {BinaryOperator::Multiply, 10, false};
            return true;
        case TokenKind::Slash:
            info = {BinaryOperator::Divide, 10, false};
            return true;
        case TokenKind::Percent:
            info = {BinaryOperator::Remainder, 10, false};
            return true;
        case TokenKind::StarStar:
            info = {BinaryOperator::Exponentiate, 11, true};
            return true;
        default:
            return false;
    }
}

struct LogicalOperatorInfo {
    LogicalOperator op;
    int precedence;
};

bool logicalOperator(TokenKind kind, LogicalOperatorInfo& info) {
    switch (kind) {
        case TokenKind::LogicalOr:
            info = {LogicalOperator::Or, 1};
            return true;
        case TokenKind::NullishCoalescing:
            info = {LogicalOperator::Nullish, 1};
            return true;
        case TokenKind::LogicalAnd:
            info = {LogicalOperator::And, 2};
            return true;
        default:
            return false;
    }
}

bool isUnparenthesizedLogical(const Node* node, LogicalOperator op) {
    if (node == nullptr || node->kind() != NodeKind::LogicalExpression) {
        return false;
    }
    if (node->isParenthesized()) return false;
    return static_cast<const LogicalExpression*>(node)->op == op;
}

bool updateOperator(TokenKind kind, UpdateOperator& op) {
    switch (kind) {
        case TokenKind::PlusPlus:
            op = UpdateOperator::Increment;
            return true;
        case TokenKind::MinusMinus:
            op = UpdateOperator::Decrement;
            return true;
        default:
            return false;
    }
}

bool unaryOperator(TokenKind kind, UnaryOperator& op) {
    switch (kind) {
        case TokenKind::Delete:
            op = UnaryOperator::Delete;
            return true;
        case TokenKind::Void:
            op = UnaryOperator::Void;
            return true;
        case TokenKind::TypeOf:
            op = UnaryOperator::TypeOf;
            return true;
        case TokenKind::Plus:
            op = UnaryOperator::Plus;
            return true;
        case TokenKind::Minus:
            op = UnaryOperator::Minus;
            return true;
        case TokenKind::BitNot:
            op = UnaryOperator::BitNot;
            return true;
        case TokenKind::LogicalNot:
            op = UnaryOperator::LogicalNot;
            return true;
        default:
            return false;
    }
}

}  // namespace

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
    // 后续在条件表达式之上逐层加入赋值、序列等节点。
    return parseConditionalExpression();
}

NodePtr Parser::parseConditionalExpression() {
    NodePtr test = parseLogicalExpression(1);
    if (test == nullptr) return nullptr;

    if (!match(TokenKind::Question)) return test;

    const SourceLocation start = test->range().start;
    // consequent / alternate 目前用条件表达式占位；赋值表达式落地后替换。
    NodePtr consequent = parseConditionalExpression();
    if (consequent == nullptr) return nullptr;
    if (!expect(TokenKind::Colon, "':'")) return nullptr;
    NodePtr alternate = parseConditionalExpression();
    if (alternate == nullptr) return nullptr;

    return std::make_unique<ConditionalExpression>(
        rangeFrom(start), std::move(test), std::move(consequent),
        std::move(alternate));
}

NodePtr Parser::parseLogicalExpression(int minPrecedence) {
    NodePtr left = parseBinaryExpression(1);
    if (left == nullptr) return nullptr;

    while (true) {
        LogicalOperatorInfo info{};
        if (!logicalOperator(current_.kind, info)) break;
        if (info.precedence < minPrecedence) break;

        const SourceLocation start = left->range().start;
        advance();

        NodePtr right = parseLogicalExpression(info.precedence + 1);
        if (right == nullptr) return nullptr;

        // ?? 不能与 || / && 直接混用；括号包裹的子树不算混用。
        if (info.op == LogicalOperator::Nullish) {
            if (isUnparenthesizedLogical(left.get(), LogicalOperator::And) ||
                isUnparenthesizedLogical(left.get(), LogicalOperator::Or) ||
                isUnparenthesizedLogical(right.get(), LogicalOperator::And) ||
                isUnparenthesizedLogical(right.get(), LogicalOperator::Or)) {
                error(start,
                      "Cannot mix nullish coalescing with '||' or '&&' without parentheses");
                return nullptr;
            }
        } else {
            if (isUnparenthesizedLogical(left.get(), LogicalOperator::Nullish) ||
                isUnparenthesizedLogical(right.get(),
                                         LogicalOperator::Nullish)) {
                error(start,
                      "Cannot mix nullish coalescing with '||' or '&&' without parentheses");
                return nullptr;
            }
        }

        left = std::make_unique<LogicalExpression>(
            rangeFrom(start), info.op, std::move(left), std::move(right));
    }
    return left;
}

NodePtr Parser::parseBinaryExpression(int minPrecedence) {
    NodePtr left = parseUnaryExpression();
    if (left == nullptr) return nullptr;

    while (true) {
        BinaryOperatorInfo info{};
        if (!binaryOperator(current_.kind, info)) break;
        if (info.precedence < minPrecedence) break;

        const SourceLocation start = left->range().start;
        advance();

        // 左结合：右侧要求更高优先级；右结合（**）用当前优先级。
        const int nextPrecedence =
            info.rightAssociative ? info.precedence : info.precedence + 1;
        NodePtr right = parseBinaryExpression(nextPrecedence);
        if (right == nullptr) return nullptr;

        left = std::make_unique<BinaryExpression>(
            rangeFrom(start), info.op, std::move(left), std::move(right));
    }
    return left;
}

NodePtr Parser::parseUnaryExpression() {
    UnaryOperator op;
    if (unaryOperator(current_.kind, op)) {
        const SourceLocation start = current_.start;
        advance();
        NodePtr argument = parseUnaryExpression();
        if (argument == nullptr) return nullptr;
        return std::make_unique<UnaryExpression>(
            rangeFrom(start), op, std::move(argument));
    }
    return parseUpdateExpression();
}

NodePtr Parser::parseUpdateExpression() {
    UpdateOperator op;
    if (updateOperator(current_.kind, op)) {
        // 前缀 ++/-- 的操作数是 UnaryExpression。
        const SourceLocation start = current_.start;
        advance();
        NodePtr argument = parseUnaryExpression();
        if (argument == nullptr) return nullptr;
        return std::make_unique<UpdateExpression>(
            rangeFrom(start), op, true, std::move(argument));
    }

    NodePtr argument = parseLeftHandSideExpression();
    if (argument == nullptr) return nullptr;

    // 后缀 ++/-- 不能跨行终止符（ASI 会把 ++/-- 留给下一条语句）。
    if (updateOperator(current_.kind, op) &&
        !current_.precededByLineTerminator) {
        const SourceLocation start = argument->range().start;
        advance();
        return std::make_unique<UpdateExpression>(
            rangeFrom(start), op, false, std::move(argument));
    }
    return argument;
}

NodePtr Parser::parseLeftHandSideExpression() {
    if (at(TokenKind::New)) return parseNewExpression();
    return parseCallExpression();
}

NodePtr Parser::parseMemberExpression() {
    const SourceLocation start = current_.start;
    NodePtr object = parsePrimaryExpression();
    if (object == nullptr) return nullptr;

    while (at(TokenKind::Dot) || at(TokenKind::LeftBracket)) {
        object = parseMemberSuffix(std::move(object), start);
        if (object == nullptr) return nullptr;
    }
    return object;
}

NodePtr Parser::parseCallExpression() {
    const SourceLocation start = current_.start;
    NodePtr callee = parseMemberExpression();
    if (callee == nullptr) return nullptr;
    return parseCallSuffix(std::move(callee), start);
}

NodePtr Parser::parseNewExpression() {
    const SourceLocation start = current_.start;
    advance();  // 'new'

    // new.target 暂不支持；先按普通 new 表达式处理。
    NodePtr callee = at(TokenKind::New) ? parseNewExpression()
                                        : parseMemberExpression();
    if (callee == nullptr) return nullptr;

    NodeList arguments;
    if (at(TokenKind::LeftParen) && !parseArguments(arguments)) {
        return nullptr;
    }

    NodePtr expression = std::make_unique<NewExpression>(
        rangeFrom(start), std::move(callee), std::move(arguments));
    // new Foo().bar / new Foo()()：new 表达式之后仍可继续成员与调用后缀。
    return parseCallSuffix(std::move(expression), start);
}

NodePtr Parser::parseMemberSuffix(NodePtr object, SourceLocation start) {
    if (at(TokenKind::Dot)) {
        advance();
        NodePtr property = parseIdentifierName();
        if (property == nullptr) return nullptr;
        return std::make_unique<MemberExpression>(
            rangeFrom(start), std::move(object), std::move(property), false);
    }

    if (at(TokenKind::LeftBracket)) {
        advance();
        NodePtr property = parseExpression();
        if (property == nullptr) return nullptr;
        if (!expect(TokenKind::RightBracket, "']'")) return nullptr;
        return std::make_unique<MemberExpression>(
            rangeFrom(start), std::move(object), std::move(property), true);
    }

    return object;
}

NodePtr Parser::parseCallSuffix(NodePtr callee, SourceLocation start) {
    while (true) {
        if (at(TokenKind::LeftParen)) {
            NodeList arguments;
            if (!parseArguments(arguments)) return nullptr;
            callee = std::make_unique<CallExpression>(
                rangeFrom(start), std::move(callee), std::move(arguments));
            continue;
        }
        if (at(TokenKind::Dot) || at(TokenKind::LeftBracket)) {
            callee = parseMemberSuffix(std::move(callee), start);
            if (callee == nullptr) return nullptr;
            continue;
        }
        break;
    }
    return callee;
}

bool Parser::parseArguments(NodeList& arguments) {
    if (!expect(TokenKind::LeftParen, "'('")) return false;
    if (match(TokenKind::RightParen)) return true;

    while (true) {
        NodePtr argument = parseExpression();
        if (argument == nullptr) return false;
        arguments.push_back(std::move(argument));

        if (!match(TokenKind::Comma)) break;
        if (at(TokenKind::RightParen)) break;  // 尾随逗号
    }

    return expect(TokenKind::RightParen, "')'");
}

NodePtr Parser::parseIdentifierName() {
    if (!isIdentifierName(current_.kind)) {
        error(current_.start, "Expected property name");
        return nullptr;
    }
    const SourceLocation start = current_.start;
    std::string name = current_.text;
    advance();
    return std::make_unique<Identifier>(rangeFrom(start), std::move(name));
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
            expression->setParenthesized(true);
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
