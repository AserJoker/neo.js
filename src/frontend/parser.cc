#include "neojs/frontend/parser.h"

#include <string>
#include <utility>

namespace neojs {

namespace {

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

Parser::Parser(const char *source, size_t source_len,
               DiagnosticSink &sink, bool module)
    : lexer_(source, source_len, sink), sink_(sink), module_(module) {}

ASTNode *Parser::newNode(NodeType t) {
  return new ASTNode(t);
}

AST *Parser::parse() {
  lexer_.skipHashbang(); /* M1.5：入口级 hashbang，首个 token 前 */
  AST *ast = new AST();
  if (module_) {
    /* 模块模式（M1.6 ③）：Module 根节点 + 自动严格模式。
     * 顶层 await 合法（module_ 为 true 时 parseStatement 放行）；
     * import/export 仅在 module 上下文合法。 */
    strict_ = true;
    ast->root = parseStatementList();
    if (ast->root != nullptr) ast->root->type = NodeType::Module;
  } else {
    ast->root = parseStatementList();
  }
  if (ast->root == nullptr || sink_.hasErrors()) {
    delete ast;
    return nullptr;
  }
  return ast;
}

/* 语句体系见 parser_stmt.cc：parseStatementList / parseStatement /
 * parseBlock / parseLabeledStatement / parseIfStatement /
 * parseExpressionStatement。共享判定 isSymbol / isKeyword 在 parser.h。 */

/* 表达式 → 逗号序列（最低优先级，M1.4）：a, b, c → Sequence[a, b, c]
 * allow_in=false 时禁用 in/instanceof 二元运算（ECMA-262 Expression[~In]，
 * 用于 for 的 init 段：for (k in obj) 中 in 是 for-in 的关键字而非运算符）。 */
ASTNode *Parser::parseExpression(bool allow_in) {
  setValueContext();
  ASTNode *expr = parseAssignment(allow_in);
  if (expr == nullptr) return nullptr;
  while (isSymbol(current(), ",")) {
    ASTNode *seq = newNode(NodeType::Sequence);
    seq->loc = expr->loc;
    seq->children.push_back(expr);
    advance(); /* , */
    ASTNode *next = parseAssignment(allow_in);
    if (next == nullptr) {
      delete seq;
      return nullptr;
    }
    seq->children.push_back(next);
    expr = seq;
  }
  return expr;
}

/* yield 表达式（M5.5a，§14.4）：yield / yield* expr / 裸 yield。
 * ECMA-262 §14.4 YieldExpression[In, Await] :
 *   yield
 *   yield [no LineTerminator here] AssignmentExpression[?In, +Yield]
 *   yield [no LineTerminator here] * AssignmentExpression[?In, +Yield]
 * 仅 generator 函数体内合法（generator_depth_ > 0，由 parseAssignment 检测后转此）。
 * children[0] = yield 值表达式（nullptr 表示裸 yield）；
 * yield_delegate 标 yield* 委托。
 * restricted production：yield 后遇换行 → 裸 yield（无操作数）。
 * yield* 中 * 与 yield 之间不得有换行（no LineTerminator here）——按 §14.4，
 * * 紧随 yield，若 yield 后换行则 * 视为乘法运算符，整个表达式为裸 yield。 */
ASTNode *Parser::parseYieldExpression(bool allow_in) {
  ASTNode *node = newNode(NodeType::YieldExpression);
  node->loc = current().start;
  advance(); /* yield */

  /* 裸 yield 判定：
   *   1) yield 后紧跟换行（restricted production） → 裸 yield
   *   2) yield 后是 ; / , / ) / ] / } / : / EOF 等表达式结束符 → 裸 yield
   *   3) yield 后跟 * 且无换行 → yield* 委托
   *   4) 否则 → yield <AssignmentExpression> */
  bool bare_yield = false;
  if (current().has_newline_before) {
    bare_yield = true;
  } else if (isSymbol(current(), ";") || isSymbol(current(), ",") ||
             isSymbol(current(), ")") || isSymbol(current(), "]") ||
             isSymbol(current(), "}") || isSymbol(current(), ":") ||
             atEof()) {
    bare_yield = true;
  }

  if (bare_yield) {
    node->children.push_back(nullptr);
    return node;
  }

  /* yield* 委托：* 紧随 yield（无换行，已由 has_newline_before 排除）。 */
  if (isSymbol(current(), "*")) {
    node->yield_delegate = true;
    advance(); /* * */
  }

  /* yield 值表达式：递归 parseAssignment（含 +Yield，允许嵌套 yield）。
   * 设值上下文：yield 后读操作数首 token。 */
  setValueContext();
  ASTNode *arg = parseAssignment(allow_in);
  if (arg == nullptr) {
    delete node;
    return nullptr;
  }
  node->children.push_back(arg);
  return node;
}

/* 赋值（M1.4）：= 与全部复合赋值；右结合。左侧须为简单赋值目标。
 * 箭头函数（M1.4 ③-4）同层：params => body（右结合）。
 * yield 表达式（M5.5a，§14.4）：仅在 generator 函数体内合法，优先级高于赋值，
 * 在此处最先检测。 */
ASTNode *Parser::parseAssignment(bool allow_in) {
  /* yield 表达式（M5.5a）：yield 是 Identifier kind，仅在 generator 函数体内
   * （generator_depth_ > 0）作为 yield 表达式解析，否则按普通标识符处理。
   * restricted production（§14.4）：yield 后遇换行即裸 yield（无操作数）。
   * yield* 委托：yield 后紧跟 *（无换行要求，* 是运算符）。 */
  if (at(TokenKind::Identifier) && current().text == "yield" &&
      generator_depth_ > 0) {
    return parseYieldExpression(allow_in);
  }
  /* 箭头判定（中缀歧义）：
   *   1. 单参：Identifier 前瞻 =>
   *   2. 括号参数：配对扫描后看 ) 后是否 =>（save/restore 回退）
   * async 箭头（M5.4）：async [x] => / async (x) =>。
   *   async(...) 可能是普通调用（async(1)），须配对扫描 ( 确认 ) 后是 =>
   *   才判为箭头；async function / async 作为普通标识符均不在此。 */
  if (at(TokenKind::Identifier) && current().text == "async") {
    Lexer::State st = lexer_.save();
    advance(); /* async */
    bool single_arrow = false;
    bool paren_arrow = false;
    if (at(TokenKind::Identifier) && !isKeyword(current(), "function") &&
        isSymbol(peek(), "=>")) {
      single_arrow = true;
    } else if (isSymbol(current(), "(")) {
      int depth = 1; /* 当前 ( 未消费，从 1 起计（下一 token 开始） */
      for (;;) {
        const Token &t = lexer_.next();
        if (t.kind == TokenKind::EndOfFile) break;
        if (isSymbol(t, "(")) depth++;
        else if (isSymbol(t, ")")) {
          depth--;
          if (depth == 0) {
            if (isSymbol(lexer_.next(), "=>")) paren_arrow = true;
            break;
          }
        }
      }
    }
    lexer_.restore(st);
    if (single_arrow) return parseArrowFunction(/*parenthesized=*/false, /*is_async=*/true);
    if (paren_arrow) return parseArrowFunction(/*parenthesized=*/true, /*is_async=*/true);
  }
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

  ASTNode *lhs = parseConditional(allow_in);
  if (lhs == nullptr) return nullptr;
  if (!isSymbol(current(), "=") && !isAssignOp(current())) return lhs;

  const Token &tok = current();
  AssignmentOpKind op = toAssignOp(tok);
  /* 赋值目标校验（M1.4：标识符 / 成员访问；M5.1a：解构模式 [a] / {a}）。
   * 解构只允许纯 = 赋值（复合赋值给解构目标非法）。 */
  bool is_pattern = lhs->type == NodeType::ArrayPattern || lhs->type == NodeType::ObjectPattern;
  if (is_pattern && op != AssignmentOpKind::Assign) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR",
                "invalid destructuring assignment target", tok.start});
    delete lhs;
    return nullptr;
  }
  /* 赋值左侧的数组/对象字面量（[a] / {p}）：转为解构模式（M5.1a）。
   * 读值位置 parsePrimary 产出 ArrayLiteral/ObjectLiteral，这里按
   * 解构赋值目标递归转换为 ArrayPattern/ObjectPattern。 */
  if (!is_pattern &&
      (lhs->type == NodeType::ArrayLiteral || lhs->type == NodeType::ObjectLiteral)) {
    if (op != AssignmentOpKind::Assign) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR",
                  "invalid destructuring assignment target", tok.start});
      delete lhs;
      return nullptr;
    }
    if (!convertLiteralToPattern(lhs)) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid assignment target",
                  tok.start});
      delete lhs;
      return nullptr;
    }
    is_pattern = true;
  }
  if (!is_pattern && lhs->type != NodeType::Identifier && lhs->type != NodeType::MemberExpression) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid assignment target", tok.start});
    delete lhs;
    return nullptr;
  }
  /* 可选链不能作为赋值目标（ECMA-262 §13.15.5：AssignmentTarget 不含
   * OptionalExpression；a?.b = 1 是语法错误）。 */
  if (lhs->type == NodeType::MemberExpression && lhs->member.optional) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR",
                "invalid assignment target: optional chaining", tok.start});
    delete lhs;
    return nullptr;
  }
  /* 严格模式禁止对 eval / arguments 赋值（ECMA-262 §13.15.1） */
  if (strict_ && lhs->type == NodeType::Identifier &&
      (lhs->name == "eval" || lhs->name == "arguments")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR",
                "assignment to eval or arguments in strict mode", tok.start});
    delete lhs;
    return nullptr;
  }
  advance(); /* 赋值运算符 */
  ASTNode *rhs = parseAssignment(allow_in); /* 右结合：a = b = c */
  if (rhs == nullptr) {
    delete lhs;
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
ASTNode *Parser::parseArrowFunction(bool parenthesized, bool is_async) {
  ASTNode *arrow = newNode(NodeType::ArrowFunctionExpression);
  arrow->loc = current().start;
  arrow->fn_is_async = is_async;
  if (is_async) advance(); /* async（current 现指向参数/括号） */

  if (parenthesized) {
    advance(); /* ( */
    while (!isSymbol(current(), ")")) {
      if (atEof()) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated arrow parameters",
                    arrow->loc});
        delete arrow;
        return nullptr;
      }
      if (isSymbol(current(), ",")) {
        advance();
        continue;
      }
      ASTNode *param;
      if (isSymbol(current(), "...")) {
        /* rest 参数：...target（target 可为标识符或解构模式） */
        ASTNode *rest = newNode(NodeType::RestElement);
        rest->loc = current().start;
        advance();
        ASTNode *target;
        if (isSymbol(current(), "{") || isSymbol(current(), "[")) {
          if (isSymbol(current(), "{")) {
            target = parseObjectPattern();
          } else {
            target = parseArrayPattern();
          }
          if (target == nullptr) {
            delete rest;
            delete arrow;
            return nullptr;
          }
        } else if (at(TokenKind::Identifier)) {
          target = newNode(NodeType::Identifier);
          target->loc = current().start;
          target->name = current().text;
          advance();
        } else {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected parameter name",
                      current().start});
          delete rest;
          delete arrow;
          return nullptr;
        }
        rest->children.push_back(target);
        param = rest;
      } else if (isSymbol(current(), "{") || isSymbol(current(), "[")) {
        if (isSymbol(current(), "{")) {
          param = parseObjectPattern();
        } else {
          param = parseArrayPattern();
        }
        if (param == nullptr) {
          delete arrow;
          return nullptr;
        }
      } else if (at(TokenKind::Identifier)) {
        param = newNode(NodeType::Identifier);
        param->loc = current().start;
        param->name = current().text;
        advance();
      } else {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected parameter name",
                    current().start});
        delete arrow;
        return nullptr;
      }
      /* 默认值 a = 1（M5.1a）：AssignmentPattern{a, 1} */
      if (isSymbol(current(), "=")) {
        ASTNode *def = parseDefaultValue(param);
        if (def == nullptr) {
          delete arrow;
          return nullptr;
        }
        param = def;
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
    delete arrow;
    return nullptr;
  }
  advance(); /* => */

  /* 函数体：{ 块语句 } 或表达式体（M1.5 ② 解锁块体）。
   * 块体检测 Directive Prologue（"use strict"），参数检查在 body 解析后
   * （strict_ 已确定）执行；外层 strict_ 状态 body 后恢复。
   * async 箭头（M5.4）：async_depth_ 递增，body 内 await 合法。 */
  ++function_depth_;
  if (is_async) ++async_depth_;
  ASTNode *body;
  bool outer_strict = strict_;
  if (isSymbol(current(), "{")) {
    body = parseBlock(/*detect_strict=*/true);
  } else {
    body = parseAssignment(); /* 表达式体右结合：x => y => z */
  }
  bool strict_params = strict_; /* 记录恢复前的值（含 body directive） */
  strict_ = outer_strict;
  if (is_async) --async_depth_;
  --function_depth_;
  if (body == nullptr) {
    delete arrow;
    return nullptr;
  }
  arrow->children.push_back(body);
  /* 函数体指令序言：从 body 块挪到函数节点（表达式体无 directives） */
  arrow->directives = std::move(body->directives);
  body->directives.clear();
  /* 严格模式参数检查（箭头函数无 name） */
  if (checkStrictParams(arrow, strict_params, /*has_name=*/false,
                        static_cast<int>(arrow->children.size()) - 1)) {
    delete arrow;
    return nullptr;
  }
  return arrow;
}

