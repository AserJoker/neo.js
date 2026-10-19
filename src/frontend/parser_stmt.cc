#include "neojs/frontend/parser.h"

#include <string>

namespace neojs {

/* 语句体系独立成文件（parser_stmt.cc），与表达式体系（parser.cc）分离。
 * 语句会随 while/for/var/function 等继续膨胀，独立文件便于维护。
 * 共享判定：isSymbol / isKeyword 内联定义于 parser.h。 */

/* Program（M1.5）：先处理 hashbang（已在 parse() 入口跳过），再 Directive
 * Prologue（字符串字面量表达式语句 → 标记 directive，遇非字符串字面量即退出
 * 序言），然后循环读取 Statement 直到 EOF。 */
ASTNode *Parser::parseStatementList() {
  ASTNode *program = newNode(NodeType::Program);
  advance(); /* 初始化当前 token */

  /* Directive Prologue：仅开头的字符串字面量表达式语句 */
  while (!atEof() && at(TokenKind::String)) {
    ASTNode *stmt = parseExpressionStatement();
    if (stmt == nullptr) {
      allocator_->destroy(program);
      return nullptr;
    }
    /* 字符串字面量表达式语句 = 指令（directive）；此处记录原始值 */
    ASTNode *expr = stmt->children[0];
    if (expr->type == NodeType::Literal && expr->literal.is_string) {
      stmt->directive = expr->literal.str;
    }
    program->children.push_back(stmt);
  }

  while (!atEof()) {
    ASTNode *stmt = parseStatement();
    if (stmt == nullptr) {
      /* 语法错误：sink 已记录；丢弃整个 Program */
      allocator_->destroy(program);
      return nullptr;
    }
    program->children.push_back(stmt);
  }
  return program;
}

/* 语句总入口（M1.5）：按首 token 特征分派。各语句相对独立，一般可通过
 * 首单词判定（if/for/while/function/return 等）；无特征值时用表达式语句兜底。
 * 当前已实现：空语句（;）、块语句（{）、标签语句（label:）、条件语句（if）；
 * 后续逐个加分派。 */
ASTNode *Parser::parseStatement() {
  /* 空语句：单分号 ;（ECMA-262 EmptyStatement） */
  if (isSymbol(current(), ";")) {
    ASTNode *stmt = newNode(NodeType::EmptyStatement);
    stmt->loc = current().start;
    advance(); /* ; */
    return stmt;
  }
  /* 块语句：{ stmt... }（M1.5 ②） */
  if (isSymbol(current(), "{")) {
    return parseBlock();
  }
  /* 标签语句（M1.5 ③）：Identifier : stmt。独立成语句，不嵌入 for/while。
   * 判定特征：语句位置 Identifier + 前瞻 :（三元 ?: 的 : 由 ? 触发，不冲突）。 */
  if (at(TokenKind::Identifier) && isSymbol(peek(), ":")) {
    return parseLabeledStatement();
  }
  /* 条件语句（M1.5 ⑤）：if (expr) stmt [else stmt] */
  if (isKeyword(current(), "if")) {
    return parseIfStatement();
  }
  /* 循环语句（M1.5 ⑥）：while (expr) stmt / do stmt while (expr) */
  if (isKeyword(current(), "while") || isKeyword(current(), "do")) {
    return parseWhileStatement();
  }
  /* 声明语句（M1.5 ①）：var / let / const。
   * let 有歧义：let a = 1 是声明；但 let = 5 / let[0] = 1 中 let 是标识符
   * （表达式）。判定：let 后跟 Identifier / { / [ → 声明；否则表达式兜底。 */
  if (isKeyword(current(), "var") || isKeyword(current(), "const")) {
    return parseVariableDeclaration();
  }
  if (isKeyword(current(), "let")) {
    /* 当前是 let，看下一 token：
     *   let a = 1 → 声明（后跟 Identifier）
     *   let {a} = o → 声明（后跟 {）
     *   let [a] = arr / let [] = arr → 声明（后跟 [ 且是数组模式）
     *   let = 5 / let[0] = 1 / let.x → 表达式（let 作标识符）
     * `let [` 需排除下标访问：let[0] 中 [0] 是成员访问。
     * 判定 [ 后是否 ]（空模式）或标识符/模式元素，是则声明。 */
    const Token &t = peek();
    if (t.kind == TokenKind::Identifier) {
      return parseVariableDeclaration();
    }
    if (t.kind == TokenKind::Symbol && t.text == "{") {
      return parseVariableDeclaration();
    }
    if (t.kind == TokenKind::Symbol && t.text == "[") {
      /* 前瞻 [ 之后的 token 判定：空模式 []（peek 为 ]）或
       * 模式元素（peek 为 Identifier/嵌套 [ 或 {）→ 声明；
       * let[0] = 1 中 [0] 是下标访问（peek 为 Number）→ 表达式。 */
      Lexer::State st = lexer_.save();
      lexer_.next(); /* 越过 [，current 现在是 [ */
      bool is_decl = false;
      const Token &after = lexer_.peek(); /* [ 之后的 token */
      if (after.kind == TokenKind::Identifier || isSymbol(after, "]") ||
          isSymbol(after, "[") || isSymbol(after, "{")) {
        is_decl = true;
      }
      lexer_.restore(st);
      if (is_decl) return parseVariableDeclaration();
    }
  }
  /* 循环语句（M1.5 ③）：for / for-in / for-of / for await */
  if (isKeyword(current(), "for")) {
    return parseForStatement();
  }

  /* 兜底：表达式语句 */
  return parseExpressionStatement();
}

/* for 语句（M1.5 ③）：三段式 for(init;cond;update) 与 for-in / for-of。
 * ECMA-262 §14.7.2~14.7.5。
 * children 布局：
 *   - ForStatement：[init?, cond?, update?, body]
 *   - ForInStatement：[left, right, body]
 *   - ForOfStatement：[left, right, body]
 * init/left 可为表达式或 var/let/const 声明（复用 parseVariableDeclaration，
 * 但其自身消费尾分号——for 的 init 由分号切分，需特殊处理：
 *   for 声明 init 用 parseVariableDeclaration（内部吃 ;），
 *   for-in/of 声明 left 不能有初始化器（const x = 1 在 for-in 中非法）。
 * for await(...of...) 仅在 async 函数/模块上下文合法（此处仅记录 is_await）。 */
ASTNode *Parser::parseForStatement() {
  bool is_await = false;
  advance(); /* for */
  if (isKeyword(current(), "await")) {
    is_await = true;
    advance(); /* await */
  }
  if (!isSymbol(current(), "(")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '(' after 'for'", current().start});
    return nullptr;
  }
  advance(); /* ( */

  /* init/left：声明（var/let/const）或表达式。空 init 段 for(;;) 用 nullptr。
   * 注意：声明 init 复用 parseVariableDeclaration，但 for-in/of 的 left
   * 不能有初始化器（const x = 1 在 for-in 中非法）——此处先不校验，
   * 声明解析后若在 for-in 且带 init 再报错。 */
  ASTNode *init = nullptr;
  bool is_decl = isKeyword(current(), "var") || isKeyword(current(), "let") ||
                 isKeyword(current(), "const");
  bool in_of_decl = false;
  if (is_decl) {
    /* 探测是否为 for-in/of 的 left 声明：声明关键字后是标识符/模式且
     * 之后紧跟 in/of（无 = 初始化器）。const 在 for-in/of 中允许无初始化。 */
    Lexer::State st = lexer_.save();
    lexer_.next(); /* 越过 var/let/const */
    /* 跳过 left 标识符或模式（简化：标识符或 {/[ 模式） */
    bool looks_pattern = lexer_.current().kind == TokenKind::Identifier;
    if (isSymbol(lexer_.current(), "{") || isSymbol(lexer_.current(), "[")) {
      looks_pattern = true;
    }
    if (looks_pattern) {
      /* left 后紧跟 in/of → for-in/of 的 left 声明（const 允许无初始化）。
       * 注：let x = 1 in obj 中 peek 是 =，此处不命中，走三段式（正确）。 */
      const Token &after = lexer_.peek();
      in_of_decl = isKeyword(after, "in") || isKeyword(after, "of");
    }
    lexer_.restore(st);
    init = parseVariableDeclaration(/*consume_semicolon=*/false, in_of_decl);
    if (init == nullptr) return nullptr;
  } else if (!isSymbol(current(), ";") && !isKeyword(current(), "in") &&
             !isKeyword(current(), "of")) {
    /* init 段用 Expression[~In]（禁止 in 运算符）：for (k in obj) 中
     * in 是 for-in 关键字；若允许 in 会把它当二元运算符吞掉（错误）。 */
    init = parseExpression(/*allow_in=*/false);
    if (init == nullptr) return nullptr;
  }

  /* for-in / for-of：left 后跟 in / of */
  if (isKeyword(current(), "in") || isKeyword(current(), "of")) {
    bool is_in = isKeyword(current(), "in");
    /* for-in/of 的 left 为声明时不能有初始化器 */
    if (is_decl) {
      for (ASTNode *d : init->children) {
        if (d->children.size() > 1) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR",
                      "for-in/of loop variable declaration may not have an initializer",
                      d->loc});
          allocator_->destroy(init);
          return nullptr;
        }
      }
    }
    advance(); /* in / of */
    setValueContext();
    ASTNode *right = parseExpression();
    if (right == nullptr) {
      allocator_->destroy(init);
      return nullptr;
    }
    if (!isSymbol(current(), ")")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ')' after for-in/of right",
                  current().start});
      allocator_->destroy(init);
      allocator_->destroy(right);
      return nullptr;
    }
    advance(); /* ) */
    ASTNode *node = newNode(is_in ? NodeType::ForInStatement : NodeType::ForOfStatement);
    node->loc = init ? init->loc : current().start;
    node->is_await = is_await;
    node->children.push_back(init); /* left（可能为 nullptr：for (; in x) 非法，但容错） */
    node->children.push_back(right);
    ++loop_depth_;
    ASTNode *body = parseStatement();
    --loop_depth_;
    if (body == nullptr) {
      allocator_->destroy(node);
      return nullptr;
    }
    node->children.push_back(body);
    return node;
  }

  /* 三段式 for：init; cond; update（init 段已消费或为空） */
  ASTNode *for_stmt = newNode(NodeType::ForStatement);
  for_stmt->loc = current().start;
  if (!isSymbol(current(), ";")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ';' after for init", current().start});
    allocator_->destroy(init);
    allocator_->destroy(for_stmt);
    return nullptr;
  }
  advance(); /* ; */
  for_stmt->children.push_back(init); /* 可 nullptr */

  /* cond 段：分号结束（可空） */
  ASTNode *cond = nullptr;
  if (!isSymbol(current(), ";")) {
    cond = parseExpression();
    if (cond == nullptr) {
      allocator_->destroy(init);
      allocator_->destroy(for_stmt);
      return nullptr;
    }
  }
  if (!isSymbol(current(), ";")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ';' after for condition",
                current().start});
    allocator_->destroy(init);
    allocator_->destroy(cond);
    allocator_->destroy(for_stmt);
    return nullptr;
  }
  advance(); /* ; */
  for_stmt->children.push_back(cond); /* 可 nullptr */

  /* update 段：右括号结束（可空） */
  ASTNode *update = nullptr;
  if (!isSymbol(current(), ")")) {
    update = parseExpression();
    if (update == nullptr) {
      allocator_->destroy(init);
      allocator_->destroy(cond);
      allocator_->destroy(for_stmt);
      return nullptr;
    }
  }
  if (!isSymbol(current(), ")")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ')' after for clauses",
                current().start});
    allocator_->destroy(init);
    allocator_->destroy(cond);
    allocator_->destroy(update);
    allocator_->destroy(for_stmt);
    return nullptr;
  }
  advance(); /* ) */
  for_stmt->children.push_back(update); /* 可 nullptr */

  ++loop_depth_;
  ASTNode *body = parseStatement();
  --loop_depth_;
  if (body == nullptr) {
    allocator_->destroy(for_stmt);
    return nullptr;
  }
  for_stmt->children.push_back(body);
  return for_stmt;
}

