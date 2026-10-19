#include "neojs/frontend/parser.h"

#include <string>

namespace neojs {

namespace {

/* 判断当前 token 是否为给定符号文本（如 "+"、"("） */
bool isSymbol(const Token &tok, const char *sym) {
  return tok.kind == TokenKind::Symbol && tok.text == sym;
}

/* 判断当前 token 是否为给定关键字文本（关键字也是 Identifier kind） */
bool isKeyword(const Token &tok, const char *kw) {
  return tok.kind == TokenKind::Identifier && tok.text == kw;
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

/* 赋值（M1.4）：= 与全部复合赋值；右结合。左侧须为简单赋值目标。
 * 箭头函数（M1.4 ③-4）同层：params => body（右结合）。 */
ASTNode *Parser::parseAssignment() {
  /* 箭头判定（中缀歧义）：
   *   1. 单参：Identifier 前瞻 =>
   *   2. 括号参数：配对扫描后看 ) 后是否 =>（save/restore 回退） */
  if (at(TokenKind::Identifier) && isSymbol(peek(), "=>")) {
    return parseArrowFunction(/*parenthesized=*/false);
  }
  if (isSymbol(current(), "(")) {
    Lexer::State st = lexer_.save();
    int depth = 1; /* 当前 ( 已消费但未计入，从 1 起计 */
    bool arrow = false;
    for (;;) {
      const Token &t = lexer_.next();
      if (t.kind == TokenKind::EndOfFile) break;
      if (isSymbol(t, "(")) depth++;
      else if (isSymbol(t, ")")) {
        depth--;
        if (depth == 0) {
          /* 匹配到外层 )：下一 token 是否 => */
          if (isSymbol(lexer_.next(), "=>")) arrow = true;
          break;
        }
      }
    }
    lexer_.restore(st);
    if (arrow) return parseArrowFunction(/*parenthesized=*/true);
  }

  ASTNode *lhs = parseConditional();
  if (lhs == nullptr) return nullptr;
  if (!isSymbol(current(), "=") && !isAssignOp(current())) return lhs;

  const Token &tok = current();
  AssignmentOpKind op = toAssignOp(tok);
  /* 赋值目标校验（M1.4：标识符 / 成员访问；解构后续扩展） */
  if (lhs->type != NodeType::Identifier && lhs->type != NodeType::MemberExpression) {
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

/* 箭头函数（M1.4 ③-4）：params => body。
 * parenthesized=false：单参 x => body（current 已消费 x，指向 =>）。
 * parenthesized=true：括号参数 (a, b) / ()，current 指向 (。
 * 本次仅表达式体（=> expr）；块体 { } 需语句体系，留 M1.5。
 * 右结合：x => y => z 嵌套。 */
ASTNode *Parser::parseArrowFunction(bool parenthesized) {
  ASTNode *arrow = newNode(NodeType::ArrowFunctionExpression);
  arrow->loc = current().start;

  if (parenthesized) {
    advance(); /* ( */
    while (!isSymbol(current(), ")")) {
      if (atEof()) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated arrow parameters",
                    arrow->loc});
        allocator_->destroy(arrow);
        return nullptr;
      }
      if (isSymbol(current(), ",")) {
        advance();
        continue;
      }
      ASTNode *param;
      if (isSymbol(current(), "...")) {
        ASTNode *rest = newNode(NodeType::RestElement);
        rest->loc = current().start;
        advance();
        if (!at(TokenKind::Identifier)) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected parameter name",
                      current().start});
          allocator_->destroy(rest);
          allocator_->destroy(arrow);
          return nullptr;
        }
        ASTNode *id = newNode(NodeType::Identifier);
        id->loc = current().start;
        id->name = current().text;
        advance();
        rest->children.push_back(id);
        param = rest;
      } else if (at(TokenKind::Identifier)) {
        param = newNode(NodeType::Identifier);
        param->loc = current().start;
        param->name = current().text;
        advance();
      } else {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected parameter name",
                    current().start});
        allocator_->destroy(arrow);
        return nullptr;
      }
      arrow->children.push_back(param);
    }
    advance(); /* ) */
  } else {
    /* 单参：current 已是参数名（Identifier），解析后指向 => */
    ASTNode *param = newNode(NodeType::Identifier);
    param->loc = current().start;
    param->name = current().text;
    advance();
    arrow->children.push_back(param);
  }

  /* 消费 => */
  if (!isSymbol(current(), "=>")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '=>'", current().start});
    allocator_->destroy(arrow);
    return nullptr;
  }
  advance(); /* => */

  /* 表达式体（右结合：x => y => z） */
  ASTNode *body = parseAssignment();
  if (body == nullptr) {
    allocator_->destroy(arrow);
    return nullptr;
  }
  arrow->children.push_back(body);
  return arrow;
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
  if (!is_unary) return parseLeftHandSide();

  advance(); /* 消费一元运算符 */
  ASTNode *operand = parseUnary(); /* 右结合：一元可嵌套（如 !!!x、- -x） */
  if (operand == nullptr) return nullptr;
  ASTNode *node = newNode(NodeType::UnaryOp);
  node->loc = tok.start;
  node->unop = op;
  node->children.push_back(operand);
  return node;
}