/* 条件表达式（M1.4）：cond ? then : else；右结合。 */
ASTNode *Parser::parseConditional(bool allow_in) {
  setValueContext();
  ASTNode *cond = parseBinary(1, allow_in);
  if (cond == nullptr) return nullptr;
  if (!isSymbol(current(), "?")) return cond;
  advance(); /* ? */
  ASTNode *then_expr = parseExpression(allow_in); /* then 分支允许序列（ECMA-262） */
  if (then_expr == nullptr) {
    delete cond;
    return nullptr;
  }
  if (!isSymbol(current(), ":")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ':' in conditional", current().start});
    delete cond;
    delete then_expr;
    return nullptr;
  }
  advance(); /* : */
  ASTNode *else_expr = parseAssignment(allow_in); /* 右结合：a ? b : c ? d : e */
  if (else_expr == nullptr) {
    delete cond;
    delete then_expr;
    return nullptr;
  }
  ASTNode *node = newNode(NodeType::Conditional);
  node->loc = cond->loc;
  node->children.push_back(cond);
  node->children.push_back(then_expr);
  node->children.push_back(else_expr);
  return node;
}

ASTNode *Parser::parseBinary(int min_prec, bool allow_in, bool no_coalesce) {
  /* 读值位置：设置 Lexer 上下文（供 / 判定，M1.3 展开） */
  setValueContext();
  ASTNode *lhs = parseUnary();
  if (lhs == nullptr) return nullptr;

  bool coalesced = no_coalesce; /* 本层已出现 ??（其后禁止 ||/&&） */
  for (;;) {
    /* 读操作位置：设置 Lexer 上下文（供 / 判定，M1.3 展开） */
    setOperatorContext();
    const Token &tok = current();
    int prec = binopPrecedence(tok);
    if (prec == 0 || prec < min_prec) break;
    /* Expression[~In]（for init 段）：in / instanceof 禁用 */
    if (!allow_in && tok.kind == TokenKind::Identifier &&
        (tok.text == "in" || tok.text == "instanceof")) {
      break;
    }
    /* 私有字段 in 特例（ECMA-262）：`#x in obj` 整体不能再作 in 的左操作数
     * （`#x in a in b` 非法）。lhs 已是「私有 in 表达式」时禁止继续。 */
    if (lhs->type == NodeType::BinaryOp && lhs->binop == BinaryOpKind::In &&
        lhs->children[0]->type == NodeType::PrivateIdentifier) {
      break;
    }
    /* nullish 隔离（ECMA-262 §12.11）：`??` 不能与 `||`/`&&` 无括号混用。
     *  - 已出现 ??（coalesced）且当前是 ||/&& → 非法
     *  - 当前是 ?? 且 lhs 顶层是 ||/&& → 非法 */
    bool tok_or_and = tok.kind == TokenKind::Symbol &&
                      (tok.text == "||" || tok.text == "&&");
    bool tok_nullish = tok.kind == TokenKind::Symbol && tok.text == "??";
    if (coalesced && tok_or_and) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR",
                  "cannot mix '??' with '||' or '&&' without parentheses", tok.start});
      delete lhs;
      return nullptr;
    }
    if (tok_nullish && lhs->type == NodeType::BinaryOp &&
        (lhs->binop == BinaryOpKind::LogicalOr || lhs->binop == BinaryOpKind::LogicalAnd)) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR",
                  "cannot mix '??' with '||' or '&&' without parentheses", tok.start});
      delete lhs;
      return nullptr;
    }
    BinaryOpKind bop = toBinop(tok);
    advance();
    /* ** 右结合：同优先级递归；其余左结合：prec+1。
     * 消费 ?? 后 rhs 递归禁止 ||/&&（no_coalesce=true），本层也置位。 */
    coalesced = coalesced || bop == BinaryOpKind::Nullish;
    ASTNode *rhs = parseBinary(bop == BinaryOpKind::Pow ? prec : prec + 1, allow_in,
                               coalesced);
    if (rhs == nullptr) {
      delete lhs;
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
 * 前缀自增自减（M1.4 ③-6）：++x / --x（优先级同前缀一元）。
 * 对齐 ECMA-262：一元运算符优先级高于二元、低于成员/调用。
 * 注意：`+`/`-` 一元 vs 二元由当前 token 判定（parseBinary 在运算符位置处理二元）。 */
ASTNode *Parser::parseUnary() {
  setValueContext();
  const Token &tok = current();
  UnaryOpKind op;
  bool is_unary = true;
  UpdateOpKind upd;
  bool is_update = false;
  if (tok.kind == TokenKind::Symbol) {
    if (tok.text == "!") op = UnaryOpKind::Not;
    else if (tok.text == "~") op = UnaryOpKind::BitNot;
    else if (tok.text == "+") op = UnaryOpKind::Pos;
    else if (tok.text == "-") op = UnaryOpKind::Neg;
    else if (tok.text == "++") { upd = UpdateOpKind::Inc; is_update = true; }
    else if (tok.text == "--") { upd = UpdateOpKind::Dec; is_update = true; }
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
  if (is_update) {
    /* 前缀自增自减：++x / --x */
    advance(); /* ++ / -- */
    ASTNode *operand = parseUnary(); /* 右结合 */
    if (operand == nullptr) return nullptr;
    /* 前缀操作数须为合法左值（标识符/成员访问） */
    if (operand->type != NodeType::Identifier && operand->type != NodeType::MemberExpression) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid increment/decrement operand",
                  tok.start});
      delete operand;
      return nullptr;
    }
    /* 可选链不能自增自减（ECMA-262 §13.4.4：UpdateExpression 的 target
     * 须为 LeftHandSideExpression，不含 OptionalExpression）。 */
    if (operand->type == NodeType::MemberExpression && operand->member.optional) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR",
                  "invalid increment/decrement operand: optional chaining", tok.start});
      delete operand;
      return nullptr;
    }
    /* 严格模式禁止 ++/-- 于 eval / arguments（ECMA-262 §13.4.4） */
    if (strict_ && operand->type == NodeType::Identifier &&
        (operand->name == "eval" || operand->name == "arguments")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR",
                  "increment/decrement of eval or arguments in strict mode", tok.start});
      delete operand;
      return nullptr;
    }
    ASTNode *node = newNode(NodeType::UpdateExpression);
    node->loc = tok.start;
    node->update_op = upd;
    node->prefix = true;
    node->children.push_back(operand);
    return node;
  }
  if (!is_unary) return parseLeftHandSide();

  /* await 合法性（M1.6 ③ + M5.4）：仅 async 函数内（async_depth_ > 0）或
   * module 顶层合法（顶层 await 特性）；普通函数/script 顶层报错。 */
  if (op == UnaryOpKind::Await && async_depth_ == 0 && !module_) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "await is only valid in async functions and the top level bodies of modules",
                tok.start});
    return nullptr;
  }
  advance(); /* 消费一元运算符 */
  ASTNode *operand = parseUnary(); /* 右结合：一元可嵌套（如 !!!x、- -x） */
  if (operand == nullptr) return nullptr;
  /* delete 裸标识符严格模式禁止（ECMA-262 §13.5.1.1） */
  if (op == UnaryOpKind::Delete && strict_ && operand->type == NodeType::Identifier) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR",
                "delete of an unqualified identifier in strict mode", tok.start});
    delete operand;
    return nullptr;
  }
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
  bool chain_optional = false; /* 一旦进入可选链，后续整链均 optional（短路语义） */
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
          delete expr;
          return nullptr;
        }
        prop = newNode(NodeType::PrivateIdentifier);
        prop->loc = current().start;
        prop->name = current().text;
        advance();
      } else {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected property name after '.'",
                    current().start});
        delete expr;
        return nullptr;
      }
      ASTNode *member = newNode(NodeType::MemberExpression);
      member->loc = expr->loc;
      member->member.computed = false;
      member->member.optional = chain_optional;
      member->member.short_circuit = false; /* 链延续：非短路点 */
      member->children.push_back(expr);
      member->children.push_back(prop);
      expr = member;
    } else if (isSymbol(current(), "?.")) {
      /* 可选链（M1.4 ③-6）：?.prop / ?.#priv / ?.[expr] / ?.(args) */
      chain_optional = true;
      advance(); /* ?. */
      if (at(TokenKind::Identifier)) {
        ASTNode *prop = newNode(NodeType::Identifier);
        prop->loc = current().start;
        prop->name = current().text;
        advance();
        ASTNode *member = newNode(NodeType::MemberExpression);
        member->loc = expr->loc;
        member->member.computed = false;
        member->member.optional = true;
        member->member.short_circuit = true; /* ?.prop 短路点 */
        member->children.push_back(expr);
        member->children.push_back(prop);
        expr = member;
      } else if (isSymbol(current(), "#")) {
        advance(); /* # */
        if (!at(TokenKind::Identifier)) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR",
                      "expected private identifier after '?.#'", current().start});
          delete expr;
          return nullptr;
        }
        ASTNode *prop = newNode(NodeType::PrivateIdentifier);
        prop->loc = current().start;
        prop->name = current().text;
        advance();
        ASTNode *member = newNode(NodeType::MemberExpression);
        member->loc = expr->loc;
        member->member.computed = false;
        member->member.optional = true;
        member->member.short_circuit = true; /* ?.#priv 短路点 */
        member->children.push_back(expr);
        member->children.push_back(prop);
        expr = member;
      } else if (isSymbol(current(), "[")) {
        advance(); /* [ */
        ASTNode *member = newNode(NodeType::MemberExpression);
        member->loc = expr->loc;
        member->member.computed = true;
        member->member.optional = true;
        member->member.short_circuit = true; /* ?.[expr] 短路点 */
        member->children.push_back(expr);
        ASTNode *index = parseExpression();
        if (index == nullptr) {
          delete member;
          return nullptr;
        }
        if (!isSymbol(current(), "]")) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ']'", current().start});
          delete member;
          return nullptr;
        }
        advance(); /* ] */
        member->children.push_back(index);
        expr = member;
      } else if (isSymbol(current(), "(")) {
        /* ?.(args)：独立短路点——判断 ?.( 左侧表达式（链前一步）是否为
         * nullish，短路则整链 undefined（区别于链延续 a?.b() 的 ()，M5.1e-e）。 */
        ASTNode *call = newNode(NodeType::CallExpression);
        call->loc = expr->loc;
        call->optional = true;
        call->call_short_circuit = true;
        call->children.push_back(expr); /* callee */
        ASTNode *args = parseArguments();
        if (args == nullptr) {
          delete call;
          return nullptr;
        }
        call->children.insert(call->children.end(), args->children.begin(),
                              args->children.end());
        delete args; /* 参数列表是临时容器，仅取子节点 */
        expr = call;
      } else {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected property name after '?.'",
                    current().start});
        delete expr;
        return nullptr;
      }
    } else if (isSymbol(current(), "[")) {
      advance(); /* [ */
      ASTNode *member = newNode(NodeType::MemberExpression);
      member->loc = expr->loc;
      member->member.computed = true;
      member->member.optional = chain_optional;
      member->member.short_circuit = false; /* 链延续：非短路点 */
      member->children.push_back(expr);
      ASTNode *index = parseExpression();
      if (index == nullptr) {
        delete member;
        return nullptr;
      }
      if (!isSymbol(current(), "]")) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ']'", current().start});
        delete member;
        return nullptr;
      }
      advance(); /* ] */
      member->children.push_back(index);
      expr = member;
    } else if (isSymbol(current(), "(")) {
      /* 普通调用：链延续（a?.b() 的 ()，非独立短路点）。callee 链短路时
       * 此调用被跳过；callee 非 nullish 时正常调用（M5.1e-e）。 */
      ASTNode *call = newNode(NodeType::CallExpression);
      call->loc = expr->loc;
      call->optional = chain_optional;
      call->call_short_circuit = false;
      call->children.push_back(expr); /* callee */
      ASTNode *args = parseArguments();
      if (args == nullptr) {
        delete call;
        return nullptr;
      }
      call->children.insert(call->children.end(), args->children.begin(),
                            args->children.end());
      delete args; /* 参数列表是临时容器，仅取子节点 */
      expr = call;
    } else if (isSymbol(current(), "++")) {
      /* 后缀自增（M1.4 ③-6）：x++。
       * Restricted production（ASI）：++ 前有换行则不能作后缀，
       * 视为下一语句的前缀 ++（a\n++b → a; ++b）。 */
      if (current().has_newline_before) break;
      if (expr->type != NodeType::Identifier && expr->type != NodeType::MemberExpression) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid increment/decrement operand",
                    current().start});
        delete expr;
        return nullptr;
      }
      /* 可选链不能自增自减（ECMA-262 §13.4.4）。 */
      if (expr->type == NodeType::MemberExpression && expr->member.optional) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR",
                    "invalid increment/decrement operand: optional chaining", current().start});
        delete expr;
        return nullptr;
      }
      /* 严格模式禁止 ++ 于 eval / arguments（ECMA-262 §13.4.4） */
      if (strict_ && expr->type == NodeType::Identifier &&
          (expr->name == "eval" || expr->name == "arguments")) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR",
                    "increment/decrement of eval or arguments in strict mode", current().start});
        delete expr;
        return nullptr;
      }
      ASTNode *node = newNode(NodeType::UpdateExpression);
      node->loc = current().start;
      node->update_op = UpdateOpKind::Inc;
      node->prefix = false;
      node->children.push_back(expr);
      advance(); /* ++ */
      expr = node;
    } else if (isSymbol(current(), "--")) {
      /* 后缀自减（M1.4 ③-6）：x--。
       * Restricted production（ASI）：-- 前有换行则不能作后缀，
       * 视为下一语句的前缀 --（a\n--b → a; --b）。 */
      if (current().has_newline_before) break;
      if (expr->type != NodeType::Identifier && expr->type != NodeType::MemberExpression) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid increment/decrement operand",
                    current().start});
        delete expr;
        return nullptr;
      }
      /* 可选链不能自增自减（ECMA-262 §13.4.4）。 */
      if (expr->type == NodeType::MemberExpression && expr->member.optional) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR",
                    "invalid increment/decrement operand: optional chaining", current().start});
        delete expr;
        return nullptr;
      }
      /* 严格模式禁止 -- 于 eval / arguments（ECMA-262 §13.4.4） */
      if (strict_ && expr->type == NodeType::Identifier &&
          (expr->name == "eval" || expr->name == "arguments")) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR",
                    "increment/decrement of eval or arguments in strict mode", current().start});
        delete expr;
        return nullptr;
      }
      ASTNode *node = newNode(NodeType::UpdateExpression);
      node->loc = current().start;
      node->update_op = UpdateOpKind::Dec;
      node->prefix = false;
      node->children.push_back(expr);
      advance(); /* -- */
      expr = node;
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
          delete callee;
          return nullptr;
        }
        prop = newNode(NodeType::PrivateIdentifier);
        prop->loc = current().start;
        prop->name = current().text;
        advance();
      } else {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected property name after '.'",
                    current().start});
        delete callee;
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
        delete member;
        return nullptr;
      }
      if (!isSymbol(current(), "]")) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ']'", current().start});
        delete member;
        return nullptr;
      }
      advance();
      member->children.push_back(index);
      callee = member;
    } else if (isSymbol(current(), "?.")) {
      /* ECMA-262：可选链不能作为 new 的 callee（new a?.b 语法错误，
       * 须写成 new (a?.b)。此处直接报错。 */
      sink_.emit({Severity::Error, "SYNTAX_ERROR",
                  "optional chaining not allowed in new callee", current().start});
      delete callee;
      return nullptr;
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
      delete node;
      return nullptr;
    }
    node->children.insert(node->children.end(), args->children.begin(),
                          args->children.end());
    delete args;
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
      delete args;
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
        delete spread;
        delete args;
        return nullptr;
      }
      spread->children.push_back(inner);
      arg = spread;
    } else {
      arg = parseAssignment();
      if (arg == nullptr) {
        delete args;
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
      /* this 关键字（M5.1c 访问器 this 绑定）：产出 ThisExpression 节点。
       * this 是保留字，但 Lexer 归为 Identifier，此处按文本特判。 */
      if (tok.text == "this") {
        advance();
        ASTNode *n = newNode(NodeType::ThisExpression);
        n->loc = tok.start;
        return n;
      }
      /* super 关键字（M5.1d-b）：super.x / super[expr]（仅 class 方法内）。
       * super() 构造调用留 M5.1d-c。super 单独出现（非 . / [ / (）为语法错误。 */
      if (tok.text == "super") return parseSuperProperty();
      /* 函数表达式：function [name](params) { body }（M1.5 ④）。
       * M5.5a：function* 标记 generator。 */
      if (tok.text == "function") {
        bool is_gen = isSymbol(peek(), "*");
        return parseFunctionExpression(/*is_async=*/false, is_gen);
      }
      /* async 函数表达式（M5.4）：async [name](params) { body }。
       * 前瞻确认 async 后跟 function（否则 async 是普通标识符）。
       * M5.5a：async function* 标记 async generator。 */
      if (tok.text == "async") {
        Lexer::State st = lexer_.save();
        lexer_.next(); /* async */
        bool is_async_fn = isKeyword(lexer_.current(), "function");
        bool is_async_gen = is_async_fn && isSymbol(lexer_.peek(), "*");
        lexer_.restore(st);
        if (is_async_fn) {
          advance(); /* async */
          return parseFunctionExpression(/*is_async=*/true, is_async_gen);
        }
      }
      /* class 表达式（M5.1d-a）：class [name] { ... } */
      if (tok.text == "class") return parseClassExpression();
      /* 动态导入 import('m')（M1.6 ③）：import 后跟 ( 时为表达式 */
      if (tok.text == "import" && isSymbol(peek(), "(")) {
        advance(); /* import */
        ASTNode *node = newNode(NodeType::ImportExpression);
        node->loc = tok.start;
        ASTNode *spec = parseArguments(); /* (a, b) */
        if (spec == nullptr) return nullptr;
        if (spec->children.size() != 1) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR",
                      "import() requires exactly one argument", tok.start});
          delete spec;
          return nullptr;
        }
        node->children.push_back(spec->children[0]);
        delete spec; /* 临时容器 */
        return node;
      }
      /* import.meta（M1.6 ③）：import 后跟 .meta。仅模块上下文合法
       * （ECMA-262 §12.3.12）；Script 中报 SyntaxError。 */
      if (tok.text == "import" && isSymbol(peek(), ".")) {
        Lexer::State st = lexer_.save();
        lexer_.next(); /* import */
        lexer_.next(); /* . */
        bool is_meta = isKeyword(lexer_.current(), "meta");
        lexer_.restore(st);
        if (is_meta) {
          if (!module_) {
            sink_.emit({Severity::Error, "SYNTAX_ERROR",
                        "import.meta is only valid in modules", tok.start});
            return nullptr;
          }
          advance(); /* import */
          advance(); /* . */
          advance(); /* meta */
          ASTNode *n = newNode(NodeType::ImportMeta);
          n->loc = tok.start;
          return n;
        }
      }
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
        /* 读值位置 { → 对象字面量（M3；解构模式仅在声明/参数等绑定位置） */
        return parseObjectPattern(/*as_literal=*/true);
      }
      if (isSymbol(tok, "[")) {
        return parseArrayPattern(/*as_literal=*/true);
      }
      if (isSymbol(tok, "(")) {
        advance();
        ASTNode *inner = parseExpression();
        if (inner == nullptr) return nullptr;
        if (!isSymbol(current(), ")")) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ')'", tok.start});
          delete inner;
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

/* super 属性访问（M5.1d-b）：super.x / super[expr]。
 * 仅 class 方法上下文（super_depth_ > 0）合法；否则语法错误。
 * 产出 SuperProperty 节点：children[0] = 属性键（Identifier 或计算键表达式），
 * member.computed 标计算键。super 后必须跟 . 或 [（super() 构造调用留
 * M5.1d-c）；单独 super / super( 在此报错。 */
ASTNode *Parser::parseSuperProperty() {
  const Token &tok = current();
  if (super_depth_ <= 0) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR",
                "'super' keyword unexpected here", tok.start});
    return nullptr;
  }
  advance(); /* super */

  if (isSymbol(current(), ".")) {
    advance(); /* . */
    if (!at(TokenKind::Identifier)) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR",
                  "expected property name after 'super.'", current().start});
      return nullptr;
    }
    ASTNode *n = newNode(NodeType::SuperProperty);
    n->loc = tok.start;
    n->member.computed = false;
    ASTNode *prop = newNode(NodeType::Identifier);
    prop->loc = current().start;
    prop->name = current().text;
    advance();
    n->children.push_back(prop);
    return n;
  }
  if (isSymbol(current(), "[")) {
    advance(); /* [ */
    setValueContext();
    ASTNode *n = newNode(NodeType::SuperProperty);
    n->loc = tok.start;
    n->member.computed = true;
    ASTNode *index = parseExpression();
    if (index == nullptr) {
      delete n;
      return nullptr;
    }
    if (!isSymbol(current(), "]")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ']'",
                  current().start});
      delete index;
      delete n;
      return nullptr;
    }
    advance(); /* ] */
    n->children.push_back(index);
    return n;
  }
  if (isSymbol(current(), "(")) {
    /* M5.1d-c1 super() 构造调用：仅派生类（extends）构造器内合法
     * （§13.3.7.1：super() 仅 derived 构造器；普通方法/基类构造器报错）。
     * 产出 SuperCall 节点，children = [arg...]（参数列表）。 */
    if (derived_ctor_depth_ <= 0) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR",
                  "'super' call is only allowed in a derived class constructor",
                  tok.start});
      return nullptr;
    }
    ASTNode *n = newNode(NodeType::SuperCall);
    n->loc = tok.start;
    ASTNode *args = parseArguments();
    if (args == nullptr) {
      delete n;
      return nullptr;
    }
    n->children.insert(n->children.end(), args->children.begin(),
                       args->children.end());
    delete args;
    return n;
  }
  /* super 后非 . / [ / (：单独 super 为语法错误。 */
  sink_.emit({Severity::Error, "SYNTAX_ERROR",
              "'super' must be followed by a property access or call",
              tok.start});
  return nullptr;
}

