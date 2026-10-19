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

/* 最小 primary：字面量 / 标识符 / 私有字段 / 对象/数组 / ( 表达式 ) */
ASTNode *Parser::parsePrimary() {
  setValueContext();
  /* 读值位置的 / 是正则（re-scan 规则）：请求 Lexer 重扫当前 Symbol '/' */
  if (current().kind == TokenKind::Symbol && current().text == "/") {
    lexer_.rescanAsRegex();
  }
  const Token &tok = current();
  switch (tok.kind) {
    case TokenKind::Number:
    case TokenKind::String:
    case TokenKind::RegExp:
    case TokenKind::TemplateStart:
      return parseLiteral();
    case TokenKind::TemplatePart:
    case TokenKind::TemplateEnd: {
      /* 顶层不应直接出现续段（须由插值表达式驱动，M1.3）；此处防呆 */
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unexpected template continuation", tok.start});
      return nullptr;
    }
    case TokenKind::Identifier: {
      /* 关键字字面量：true / false / null */
      if (tok.text == "true" || tok.text == "false" || tok.text == "null")
        return parseLiteral();
      ASTNode *n = newNode(NodeType::Identifier);
      n->loc = tok.start;
      n->name = tok.text;
      advance();
      return n;
    }
    case TokenKind::Symbol:
      if (isSymbol(tok, "#")) {
        /* 私有字段：#name */
        advance();
        if (!at(TokenKind::Identifier)) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected private identifier", tok.start});
          return nullptr;
        }
        ASTNode *n = newNode(NodeType::PrivateIdentifier);
        n->loc = tok.start;
        n->name = current().text;
        advance();
        return n;
      }
      if (isSymbol(tok, "{")) {
        /* 读值位置 { → 对象字面量 / 解构模式（ESTree 中结构一致） */
        return parseObjectPattern();
      }
      if (isSymbol(tok, "[")) {
        return parseArrayPattern();
      }
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

/* 字面量：数字 / 字符串 / 布尔 / null / 正则 / 无插值模板串 */
ASTNode *Parser::parseLiteral() {
  const Token &tok = current();
  switch (tok.kind) {
    case TokenKind::Number: {
      ASTNode *n = newNode(NodeType::Literal);
      n->loc = tok.start;
      n->literal.number = tok.number;
      n->literal.is_number = true;
      n->literal.is_bigint = tok.is_bigint;
      n->literal.text = tok.text;
      advance();
      return n;
    }
    case TokenKind::String: {
      ASTNode *n = newNode(NodeType::Literal);
      n->loc = tok.start;
      n->literal.is_string = true;
      n->literal.str = tok.str_value;
      n->literal.text = tok.text;
      advance();
      return n;
    }
    case TokenKind::RegExp: {
      ASTNode *n = newNode(NodeType::RegExpLiteral);
      n->loc = tok.start;
      /* text 形如 /pattern/flags：去首尾斜杠，flags 取最后一段 */
      const std::string &s = tok.text;
      size_t last_slash = s.rfind('/');
      if (last_slash != std::string::npos && last_slash > 0) {
        n->regexp.pattern = s.substr(1, last_slash - 1);
        n->regexp.flags = s.substr(last_slash + 1);
      }
      advance();
      return n;
    }
    case TokenKind::TemplateStart: {
      if (tok.has_interpolation) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "template interpolation not yet supported",
                    tok.start});
        return nullptr;
      }
      ASTNode *n = newNode(NodeType::Literal);
      n->loc = tok.start;
      n->literal.is_string = true;
      n->literal.str = tok.str_value;
      n->literal.text = tok.text;
      advance();
      return n;
    }
    case TokenKind::Identifier: {
      /* true / false / null */
      ASTNode *n = newNode(NodeType::Literal);
      n->loc = tok.start;
      if (tok.text == "true" || tok.text == "false") {
        n->literal.is_boolean = true;
        n->literal.bool_value = (tok.text == "true");
      } else { /* null */
        n->literal.is_null = true;
      }
      n->literal.text = tok.text;
      advance();
      return n;
    }
    default:
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unexpected token", tok.start});
      return nullptr;
  }
}

