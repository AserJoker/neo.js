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

bool assignmentOperator(TokenKind kind, AssignmentOperator& op) {
    switch (kind) {
        case TokenKind::Assign:
            op = AssignmentOperator::Assign;
            return true;
        case TokenKind::PlusAssign:
            op = AssignmentOperator::Add;
            return true;
        case TokenKind::MinusAssign:
            op = AssignmentOperator::Subtract;
            return true;
        case TokenKind::StarAssign:
            op = AssignmentOperator::Multiply;
            return true;
        case TokenKind::SlashAssign:
            op = AssignmentOperator::Divide;
            return true;
        case TokenKind::PercentAssign:
            op = AssignmentOperator::Remainder;
            return true;
        case TokenKind::StarStarAssign:
            op = AssignmentOperator::Exponentiate;
            return true;
        case TokenKind::LeftShiftAssign:
            op = AssignmentOperator::LeftShift;
            return true;
        case TokenKind::RightShiftAssign:
            op = AssignmentOperator::RightShift;
            return true;
        case TokenKind::UnsignedRightShiftAssign:
            op = AssignmentOperator::UnsignedRightShift;
            return true;
        case TokenKind::BitAndAssign:
            op = AssignmentOperator::BitAnd;
            return true;
        case TokenKind::BitOrAssign:
            op = AssignmentOperator::BitOr;
            return true;
        case TokenKind::BitXorAssign:
            op = AssignmentOperator::BitXor;
            return true;
        case TokenKind::LogicalAndAssign:
            op = AssignmentOperator::LogicalAnd;
            return true;
        case TokenKind::LogicalOrAssign:
            op = AssignmentOperator::LogicalOr;
            return true;
        case TokenKind::NullishAssign:
            op = AssignmentOperator::Nullish;
            return true;
        default:
            return false;
    }
}

bool isSimpleAssignmentTarget(const Node* node) {
    return node != nullptr && (node->kind() == NodeKind::Identifier ||
                               node->kind() == NodeKind::MemberExpression);
}

