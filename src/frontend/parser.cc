#include "neojs/frontend/parser.h"

namespace neojs {

namespace {

/* 二元运算符优先级（M1.1 最小集）：* / % > + -（左结合） */
int binopPrecedence(TokenKind k) {
  switch (k) {
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
      return 2;
    case TokenKind::Plus:
    case TokenKind::Minus:
      return 1;
    default:
      return 0;
  }
}

BinaryOpKind toBinop(TokenKind k) {
  switch (k) {
    case TokenKind::Plus:
      return BinaryOpKind::Add;
    case TokenKind::Minus:
      return BinaryOpKind::Sub;
    case TokenKind::Star:
      return BinaryOpKind::Mul;
    case TokenKind::Slash:
      return BinaryOpKind::Div;
    case TokenKind::Percent:
      return BinaryOpKind::Mod;
    default:
      return BinaryOpKind::Add; /* unreachable */
  }
}

}  // namespace

Parser::Parser(JSAllocator *allocator, const char *source, size_t source_len,
               DiagnosticSink &sink)
    : allocator_(allocator), lexer_(source, source_len, sink), sink_(sink) {}

ASTNode *Parser::newNode(NodeType t) {
  return allocator_->create<ASTNode>(t);
}

AST *Parser::parse() {
  AST *ast = allocator_->create<AST>();
  ast->allocator = allocator_;
  ast->root = parseStatementList();
  if (ast->root == nullptr || sink_.hasErrors()) {
    allocator_->destroy(ast);
    return nullptr;
  }
  return ast;
}

/* Program → 表达式语句列表（M1.1：语句仅表达式语句 + 分号） */
ASTNode *Parser::parseStatementList() {
  ASTNode *program = newNode(NodeType::Program);
  advance(); /* 初始化当前 token */
  while (!atEof()) {
    ASTNode *stmt = parseExpressionStatement();
    if (stmt == nullptr) {
      /* 语法错误：sink 已记录；丢弃整个 Program */
      allocator_->destroy(program);
      return nullptr;
    }
    program->children.push_back(stmt);
  }
  return program;
}

/* 表达式语句：表达式 + 可选分号 */
ASTNode *Parser::parseExpressionStatement() {
  ASTNode *expr = parseExpression();
  if (expr == nullptr) return nullptr;
  ASTNode *stmt = newNode(NodeType::ExprStmt);
  stmt->children.push_back(expr);
  if (at(TokenKind::Semicolon)) advance();
  return stmt;
}

/* 表达式 → 二元运算（Pratt 风格最小版；M1.4 扩充） */
ASTNode *Parser::parseExpression() {
  return parseBinary(1);
}

ASTNode *Parser::parseBinary(int min_prec) {
  /* 读值位置：设置 Lexer 上下文（供 / 判定，M1.3 展开） */
  setValueContext();
  ASTNode *lhs = parsePrimary();
  if (lhs == nullptr) return nullptr;

  for (;;) {
    /* 读操作位置：设置 Lexer 上下文（供 / 判定，M1.3 展开） */
    setOperatorContext();
    TokenKind op = current().kind;
    int prec = binopPrecedence(op);
    if (prec == 0 || prec < min_prec) break;
    BinaryOpKind bop = toBinop(op);
    advance();
    ASTNode *rhs = parseBinary(prec + 1); /* 左结合 */
    if (rhs == nullptr) {
      allocator_->destroy(lhs);
      return nullptr;
    }
    ASTNode *node = newNode(NodeType::BinaryOp);
    node->loc = lhs->loc;
    node->binop = bop;
    node->children.push_back(lhs);
    node->children.push_back(rhs);
    lhs = node;
  }
  return lhs;
}

/* 最小 primary：数字 / 标识符 / ( 表达式 ) */
ASTNode *Parser::parsePrimary() {
  setValueContext();
  const Token &tok = current();
  switch (tok.kind) {
    case TokenKind::Number: {
      ASTNode *n = newNode(NodeType::Literal);
      n->loc = tok.start;
      n->literal.number = tok.number;
      n->literal.is_number = true;
      n->literal.text = tok.text;
      advance();
      return n;
    }
    case TokenKind::Identifier: {
      ASTNode *n = newNode(NodeType::Identifier);
      n->loc = tok.start;
      n->name = tok.text;
      advance();
      return n;
    }
    case TokenKind::LParen: {
      advance();
      ASTNode *inner = parseExpression();
      if (inner == nullptr) return nullptr;
      if (!at(TokenKind::RParen)) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ')'", tok.start});
        allocator_->destroy(inner);
        return nullptr;
      }
      advance();
      return inner;
    }
    default:
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unexpected token", tok.start});
      return nullptr;
  }
}

}  // namespace neojs