/* 声明语句（M1.5 ①）：var a = 1, b; / let [x, y] = arr; / const {p} = obj;
 * ECMA-262 §14.3（var）、§14.4（let/const）。
 * 结构：VariableDeclaration(varkind)[VariableDeclarator...]
 *   VariableDeclarator[ id, init? ]；id 可为 Identifier 或模式（解构）。
 * const 必须初始化；let/var 可省。声明以分号结束（ASI）。 */
ASTNode *Parser::parseVariableDeclaration(bool consume_semicolon,
                                          bool allow_missing_const_init) {
  ASTNode *decl = newNode(NodeType::VariableDeclaration);
  decl->loc = current().start;
  const std::string &kw = current().text;
  decl->varkind = kw == "var" ? VarKind::Var : (kw == "let" ? VarKind::Let : VarKind::Const);
  advance(); /* var / let / const */

  for (;;) {
    ASTNode *id;
    if (isSymbol(current(), "{") || isSymbol(current(), "[")) {
      /* 解构声明：{a} / [a]（模式） */
      if (isSymbol(current(), "{")) {
        id = parseObjectPattern();
      } else {
        id = parseArrayPattern();
      }
      if (id == nullptr) {
        allocator_->destroy(decl);
        return nullptr;
      }
    } else if (at(TokenKind::Identifier)) {
      id = newNode(NodeType::Identifier);
      id->loc = current().start;
      id->name = current().text;
      advance();
    } else {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected variable name",
                  current().start});
      allocator_->destroy(decl);
      return nullptr;
    }

    ASTNode *declarator = newNode(NodeType::VariableDeclarator);
    declarator->loc = id->loc;
    declarator->children.push_back(id);

    /* 初始化：= expr */
    if (isSymbol(current(), "=")) {
      advance();
      setValueContext();
      ASTNode *init = parseAssignment();
      if (init == nullptr) {
        allocator_->destroy(declarator);
        allocator_->destroy(decl);
        return nullptr;
      }
      declarator->children.push_back(init);
    } else if (decl->varkind == VarKind::Const && !allow_missing_const_init) {
      /* const 必须初始化（for-in/of 的 left 除外，见 allow_missing_const_init） */
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "missing initializer in const declaration",
                  current().start});
      allocator_->destroy(declarator);
      allocator_->destroy(decl);
      return nullptr;
    }

    decl->children.push_back(declarator);
    if (!isSymbol(current(), ",")) break;
    advance(); /* , */
  }

  /* 分号结束（ASI 补全）。consume_semicolon=false（for 的 init 段）：
   * 分号由 for 结构消费，此处不吞。 */
  if (consume_semicolon) {
    if (isSymbol(current(), ";")) {
      advance();
    } else if (!atEof() && !isSymbol(current(), "}") && !current().has_newline_before) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ';' after variable declaration",
                  current().start});
      allocator_->destroy(decl);
      return nullptr;
    }
  }
  return decl;
}