bool isForInOfLeft(const Node* node) {
    if (node == nullptr) return false;
    return node->kind() == NodeKind::VariableDeclaration ||
           node->kind() == NodeKind::Identifier ||
           node->kind() == NodeKind::MemberExpression;
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
        NodePtr statement = parseStatementListItem(inDirectivePrologue);
        if (statement == nullptr) {
            inDirectivePrologue = false;
            synchronize();
            continue;
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

void Parser::synchronize(bool stopAtRightBrace) {
    while (!at(TokenKind::EndOfInput)) {
        if (stopAtRightBrace && at(TokenKind::RightBrace)) return;
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

NodePtr Parser::parseStatementListItem(bool& inDirectivePrologue) {
    NodePtr statement = parseStatement();
    if (statement == nullptr) return nullptr;

    if (!inDirectivePrologue) return statement;

    // 指令序言只接受“裸字符串字面量”表达式语句；任何延续（二元/成员/调用/
    // 括号）都会让顶层节点不再是 StringLiteral，从而结束指令序言。
    if (statement->kind() != NodeKind::ExpressionStatement) {
        inDirectivePrologue = false;
        return statement;
    }
    auto* expressionStatement =
        static_cast<ExpressionStatement*>(statement.get());
    if (expressionStatement->expression == nullptr ||
        expressionStatement->expression->kind() != NodeKind::StringLiteral ||
        expressionStatement->expression->isParenthesized()) {
        inDirectivePrologue = false;
        return statement;
    }

    NodePtr directive = parseDirective(std::move(statement));
    if (directive != nullptr) {
        auto* value = static_cast<Directive*>(directive.get());
        if (value->isUseStrict()) {
            strict_ = true;
            // 当前 token 已按旧严格性扫描，重扫一次。
            rescan(LexicalGoal::RegExp);
        }
    }
    return directive;
}

NodePtr Parser::parseDirective(NodePtr statement) {
    auto* expressionStatement =
        static_cast<ExpressionStatement*>(statement.get());
    NodePtr expression = std::move(expressionStatement->expression);
    auto* literal = static_cast<StringLiteral*>(expression.get());

    std::string value = literal->value;
    const SourceRange literalRange = literal->range();
    const std::string_view source = lexer_.stream().source();
    const size_t length = literalRange.end.offset - literalRange.start.offset;
    std::string raw(source.substr(literalRange.start.offset, length));

    return std::make_unique<Directive>(statement->range(), std::move(value),
                                       std::move(raw));
}

NodePtr Parser::parseStatement() {
    if (at(TokenKind::LeftBrace)) return parseBlockStatement();
    if (at(TokenKind::Semicolon)) return parseEmptyStatement();
    if (at(TokenKind::Var) || at(TokenKind::Const)) {
        return parseVariableDeclaration();
    }
    if (at(TokenKind::Identifier) && current_.text == "let" &&
        isLetDeclarationStart()) {
        return parseVariableDeclaration();
    }
    if (at(TokenKind::If)) return parseIfStatement();
    if (at(TokenKind::While)) return parseWhileStatement();
    if (at(TokenKind::Do)) return parseDoWhileStatement();
    if (at(TokenKind::For)) return parseForStatement();
    if (at(TokenKind::Return)) return parseReturnStatement();
    if (at(TokenKind::Throw)) return parseThrowStatement();
    if (at(TokenKind::Break)) return parseBreakStatement();
    if (at(TokenKind::Continue)) return parseContinueStatement();
    if (at(TokenKind::Try)) return parseTryStatement();
    if (at(TokenKind::Switch)) return parseSwitchStatement();
    if (at(TokenKind::With)) return parseWithStatement();
    if (at(TokenKind::Debugger)) return parseDebuggerStatement();
    if (at(TokenKind::Identifier) && peek().kind == TokenKind::Colon) {
        return parseLabeledStatement();
    }
    return parseExpressionStatement();
}

NodePtr Parser::parseLabeledStatement() {
    const SourceLocation start = current_.start;
    std::string label = current_.text;
    advance();  // label
    if (!expect(TokenKind::Colon, "':'")) return nullptr;

    NodePtr body = parseStatement();
    if (body == nullptr) return nullptr;

    return std::make_unique<LabeledStatement>(
        rangeFrom(start), std::move(label), std::move(body));
}

NodePtr Parser::parseWithStatement() {
    const SourceLocation start = current_.start;
    if (strict_) {
        error(start, "Strict mode code may not include a with statement");
        return nullptr;
    }
    advance();  // 'with'

    if (!expect(TokenKind::LeftParen, "'('")) return nullptr;
    NodePtr object = parseExpression();
    if (object == nullptr) return nullptr;
    if (!expect(TokenKind::RightParen, "')'")) return nullptr;

    NodePtr body = parseStatement();
    if (body == nullptr) return nullptr;

    return std::make_unique<WithStatement>(rangeFrom(start), std::move(object),
                                           std::move(body));
}

NodePtr Parser::parseDebuggerStatement() {
    const SourceLocation start = current_.start;
    advance();  // 'debugger'
    if (!consumeSemicolon()) return nullptr;
    return std::make_unique<DebuggerStatement>(rangeFrom(start));
}

NodePtr Parser::parseSwitchStatement() {
    const SourceLocation start = current_.start;
    advance();  // 'switch'

    if (!expect(TokenKind::LeftParen, "'('")) return nullptr;
    NodePtr discriminant = parseExpression();
    if (discriminant == nullptr) return nullptr;
    if (!expect(TokenKind::RightParen, "')'")) return nullptr;
    if (!expect(TokenKind::LeftBrace, "'{'")) return nullptr;

    NodeList cases;
    while (!at(TokenKind::RightBrace) && !at(TokenKind::EndOfInput)) {
        NodePtr clause;
        if (at(TokenKind::Case)) {
            clause = parseSwitchCase(false);
        } else if (at(TokenKind::Default)) {
            clause = parseSwitchCase(true);
        } else {
            error(current_.start, "Expected 'case' or 'default'");
            return nullptr;
        }
        if (clause == nullptr) return nullptr;
        cases.push_back(std::move(clause));
    }

    if (!expect(TokenKind::RightBrace, "'}'")) return nullptr;
    return std::make_unique<SwitchStatement>(
        rangeFrom(start), std::move(discriminant), std::move(cases));
}

NodePtr Parser::parseSwitchCase(bool isDefault) {
    const SourceLocation start = current_.start;
    advance();  // 'case' / 'default'

    NodePtr test;
    if (!isDefault) {
        test = parseExpression();
        if (test == nullptr) return nullptr;
    }
    if (!expect(TokenKind::Colon, "':'")) return nullptr;

    NodeList consequent;
    while (!at(TokenKind::Case) && !at(TokenKind::Default) &&
           !at(TokenKind::RightBrace) && !at(TokenKind::EndOfInput)) {
        NodePtr statement = parseStatement();
        if (statement == nullptr) break;
        consequent.push_back(std::move(statement));
    }

    return std::make_unique<SwitchCase>(rangeFrom(start), std::move(test),
                                        std::move(consequent));
}

NodePtr Parser::parseTryStatement() {
    const SourceLocation start = current_.start;
    advance();  // 'try'

    NodePtr block = parseBlockStatement();
    if (block == nullptr) return nullptr;

    NodePtr handler;
    if (at(TokenKind::Catch)) {
        handler = parseCatchClause();
        if (handler == nullptr) return nullptr;
    }

    NodePtr finalizer;
    if (at(TokenKind::Finally)) {
        advance();
        finalizer = parseBlockStatement();
        if (finalizer == nullptr) return nullptr;
    }

    if (handler == nullptr && finalizer == nullptr) {
        error(start, "Missing catch or finally after try");
        return nullptr;
    }

    return std::make_unique<TryStatement>(
        rangeFrom(start), std::move(block), std::move(handler),
        std::move(finalizer));
}

NodePtr Parser::parseCatchClause() {
    const SourceLocation start = current_.start;
    advance();  // 'catch'

    NodePtr param;
    if (at(TokenKind::LeftParen)) {
        advance();
        if (at(TokenKind::RightParen)) {
            error(current_.start, "Expected catch parameter");
            return nullptr;
        }
        param = parseIdentifier();
        if (param == nullptr) return nullptr;
        if (!expect(TokenKind::RightParen, "')'")) return nullptr;
    }

    NodePtr body = parseBlockStatement();
    if (body == nullptr) return nullptr;

    return std::make_unique<CatchClause>(rangeFrom(start), std::move(param),
                                         std::move(body));
}

NodePtr Parser::parseBreakStatement() {
    const SourceLocation start = current_.start;
    advance();  // 'break'

    std::string label;
    if (at(TokenKind::Identifier) && !current_.precededByLineTerminator) {
        label = current_.text;
        advance();
    }

    if (!consumeSemicolon()) return nullptr;
    return std::make_unique<BreakStatement>(rangeFrom(start), std::move(label));
}

NodePtr Parser::parseContinueStatement() {
    const SourceLocation start = current_.start;
    advance();  // 'continue'

    std::string label;
    if (at(TokenKind::Identifier) && !current_.precededByLineTerminator) {
        label = current_.text;
        advance();
    }

    if (!consumeSemicolon()) return nullptr;
    return std::make_unique<ContinueStatement>(rangeFrom(start),
                                               std::move(label));
}

NodePtr Parser::parseThrowStatement() {
    const SourceLocation start = current_.start;
    advance();  // 'throw'

    // 受限产生式：throw 后不允许行终止符。
    if (current_.precededByLineTerminator) {
        error(current_.start, "Illegal newline after throw");
        return nullptr;
    }

    NodePtr argument = parseExpression();
    if (argument == nullptr) return nullptr;
    if (!consumeSemicolon()) return nullptr;

    return std::make_unique<ThrowStatement>(rangeFrom(start),
                                            std::move(argument));
}

NodePtr Parser::parseReturnStatement() {
    const SourceLocation start = current_.start;
    advance();  // 'return'

    // 受限产生式：return 后出现行终止符即视为无返回值。
    NodePtr argument;
    if (!at(TokenKind::Semicolon) && !at(TokenKind::RightBrace) &&
        !at(TokenKind::EndOfInput) && !current_.precededByLineTerminator) {
        argument = parseExpression();
        if (argument == nullptr) return nullptr;
    }

    if (!consumeSemicolon()) return nullptr;
    return std::make_unique<ReturnStatement>(rangeFrom(start),
                                             std::move(argument));
}

NodePtr Parser::parseForStatement() {
    const SourceLocation start = current_.start;
    advance();  // 'for'

    if (!expect(TokenKind::LeftParen, "'('")) return nullptr;

    NodePtr init;
    if (!at(TokenKind::Semicolon)) {
        const bool declaration =
            at(TokenKind::Var) || at(TokenKind::Const) ||
            (at(TokenKind::Identifier) && current_.text == "let" &&
             isLetDeclarationStart());

        // for 头部用 [~In] 解析，避免把 for-in 的 in 当成二元运算符。
        const bool savedAllowIn = allowIn_;
        allowIn_ = false;
        init = declaration ? parseVariableDeclaration(false) : parseExpression();
        allowIn_ = savedAllowIn;
        if (init == nullptr) return nullptr;

        if (at(TokenKind::In)) {
            return parseForInStatement(std::move(init), start);
        }
        if (at(TokenKind::Identifier) && current_.text == "of") {
            return parseForOfStatement(std::move(init), start);
        }
    }
    if (!expect(TokenKind::Semicolon, "';'")) return nullptr;

    NodePtr test;
    if (!at(TokenKind::Semicolon)) {
        test = parseExpression();
        if (test == nullptr) return nullptr;
    }
    if (!expect(TokenKind::Semicolon, "';'")) return nullptr;

    NodePtr update;
    if (!at(TokenKind::RightParen)) {
        update = parseExpression();
        if (update == nullptr) return nullptr;
    }
    if (!expect(TokenKind::RightParen, "')'")) return nullptr;

    NodePtr body = parseStatement();
    if (body == nullptr) return nullptr;

    return std::make_unique<ForStatement>(
        rangeFrom(start), std::move(init), std::move(test), std::move(update),
        std::move(body));
}

NodePtr Parser::parseForInStatement(NodePtr left, SourceLocation start) {
    if (!isForInOfLeft(left.get())) {
        error(left->range().start, "Invalid left-hand side in for-in");
        return nullptr;
    }
    advance();  // 'in'

    NodePtr right = parseExpression();
    if (right == nullptr) return nullptr;
    if (!expect(TokenKind::RightParen, "')'")) return nullptr;

    NodePtr body = parseStatement();
    if (body == nullptr) return nullptr;

    return std::make_unique<ForInStatement>(
        rangeFrom(start), std::move(left), std::move(right), std::move(body));
}

NodePtr Parser::parseForOfStatement(NodePtr left, SourceLocation start) {
    if (!isForInOfLeft(left.get())) {
        error(left->range().start, "Invalid left-hand side in for-of");
        return nullptr;
    }
    advance();  // 'of'

    // for-of 的右侧是 AssignmentExpression，逗号属于外层分隔。
    NodePtr right = parseAssignmentExpression();
    if (right == nullptr) return nullptr;
    if (!expect(TokenKind::RightParen, "')'")) return nullptr;

    NodePtr body = parseStatement();
    if (body == nullptr) return nullptr;

    return std::make_unique<ForOfStatement>(
        rangeFrom(start), std::move(left), std::move(right), std::move(body));
}

NodePtr Parser::parseDoWhileStatement() {
    const SourceLocation start = current_.start;
    advance();  // 'do'

    NodePtr body = parseStatement();
    if (body == nullptr) return nullptr;

    if (!expect(TokenKind::While, "'while'")) return nullptr;
    if (!expect(TokenKind::LeftParen, "'('")) return nullptr;
    NodePtr test = parseExpression();
    if (test == nullptr) return nullptr;
    if (!expect(TokenKind::RightParen, "')'")) return nullptr;

    // do-while 末尾的分号可由 ASI 无条件插入。
    match(TokenKind::Semicolon);

    return std::make_unique<DoWhileStatement>(rangeFrom(start),
                                              std::move(body), std::move(test));
}

NodePtr Parser::parseWhileStatement() {
    const SourceLocation start = current_.start;
    advance();  // 'while'

    if (!expect(TokenKind::LeftParen, "'('")) return nullptr;
    NodePtr test = parseExpression();
    if (test == nullptr) return nullptr;
    if (!expect(TokenKind::RightParen, "')'")) return nullptr;

    NodePtr body = parseStatement();
    if (body == nullptr) return nullptr;

    return std::make_unique<WhileStatement>(rangeFrom(start), std::move(test),
                                            std::move(body));
}

NodePtr Parser::parseIfStatement() {
    const SourceLocation start = current_.start;
    advance();  // 'if'

    if (!expect(TokenKind::LeftParen, "'('")) return nullptr;
    NodePtr test = parseExpression();
    if (test == nullptr) return nullptr;
    if (!expect(TokenKind::RightParen, "')'")) return nullptr;

    NodePtr consequent = parseStatement();
    if (consequent == nullptr) return nullptr;

    NodePtr alternate;
    if (match(TokenKind::Else)) {
        alternate = parseStatement();
        if (alternate == nullptr) return nullptr;
    }

    return std::make_unique<IfStatement>(
        rangeFrom(start), std::move(test), std::move(consequent),
        std::move(alternate));
}

bool Parser::isLetDeclarationStart() {
    // `let` 是上下文关键字：`let x` 是声明，`let = 1` / `let;` 是标识符。
    const Token next = peek();
    return next.kind == TokenKind::Identifier ||
           next.kind == TokenKind::LeftBracket ||
           next.kind == TokenKind::LeftBrace;
}

NodePtr Parser::parseVariableDeclaration(bool consumeTerminator) {
    const SourceLocation start = current_.start;
    VariableKind kind = VariableKind::Var;
    if (at(TokenKind::Var)) {
        kind = VariableKind::Var;
    } else if (at(TokenKind::Const)) {
        kind = VariableKind::Const;
    } else {
        kind = VariableKind::Let;
    }
    advance();

    NodeList declarations;
    while (true) {
        NodePtr declarator = parseVariableDeclarator();
        if (declarator == nullptr) return nullptr;
        if (kind == VariableKind::Const &&
            static_cast<const VariableDeclarator*>(declarator.get())->init ==
                nullptr) {
            error(declarator->range().start,
                  "Missing initializer in const declaration");
            return nullptr;
        }
        declarations.push_back(std::move(declarator));
        if (!match(TokenKind::Comma)) break;
    }

    if (consumeTerminator && !consumeSemicolon()) return nullptr;
    return std::make_unique<VariableDeclaration>(
        rangeFrom(start), kind, std::move(declarations));
}

NodePtr Parser::parseVariableDeclarator() {
    const SourceLocation start = current_.start;
    NodePtr id = parseIdentifier();
    if (id == nullptr) return nullptr;

    NodePtr init;
    if (match(TokenKind::Assign)) {
        init = parseAssignmentExpression();
        if (init == nullptr) return nullptr;
    }

    return std::make_unique<VariableDeclarator>(rangeFrom(start),
                                                std::move(id), std::move(init));
}

NodePtr Parser::parseBlockStatement() {
    const SourceLocation start = current_.start;
    if (!expect(TokenKind::LeftBrace, "'{'")) return nullptr;

    NodeList body;
    while (!at(TokenKind::RightBrace) && !at(TokenKind::EndOfInput)) {
        NodePtr statement = parseStatement();
        if (statement != nullptr) {
            body.push_back(std::move(statement));
        } else {
            synchronize(true);
        }
    }

    if (!expect(TokenKind::RightBrace, "'}'")) return nullptr;
    return std::make_unique<BlockStatement>(rangeFrom(start), std::move(body));
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
    return parseSequenceExpression();
}

NodePtr Parser::parseSequenceExpression() {
    NodePtr first = parseAssignmentExpression();
    if (first == nullptr) return nullptr;
    if (!at(TokenKind::Comma)) return first;

    const SourceLocation start = first->range().start;
    NodeList expressions;
    expressions.push_back(std::move(first));

    while (match(TokenKind::Comma)) {
        NodePtr next = parseAssignmentExpression();
        if (next == nullptr) return nullptr;
        expressions.push_back(std::move(next));
    }

    return std::make_unique<SequenceExpression>(rangeFrom(start),
                                                std::move(expressions));
}

NodePtr Parser::parseAssignmentExpression() {
    NodePtr left = parseConditionalExpression();
    if (left == nullptr) return nullptr;

    AssignmentOperator op;
    if (!assignmentOperator(current_.kind, op)) return left;

    if (!isSimpleAssignmentTarget(left.get())) {
        error(left->range().start, "Invalid assignment target");
        return nullptr;
    }

    const SourceLocation start = left->range().start;
    advance();
    NodePtr right = parseAssignmentExpression();
    if (right == nullptr) return nullptr;

    return std::make_unique<AssignmentExpression>(
        rangeFrom(start), op, std::move(left), std::move(right));
}

NodePtr Parser::parseConditionalExpression() {
    NodePtr test = parseLogicalExpression(1);
    if (test == nullptr) return nullptr;

    if (!match(TokenKind::Question)) return test;

    const SourceLocation start = test->range().start;
    NodePtr consequent = parseAssignmentExpression();
    if (consequent == nullptr) return nullptr;
    if (!expect(TokenKind::Colon, "':'")) return nullptr;
    NodePtr alternate = parseAssignmentExpression();
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
        if (info.op == BinaryOperator::In && !allowIn_) break;
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
        // 参数之间用逗号分隔，因此每个参数只解析到赋值表达式，不能吞掉逗号。
        NodePtr argument = parseAssignmentExpression();
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
            // 括号重置 for 头部的 [~In] 限制。
            const bool savedAllowIn = allowIn_;
            allowIn_ = true;
            NodePtr expression = parseExpression();
            allowIn_ = savedAllowIn;
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
    if (!at(TokenKind::Identifier)) {
        error(current_.start, "Expected identifier");
        return nullptr;
    }
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