/* 字面量：数字 / 字符串 / 布尔 / null / 正则 / 无插值模板串 */
ASTNode *Parser::parseLiteral() {
  const Token &tok = current();
  switch (tok.kind) {
    case TokenKind::Number: {
      /* 旧式八进制（0123）严格模式禁止（ECMA-262 §12.9.3 / Annex B） */
      if (strict_ && tok.legacy_octal) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR",
                    "legacy octal literals are not allowed in strict mode", tok.start});
        return nullptr;
      }
      ASTNode *n = newNode(NodeType::Literal);
      n->loc = tok.start;
      n->literal.number = tok.number;
      n->literal.is_number = true;
      n->literal.is_bigint = tok.is_bigint;
      n->literal.bigint_str = tok.bigint_str; /* M4.3：BigInt 十进制数字串 */
      n->literal.text = tok.text;
      advance();
      return n;
    }
    case TokenKind::String: {
      /* 旧式八进制转义（\1-\7）严格模式禁止（ECMA-262 §12.9.4 / Annex B） */
      if (strict_ && tok.legacy_octal_escape) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR",
                    "legacy octal escape sequences are not allowed in strict mode", tok.start});
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
      delete tpl;
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
      delete tpl;
      return nullptr;
    }
    tpl->children.push_back(expr);

    /* 表达式结束后必须是模板闭合 } */
    if (!isSymbol(current(), "}")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '}' in template", current().start});
      delete tpl;
      return nullptr;
    }
    lexer_.rescanTemplatePart(); /* 把 }... 重扫为 TemplatePart/TemplateEnd */
  }
  return tpl;
}

