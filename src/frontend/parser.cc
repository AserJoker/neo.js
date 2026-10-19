#include "neojs/frontend/parser.h"

#include <string>

namespace neojs {

namespace {

/* 判断当前 token 是否为给定符号文本（如 "+"、"("） */
bool isSymbol(const Token &tok, const char *sym) {
  return tok.kind == TokenKind::Symbol && tok.text == sym;
}

/* 二元运算符优先级（M1.4 全套，对齐 ECMA-262 §12.11 自高到低）：
 *   **（右结合）> * / % > + - > << >> >>> > < <= > >= in instanceof
 *   > == != === !== > & > ^ > | > && > || > ??
 * 返回 0 表示当前 token 不是二元运算符。 */
int binopPrecedence(const Token &tok) {
  if (tok.kind == TokenKind::Identifier) {
    /* 关键字形式：in / instanceof */
    if (tok.text == "in") return 7;
    if (tok.text == "instanceof") return 7;
    return 0;
  }
  if (tok.kind != TokenKind::Symbol) return 0;
  const std::string &s = tok.text;
  if (s == "**") return 11;
  if (s == "*" || s == "/" || s == "%") return 10;
  if (s == "+" || s == "-") return 9;
  if (s == "<<" || s == ">>" || s == ">>>") return 8;
  if (s == "<" || s == "<=" || s == ">" || s == ">=") return 7;
  if (s == "==" || s == "!=" || s == "===" || s == "!==") return 6;
  if (s == "&") return 5;
  if (s == "^") return 4;
  if (s == "|") return 3;
  if (s == "&&") return 2;
  if (s == "||") return 1;
  if (s == "??") return 1;
  return 0;
}

BinaryOpKind toBinop(const Token &tok) {
  const std::string &s = tok.text;
  if (s == "+") return BinaryOpKind::Add;
  if (s == "-") return BinaryOpKind::Sub;
  if (s == "*") return BinaryOpKind::Mul;
  if (s == "/") return BinaryOpKind::Div;
  if (s == "%") return BinaryOpKind::Mod;
  if (s == "**") return BinaryOpKind::Pow;
  if (s == "<<") return BinaryOpKind::LShift;
  if (s == ">>") return BinaryOpKind::RShift;
  if (s == ">>>") return BinaryOpKind::URShift;
  if (s == "<") return BinaryOpKind::Lt;
  if (s == "<=") return BinaryOpKind::Le;
  if (s == ">") return BinaryOpKind::Gt;
  if (s == ">=") return BinaryOpKind::Ge;
  if (s == "in") return BinaryOpKind::In;
  if (s == "instanceof") return BinaryOpKind::Instanceof;
  if (s == "==") return BinaryOpKind::Eq;
  if (s == "!=") return BinaryOpKind::Ne;
  if (s == "===") return BinaryOpKind::StrictEq;
  if (s == "!==") return BinaryOpKind::StrictNe;
  if (s == "&") return BinaryOpKind::BitAnd;
  if (s == "^") return BinaryOpKind::BitXor;
  if (s == "|") return BinaryOpKind::BitOr;
  if (s == "&&") return BinaryOpKind::LogicalAnd;
  if (s == "||") return BinaryOpKind::LogicalOr;
  if (s == "??") return BinaryOpKind::Nullish;
  return BinaryOpKind::Add; /* unreachable */
}

/* 判断当前 token 是否为赋值运算符（含 = 与全部复合赋值） */
bool isAssignOp(const Token &tok) {
  if (tok.kind != TokenKind::Symbol) return false;
  const std::string &s = tok.text;
  return s == "=" || s == "+=" || s == "-=" || s == "*=" || s == "/=" ||
         s == "%=" || s == "**=" || s == "<<=" || s == ">>=" ||
         s == ">>>=" || s == "&=" || s == "^=" || s == "|=" ||
         s == "&&=" || s == "||=" || s == "??" "=";  /* 拼接避免 trigraph */
}

AssignmentOpKind toAssignOp(const Token &tok) {
  const std::string &s = tok.text;
  if (s == "=") return AssignmentOpKind::Assign;
  if (s == "+=") return AssignmentOpKind::AddAssign;
  if (s == "-=") return AssignmentOpKind::SubAssign;
  if (s == "*=") return AssignmentOpKind::MulAssign;
  if (s == "/=") return AssignmentOpKind::DivAssign;
  if (s == "%=") return AssignmentOpKind::ModAssign;
  if (s == "**=") return AssignmentOpKind::PowAssign;
  if (s == "<<=") return AssignmentOpKind::LShiftAssign;
  if (s == ">>=") return AssignmentOpKind::RShiftAssign;
  if (s == ">>>=") return AssignmentOpKind::URShiftAssign;
  if (s == "&=") return AssignmentOpKind::BitAndAssign;
  if (s == "^=") return AssignmentOpKind::BitXorAssign;
  if (s == "|=") return AssignmentOpKind::BitOrAssign;
  if (s == "&&=") return AssignmentOpKind::LogicalAndAssign;
  if (s == "||=") return AssignmentOpKind::LogicalOrAssign;
  if (s == "??" "=") return AssignmentOpKind::NullishAssign;  /* 拼接避免 trigraph */
  return AssignmentOpKind::Assign; /* unreachable */
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

/* 表达式 → 逗号序列（最低优先级，M1.4）：a, b, c → Sequence[a, b, c] */
ASTNode *Parser::parseExpression() {
  setValueContext();
  ASTNode *expr = parseAssignment();
  if (expr == nullptr) return nullptr;
  while (isSymbol(current(), ",")) {
    ASTNode *seq = newNode(NodeType::Sequence);
    seq->loc = expr->loc;
    seq->children.push_back(expr);
    advance(); /* , */
    ASTNode *next = parseAssignment();
    if (next == nullptr) {
      allocator_->destroy(seq);
      return nullptr;
    }
    seq->children.push_back(next);
    expr = seq;
  }
  return expr;
}

/* 赋值（M1.4）：= 与全部复合赋值；右结合。左侧须为简单赋值目标。 */
ASTNode *Parser::parseAssignment() {
  ASTNode *lhs = parseConditional();
  if (lhs == nullptr) return nullptr;
  if (!isSymbol(current(), "=") && !isAssignOp(current())) return lhs;

  const Token &tok = current();
  AssignmentOpKind op = toAssignOp(tok);
  /* 简单赋值目标校验（M1.4 最小集：标识符；成员/解构后续扩展） */
  if (lhs->type != NodeType::Identifier) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid assignment target", tok.start});
    allocator_->destroy(lhs);
    return nullptr;
  }
  advance(); /* 赋值运算符 */
  ASTNode *rhs = parseAssignment(); /* 右结合：a = b = c */
  if (rhs == nullptr) {
    allocator_->destroy(lhs);
    return nullptr;
  }
  ASTNode *node = newNode(NodeType::Assignment);
  node->loc = tok.start;
  node->assign_op = op;
  node->children.push_back(lhs);
  node->children.push_back(rhs);
  return node;
}

/* 条件表达式（M1.4）：cond ? then : else；右结合。 */
ASTNode *Parser::parseConditional() {
  setValueContext();
  ASTNode *cond = parseBinary(1);
  if (cond == nullptr) return nullptr;
  if (!isSymbol(current(), "?")) return cond;
  advance(); /* ? */
  ASTNode *then_expr = parseExpression(); /* then 分支允许序列（ECMA-262） */
  if (then_expr == nullptr) {
    allocator_->destroy(cond);
    return nullptr;
  }
  if (!isSymbol(current(), ":")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ':' in conditional", current().start});
    allocator_->destroy(cond);
    allocator_->destroy(then_expr);
    return nullptr;
  }
  advance(); /* : */
  ASTNode *else_expr = parseAssignment(); /* 右结合：a ? b : c ? d : e */
  if (else_expr == nullptr) {
    allocator_->destroy(cond);
    allocator_->destroy(then_expr);
    return nullptr;
  }
  ASTNode *node = newNode(NodeType::Conditional);
  node->loc = cond->loc;
  node->children.push_back(cond);
  node->children.push_back(then_expr);
  node->children.push_back(else_expr);
  return node;
}

ASTNode *Parser::parseBinary(int min_prec) {
  /* 读值位置：设置 Lexer 上下文（供 / 判定，M1.3 展开） */
  setValueContext();
  ASTNode *lhs = parseUnary();
  if (lhs == nullptr) return nullptr;

  for (;;) {
    /* 读操作位置：设置 Lexer 上下文（供 / 判定，M1.3 展开） */
    setOperatorContext();
    const Token &tok = current();
    int prec = binopPrecedence(tok);
    if (prec == 0 || prec < min_prec) break;
    BinaryOpKind bop = toBinop(tok);
    advance();
    /* ** 右结合：同优先级递归；其余左结合：prec+1 */
    ASTNode *rhs = parseBinary(bop == BinaryOpKind::Pow ? prec : prec + 1);
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

/* 一元运算（M1.4）：! ~ + - 与关键字型 typeof/void/delete/await
 * 对齐 ECMA-262：一元运算符优先级高于二元、低于成员/调用（后续 M1.4 扩展）。
 * 注意：`+`/`-` 一元 vs 二元由当前 token 判定（parseBinary 在运算符位置处理二元）。 */
ASTNode *Parser::parseUnary() {
  setValueContext();
  const Token &tok = current();
  UnaryOpKind op;
  bool is_unary = true;
  if (tok.kind == TokenKind::Symbol) {
    if (tok.text == "!") op = UnaryOpKind::Not;
    else if (tok.text == "~") op = UnaryOpKind::BitNot;
    else if (tok.text == "+") op = UnaryOpKind::Pos;
    else if (tok.text == "-") op = UnaryOpKind::Neg;
    else is_unary = false;
  } else if (tok.kind == TokenKind::Identifier) {
    if (tok.text == "typeof") op = UnaryOpKind::Typeof;
    else if (tok.text == "void") op = UnaryOpKind::Void;
    else if (tok.text == "delete") op = UnaryOpKind::Delete;
    else if (tok.text == "await") op = UnaryOpKind::Await;
    else is_unary = false;
  } else {
    is_unary = false;
  }
  if (!is_unary) return parsePrimary();

  advance(); /* 消费一元运算符 */
  ASTNode *operand = parseUnary(); /* 右结合：一元可嵌套（如 !!!x、- -x） */
  if (operand == nullptr) return nullptr;
  ASTNode *node = newNode(NodeType::UnaryOp);
  node->loc = tok.start;
  node->unop = op;
  node->children.push_back(operand);
  return node;
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
      ASTNode *arg = parseAssignment();
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
        ASTNode *val = parseAssignment();
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
      ASTNode *val = parseAssignment();
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
      ASTNode *arg = parseAssignment();
      if (arg == nullptr) {
        allocator_->destroy(arr);
        return nullptr;
      }
      spread->children.push_back(arg);
      arr->children.push_back(spread);
      continue;
    }
    ASTNode *elem = parseAssignment();
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