/* 左手边表达式（M1.4 ③-3）：primary 加成员/调用后缀链。
 *   a.b.c → Member(Member(a, b), c)
 *   a[i](x) → Call(Member(a, i), [x])
 * 对齐 ECMA-262：MemberExpression 是合法赋值目标（左值）。 */
ASTNode *Parser::parseLeftHandSide() {
  ASTNode *expr = parseNewExpression();
  if (expr == nullptr) return nullptr;
  for (;;) {
    if (isSymbol(current(), ".")) {
      advance(); /* . */
      /* 属性名：普通标识符 或 私有字段 #name */
      ASTNode *prop = nullptr;
      if (at(TokenKind::Identifier)) {
        prop = newNode(NodeType::Identifier);
        prop->loc = current().start;
        prop->name = current().text;
        advance();
      } else if (isSymbol(current(), "#")) {
        advance(); /* # */
        if (!at(TokenKind::Identifier)) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected private identifier after '#'",
                      current().start});
          allocator_->destroy(expr);
          return nullptr;
        }
        prop = newNode(NodeType::PrivateIdentifier);
        prop->loc = current().start;
        prop->name = current().text;
        advance();
      } else {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected property name after '.'",
                    current().start});
        allocator_->destroy(expr);
        return nullptr;
      }
      ASTNode *member = newNode(NodeType::MemberExpression);
      member->loc = expr->loc;
      member->member.computed = false;
      member->children.push_back(expr);
      member->children.push_back(prop);
      expr = member;
    } else if (isSymbol(current(), "[")) {
      advance(); /* [ */
      ASTNode *member = newNode(NodeType::MemberExpression);
      member->loc = expr->loc;
      member->member.computed = true;
      member->children.push_back(expr);
      ASTNode *index = parseExpression();
      if (index == nullptr) {
        allocator_->destroy(member);
        return nullptr;
      }
      if (!isSymbol(current(), "]")) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ']'", current().start});
        allocator_->destroy(member);
        return nullptr;
      }
      advance(); /* ] */
      member->children.push_back(index);
      expr = member;
    } else if (isSymbol(current(), "(")) {
      ASTNode *call = newNode(NodeType::CallExpression);
      call->loc = expr->loc;
      call->children.push_back(expr); /* callee */
      ASTNode *args = parseArguments();
      if (args == nullptr) {
        allocator_->destroy(call);
        return nullptr;
      }
      call->children.insert(call->children.end(), args->children.begin(),
                            args->children.end());
      allocator_->destroy(args); /* 参数列表是临时容器，仅取子节点 */
      expr = call;
    } else {
      break;
    }
  }
  return expr;
}

/* new 构造（M1.4 ③-3）：new Foo(args) / new Foo / new a.b.C()。
 * 对齐 ECMA-262：new 的 callee 是 MemberExpression（不含调用后缀），
 * 但 new Foo().bar 的 .bar 归属外层（new 优先级低于成员/调用后缀）。 */
ASTNode *Parser::parseNewExpression() {
  if (!isKeyword(current(), "new")) return parsePrimary();
  advance(); /* new */
  ASTNode *callee = parseNewExpression(); /* 支持 new new Foo() 嵌套 */
  if (callee == nullptr) return nullptr;
  /* new 的 callee 吃成员链，但不吃调用：new Foo() 中 () 是 new 的参数 */
  for (;;) {
    if (isSymbol(current(), ".")) {
      advance();
      ASTNode *prop = nullptr;
      if (at(TokenKind::Identifier)) {
        prop = newNode(NodeType::Identifier);
        prop->loc = current().start;
        prop->name = current().text;
        advance();
      } else if (isSymbol(current(), "#")) {
        advance();
        if (!at(TokenKind::Identifier)) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected private identifier after '#'",
                      current().start});
          allocator_->destroy(callee);
          return nullptr;
        }
        prop = newNode(NodeType::PrivateIdentifier);
        prop->loc = current().start;
        prop->name = current().text;
        advance();
      } else {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected property name after '.'",
                    current().start});
        allocator_->destroy(callee);
        return nullptr;
      }
      ASTNode *member = newNode(NodeType::MemberExpression);
      member->loc = callee->loc;
      member->member.computed = false;
      member->children.push_back(callee);
      member->children.push_back(prop);
      callee = member;
    } else if (isSymbol(current(), "[")) {
      advance();
      ASTNode *member = newNode(NodeType::MemberExpression);
      member->loc = callee->loc;
      member->member.computed = true;
      member->children.push_back(callee);
      ASTNode *index = parseExpression();
      if (index == nullptr) {
        allocator_->destroy(member);
        return nullptr;
      }
      if (!isSymbol(current(), "]")) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ']'", current().start});
        allocator_->destroy(member);
        return nullptr;
      }
      advance();
      member->children.push_back(index);
      callee = member;
    } else {
      break;
    }
  }
  ASTNode *node = newNode(NodeType::NewExpression);
  node->loc = callee->loc;
  node->children.push_back(callee);
  /* new Foo(...) 显式参数；无括号则 new Foo 无参数 */
  if (isSymbol(current(), "(")) {
    ASTNode *args = parseArguments();
    if (args == nullptr) {
      allocator_->destroy(node);
      return nullptr;
    }
    node->children.insert(node->children.end(), args->children.begin(),
                          args->children.end());
    allocator_->destroy(args);
  }
  return node;
}