/* 绑定位置受限目标（M5.1a）：Identifier / 嵌套模式。不消费 '='
 * （默认值由调用方经 parseDefaultValue 处理）——避免 parseAssignment
 * 把 [a, b=2] / {p: q=1} 吞成 Assignment 节点。 */
ASTNode *Parser::parseBindingTarget() {
  if (at(TokenKind::Identifier)) {
    ASTNode *id = newNode(NodeType::Identifier);
    id->loc = current().start;
    id->name = current().text;
    advance();
    return id;
  }
  if (isSymbol(current(), "{")) return parseObjectPattern();
  if (isSymbol(current(), "[")) return parseArrayPattern();
  sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected binding target",
              current().start});
  return nullptr;
}

/* 字面量 → 解构模式（M5.1a）：赋值左侧 [a] / {p} 递归转模式。
 * 原地修改 node（ArrayLiteral->ArrayPattern、ObjectLiteral->ObjectPattern）；
 * 元素/值中的 Assignment 节点 -> AssignmentPattern；其余非绑定目标失败。 */
bool Parser::convertLiteralToPattern(ASTNode *node) {
  if (node->type == NodeType::ArrayLiteral) {
    node->type = NodeType::ArrayPattern;
    for (ASTNode *elem : node->children) {
      if (elem == nullptr) continue; /* 空洞 */
      if (elem->type == NodeType::SpreadElement) {
        if (elem->children.empty()) return false;
        if (!convertLiteralToPattern(elem->children[0])) return false;
      } else if (elem->type == NodeType::Assignment) {
        /* 读值位置 [a, b = 2] 的 b = 2 已被 parseAssignment 吞成 Assignment。
         * 转成 AssignmentPattern{b, 2}。 */
        ASTNode *target = elem->children[0];
        if (target->type != NodeType::Identifier &&
            target->type != NodeType::ArrayLiteral &&
            target->type != NodeType::ObjectLiteral)
          return false;
        if (!convertLiteralToPattern(target)) return false;
        elem->type = NodeType::AssignmentPattern;
      } else if (!convertLiteralToPattern(elem)) {
        return false;
      }
    }
    return true;
  }
  if (node->type == NodeType::ObjectLiteral) {
    node->type = NodeType::ObjectPattern;
    for (ASTNode *member : node->children) {
      if (member == nullptr) continue;
      if (member->type == NodeType::SpreadElement) {
        if (member->children.empty()) return false;
        if (!convertLiteralToPattern(member->children[0])) return false;
        continue;
      }
      if (member->type != NodeType::Property || member->children.size() < 1) {
        return false;
      }
      /* 简写 { p }：单 child，无需转换。 */
      if (member->children.size() == 1) continue;
      ASTNode *val = member->children[1];
      if (val->type == NodeType::Assignment) {
        ASTNode *target = val->children[0];
        if (target->type != NodeType::Identifier &&
            target->type != NodeType::ArrayLiteral &&
            target->type != NodeType::ObjectLiteral)
          return false;
        if (!convertLiteralToPattern(target)) return false;
        val->type = NodeType::AssignmentPattern;
      } else if (!convertLiteralToPattern(val)) {
        return false;
      }
    }
    return true;
  }
  /* 叶子：Identifier/MemberExpression 是合法绑定目标（成员如 {k: dst.k}，
   * 含私有字段 dst.#x，M5.1e-d）；其他（Literal/调用等）非法。 */
  return node->type == NodeType::Identifier ||
         node->type == NodeType::MemberExpression;
}

