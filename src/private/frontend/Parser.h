// frontend/Parser.h - 语法分析器框架
//
// Parser 持有 Lexer，按需拉取 token；词法目标（正则/除法、模板续段）由
// 当前语法位置决定。
//
// 约定：**一个节点一个 parse 函数**。分派函数（parseStatement /
// parsePrimaryExpression）只做 token → 具体 parse 函数的路由，不构造节点；
// 具体 parse 函数只负责该节点的语法、子节点与 SourceRange。
//
// 词法目标切换：
//   - 表达式起始位置用 LexicalGoal::RegExp 取 token；
//   - 运算符位置用 LexicalGoal::Div；
//   - 若当前 token 已按 Div 扫成 `/`，进入表达式起始位置时用 rescan() 重扫。
//
// 错误处理：解析函数失败返回 nullptr 并记录 ParseError；parseProgram 负责
// 同步到语句边界后继续，最终返回带错误的 Program（便于 IDE/诊断）。
//
// 关联 ADR: 001（前端架构）、054（词法器）、055（Parser 与 AST）

#ifndef NEO_FRONTEND_PARSER_H
#define NEO_FRONTEND_PARSER_H

#include "frontend/Lexer.h"
#include "frontend/Node.h"

#include <string>
#include <string_view>
#include <vector>

namespace neo::frontend {

struct ParseError {
    SourceLocation location;
    std::string message;
};

struct ParseResult {
    NodePtr program;
    std::vector<ParseError> errors;

    bool ok() const { return program != nullptr && errors.empty(); }
};

class Parser {
public:
    explicit Parser(std::string_view source);

    ParseResult parseProgram();

private:
    const Token& current() const { return current_; }
    bool at(TokenKind kind) const { return current_.kind == kind; }

    bool match(TokenKind kind);
    bool expect(TokenKind kind, std::string_view what);
    bool consumeSemicolon();

    // 消费当前 token，并按 goal 扫描下一个。
    void advance(LexicalGoal goal = LexicalGoal::Div);
    // 不改变游标地前瞻下一个 token。
    Token peek(LexicalGoal goal = LexicalGoal::Div);
    // 用新的词法目标重扫当前 token（例如 `/` 从除法改为正则）。
    void rescan(LexicalGoal goal);

    void error(SourceLocation location, std::string message);
    // stopAtRightBrace：块内恢复时停在 `}`，交给块解析消费。
    void synchronize(bool stopAtRightBrace = false);

    SourceRange rangeFrom(SourceLocation start) const {
        return SourceRange{start, previousEnd_};
    }

    // 语句
    NodePtr parseStatementListItem(bool& inDirectivePrologue);
    NodePtr parseDirective(NodePtr statement);
    NodePtr parseStatement();
    NodePtr parseBlockStatement();
    NodePtr parseEmptyStatement();
    NodePtr parseExpressionStatement();
    NodePtr parseVariableDeclaration(bool consumeTerminator = true);
    NodePtr parseVariableDeclarator();
    NodePtr parseIfStatement();
    NodePtr parseWhileStatement();
    NodePtr parseDoWhileStatement();
    NodePtr parseForStatement();
    NodePtr parseForInStatement(NodePtr left, SourceLocation start);
    NodePtr parseForOfStatement(NodePtr left, SourceLocation start);
    NodePtr parseReturnStatement();
    NodePtr parseThrowStatement();
    NodePtr parseBreakStatement();
    NodePtr parseContinueStatement();
    NodePtr parseTryStatement();
    NodePtr parseCatchClause();
    NodePtr parseSwitchStatement();
    NodePtr parseSwitchCase(bool isDefault);
    NodePtr parseLabeledStatement();
    NodePtr parseWithStatement();
    NodePtr parseDebuggerStatement();
    NodePtr parseFunctionDeclaration(bool async);
    NodePtr parseFunctionBody();
    bool parseFormalParameters(NodeList& params);
    NodePtr parseRestElement();
    bool isLetDeclarationStart();
    bool isAsyncFunctionStart();

    // 模块
    NodePtr parseExportDeclaration();
    NodePtr parseExportNamedDeclaration(SourceLocation start);
    NodePtr parseExportDefaultDeclaration(SourceLocation start);

    // 表达式
    NodePtr parseExpression();
    NodePtr parseSequenceExpression();
    NodePtr parseAssignmentExpression();
    NodePtr parseArrowFunction();
    bool isArrowFunctionStart();
    bool parenthesizedArrowAhead();
    bool scanArrowAfterParen();
    NodePtr parseConditionalExpression();
    NodePtr parseLogicalExpression(int minPrecedence);
    NodePtr parseBinaryExpression(int minPrecedence);
    NodePtr parseUnaryExpression();
    NodePtr parseUpdateExpression();
    NodePtr parseLeftHandSideExpression();
    NodePtr parseMemberExpression();
    NodePtr parseCallExpression();
    NodePtr parseNewExpression();
    NodePtr parsePrimaryExpression();

    // 左值后缀与参数（辅助函数，不单独对应节点）
    NodePtr parseMemberSuffix(NodePtr object, SourceLocation start);
    NodePtr parseCallSuffix(NodePtr callee, SourceLocation start);
    bool parseArguments(NodeList& arguments);
    NodePtr parseIdentifierName();

    NodePtr parseIdentifier();
    NodePtr parseNumericLiteral();
    NodePtr parseBigIntLiteral();
    NodePtr parseStringLiteral();
    NodePtr parseBooleanLiteral();
    NodePtr parseNullLiteral();
    NodePtr parseRegExpLiteral();

    Lexer lexer_;
    Token current_;
    LexerState currentStart_;
    SourceLocation previousEnd_;
    bool strict_ = false;
    // for 头部禁用 in 作为二元运算符，避免把 for-in 的 in 吃掉。
    bool allowIn_ = true;
    std::vector<ParseError> errors_;
};

}  // namespace neo::frontend

#endif  // NEO_FRONTEND_PARSER_H