/* 对象模式（字面量/解构共用）：{ key: value, ...spread, shorthand } */
ASTNode *Parser::parseObjectPattern() {
  ASTNode *obj = newNode(NodeType::ObjectPattern);
  obj->loc = current().start;
  advance(); /* { */
  while (!isSymbol(current(), "}")) {
    if (atEof()) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated object literal", obj->loc});
      allocator_->destroy(obj);
      return nullptr;
    }
    if (isSymbol(current(), ",")) {
      advance();
      continue;
    }
    if (isSymbol(current(), "...")) {
      /* SpreadElement：...expr */
      ASTNode *spread = newNode(NodeType::SpreadElement);
      spread->loc = current().start;
      advance();
      ASTNode *arg = parseExpression();
      if (arg == nullptr) {
        allocator_->destroy(obj);
        return nullptr;
      }
      spread->children.push_back(arg);
      obj->children.push_back(spread);
      continue;
    }
    /* 成员：key: value（或 get/set/method——M1.5 再扩展） */
    ASTNode *prop = newNode(NodeType::Property);
    prop->loc = current().start;
    /* key */
    if (at(TokenKind::Identifier)) {
      ASTNode *key = newNode(NodeType::Identifier);
      key->loc = current().start;
      key->name = current().text;
      advance();
      /* 简写 { key }：无冒号 */
      if (isSymbol(current(), ":")) {
        prop->prop.kind = PropertyKind::Init;
        advance();
        ASTNode *val = parseExpression();
        if (val == nullptr) {
          allocator_->destroy(prop);
          allocator_->destroy(obj);
          return nullptr;
        }
        prop->children.push_back(key);
        prop->children.push_back(val);
      } else {
        prop->prop.kind = PropertyKind::Shorthand;
        prop->children.push_back(key);
      }
    } else if (at(TokenKind::String) || at(TokenKind::Number)) {
      ASTNode *key = newNode(NodeType::Literal);
      key->loc = current().start;
      key->literal.is_string = true;
      key->literal.str = current().str_value;
      key->literal.text = current().text;
      advance();
      if (!isSymbol(current(), ":")) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ':' after property key", prop->loc});
        allocator_->destroy(prop);
        allocator_->destroy(obj);
        return nullptr;
      }
      advance();
      ASTNode *val = parseExpression();
      if (val == nullptr) {
        allocator_->destroy(prop);
        allocator_->destroy(obj);
        return nullptr;
      }
      prop->children.push_back(key);
      prop->children.push_back(val);
    } else {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid property key", current().start});
      allocator_->destroy(obj);
      return nullptr;
    }
    obj->children.push_back(prop);
  }
  advance(); /* } */
  return obj;
}

/* 数组模式（字面量/解构共用）：[ elem, ...rest ] */
ASTNode *Parser::parseArrayPattern() {
  ASTNode *arr = newNode(NodeType::ArrayPattern);
  arr->loc = current().start;
  advance(); /* [ */
  while (!isSymbol(current(), "]")) {
    if (atEof()) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated array literal", arr->loc});
      allocator_->destroy(arr);
      return nullptr;
    }
    if (isSymbol(current(), ",")) {
      /* 空位（稀疏数组） */
      advance();
      continue;
    }
    if (isSymbol(current(), "...")) {
      ASTNode *spread = newNode(NodeType::SpreadElement);
      spread->loc = current().start;
      advance();
      ASTNode *arg = parseExpression();
      if (arg == nullptr) {
        allocator_->destroy(arr);
        return nullptr;
      }
      spread->children.push_back(arg);
      arr->children.push_back(spread);
      continue;
    }
    ASTNode *elem = parseExpression();
    if (elem == nullptr) {
      allocator_->destroy(arr);
      return nullptr;
    }
    arr->children.push_back(elem);
  }
  advance(); /* ] */
  return arr;
}

}  // namespace neojs