/* 默认值（M5.1a）：target = default（current 指向 =）。产出
 * AssignmentPattern{target, default}。target 所有权转移：失败时销毁。 */
ASTNode *Parser::parseDefaultValue(ASTNode *target) {
  advance(); /* 消费 = */
  setValueContext();
  ASTNode *def = parseAssignment();
  if (def == nullptr) {
    delete target;
    return nullptr;
  }
  ASTNode *node = newNode(NodeType::AssignmentPattern);
  node->loc = target->loc;
  node->children.push_back(target);
  node->children.push_back(def);
  return node;
}

/* 对象模式（解构/字面量共用）：{ key: value, ...spread, shorthand }
 * as_literal=true 时产出 ObjectLiteral（读值位置）；否则 ObjectPattern（绑定位置）。 */
ASTNode *Parser::parseObjectPattern(bool as_literal) {
  ASTNode *obj = newNode(as_literal ? NodeType::ObjectLiteral : NodeType::ObjectPattern);
  obj->loc = current().start;
  advance(); /* { */
  while (!isSymbol(current(), "}")) {
    if (atEof()) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated object literal", obj->loc});
      delete obj;
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
      ASTNode *arg = as_literal ? parseAssignment() : parseBindingTarget();
      if (arg == nullptr) {
        delete obj;
        return nullptr;
      }
      spread->children.push_back(arg);
      obj->children.push_back(spread);
      continue;
    }
    /* 成员：key: value / method / getter / setter / shorthand / 计算键 [expr]（M5.1c） */
    ASTNode *prop = newNode(NodeType::Property);
    prop->loc = current().start;
    /* get/set 前缀：仅当后跟 key + '(' 时才是访问器（否则作普通属性名）。 */
    bool getset_prefix = false;
    PropertyKind getset_kind = PropertyKind::Init;
    if (at(TokenKind::Identifier) &&
        (current().text == "get" || current().text == "set")) {
      /* 保存 get/set 关键字文本（advance 后 current 变为 key）。 */
      const std::string getset_word = current().text;
      Lexer::State st = lexer_.save();
      advance(); /* get/set */
      /* 访问器探测（M5.1c）：
       *   key 为普通 Identifier：后跟 '(' 才是访问器（get x(){}）；
       *   key 为计算键 [expr]：get/set 后跟 '[' 必然是访问器（get [x](){}），
       *     因为 `get [x]: v` 是语法错误，不存在普通属性形态。 */
      bool is_getset;
      if (at(TokenKind::Identifier)) {
        is_getset = isSymbol(peek(), "(");
      } else if (isSymbol(current(), "[")) {
        is_getset = true;
      } else {
        is_getset = false;
      }
      if (is_getset) {
        getset_prefix = true;
        getset_kind = getset_word == "get" ? PropertyKind::Get
                                            : PropertyKind::Set;
        /* 已确认 get/set 前缀：不 advance，让下方统一 key 解析处理当前 token（key）。 */
      } else {
        lexer_.restore(st);
      }
    }
    /* async 方法前缀（M5.4）：{ async m() {} }。
     * async 后跟方法名起始（Identifier/String/Number/[）且后跟 ( 才是
     * async 方法；{ async: 1 } / { async(){} } 的 async 是普通键。 */
    bool async_prefix = false;
    if (at(TokenKind::Identifier) && current().text == "async") {
      Lexer::State st = lexer_.save();
      advance(); /* async */
      bool is_method_start = at(TokenKind::Identifier) || at(TokenKind::String) ||
                             at(TokenKind::Number) || isSymbol(current(), "[");
      if (is_method_start && isSymbol(peek(), "(")) {
        async_prefix = true;
      } else {
        lexer_.restore(st);
      }
    }
    /* generator 方法前缀（M5.5a）：{ * m() {} } / { *m() {} }。
     * * 后跟方法名起始（Identifier/String/Number/[）；与 async 前缀互斥
     * （async generator 方法 { async * m() {} } 在 async_prefix 置位后
     * 再检测 *）。*/
    bool generator_prefix = false;
    if (isSymbol(current(), "*")) {
      Lexer::State st = lexer_.save();
      advance(); /* * */
      bool is_method_start = at(TokenKind::Identifier) || at(TokenKind::String) ||
                             at(TokenKind::Number) || isSymbol(current(), "[");
      if (is_method_start) {
        generator_prefix = true;
      } else {
        lexer_.restore(st);
      }
    }
    /* key */
    ASTNode *key = nullptr;
    bool computed = false;
    if (isSymbol(current(), "[")) {
      /* 计算键 [expr]：key 为任意表达式。 */
      computed = true;
      prop->prop.is_computed = true;
      advance(); /* [ */
      key = parseAssignment();
      if (key == nullptr) {
        delete prop;
        delete obj;
        return nullptr;
      }
      if (!isSymbol(current(), "]")) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ']' after computed key",
                    current().start});
        delete prop;
        delete obj;
        return nullptr;
      }
      advance(); /* ] */
    } else if (at(TokenKind::Identifier)) {
      key = newNode(NodeType::Identifier);
      key->loc = current().start;
      key->name = current().text;
      advance();
    } else if (at(TokenKind::String) || at(TokenKind::Number)) {
      key = newNode(NodeType::Literal);
      key->loc = current().start;
      if (at(TokenKind::String)) {
        key->literal.is_string = true;
        key->literal.str = current().str_value;
      } else {
        key->literal.is_number = true;
        key->literal.number = current().number;
      }
      key->literal.text = current().text;
      advance();
    } else {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid property key", current().start});
      delete obj;
      return nullptr;
    }

    /* 成员形态分派：method / getter / setter / Init / shorthand */
    if (getset_prefix) {
      /* get key(){} / set key(v){}（M5.1c）：后跟参数列表。 */
      prop->prop.kind = getset_kind;
      ASTNode *fn = parseMethodFunction();
      if (fn == nullptr) {
        delete prop;
        delete obj;
        return nullptr;
      }
      prop->children.push_back(key);
      prop->children.push_back(fn);
    } else if (isSymbol(current(), "(")) {
      /* method key(){}（M5.1c）：函数体紧随；async 方法（M5.4）；
       * generator 方法（M5.5a）。 */
      prop->prop.kind = PropertyKind::Method;
      ASTNode *fn = parseMethodFunction(/*is_async=*/async_prefix,
                                        /*is_generator=*/generator_prefix);
      if (fn == nullptr) {
        delete prop;
        delete obj;
        return nullptr;
      }
      prop->children.push_back(key);
      prop->children.push_back(fn);
    } else if (isSymbol(current(), ":")) {
      /* key: value */
      prop->prop.kind = PropertyKind::Init;
      advance();
      ASTNode *val = as_literal ? parseAssignment() : parseBindingTarget();
      if (val == nullptr) {
        delete prop;
        delete obj;
        return nullptr;
      }
      if (!as_literal && isSymbol(current(), "=")) {
        /* 绑定位置重命名默认值 {p: q = 1}。 */
        ASTNode *def = parseDefaultValue(val);
        if (def == nullptr) {
          delete prop;
          delete obj;
          return nullptr;
        }
        prop->children.push_back(key);
        prop->children.push_back(def);
      } else {
        prop->children.push_back(key);
        prop->children.push_back(val);
      }
    } else if (isSymbol(current(), "=")) {
      /* 简写默认值 { key = default }（M5.1a），仅绑定位置。 */
      if (as_literal) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR",
                    "unexpected '=' in object literal", current().start});
        delete prop;
        delete obj;
        return nullptr;
      }
      prop->prop.kind = PropertyKind::Init;
      ASTNode *def = parseDefaultValue(key);
      if (def == nullptr) {
        delete prop;
        delete obj;
        return nullptr;
      }
      /* 简写默认值 { key = default }：key 归 AssignmentPattern 持有
       * （def->children[0]），prop 只持有 def，避免同一 key 指针被
       * prop 与 AssignmentPattern 双重释放（M5.1e-d 修复双 free）。 */
      prop->children.push_back(def);
    } else {
      /* 简写 { key } */
      prop->prop.kind = PropertyKind::Shorthand;
      prop->children.push_back(key);
    }
    obj->children.push_back(prop);
  }
  advance(); /* } */
  return obj;
}