/* 循环语句（M1.5 ⑥）：while (expr) stmt 与 do stmt while (expr);
 * ECMA-262 §14.4（while）、§14.6.1（do-while）。
 * children 布局：
 *   - WhileStatement：[条件, 循环体]
 *   - DoWhileStatement：[循环体, 条件]
 * do-while 尾随分号由 ASI 补全（换行/EOF 均可，分号非必须）。 */
ASTNode *Parser::parseWhileStatement() {
  bool is_do = isKeyword(current(), "do");
  ASTNode *stmt = newNode(is_do ? NodeType::DoWhileStatement : NodeType::WhileStatement);
  stmt->loc = current().start;
  advance(); /* while / do */

  if (is_do) {
    /* do stmt while (expr) */
    ASTNode *body = parseStatement();
    if (body == nullptr) {
      allocator_->destroy(stmt);
      return nullptr;
    }
    if (!isKeyword(current(), "while")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected 'while' after do body",
                  current().start});
      allocator_->destroy(stmt);
      allocator_->destroy(body);
      return nullptr;
    }
    stmt->children.push_back(body);
    advance(); /* while */
  }

  if (!isSymbol(current(), "(")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '(' after loop keyword",
                current().start});
    allocator_->destroy(stmt);
    return nullptr;
  }
  advance(); /* ( */
  setValueContext();
  ASTNode *cond = parseExpression();
  if (cond == nullptr) {
    allocator_->destroy(stmt);
    return nullptr;
  }
  if (!isSymbol(current(), ")")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ')' after loop condition",
                current().start});
    allocator_->destroy(stmt);
    allocator_->destroy(cond);
    return nullptr;
  }
  advance(); /* ) */

  if (is_do) {
    /* do-while：children[body, cond]（body 已在前段入列） */
    stmt->children.push_back(cond);
    /* do-while 尾分号可选（ASI 补全）：; / 换行 / EOF 均合法 */
    if (isSymbol(current(), ";")) advance();
  } else {
    /* while：children[cond, body]；body 在循环上下文内（break/continue 可指向） */
    stmt->children.push_back(cond);
    ++loop_depth_;
    ASTNode *body = parseStatement();
    --loop_depth_;
    if (body == nullptr) {
      allocator_->destroy(stmt);
      return nullptr;
    }
    stmt->children.push_back(body);
  }
  return stmt;
}