/* 实参列表：f(a, b, ...rest)。返回临时容器节点（调用方取子节点后释放）。 */
ASTNode *Parser::parseArguments() {
  ASTNode *args = newNode(NodeType::Sequence); /* 复用：仅作子节点容器 */
  args->loc = current().start;
  advance(); /* ( */
  while (!isSymbol(current(), ")")) {
    if (atEof()) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated argument list", args->loc});
      allocator_->destroy(args);
      return nullptr;
    }
    if (isSymbol(current(), ",")) {
      advance();
      continue;
    }
    ASTNode *arg;
    if (isSymbol(current(), "...")) {
      ASTNode *spread = newNode(NodeType::SpreadElement);
      spread->loc = current().start;
      advance();
      ASTNode *inner = parseAssignment();
      if (inner == nullptr) {
        allocator_->destroy(spread);
        allocator_->destroy(args);
        return nullptr;
      }
      spread->children.push_back(inner);
      arg = spread;
    } else {
      arg = parseAssignment();
      if (arg == nullptr) {
        allocator_->destroy(args);
        return nullptr;
      }
    }
    args->children.push_back(arg);
  }
  advance(); /* ) */
  return args;
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
      return parseLiteral();
    case TokenKind::TemplateStart:
      return parseTemplateLiteral();
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
      /* 主入口 parsePrimary 已将插值模板分流到 parseTemplateLiteral；
       * 此处仅兜底无插值整段（直接调用 parseLiteral 时）。 */
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

/* 模板串（M1.4 ③-5）：`...` 或 `a${expr}b${expr}c`。
 * children 交替布局：[quasi0, expr0, quasi1, expr1, ..., quasiN]（quasi 数 = expr 数 + 1）。
 * quasi 用 Literal 节点承载（is_string + str 为解码后的准值）。
 * 流程：
 *   - 头部 TemplateStart（`...` 或 `...${）：无插值→单 quasi 结束；有插值→准值入列后 parseExpression。
 *   - 插值表达式结束，current 应为 `}`：调用 lexer_.rescanTemplatePart() 重扫为
 *     TemplatePart（}...${，续插值）或 TemplateEnd（}...`，收尾）。
 */
ASTNode *Parser::parseTemplateLiteral() {
  ASTNode *tpl = newNode(NodeType::TemplateLiteral);
  tpl->loc = current().start;

  for (;;) {
    const Token &head = current();
    if (head.kind != TokenKind::TemplateStart && head.kind != TokenKind::TemplatePart &&
        head.kind != TokenKind::TemplateEnd) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected template segment", head.start});
      allocator_->destroy(tpl);
      return nullptr;
    }
    /* quasi：解码后的准值 */
    ASTNode *quasi = newNode(NodeType::Literal);
    quasi->loc = head.start;
    quasi->literal.is_string = true;
    quasi->literal.str = head.str_value;
    quasi->literal.text = head.text;
    tpl->children.push_back(quasi);
    bool has_next = head.has_interpolation; /* ${ 后有插值 */
    advance(); /* 越过本段 */

    if (!has_next) {
      /* 无插值：模板结束（TemplateEnd / 无插值 TemplateStart） */
      break;
    }

    /* 解析插值表达式 ${ expr } */
    ASTNode *expr = parseExpression();
    if (expr == nullptr) {
      allocator_->destroy(tpl);
      return nullptr;
    }
    tpl->children.push_back(expr);

    /* 表达式结束后必须是模板闭合 } */
    if (!isSymbol(current(), "}")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '}' in template", current().start});
      allocator_->destroy(tpl);
      return nullptr;
    }
    lexer_.rescanTemplatePart(); /* 把 }... 重扫为 TemplatePart/TemplateEnd */
  }
  return tpl;
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