/* 数组模式（解构/字面量共用）：[ elem, ...rest ]
 * as_literal=true 时产出 ArrayLiteral（读值位置）；否则 ArrayPattern（绑定位置）。 */
ASTNode *Parser::parseArrayPattern(bool as_literal) {
  ASTNode *arr = newNode(as_literal ? NodeType::ArrayLiteral : NodeType::ArrayPattern);
  arr->loc = current().start;
  advance(); /* [ */
  while (!isSymbol(current(), "]")) {
    if (atEof()) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated array literal", arr->loc});
      delete arr;
      return nullptr;
    }
    if (isSymbol(current(), ",")) {
      /* 空位（稀疏数组）。数组字面量：空洞不产生值（编译端紧凑存储）；
       * 解构模式：压 nullptr 占位保持索引语义（[a,,b] 的 b 索引 2）。 */
      advance();
      if (!as_literal) arr->children.push_back(nullptr);
      continue;
    }
    if (isSymbol(current(), "...")) {
      ASTNode *spread = newNode(NodeType::SpreadElement);
      spread->loc = current().start;
      advance();
      ASTNode *arg = as_literal ? parseAssignment() : parseBindingTarget();
      if (arg == nullptr) {
        delete arr;
        return nullptr;
      }
      spread->children.push_back(arg);
      arr->children.push_back(spread);
      /* 消费分隔逗号（否则下一轮循环把 [,] 误当空洞占位）。 */
      if (isSymbol(current(), ",")) advance();
      continue;
    }
    ASTNode *elem = nullptr;
    if (as_literal) {
      /* 读值位置：任意表达式元素。 */
      elem = parseAssignment();
    } else {
      /* 绑定位置：受限绑定目标（Identifier/嵌套模式），不吞 '='。 */
      elem = parseBindingTarget();
    }
    if (elem == nullptr) {
      delete arr;
      return nullptr;
    }
    /* 默认值 [a = 1]（M5.1a）：AssignmentPattern{a, 1}。读值位置（数组字面量）不允许。 */
    if (isSymbol(current(), "=")) {
      if (as_literal) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR",
                    "unexpected '=' in array literal", current().start});
        delete elem;
        delete arr;
        return nullptr;
      }
      ASTNode *def = parseDefaultValue(elem);
      if (def == nullptr) {
        delete arr;
        return nullptr;
      }
      elem = def;
    }
    arr->children.push_back(elem);
    /* 消费分隔逗号（非空洞）：进入下一元素。 */
    if (isSymbol(current(), ",")) advance();
  }
  advance(); /* ] */
  return arr;
}

}  // namespace neojs