/* 条件语句（M1.5 ⑤）：if (expr) stmt [else stmt]。ECMA-262 §14.6。
 * children[0] = 条件表达式；children[1] = then 分支；children[2] = else 分支（无 else 时缺省）。
 * 悬挂 else（dangling else）：else 与最近的未匹配 if 结合（递归下降天然满足）。 */
ASTNode *Parser::parseIfStatement() {
  ASTNode *stmt = newNode(NodeType::IfStatement);
  stmt->loc = current().start;
  advance(); /* if */
  if (!isSymbol(current(), "(")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '(' after 'if'", current().start});
    allocator_->destroy(stmt);
    return nullptr;
  }
  advance(); /* ( */
  setValueContext();
  ASTNode *cond = parseExpression();
  if (cond == nullptr) {
    allocator_->destroy(stmt);
    return nullptr;
  }
  if (!isSymbol(current(), ")")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ')' after if condition",
                current().start});
    allocator_->destroy(stmt);
    allocator_->destroy(cond);
    return nullptr;
  }
  advance(); /* ) */
  stmt->children.push_back(cond);
  ASTNode *then_branch = parseStatement();
  if (then_branch == nullptr) {
    allocator_->destroy(stmt);
    return nullptr;
  }
  stmt->children.push_back(then_branch);
  if (isKeyword(current(), "else")) {
    advance(); /* else */
    ASTNode *else_branch = parseStatement();
    if (else_branch == nullptr) {
      allocator_->destroy(stmt);
      return nullptr;
    }
    stmt->children.push_back(else_branch);
  }
  return stmt;
}

