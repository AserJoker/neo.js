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

bool isPropertyNameStart(TokenKind kind) {
    return isIdentifierName(kind) || kind == TokenKind::StringLiteral ||
           kind == TokenKind::NumericLiteral ||
           kind == TokenKind::BigIntLiteral ||
           kind == TokenKind::LeftBracket;
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
           node->kind() == NodeKind::MemberExpression ||
           node->kind() == NodeKind::ArrayPattern ||
           node->kind() == NodeKind::ObjectPattern;
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
        NodePtr statement;
        if (at(TokenKind::Export)) {
            // export 只允许出现在模块顶层。
            inDirectivePrologue = false;
            statement = parseExportDeclaration();
        } else {
            statement = parseStatementListItem(inDirectivePrologue);
        }
        if (statement == nullptr) {
            inDirectivePrologue = false;
            synchronize();
            continue;
        }
        body.push_back(std::move(statement));
    }

    ParseResult result;
    result.program = std::make_unique<Program>(
        SourceRange{start, previousEnd_}, std::move(body));
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
    current_ = lexer_.next({goal});
    if (current_.kind == TokenKind::Illegal) {
        error(current_.start, current_.text);
    }
}

Token Parser::peek(LexicalGoal goal) {
    const LexerState state = lexer_.save();
    Token token = lexer_.next({goal});
    lexer_.restore(state);
    return token;
}

