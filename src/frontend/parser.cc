#include "neojs/frontend/parser.h"

#include <string>

namespace neojs {

namespace {

/* 判断当前 token 是否为给定符号文本（如 "+"、"("） */
bool isSymbol(const Token &tok, const char *sym) {
  return tok.kind == TokenKind::Symbol && tok.text == sym;
}

/* 二元运算符优先级（M1.1 最小集）：* / % > + -（左结合） */
int binopPrecedence(const Token &tok) {
  if (tok.kind != TokenKind::Symbol) return 0;
  const std::string &s = tok.text;
  if (s == "*" || s == "/" || s == "%") return 2;
  if (s == "+" || s == "-") return 1;
  return 0;
}

BinaryOpKind toBinop(const std::string &s) {
  if (s == "+") return BinaryOpKind::Add;
  if (s == "-") return BinaryOpKind::Sub;
  if (s == "*") return BinaryOpKind::Mul;
  if (s == "/") return BinaryOpKind::Div;
  if (s == "%") return BinaryOpKind::Mod;
  return BinaryOpKind::Add; /* unreachable */
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
  if (at(TokenKind::Symbol) && current().text == ";") advance();
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
    const Token &tok = current();
    int prec = binopPrecedence(tok);
    if (prec == 0 || prec < min_prec) break;
    BinaryOpKind bop = toBinop(tok.text);
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
    case TokenKind::Symbol:
      if (isSymbol(tok, "(")) {
        advance();
        ASTNode *inner = parseExpression();
        if (inner == nullptr) return nullptr;
        if (!isSymbol(current(), ")")) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ')'", tok.start});
          allocator_->destroy(inner);
          return nullptr;
        }
        advance();
        return inner;
      }
      break;
    default:
      break;
  }
  sink_.emit({Severity::Error, "SYNTAX_ERROR", "unexpected token", tok.start});
  return nullptr;
}

}  // namespace neojs