/* 标签语句（M1.5 ③）：label: stmt。独立成语句（对齐 ESTree LabeledStatement），
 * 而非嵌入 for/while。children[0] 为被标记的语句。label 名存 name。
 * 支持嵌套标签：a: b: stmt。 */
ASTNode *Parser::parseLabeledStatement() {
  ASTNode *label = newNode(NodeType::LabeledStatement);
  label->loc = current().start;
  label->name = current().text;
  advance(); /* label 名 */
  if (!isSymbol(current(), ":")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ':' after label", current().start});
    allocator_->destroy(label);
    return nullptr;
  }
  advance(); /* : */
  ASTNode *body = parseStatement();
  if (body == nullptr) {
    allocator_->destroy(label);
    return nullptr;
  }
  label->children.push_back(body);
  return label;
}

/* 块语句（M1.5 ②）：{ stmt... }。空块合法。
 * 注意：读值位置的 { 是对象字面量/模式（parsePrimary），由调用方区分；
 * 语句位置的 { 在此进入块。 */
ASTNode *Parser::parseBlock() {
  ASTNode *block = newNode(NodeType::BlockStatement);
  block->loc = current().start;
  advance(); /* { */
  while (!isSymbol(current(), "}")) {
    if (atEof()) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated block", block->loc});
      allocator_->destroy(block);
      return nullptr;
    }
    ASTNode *stmt = parseStatement();
    if (stmt == nullptr) {
      allocator_->destroy(block);
      return nullptr;
    }
    block->children.push_back(stmt);
  }
  advance(); /* } */
  return block;
}

/* 表达式语句（M1.5 ④）：表达式 + 分号。分号由 ASI（自动分号插入）补全，
 * ECMA-262 §12.10：
 *   - 显式分号：直接消费。
 *   - 隐式插入（不报错），满足任一：
 *       ① 当前 token 前有 LineTerminator（表达式已完整，无法继续）；
 *       ② 当前 token 是 }（块语句/语句列表结束）；
 *       ③ 当前是 EOF（源码结束）。
 *   - 否则（同一行仍有无法继续的 token，如 a b）→ 语法错误（缺分号）。 */
ASTNode *Parser::parseExpressionStatement() {
  ASTNode *expr = parseExpression();
  if (expr == nullptr) return nullptr;
  ASTNode *stmt = newNode(NodeType::ExprStmt);
  stmt->children.push_back(expr);
  if (isSymbol(current(), ";")) {
    advance(); /* 显式分号 */
  } else if (!atEof() && !isSymbol(current(), "}") && !current().has_newline_before) {
    /* 同一行出现无法继续表达式的 token：缺分号（a b 是语法错误） */
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ';' before statement",
                current().start});
    allocator_->destroy(stmt);
    return nullptr;
  }
  /* 其余（换行后 token / } / EOF）由 ASI 隐式插入分号 */
  return stmt;
}

}  // namespace neojs