void Parser::rescan(LexicalGoal goal) {
    lexer_.restore(currentStart_);
    current_ = lexer_.next({goal});
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

    // 严格性不在 Parser 判定；Directive 节点保留给 Sema。
    return parseDirective(std::move(statement));
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
    if (at(TokenKind::Function)) return parseFunctionDeclaration(false);
    if (isAsyncFunctionStart()) return parseFunctionDeclaration(true);
    if (at(TokenKind::Class)) return parseClassDeclaration();
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

bool Parser::isAsyncFunctionStart() {
    // async 是上下文关键字：只有紧跟同一行的 function 才构成异步函数。
    if (!at(TokenKind::Identifier) || current_.text != "async") return false;
    const Token next = peek();
    return next.kind == TokenKind::Function &&
           !next.precededByLineTerminator;
}

NodePtr Parser::parseFunctionDeclaration(bool async) {
    const SourceLocation start = current_.start;
    if (async) advance();  // 'async'
    if (!expect(TokenKind::Function, "'function'")) return nullptr;

    const bool generator = match(TokenKind::Star);

    // 名字是否必需由 Sema 在遍历语法树时判定（普通声明必需，
    // export default 可匿名），Parser 只记录实际出现的名字。
    NodePtr id;
    if (at(TokenKind::Identifier)) {
        id = parseIdentifier();
    }

    NodeList params;
    if (!parseFormalParameters(params)) return nullptr;

    const bool savedAsync = inAsync_;
    const bool savedGenerator = inGenerator_;
    inAsync_ = async;
    inGenerator_ = generator;
    NodePtr body = parseFunctionBody();
    inAsync_ = savedAsync;
    inGenerator_ = savedGenerator;
    if (body == nullptr) return nullptr;
    rescan(LexicalGoal::RegExp);

    return std::make_unique<FunctionDeclaration>(
        rangeFrom(start), std::move(id), std::move(params), std::move(body),
        async, generator);
}

NodePtr Parser::parseFunctionExpression(bool async) {
    const SourceLocation start = current_.start;
    if (async) advance();  // 'async'
    if (!expect(TokenKind::Function, "'function'")) return nullptr;

    const bool generator = match(TokenKind::Star);

    // 函数表达式的名字可选（命名函数表达式）。
    NodePtr id;
    if (at(TokenKind::Identifier)) {
        id = parseIdentifier();
    }

    NodeList params;
    if (!parseFormalParameters(params)) return nullptr;

    const bool savedAsync = inAsync_;
    const bool savedGenerator = inGenerator_;
    inAsync_ = async;
    inGenerator_ = generator;
    NodePtr body = parseFunctionBody();
    inAsync_ = savedAsync;
    inGenerator_ = savedGenerator;
    if (body == nullptr) return nullptr;
    rescan(LexicalGoal::Div);

    return std::make_unique<FunctionExpression>(
        rangeFrom(start), std::move(id), std::move(params), std::move(body),
        async, generator);
}

bool Parser::parseFormalParameters(NodeList& params) {
    if (!expect(TokenKind::LeftParen, "'('")) return false;
    if (match(TokenKind::RightParen)) return true;

    while (true) {
        if (at(TokenKind::Ellipsis)) {
            NodePtr rest = parseRestElement();
            if (rest == nullptr) return false;
            params.push_back(std::move(rest));
            // rest 参数必须是最后一个，且后面不能有逗号。
            if (at(TokenKind::Comma)) {
                error(current_.start, "Rest parameter must be last");
                return false;
            }
            break;
        }

        NodePtr param = parseBindingElement();
        if (param == nullptr) return false;
        params.push_back(std::move(param));

        if (!match(TokenKind::Comma)) break;
        if (at(TokenKind::RightParen)) break;  // 尾随逗号
    }

    return expect(TokenKind::RightParen, "')'");
}

NodePtr Parser::parseRestElement() {
    const SourceLocation start = current_.start;
    if (!expect(TokenKind::Ellipsis, "'...'")) return nullptr;
    NodePtr argument = parseBindingPattern();
    if (argument == nullptr) return nullptr;
    return std::make_unique<RestElement>(rangeFrom(start),
                                         std::move(argument));
}

NodePtr Parser::parseSpreadElement() {
    const SourceLocation start = current_.start;
    if (!expect(TokenKind::Ellipsis, "'...'")) return nullptr;
    NodePtr argument = parseAssignmentExpression();
    if (argument == nullptr) return nullptr;
    return std::make_unique<SpreadElement>(rangeFrom(start),
                                           std::move(argument));
}

NodePtr Parser::parseFunctionBody() {
    const SourceLocation start = current_.start;
    if (!expect(TokenKind::LeftBrace, "'{'")) return nullptr;

    // 函数体有自己的指令序言；严格性由 Sema 根据 Directive 节点判定。
    bool inDirectivePrologue = true;
    NodeList body;
    while (!at(TokenKind::RightBrace) && !at(TokenKind::EndOfInput)) {
        NodePtr statement = parseStatementListItem(inDirectivePrologue);
        if (statement != nullptr) {
            body.push_back(std::move(statement));
        } else {
            synchronize(true);
        }
    }

    if (!expect(TokenKind::RightBrace, "'}'")) return nullptr;
    return std::make_unique<BlockStatement>(rangeFrom(start), std::move(body));
}

// ---------------------------------------------------------------------------
// class
// ---------------------------------------------------------------------------

NodePtr Parser::parseClassDeclaration() {
    const SourceLocation start = current_.start;
    advance();  // 'class'

    // 名字是否必需由 Sema 判定（export default 可匿名）。
    NodePtr id;
    if (at(TokenKind::Identifier)) {
        id = parseIdentifier();
    }

    NodePtr superClass;
    if (match(TokenKind::Extends)) {
        superClass = parseLeftHandSideExpression();
        if (superClass == nullptr) return nullptr;
    }

    NodePtr body = parseClassBody();
    if (body == nullptr) return nullptr;
    rescan(LexicalGoal::RegExp);

    return std::make_unique<ClassDeclaration>(
        rangeFrom(start), std::move(id), std::move(superClass),
        std::move(body));
}

NodePtr Parser::parseClassExpression() {
    const SourceLocation start = current_.start;
    advance();  // 'class'

    NodePtr id;
    if (at(TokenKind::Identifier)) {
        id = parseIdentifier();
    }

    NodePtr superClass;
    if (match(TokenKind::Extends)) {
        superClass = parseLeftHandSideExpression();
        if (superClass == nullptr) return nullptr;
    }

    NodePtr body = parseClassBody();
    if (body == nullptr) return nullptr;
    rescan(LexicalGoal::Div);

    return std::make_unique<ClassExpression>(
        rangeFrom(start), std::move(id), std::move(superClass),
        std::move(body));
}

NodePtr Parser::parseClassBody() {
    const SourceLocation start = current_.start;

    // class 体恒为严格模式代码，由 Sema 根据节点类型判定。
    if (!expect(TokenKind::LeftBrace, "'{'")) return nullptr;

    NodeList elements;
    while (!at(TokenKind::RightBrace) && !at(TokenKind::EndOfInput)) {
        if (match(TokenKind::Semicolon)) continue;
        NodePtr element = parseClassElement();
        if (element == nullptr) return nullptr;
        elements.push_back(std::move(element));
    }

    if (!expect(TokenKind::RightBrace, "'}'")) return nullptr;
    return std::make_unique<ClassBody>(rangeFrom(start), std::move(elements));
}

NodePtr Parser::parseClassKey(bool& computed, bool& identifierKey) {
    if (at(TokenKind::PrivateIdentifier)) {
        const SourceLocation start = current_.start;
        std::string name = current_.text;
        advance();
        computed = false;
        identifierKey = false;
        return std::make_unique<PrivateIdentifier>(rangeFrom(start),
                                                   std::move(name));
    }
    return parsePropertyKey(computed, identifierKey);
}

NodePtr Parser::parseClassElement() {
    const SourceLocation start = current_.start;

    bool isStatic = false;
    if (at(TokenKind::Identifier) && current_.text == "static") {
        const Token next = peek();
        if (next.kind == TokenKind::LeftBrace) {
            error(current_.start, "Static blocks are not supported");
            return nullptr;
        }
        if (next.kind == TokenKind::Star ||
            next.kind == TokenKind::PrivateIdentifier ||
            isPropertyNameStart(next.kind)) {
            isStatic = true;
            advance();  // 'static'
        }
    }

    // 生成器方法：*key() {}
    if (at(TokenKind::Star)) {
        advance();
        return parseClassMethod(start, isStatic, false, true,
                                MethodKind::Method);
    }

    // get/set 访问器
    if (at(TokenKind::Identifier) &&
        (current_.text == "get" || current_.text == "set")) {
        const Token next = peek();
        if (next.kind == TokenKind::PrivateIdentifier ||
            isPropertyNameStart(next.kind)) {
            const bool getter = current_.text == "get";
            advance();  // 'get' / 'set'
            return parseClassMethod(start, isStatic, false, false,
                                    getter ? MethodKind::Get
                                           : MethodKind::Set);
        }
    }

    // async 方法：async key() {} / async *key() {}
    if (at(TokenKind::Identifier) && current_.text == "async") {
        const Token next = peek();
        if (!next.precededByLineTerminator &&
            (next.kind == TokenKind::Star ||
             next.kind == TokenKind::PrivateIdentifier ||
             isPropertyNameStart(next.kind))) {
            advance();  // 'async'
            const bool generator = match(TokenKind::Star);
            return parseClassMethod(start, isStatic, true, generator,
                                    MethodKind::Method);
        }
    }

    bool computed = false;
    bool identifierKey = false;
    NodePtr key = parseClassKey(computed, identifierKey);
    if (key == nullptr) return nullptr;

    if (at(TokenKind::LeftParen)) {
        MethodKind methodKind = MethodKind::Method;
        if (!isStatic && !computed) {
            if ((key->kind() == NodeKind::Identifier &&
                 static_cast<const Identifier&>(*key).name == "constructor") ||
                (key->kind() == NodeKind::StringLiteral &&
                 static_cast<const StringLiteral&>(*key).value ==
                     "constructor")) {
                methodKind = MethodKind::Constructor;
            }
        }
        return parseClassMethodWithKey(start, std::move(key), computed,
                                       isStatic, false, false, methodKind);
    }

    // 字段：key; / key = value;
    NodePtr value;
    if (match(TokenKind::Assign)) {
        // 字段初始化器不在 async/generator 上下文中。
        const bool savedAsync = inAsync_;
        const bool savedGenerator = inGenerator_;
        inAsync_ = false;
        inGenerator_ = false;
        value = parseAssignmentExpression();
        inAsync_ = savedAsync;
        inGenerator_ = savedGenerator;
        if (value == nullptr) return nullptr;
    }
    if (!consumeSemicolon()) return nullptr;

    return std::make_unique<PropertyDefinition>(
        rangeFrom(start), std::move(key), std::move(value), computed, isStatic);
}

NodePtr Parser::parseClassMethod(SourceLocation start, bool isStatic,
                                 bool async, bool generator,
                                 MethodKind methodKind) {
    bool computed = false;
    bool identifierKey = false;
    NodePtr key = parseClassKey(computed, identifierKey);
    if (key == nullptr) return nullptr;
    return parseClassMethodWithKey(start, std::move(key), computed, isStatic,
                                   async, generator, methodKind);
}

NodePtr Parser::parseClassMethodWithKey(SourceLocation start, NodePtr key,
                                        bool computed, bool isStatic,
                                        bool async, bool generator,
                                        MethodKind methodKind) {
    NodeList params;
    if (!parseFormalParameters(params)) return nullptr;

    const bool savedAsync = inAsync_;
    const bool savedGenerator = inGenerator_;
    inAsync_ = async;
    inGenerator_ = generator;
    NodePtr body = parseFunctionBody();
    inAsync_ = savedAsync;
    inGenerator_ = savedGenerator;
    if (body == nullptr) return nullptr;
    rescan(LexicalGoal::Div);

    auto function = std::make_unique<FunctionExpression>(
        rangeFrom(start), nullptr, std::move(params), std::move(body), async,
        generator);
    return std::make_unique<MethodDefinition>(
        rangeFrom(start), std::move(key), std::move(function), computed,
        isStatic, methodKind);
}

NodePtr Parser::parseSuperExpression() {
    const SourceLocation start = current_.start;
    advance();  // 'super'
    return std::make_unique<SuperExpression>(rangeFrom(start));
}

// ---------------------------------------------------------------------------
// 模块
// ---------------------------------------------------------------------------

NodePtr Parser::parseExportDeclaration() {
    const SourceLocation start = current_.start;
    advance();  // 'export'
    if (match(TokenKind::Default)) {
        return parseExportDefaultDeclaration(start);
    }
    return parseExportNamedDeclaration(start);
}

NodePtr Parser::parseExportNamedDeclaration(SourceLocation start) {
    NodePtr declaration;
    if (at(TokenKind::Function)) {
        declaration = parseFunctionDeclaration(false);
    } else if (isAsyncFunctionStart()) {
        declaration = parseFunctionDeclaration(true);
    } else if (at(TokenKind::Class)) {
        declaration = parseClassDeclaration();
    } else {
        error(current_.start,
              "Only function and class declarations are supported in export");
        return nullptr;
    }
    if (declaration == nullptr) return nullptr;

    return std::make_unique<ExportNamedDeclaration>(rangeFrom(start),
                                                    std::move(declaration));
}

NodePtr Parser::parseExportDefaultDeclaration(SourceLocation start) {
    NodePtr declaration;
    if (at(TokenKind::Function)) {
        declaration = parseFunctionDeclaration(false);
    } else if (isAsyncFunctionStart()) {
        declaration = parseFunctionDeclaration(true);
    } else if (at(TokenKind::Class)) {
        declaration = parseClassDeclaration();
    } else {
        error(current_.start,
              "Only function and class declarations are supported in export "
              "default");
        return nullptr;
    }
    if (declaration == nullptr) return nullptr;

    return std::make_unique<ExportDefaultDeclaration>(rangeFrom(start),
                                                      std::move(declaration));
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
        param = parseBindingPattern();
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

        // 不是 for-in/of，const 声明必须有初始化。
        if (declaration) {
            const auto* variableDeclaration =
                static_cast<const VariableDeclaration*>(init.get());
            if (variableDeclaration->variableKind == VariableKind::Const) {
                for (const NodePtr& declarator :
                     variableDeclaration->declarations) {
                    if (static_cast<const VariableDeclarator*>(declarator.get())
                            ->init == nullptr) {
                        error(declarator->range().start,
                              "Missing initializer in const declaration");
                        return nullptr;
                    }
                }
            }
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
    if (left->kind() == NodeKind::ArrayExpression ||
        left->kind() == NodeKind::ObjectExpression) {
        left = convertToAssignmentPattern(std::move(left));
        if (left == nullptr) return nullptr;
    }
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
    if (left->kind() == NodeKind::ArrayExpression ||
        left->kind() == NodeKind::ObjectExpression) {
        left = convertToAssignmentPattern(std::move(left));
        if (left == nullptr) return nullptr;
    }
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
        // for-in/of 头部允许 const 不带初始化，由 parseForStatement 区分。
        if (consumeTerminator && kind == VariableKind::Const &&
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
    NodePtr id = parseBindingPattern();
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

bool Parser::isArrowFunctionStart() {
    if (at(TokenKind::LeftParen)) return parenthesizedArrowAhead();

    if (!at(TokenKind::Identifier)) return false;
    if (current_.text != "async") {
        // 单参数箭头函数：name => ...，=> 前不允许行终止符。
        const Token next = peek();
        return next.kind == TokenKind::Arrow &&
               !next.precededByLineTerminator;
    }

    // async 是上下文关键字：async [no LT] ArrowParameters [no LT] =>。
    const LexerState state = lexer_.save();
    const Token next = lexer_.next({LexicalGoal::Div});
    bool result = false;
    if (!next.precededByLineTerminator) {
        if (next.kind == TokenKind::Identifier) {
            const Token arrow = lexer_.next({LexicalGoal::Div});
            result = arrow.kind == TokenKind::Arrow &&
                     !arrow.precededByLineTerminator;
        } else if (next.kind == TokenKind::LeftParen) {
            result = scanArrowAfterParen();
        }
    }
    lexer_.restore(state);
    return result;
}

bool Parser::parenthesizedArrowAhead() {
    const LexerState state = lexer_.save();
    const bool result = scanArrowAfterParen();
    lexer_.restore(state);
    return result;
}

bool Parser::scanArrowAfterParen() {
    // 调用时 lexer_ 已越过 '('，只需找到配对的 ')' 再看下一个 token。
    int depth = 1;
    while (true) {
        const Token token = lexer_.next({LexicalGoal::Div});
        if (token.kind == TokenKind::EndOfInput ||
            token.kind == TokenKind::Illegal) {
            return false;
        }
        if (token.kind == TokenKind::LeftParen) {
            ++depth;
        } else if (token.kind == TokenKind::RightParen) {
            if (--depth == 0) {
                const Token arrow = lexer_.next({LexicalGoal::Div});
                return arrow.kind == TokenKind::Arrow &&
                       !arrow.precededByLineTerminator;
            }
        }
    }
}

NodePtr Parser::parseArrowFunction() {
    const SourceLocation start = current_.start;
    const bool async = at(TokenKind::Identifier) && current_.text == "async";
    if (async) advance();  // 'async'

    NodeList params;
    if (at(TokenKind::LeftParen)) {
        if (!parseFormalParameters(params)) return nullptr;
    } else {
        NodePtr param = parseIdentifier();
        if (param == nullptr) return nullptr;
        params.push_back(std::move(param));
    }

    if (at(TokenKind::Arrow) && current_.precededByLineTerminator) {
        error(current_.start, "Line terminator before '=>'");
        return nullptr;
    }
    if (!expect(TokenKind::Arrow, "'=>'")) return nullptr;

    const bool savedAsync = inAsync_;
    const bool savedGenerator = inGenerator_;
    inAsync_ = async;
    inGenerator_ = false;

    NodePtr body;
    bool expression = false;
    if (at(TokenKind::LeftBrace)) {
        body = parseFunctionBody();
        if (body == nullptr) {
            inAsync_ = savedAsync;
            inGenerator_ = savedGenerator;
            return nullptr;
        }
        rescan(LexicalGoal::Div);
    } else {
        body = parseAssignmentExpression();
        if (body == nullptr) {
            inAsync_ = savedAsync;
            inGenerator_ = savedGenerator;
            return nullptr;
        }
        expression = true;
    }

    inAsync_ = savedAsync;
    inGenerator_ = savedGenerator;

    return std::make_unique<ArrowFunctionExpression>(
        rangeFrom(start), std::move(params), std::move(body), async, expression);
}

NodePtr Parser::parseAssignmentExpression() {
    if (isArrowFunctionStart()) return parseArrowFunction();

    NodePtr left = parseConditionalExpression();
    if (left == nullptr) return nullptr;

    AssignmentOperator op;
    if (!assignmentOperator(current_.kind, op)) return left;

    // 解构赋值左侧先按字面量解析，再重解释为模式；复合赋值仍只接受简单目标。
    if (op == AssignmentOperator::Assign &&
        (left->kind() == NodeKind::ArrayExpression ||
         left->kind() == NodeKind::ObjectExpression)) {
        left = convertToAssignmentPattern(std::move(left));
        if (left == nullptr) return nullptr;
    } else if (!isSimpleAssignmentTarget(left.get())) {
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
    if (inAsync_ && at(TokenKind::Identifier) && current_.text == "await") {
        return parseAwaitExpression();
    }

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

NodePtr Parser::parseAwaitExpression() {
    const SourceLocation start = current_.start;
    advance();  // 'await'
    NodePtr argument = parseUnaryExpression();
    if (argument == nullptr) return nullptr;
    return std::make_unique<AwaitExpression>(rangeFrom(start),
                                             std::move(argument));
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

    bool optionalChain = false;
    NodePtr expression = parseCallExpression(optionalChain);
    if (expression == nullptr) return nullptr;
    if (optionalChain) {
        return std::make_unique<ChainExpression>(expression->range(),
                                                 std::move(expression));
    }
    return expression;
}

NodePtr Parser::parseMemberExpression(bool& optionalChain) {
    const SourceLocation start = current_.start;
    NodePtr object = parsePrimaryExpression();
    if (object == nullptr) return nullptr;

    while (true) {
        if (at(TokenKind::QuestionDot)) {
            const Token next = peek();
            if (isIdentifierName(next.kind) ||
                next.kind == TokenKind::LeftBracket ||
                next.kind == TokenKind::PrivateIdentifier) {
                optionalChain = true;
                advance();  // '?.'
                object = parseMemberSuffix(std::move(object), start, true);
                if (object == nullptr) return nullptr;
                continue;
            }
            // '?.' 后是 '('：调用后缀，交给 parseCallSuffix。
            break;
        }
        if (at(TokenKind::NoSubstitutionTemplate) ||
            at(TokenKind::TemplateHead)) {
            object = parseTaggedTemplateExpression(std::move(object), start);
            if (object == nullptr) return nullptr;
            continue;
        }
        if (at(TokenKind::Dot) || at(TokenKind::LeftBracket)) {
            object = parseMemberSuffix(std::move(object), start);
            if (object == nullptr) return nullptr;
            continue;
        }
        break;
    }
    return object;
}

NodePtr Parser::parseCallExpression(bool& optionalChain) {
    const SourceLocation start = current_.start;
    NodePtr callee = parseMemberExpression(optionalChain);
    if (callee == nullptr) return nullptr;
    return parseCallSuffix(std::move(callee), start, optionalChain);
}

NodePtr Parser::parseNewExpression() {
    const SourceLocation start = current_.start;
    advance();  // 'new'

    if (at(TokenKind::Dot)) {
        NodePtr meta = parseMetaProperty("new", "target",
                                         SourceRange{start, previousEnd_});
        if (meta == nullptr) return nullptr;
        bool optionalChain = false;
        meta = parseCallSuffix(std::move(meta), start, optionalChain);
        if (meta == nullptr) return nullptr;
        if (optionalChain) {
            return std::make_unique<ChainExpression>(meta->range(),
                                                     std::move(meta));
        }
        return meta;
    }

    bool optionalChain = false;
    NodePtr callee = at(TokenKind::New)
                         ? parseNewExpression()
                         : parseMemberExpression(optionalChain);
    if (callee == nullptr) return nullptr;
    if (optionalChain) {
        error(callee->range().start,
              "Invalid optional chain in new expression");
        return nullptr;
    }

    NodeList arguments;
    if (at(TokenKind::LeftParen) && !parseArguments(arguments)) {
        return nullptr;
    }

    NodePtr expression = std::make_unique<NewExpression>(
        rangeFrom(start), std::move(callee), std::move(arguments));
    // new Foo().bar / new Foo()()：new 表达式之后仍可继续成员与调用后缀。
    expression = parseCallSuffix(std::move(expression), start, optionalChain);
    if (expression == nullptr) return nullptr;
    if (optionalChain) {
        return std::make_unique<ChainExpression>(expression->range(),
                                                 std::move(expression));
    }
    return expression;
}

NodePtr Parser::parseMetaProperty(std::string metaName,
                                  std::string propertyName,
                                  SourceRange metaRange) {
    if (!expect(TokenKind::Dot, "'.'")) return nullptr;
    if (!at(TokenKind::Identifier) || current_.text != propertyName) {
        error(current_.start, "Expected '" + propertyName + "'");
        return nullptr;
    }
    NodePtr property = parseIdentifierName();
    if (property == nullptr) return nullptr;

    NodePtr meta =
        std::make_unique<Identifier>(metaRange, std::move(metaName));
    return std::make_unique<MetaProperty>(rangeFrom(metaRange.start),
                                          std::move(meta),
                                          std::move(property));
}

NodePtr Parser::parseImportExpression(SourceLocation start) {
    if (!expect(TokenKind::LeftParen, "'('")) return nullptr;

    NodePtr source = parseAssignmentExpression();
    if (source == nullptr) return nullptr;

    if (!expect(TokenKind::RightParen, "')'")) return nullptr;
    return std::make_unique<ImportExpression>(rangeFrom(start),
                                              std::move(source));
}

NodePtr Parser::parseMemberSuffix(NodePtr object, SourceLocation start,
                                  bool optional) {
    if (optional) {
        // '?.' 之后：IdentifierName / [expr] / #private
        if (at(TokenKind::LeftBracket)) {
            advance();
            NodePtr property = parseExpression();
            if (property == nullptr) return nullptr;
            if (!expect(TokenKind::RightBracket, "']'")) return nullptr;
            return std::make_unique<MemberExpression>(
                rangeFrom(start), std::move(object), std::move(property), true,
                true);
        }
        if (at(TokenKind::PrivateIdentifier)) {
            const SourceLocation propertyStart = current_.start;
            std::string name = current_.text;
            advance();
            NodePtr property = std::make_unique<PrivateIdentifier>(
                rangeFrom(propertyStart), std::move(name));
            return std::make_unique<MemberExpression>(
                rangeFrom(start), std::move(object), std::move(property), false,
                true);
        }
        NodePtr property = parseIdentifierName();
        if (property == nullptr) return nullptr;
        return std::make_unique<MemberExpression>(
            rangeFrom(start), std::move(object), std::move(property), false,
            true);
    }

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

NodePtr Parser::parseCallSuffix(NodePtr callee, SourceLocation start,
                                bool& optionalChain) {
    while (true) {
        if (at(TokenKind::QuestionDot)) {
            optionalChain = true;
            advance();  // '?.'
            if (at(TokenKind::LeftParen)) {
                NodeList arguments;
                if (!parseArguments(arguments)) return nullptr;
                callee = std::make_unique<CallExpression>(
                    rangeFrom(start), std::move(callee), std::move(arguments),
                    true);
                continue;
            }
            callee = parseMemberSuffix(std::move(callee), start, true);
            if (callee == nullptr) return nullptr;
            continue;
        }
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
        if (at(TokenKind::NoSubstitutionTemplate) ||
            at(TokenKind::TemplateHead)) {
            callee = parseTaggedTemplateExpression(std::move(callee), start);
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
        NodePtr argument;
        if (at(TokenKind::Ellipsis)) {
            argument = parseSpreadElement();
        } else {
            argument = parseAssignmentExpression();
        }
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
    // async function 表达式（async 与 function 之间不允许行终止符）。
    if (at(TokenKind::Identifier) && current_.text == "async") {
        const Token next = peek();
        if (next.kind == TokenKind::Function &&
            !next.precededByLineTerminator) {
            return parseFunctionExpression(true);
        }
    }

    switch (current_.kind) {
        case TokenKind::Identifier:
            return parseIdentifier();
        case TokenKind::This:
            return parseThisExpression();
        case TokenKind::Super:
            return parseSuperExpression();
        case TokenKind::Function:
            return parseFunctionExpression(false);
        case TokenKind::Class:
            return parseClassExpression();
        case TokenKind::Import: {
            const SourceLocation start = current_.start;
            advance();  // 'import'
            if (at(TokenKind::Dot)) {
                return parseMetaProperty("import", "meta",
                                         SourceRange{start, previousEnd_});
            }
            if (at(TokenKind::LeftParen)) {
                return parseImportExpression(start);
            }
            error(start, "Expected import.meta or import(...)");
            return nullptr;
        }
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
        case TokenKind::NoSubstitutionTemplate:
        case TokenKind::TemplateHead:
            return parseTemplateLiteral();
        case TokenKind::LeftBracket:
            return parseArrayExpression();
        case TokenKind::LeftBrace:
            return parseObjectExpression();
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

NodePtr Parser::parseThisExpression() {
    const SourceLocation start = current_.start;
    advance();  // 'this'
    return std::make_unique<ThisExpression>(rangeFrom(start));
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
    const bool legacyLeadingZero = current_.legacyLeadingZero;
    advance();
    return std::make_unique<NumericLiteral>(rangeFrom(start), value,
                                            legacyLeadingZero);
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
    const bool legacyOctalEscape = current_.legacyOctalEscape;
    const bool legacyEightOrNine = current_.legacyEightOrNine;
    advance();
    return std::make_unique<StringLiteral>(rangeFrom(start), std::move(value),
                                           legacyOctalEscape,
                                           legacyEightOrNine);
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

NodePtr Parser::parseTemplateLiteral() {
    const SourceLocation start = current_.start;
    NodeList quasis;
    NodeList expressions;

    if (at(TokenKind::NoSubstitutionTemplate)) {
        const SourceLocation elementStart = current_.start;
        std::string cooked = current_.text;
        advance();
        quasis.push_back(std::make_unique<TemplateElement>(
            rangeFrom(elementStart), std::move(cooked)));
        return std::make_unique<TemplateLiteral>(
            rangeFrom(start), std::move(quasis), std::move(expressions));
    }

    if (!at(TokenKind::TemplateHead)) {
        error(current_.start, "Expected template literal");
        return nullptr;
    }

    {
        const SourceLocation elementStart = current_.start;
        std::string cooked = current_.text;
        advance(LexicalGoal::RegExp);
        quasis.push_back(std::make_unique<TemplateElement>(
            rangeFrom(elementStart), std::move(cooked)));
    }

    while (true) {
        // 模板替换表达式是完整的 Expression，不受外层 [~In] 影响。
        const bool savedAllowIn = allowIn_;
        allowIn_ = true;
        NodePtr expression = parseExpression();
        allowIn_ = savedAllowIn;
        if (expression == nullptr) return nullptr;
        expressions.push_back(std::move(expression));

        if (!at(TokenKind::RightBrace)) {
            error(current_.start, "Expected '}' in template literal");
            return nullptr;
        }
        rescan(LexicalGoal::TemplateContinuation);

        if (at(TokenKind::TemplateMiddle)) {
            const SourceLocation elementStart = current_.start;
            std::string cooked = current_.text;
            advance(LexicalGoal::RegExp);
            quasis.push_back(std::make_unique<TemplateElement>(
                rangeFrom(elementStart), std::move(cooked)));
            continue;
        }
        if (at(TokenKind::TemplateTail)) {
            const SourceLocation elementStart = current_.start;
            std::string cooked = current_.text;
            advance();
            quasis.push_back(std::make_unique<TemplateElement>(
                rangeFrom(elementStart), std::move(cooked)));
            break;
        }

        error(current_.start, "Expected template continuation");
        return nullptr;
    }

    return std::make_unique<TemplateLiteral>(
        rangeFrom(start), std::move(quasis), std::move(expressions));
}

NodePtr Parser::parseTaggedTemplateExpression(NodePtr tag,
                                              SourceLocation start) {
    NodePtr quasi = parseTemplateLiteral();
    if (quasi == nullptr) return nullptr;
    return std::make_unique<TaggedTemplateExpression>(
        rangeFrom(start), std::move(tag), std::move(quasi));
}

NodePtr Parser::parseArrayExpression() {
    const SourceLocation start = current_.start;
    if (!expect(TokenKind::LeftBracket, "'['")) return nullptr;

    NodeList elements;
    while (!at(TokenKind::RightBracket) && !at(TokenKind::EndOfInput)) {
        if (at(TokenKind::Comma)) {
            // elision：连续逗号之间的空位用空指针表示。
            elements.push_back(nullptr);
            advance();
            continue;
        }

        NodePtr element;
        if (at(TokenKind::Ellipsis)) {
            element = parseSpreadElement();
        } else {
            element = parseAssignmentExpression();
        }
        if (element == nullptr) return nullptr;
        elements.push_back(std::move(element));

        if (!match(TokenKind::Comma)) break;
    }

    if (!expect(TokenKind::RightBracket, "']'")) return nullptr;
    return std::make_unique<ArrayExpression>(rangeFrom(start),
                                             std::move(elements));
}

NodePtr Parser::parseObjectExpression() {
    const SourceLocation start = current_.start;
    if (!expect(TokenKind::LeftBrace, "'{'")) return nullptr;

    NodeList properties;
    while (!at(TokenKind::RightBrace) && !at(TokenKind::EndOfInput)) {
        NodePtr property;
        if (at(TokenKind::Ellipsis)) {
            property = parseSpreadElement();
        } else {
            property = parseProperty();
        }
        if (property == nullptr) return nullptr;
        properties.push_back(std::move(property));

        if (!match(TokenKind::Comma)) break;
    }

    if (!expect(TokenKind::RightBrace, "'}'")) return nullptr;
    return std::make_unique<ObjectExpression>(rangeFrom(start),
                                              std::move(properties));
}

NodePtr Parser::parsePropertyKey(bool& computed, bool& identifierKey) {
    computed = false;
    identifierKey = false;

    if (at(TokenKind::LeftBracket)) {
        computed = true;
        advance();  // '['
        NodePtr key = parseAssignmentExpression();
        if (key == nullptr) return nullptr;
        if (!expect(TokenKind::RightBracket, "']'")) return nullptr;
        return key;
    }
    if (at(TokenKind::StringLiteral)) return parseStringLiteral();
    if (at(TokenKind::NumericLiteral)) return parseNumericLiteral();
    if (at(TokenKind::BigIntLiteral)) return parseBigIntLiteral();

    // 非计算键可以是任意 IdentifierName（含保留字）。
    identifierKey = at(TokenKind::Identifier);
    return parseIdentifierName();
}

NodePtr Parser::parseMethodProperty(SourceLocation start, bool async,
                                    bool generator,
                                    PropertyKind propertyKind) {
    bool computed = false;
    bool identifierKey = false;
    NodePtr key = parsePropertyKey(computed, identifierKey);
    if (key == nullptr) return nullptr;
    return parseMethodPropertyWithKey(start, std::move(key), computed, async,
                                      generator, propertyKind);
}

NodePtr Parser::parseMethodPropertyWithKey(SourceLocation start, NodePtr key,
                                           bool computed, bool async,
                                           bool generator,
                                           PropertyKind propertyKind) {
    NodeList params;
    if (!parseFormalParameters(params)) return nullptr;

    const bool savedAsync = inAsync_;
    const bool savedGenerator = inGenerator_;
    inAsync_ = async;
    inGenerator_ = generator;
    NodePtr body = parseFunctionBody();
    inAsync_ = savedAsync;
    inGenerator_ = savedGenerator;
    if (body == nullptr) return nullptr;
    rescan(LexicalGoal::Div);

    auto function = std::make_unique<FunctionExpression>(
        rangeFrom(start), nullptr, std::move(params), std::move(body), async,
        generator);
    return std::make_unique<Property>(rangeFrom(start), std::move(key),
                                      std::move(function), computed, false,
                                      true, propertyKind);
}

NodePtr Parser::parseProperty() {
    const SourceLocation start = current_.start;

    // 生成器方法：*key() {}
    if (at(TokenKind::Star)) {
        advance();
        return parseMethodProperty(start, false, true, PropertyKind::Init);
    }

    // get/set 访问器：get key() {} / set key(v) {}
    if (at(TokenKind::Identifier) &&
        (current_.text == "get" || current_.text == "set")) {
        const Token next = peek();
        if (isPropertyNameStart(next.kind)) {
            const bool getter = current_.text == "get";
            advance();  // 'get' / 'set'
            return parseMethodProperty(start, false, false,
                                       getter ? PropertyKind::Get
                                              : PropertyKind::Set);
        }
    }

    // async 方法：async key() {} / async *key() {}
    if (at(TokenKind::Identifier) && current_.text == "async") {
        const Token next = peek();
        if (!next.precededByLineTerminator &&
            (next.kind == TokenKind::Star ||
             isPropertyNameStart(next.kind))) {
            advance();  // 'async'
            const bool generator = match(TokenKind::Star);
            return parseMethodProperty(start, true, generator,
                                       PropertyKind::Init);
        }
    }

    bool computed = false;
    bool identifierKey = false;
    NodePtr key = parsePropertyKey(computed, identifierKey);
    if (key == nullptr) return nullptr;

    if (match(TokenKind::Colon)) {
        NodePtr value = parseAssignmentExpression();
        if (value == nullptr) return nullptr;
        return std::make_unique<Property>(rangeFrom(start), std::move(key),
                                          std::move(value), computed, false);
    }

    // 方法简写：key() {}
    if (at(TokenKind::LeftParen)) {
        return parseMethodPropertyWithKey(start, std::move(key), computed,
                                          false, false, PropertyKind::Init);
    }

    // 简写 {a}：只有普通标识符可以简写，保留字/字面量键不行。
    // `{a = 1}` 先按 cover grammar 接受，只有转换为赋值模式时才合法。
    if (identifierKey && (at(TokenKind::Comma) || at(TokenKind::RightBrace) ||
                          at(TokenKind::Assign))) {
        const auto* identifier = static_cast<const Identifier*>(key.get());
        const SourceLocation valueStart = identifier->range().start;
        NodePtr value = std::make_unique<Identifier>(identifier->range(),
                                                     identifier->name);
        if (match(TokenKind::Assign)) {
            NodePtr right = parseAssignmentExpression();
            if (right == nullptr) return nullptr;
            value = std::make_unique<AssignmentExpression>(
                rangeFrom(valueStart), AssignmentOperator::Assign,
                std::move(value), std::move(right));
        }
        return std::make_unique<Property>(rangeFrom(start), std::move(key),
                                          std::move(value), false, true);
    }

    error(current_.start, "Expected ':'");
    return nullptr;
}

// ---------------------------------------------------------------------------
// 模式
// ---------------------------------------------------------------------------

NodePtr Parser::parseBindingPattern() {
    if (at(TokenKind::LeftBrace)) return parseObjectPattern();
    if (at(TokenKind::LeftBracket)) return parseArrayPattern();
    return parseIdentifier();
}

NodePtr Parser::parseBindingElement() {
    const SourceLocation start = current_.start;
    NodePtr target = parseBindingPattern();
    if (target == nullptr) return nullptr;

    if (match(TokenKind::Assign)) {
        NodePtr right = parseAssignmentExpression();
        if (right == nullptr) return nullptr;
        return std::make_unique<AssignmentPattern>(
            rangeFrom(start), std::move(target), std::move(right));
    }
    return target;
}

NodePtr Parser::parseObjectPattern() {
    const SourceLocation start = current_.start;
    if (!expect(TokenKind::LeftBrace, "'{'")) return nullptr;

    NodeList properties;
    while (!at(TokenKind::RightBrace) && !at(TokenKind::EndOfInput)) {
        if (at(TokenKind::Ellipsis)) {
            NodePtr rest = parseRestElement();
            if (rest == nullptr) return nullptr;
            const auto* restElement = static_cast<const RestElement*>(rest.get());
            if (restElement->argument->kind() != NodeKind::Identifier) {
                error(restElement->argument->range().start,
                      "Rest property must be an identifier");
                return nullptr;
            }
            properties.push_back(std::move(rest));
            if (at(TokenKind::Comma)) {
                error(current_.start, "Rest property must be last");
                return nullptr;
            }
            break;
        }

        NodePtr property = parseObjectPatternProperty();
        if (property == nullptr) return nullptr;
        properties.push_back(std::move(property));

        if (!match(TokenKind::Comma)) break;
    }

    if (!expect(TokenKind::RightBrace, "'}'")) return nullptr;
    return std::make_unique<ObjectPattern>(rangeFrom(start),
                                           std::move(properties));
}

NodePtr Parser::parseObjectPatternProperty() {
    const SourceLocation start = current_.start;

    bool computed = false;
    bool identifierKey = false;
    NodePtr key;

    if (at(TokenKind::LeftBracket)) {
        computed = true;
        advance();  // '['
        key = parseAssignmentExpression();
        if (key == nullptr) return nullptr;
        if (!expect(TokenKind::RightBracket, "']'")) return nullptr;
    } else if (at(TokenKind::StringLiteral)) {
        key = parseStringLiteral();
    } else if (at(TokenKind::NumericLiteral)) {
        key = parseNumericLiteral();
    } else if (at(TokenKind::BigIntLiteral)) {
        key = parseBigIntLiteral();
    } else {
        identifierKey = at(TokenKind::Identifier);
        key = parseIdentifierName();
        if (key == nullptr) return nullptr;
    }

    if (match(TokenKind::Colon)) {
        NodePtr value = parseBindingElement();
        if (value == nullptr) return nullptr;
        return std::make_unique<Property>(rangeFrom(start), std::move(key),
                                          std::move(value), computed, false);
    }

    if (identifierKey && (at(TokenKind::Comma) || at(TokenKind::RightBrace) ||
                          at(TokenKind::Assign))) {
        const auto* identifier = static_cast<const Identifier*>(key.get());
        const SourceLocation valueStart = identifier->range().start;
        NodePtr value = std::make_unique<Identifier>(identifier->range(),
                                                     identifier->name);
        if (match(TokenKind::Assign)) {
            NodePtr right = parseAssignmentExpression();
            if (right == nullptr) return nullptr;
            value = std::make_unique<AssignmentPattern>(
                rangeFrom(valueStart), std::move(value), std::move(right));
        }
        return std::make_unique<Property>(rangeFrom(start), std::move(key),
                                          std::move(value), false, true);
    }

    error(current_.start, "Expected ':'");
    return nullptr;
}

NodePtr Parser::parseArrayPattern() {
    const SourceLocation start = current_.start;
    if (!expect(TokenKind::LeftBracket, "'['")) return nullptr;

    NodeList elements;
    while (!at(TokenKind::RightBracket) && !at(TokenKind::EndOfInput)) {
        if (at(TokenKind::Comma)) {
            elements.push_back(nullptr);
            advance();
            continue;
        }

        const bool rest = at(TokenKind::Ellipsis);
        NodePtr element = rest ? parseRestElement() : parseBindingElement();
        if (element == nullptr) return nullptr;
        elements.push_back(std::move(element));

        if (rest) {
            if (at(TokenKind::Comma)) {
                error(current_.start, "Rest element must be last");
                return nullptr;
            }
            break;
        }

        if (!match(TokenKind::Comma)) break;
    }

    if (!expect(TokenKind::RightBracket, "']'")) return nullptr;
    return std::make_unique<ArrayPattern>(rangeFrom(start),
                                          std::move(elements));
}

NodePtr Parser::convertToAssignmentPattern(NodePtr node) {
    switch (node->kind()) {
        case NodeKind::Identifier:
        case NodeKind::MemberExpression:
            return node;

        case NodeKind::ArrayExpression: {
            auto* array = static_cast<ArrayExpression*>(node.get());
            NodeList elements;
            for (size_t i = 0; i < array->elements.size(); ++i) {
                NodePtr& element = array->elements[i];
                if (element == nullptr) {
                    elements.push_back(nullptr);
                    continue;
                }
                if (element->kind() == NodeKind::SpreadElement) {
                    if (i + 1 != array->elements.size()) {
                        error(element->range().start,
                              "Rest element must be last");
                        return nullptr;
                    }
                    auto* spread = static_cast<SpreadElement*>(element.get());
                    NodePtr argument =
                        convertToAssignmentPattern(std::move(spread->argument));
                    if (argument == nullptr) return nullptr;
                    elements.push_back(std::make_unique<RestElement>(
                        element->range(), std::move(argument)));
                    continue;
                }
                NodePtr converted =
                    convertToAssignmentPattern(std::move(element));
                if (converted == nullptr) return nullptr;
                elements.push_back(std::move(converted));
            }
            return std::make_unique<ArrayPattern>(node->range(),
                                                  std::move(elements));
        }

        case NodeKind::ObjectExpression: {
            auto* object = static_cast<ObjectExpression*>(node.get());
            NodeList properties;
            for (size_t i = 0; i < object->properties.size(); ++i) {
                NodePtr& property = object->properties[i];
                if (property->kind() == NodeKind::SpreadElement) {
                    if (i + 1 != object->properties.size()) {
                        error(property->range().start,
                              "Rest property must be last");
                        return nullptr;
                    }
                    auto* spread = static_cast<SpreadElement*>(property.get());
                    NodePtr argument =
                        convertToAssignmentPattern(std::move(spread->argument));
                    if (argument == nullptr) return nullptr;
                    properties.push_back(std::make_unique<RestElement>(
                        property->range(), std::move(argument)));
                    continue;
                }

                auto* entry = static_cast<Property*>(property.get());
                if (entry->method ||
                    entry->propertyKind != PropertyKind::Init) {
                    error(entry->range().start, "Invalid assignment target");
                    return nullptr;
                }
                NodePtr value =
                    convertToAssignmentPattern(std::move(entry->value));
                if (value == nullptr) return nullptr;
                properties.push_back(std::make_unique<Property>(
                    entry->range(), std::move(entry->key), std::move(value),
                    entry->computed, entry->shorthand));
            }
            return std::make_unique<ObjectPattern>(node->range(),
                                                   std::move(properties));
        }

        case NodeKind::AssignmentExpression: {
            auto* assignment = static_cast<AssignmentExpression*>(node.get());
            if (assignment->op != AssignmentOperator::Assign) {
                error(node->range().start, "Invalid assignment target");
                return nullptr;
            }
            NodePtr left =
                convertToAssignmentPattern(std::move(assignment->left));
            if (left == nullptr) return nullptr;
            return std::make_unique<AssignmentPattern>(
                node->range(), std::move(left), std::move(assignment->right));
        }

        default:
            error(node->range().start, "Invalid assignment target");
            return nullptr;
    }
}

}  // namespace neo::frontend
