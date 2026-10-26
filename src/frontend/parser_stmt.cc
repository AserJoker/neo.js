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
      if (stmt->directive == "use strict") strict_ = true; /* 严格模式 */
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
  ++statement_nesting_; /* 语句嵌套深度：import/export 仅模块顶层（==0） */
  ASTNode *result = parseStatementInner();
  --statement_nesting_;
  return result;
}

/* 语句分派实现（parseStatement 的嵌套深度包装见上） */
ASTNode *Parser::parseStatementInner() {
  /* 空语句：单分号 ;（ECMA-262 EmptyStatement） */
  if (isSymbol(current(), ";")) {
    ASTNode *stmt = newNode(NodeType::EmptyStatement);
    stmt->loc = current().start;
    advance(); /* ; */
    return stmt;
  }
  /* 块语句：{ stmt... }（M1.5 ②）。
   * 例外：{ 后是解构赋值模式（{a} = obj，M5.1a）时不能当块——
   * 前瞻匹配完整对象模式 { ... } 且其后是 = 时走表达式语句。 */
  if (isSymbol(current(), "{")) {
    Lexer::State st = lexer_.save();
    lexer_.next(); /* 越过 { */
    int depth = 1;
    bool is_destructure = false;
    while (depth > 0) {
      const Token &t = lexer_.next();
      if (t.kind == TokenKind::EndOfFile) break;
      if (isSymbol(t, "{")) depth++;
      else if (isSymbol(t, "}")) depth--;
      if (depth == 0) {
        if (isSymbol(lexer_.current(), "=")) is_destructure = true;
        break;
      }
    }
    lexer_.restore(st);
    if (is_destructure) return parseExpressionStatement();
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
  /* 函数声明（M1.5 ④）：function name(params) { body } */
  if (isKeyword(current(), "function")) {
    return parseFunctionDeclaration();
  }
  /* async 函数声明（M5.4）：async function name(params) { body }。
   * save/restore 前瞻确认 async 后跟 function（否则 async 是普通标识符）。 */
  if (at(TokenKind::Identifier) && current().text == "async") {
    Lexer::State st = lexer_.save();
    advance(); /* async */
    bool is_async_fn = isKeyword(current(), "function");
    lexer_.restore(st);
    if (is_async_fn) {
      advance(); /* async */
      return parseFunctionDeclaration(/*allow_anonymous=*/false, /*is_async=*/true);
    }
  }
  /* class 声明（M5.1d-a）：class C { ... } */
  if (isKeyword(current(), "class")) {
    return parseClassDeclaration();
  }
  /* 返回语句（M1.5 ④）：return [expr]。restricted production：
   * return 后换行立即插入分号（return\nx → return; x）。顶层 return 报错。 */
  if (isKeyword(current(), "return")) {
    return parseReturnStatement();
  }
  /* 跳出/续环（M1.5 ⑤）：break [label] / continue [label]。restricted production。 */
  if (isKeyword(current(), "break") || isKeyword(current(), "continue")) {
    if (isKeyword(current(), "break")) return parseBreakStatement();
    return parseContinueStatement();
  }
  /* 抛出（M1.5 ⑥）：throw expr。restricted production：throw 后换行报错。 */
  if (isKeyword(current(), "throw")) {
    return parseThrowStatement();
  }
  /* 异常处理（M1.5 ⑥）：try block [catch] [finally] */
  if (isKeyword(current(), "try")) {
    return parseTryStatement();
  }
  /* with 语句（M1.5 ⑦）：with (obj) stmt。废弃语法 → Warning（不中止）。
   * 严格模式报 SyntaxError（当前尚无严格模式检测，留 M1.6）。 */
  if (isKeyword(current(), "with")) {
    return parseWithStatement();
  }
  /* debugger 语句（M1.5 ⑦）：空语句，编译开关关闭时丢弃 */
  if (isKeyword(current(), "debugger")) {
    return parseDebuggerStatement();
  }
  /* 模块语句（M1.6 ③）：import / export 仅在模块顶层合法
   * （ModuleItemList，ECMA-262 §16.2；不可嵌套在块/语句体内）。 */
  if (isKeyword(current(), "import")) {
    /* import 有歧义：import('m') 是动态导入表达式（读值位置），
     * import x from 'm' 是声明语句。语句位置若 import 后紧跟 ( 则为
     * 动态导入表达式语句，否则是 import 声明。动态导入由表达式体系处理
     * （parsePrimary），此处仅处理声明。 */
    if (isSymbol(peek(), "(") || isSymbol(peek(), ".")) {
      /* import('m') 动态导入 / import.meta 表达式语句 */
      return parseExpressionStatement();
    }
    if (!module_) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "import declaration outside module",
                  current().start});
      return nullptr;
    }
    if (statement_nesting_ != 1) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "import declaration must be at top level of module",
                  current().start});
      return nullptr;
    }
    return parseImportDeclaration();
  }
  if (isKeyword(current(), "export")) {
    if (!module_) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "export declaration outside module",
                  current().start});
      return nullptr;
    }
    if (statement_nesting_ != 1) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "export declaration must be at top level of module",
                  current().start});
      return nullptr;
    }
    return parseExportDeclaration();
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
     * 之后紧跟 in/of（无 = 初始化器）。const 在 for-in/of 中允许无初始化。
     * 标识符 left：peek 看 in/of。模式 left（{a}/[k]）：需跳过整个模式
     * （跟踪 { } / [ ] 嵌套深度）再查 in/of，模式内部可有多层嵌套（M5.1e-d）。 */
    Lexer::State st = lexer_.save();
    lexer_.next(); /* 越过 var/let/const */
    bool in_of_after = false;
    if (lexer_.current().kind == TokenKind::Identifier) {
      /* left 后紧跟 in/of → for-in/of 的 left 声明（const 允许无初始化）。
       * 注：let x = 1 in obj 中 peek 是 =，此处不命中，走三段式（正确）。 */
      const Token &after = lexer_.peek();
      in_of_after = isKeyword(after, "in") || isKeyword(after, "of");
    } else if (isSymbol(lexer_.current(), "{") || isSymbol(lexer_.current(), "[")) {
      /* 跳过完整模式：跟踪括号嵌套深度到闭合，再查 in/of。
       * depth 初始 1：开头 { / [ 已计入。 */
      const char open_c = lexer_.current().text[0];
      const char close_c = (open_c == '{') ? '}' : ']';
      int depth = 1;
      bool closed = false;
      for (;;) {
        const Token &t = lexer_.next();
        if (t.kind == TokenKind::EndOfFile) break;
        if (isSymbol(t, open_c == '{' ? "{" : "[")) {
          ++depth;
        } else if (isSymbol(t, close_c == '}' ? "}" : "]")) {
          --depth;
          if (depth == 0) {
            closed = true;
            break;
          }
        }
      }
      if (closed) {
        const Token &after = lexer_.peek();
        in_of_after = isKeyword(after, "in") || isKeyword(after, "of");
      }
    }
    lexer_.restore(st);
    in_of_decl = in_of_after;
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
    /* 非声明 left 为数组/对象字面量时转为解构模式（M5.1e-d）：
     * for ({a} of arr) / for ([k] in obj)——字面量由 parseExpression
     * 产出 ArrayLiteral/ObjectLiteral，需递归转为对应模式。 */
    if (!is_decl && init != nullptr &&
        (init->type == NodeType::ArrayLiteral ||
         init->type == NodeType::ObjectLiteral)) {
      if (!convertLiteralToPattern(init)) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR",
                    "invalid for-in/of left target", init->loc});
        allocator_->destroy(init);
        return nullptr;
      }
    }
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
    } else if (strict_ && init != nullptr && init->type == NodeType::Identifier &&
               (init->name == "eval" || init->name == "arguments")) {
      /* 严格模式禁止 eval / arguments 作 for-in/of left（ECMA-262 §14.7.4） */
      sink_.emit({Severity::Error, "SYNTAX_ERROR",
                  "unexpected eval or arguments in strict mode", init->loc});
      allocator_->destroy(init);
      return nullptr;
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
      /* 严格模式禁止 eval / arguments 作绑定名（ECMA-262 §14.3.1） */
      if (strict_ && (current().text == "eval" || current().text == "arguments")) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR",
                    "unexpected eval or arguments in strict mode", current().start});
        allocator_->destroy(decl);
        return nullptr;
      }
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
    /* do stmt while (expr)：body 在循环上下文内（break/continue 可指向） */
    ++loop_depth_;
    ASTNode *body = parseStatement();
    --loop_depth_;
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
 * 支持嵌套标签：a: b: stmt。
 * 标签登记（M1.5 ⑤）：解析 body 期间标签名入 label_stack_，
 * 供 break/continue 引用校验；body 结束后弹出。 */
ASTNode *Parser::parseLabeledStatement() {
  ASTNode *label = newNode(NodeType::LabeledStatement);
  label->loc = current().start;
  label->name = current().text;
  /* 严格模式禁止 eval / arguments 作标签名（ECMA-262 §14.13.1） */
  if (strict_ && (label->name == "eval" || label->name == "arguments")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR",
                "unexpected eval or arguments in strict mode", current().start});
    allocator_->destroy(label);
    return nullptr;
  }
  advance(); /* label 名 */
  if (!isSymbol(current(), ":")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ':' after label", current().start});
    allocator_->destroy(label);
    return nullptr;
  }
  advance(); /* : */
  label_stack_.push_back(label->name);
  ASTNode *body = parseStatement();
  label_stack_.pop_back();
  if (body == nullptr) {
    allocator_->destroy(label);
    return nullptr;
  }
  label->children.push_back(body);
  return label;
}

/* 收集绑定名（M1.6 ②）：递归遍历参数节点，收集 Identifier 名。
 * 支持：Identifier、RestElement（...rest 的 target）、
 * ObjectPattern/ArrayPattern（解构内绑定）、AssignmentPattern（默认值）。 */
void Parser::collectBindingNames(const ASTNode *node, std::vector<std::string> &names) {
  if (node == nullptr) return;
  switch (node->type) {
    case NodeType::Identifier:
      names.push_back(node->name);
      break;
    case NodeType::RestElement:
    case NodeType::AssignmentPattern:
      /* 单子节点为目标绑定 */
      if (!node->children.empty()) collectBindingNames(node->children[0], names);
      break;
    case NodeType::ObjectPattern:
    case NodeType::ArrayPattern:
      for (const ASTNode *child : node->children) collectBindingNames(child, names);
      break;
    case NodeType::Property: {
      /* 对象模式成员：{ key: val } 中 val 是绑定目标；简写 { key } 时 children[0] 即绑定 */
      if (node->children.size() >= 2) {
        collectBindingNames(node->children[1], names); /* val */
      } else if (!node->children.empty()) {
        collectBindingNames(node->children[0], names); /* 简写 */
      }
      break;
    }
    case NodeType::SpreadElement:
      if (!node->children.empty()) collectBindingNames(node->children[0], names);
      break;
    default:
      break;
  }
}

/* 严格模式参数检查（M1.6 ②）：函数参数不能重名（ECMA-262 §14.1.2），
 * 不能是 eval / arguments（§14.1.2 与 §14.2.1 的 BindingIdentifier 限制）。
 * 该检查须在 body 解析完成后执行——因为 body 内的 "use strict" directive
 * 会让整函数成为严格模式（参数也受约束）。
 * fn children 布局：[id?, params..., body]；has_name=true 时 id 为 children[0]，
 * body_index 是 body 在 children 中的下标。
 * 返回 true 表示已发错误（调用方须销毁 fn）。 */
bool Parser::checkStrictParams(ASTNode *fn, bool strict, bool has_name, int body_index) {
  if (!strict) return false;
  /* 函数名不能是 eval / arguments（ECMA-262 §14.1.2 BindingIdentifier） */
  if (has_name && fn->children[0]->type == NodeType::Identifier &&
      (fn->children[0]->name == "eval" || fn->children[0]->name == "arguments")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR",
                "unexpected eval or arguments in strict mode", fn->loc});
    return true;
  }
  std::vector<std::string> names;
  /* 收集所有参数绑定名（id 之前到 body 之前的 children） */
  int start = has_name ? 1 : 0;
  for (int i = start; i < body_index; ++i) {
    collectBindingNames(fn->children[i], names);
  }
  /* 重名检测 */
  for (size_t i = 0; i < names.size(); ++i) {
    if (names[i] == "eval" || names[i] == "arguments") {
      sink_.emit({Severity::Error, "SYNTAX_ERROR",
                  "unexpected eval or arguments in strict mode", fn->loc});
      return true;
    }
    for (size_t j = i + 1; j < names.size(); ++j) {
      if (names[i] == names[j]) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR",
                    "duplicate parameter name not allowed in this context", fn->loc});
        return true;
      }
    }
  }
  return false;
}

/* 函数声明（M1.5 ④）：function name(params) { body }
 * ECMA-262 §15.4。children 布局：[id, params..., body]。
 * 声明必须有名字（function () {} 是表达式）；这里容错为 name 可空。
 * 参数：Identifier 或解构模式（后续展开）；body 为块语句。
 * function_depth_ 递增：body 内允许 return（顶层 return 报错）。 */
ASTNode *Parser::parseFunctionDeclaration(bool allow_anonymous, bool is_async) {
  ASTNode *fn = newNode(NodeType::FunctionDeclaration);
  fn->loc = current().start;
  fn->fn_is_async = is_async;
  advance(); /* function */

  /* 函数名（声明必需；缺省报错。allow_anonymous 供 export default 匿名声明） */
  bool has_name = false;
  fn->fn_has_name = false;
  if (at(TokenKind::Identifier)) {
    ASTNode *id = newNode(NodeType::Identifier);
    id->loc = current().start;
    id->name = current().text;
    advance();
    fn->children.push_back(id);
    has_name = true;
    fn->fn_has_name = true;
  } else if (!allow_anonymous) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected function name", current().start});
    allocator_->destroy(fn);
    return nullptr;
  }

  /* 参数列表：(a, b, ...rest) */
  if (!isSymbol(current(), "(")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '(' after function name",
                current().start});
    allocator_->destroy(fn);
    return nullptr;
  }
  advance(); /* ( */
  while (!isSymbol(current(), ")")) {
    if (atEof()) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated parameter list", fn->loc});
      allocator_->destroy(fn);
      return nullptr;
    }
    if (isSymbol(current(), ",")) {
      /* 尾逗号容错：f(a,) 合法（ES2017+） */
      advance();
      if (isSymbol(current(), ")")) break;
      continue;
    }
    /* 参数：标识符 / 解构模式 / 默认值 / rest（M5.1a 扩展） */
    ASTNode *param;
    if (isSymbol(current(), "...")) {
      /* rest 参数：...target（target 可为标识符或解构模式） */
      advance();
      ASTNode *rest = newNode(NodeType::RestElement);
      rest->loc = current().start;
      ASTNode *target;
      if (isSymbol(current(), "{") || isSymbol(current(), "[")) {
        if (isSymbol(current(), "{")) {
          target = parseObjectPattern();
        } else {
          target = parseArrayPattern();
        }
      } else if (at(TokenKind::Identifier)) {
        target = newNode(NodeType::Identifier);
        target->loc = current().start;
        target->name = current().text;
        advance();
      } else {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected parameter name",
                    current().start});
        allocator_->destroy(rest);
        allocator_->destroy(fn);
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
        allocator_->destroy(fn);
        return nullptr;
      }
    } else if (at(TokenKind::Identifier)) {
      param = newNode(NodeType::Identifier);
      param->loc = current().start;
      param->name = current().text;
      advance();
    } else {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected parameter", current().start});
      allocator_->destroy(fn);
      return nullptr;
    }
    /* 默认值 a = 1（M5.1a）：AssignmentPattern{a, 1} */
    if (isSymbol(current(), "=")) {
      ASTNode *def = parseDefaultValue(param);
      if (def == nullptr) {
        allocator_->destroy(fn);
        return nullptr;
      }
      param = def;
    }
    fn->children.push_back(param);
    if (!isSymbol(current(), ",") && !isSymbol(current(), ")")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ',' or ')' in parameter list",
                  current().start});
      allocator_->destroy(fn);
      return nullptr;
    }
  }
  advance(); /* ) */

  /* 函数体：{ 块语句 }；function_depth_ 递增（body 内 return 合法）。
   * async 函数（M5.4）：async_depth_ 递增，body 内 await 合法。 */
  if (!isSymbol(current(), "{")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '{' before function body",
                current().start});
    allocator_->destroy(fn);
    return nullptr;
  }
  ++function_depth_;
  if (is_async) ++async_depth_;
  int outer_super = super_depth_;
  super_depth_ = 0; /* 普通函数不继承 super 上下文（M5.1d-b） */
  bool outer_strict = strict_;
  ASTNode *body = parseBlock(/*detect_strict=*/true);
  bool strict_params = strict_; /* body 解析后 strict_ 已含 body directive 影响 */
  strict_ = outer_strict; /* 恢复外层严格状态（body 内 directive 不泄漏） */
  super_depth_ = outer_super;
  if (is_async) --async_depth_;
  --function_depth_;
  if (body == nullptr) {
    allocator_->destroy(fn);
    return nullptr;
  }
  fn->children.push_back(body);
  /* 严格模式参数检查（strict_params 已含 body directive 影响） */
  if (checkStrictParams(fn, strict_params, has_name,
                        static_cast<int>(fn->children.size()) - 1)) {
    allocator_->destroy(fn);
    return nullptr;
  }
  return fn;
}

/* 函数表达式（M1.5 ④）：function [name](params) { body }
 * 与声明结构相同，仅节点类型与 name 可选不同。供 parsePrimary 分派。 */
ASTNode *Parser::parseFunctionExpression(bool is_async) {
  ASTNode *fn = newNode(NodeType::FunctionExpression);
  fn->loc = current().start;
  fn->fn_is_async = is_async;
  advance(); /* function */

  /* 函数名可选（匿名函数表达式合法）。记录是否有名：children[0] 是否函数名
   * （匿名时 children[0] 是首个参数，checkStrictParams 需区分）。 */
  bool has_name = false;
  fn->fn_has_name = false;
  if (at(TokenKind::Identifier)) {
    ASTNode *id = newNode(NodeType::Identifier);
    id->loc = current().start;
    id->name = current().text;
    advance();
    fn->children.push_back(id);
    has_name = true;
    fn->fn_has_name = true;
  }

  /* 参数列表：(a, b, ...rest) */
  if (!isSymbol(current(), "(")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '(' after 'function'",
                current().start});
    allocator_->destroy(fn);
    return nullptr;
  }
  advance(); /* ( */
  while (!isSymbol(current(), ")")) {
    if (atEof()) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated parameter list", fn->loc});
      allocator_->destroy(fn);
      return nullptr;
    }
    if (isSymbol(current(), ",")) {
      advance();
      if (isSymbol(current(), ")")) break;
      continue;
    }
    ASTNode *param;
    if (isSymbol(current(), "...")) {
      /* rest 参数：...target（target 可为标识符或解构模式） */
      advance();
      ASTNode *rest = newNode(NodeType::RestElement);
      rest->loc = current().start;
      ASTNode *target;
      if (isSymbol(current(), "{") || isSymbol(current(), "[")) {
        if (isSymbol(current(), "{")) {
          target = parseObjectPattern();
        } else {
          target = parseArrayPattern();
        }
      } else if (at(TokenKind::Identifier)) {
        target = newNode(NodeType::Identifier);
        target->loc = current().start;
        target->name = current().text;
        advance();
      } else {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected parameter name",
                    current().start});
        allocator_->destroy(rest);
        allocator_->destroy(fn);
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
        allocator_->destroy(fn);
        return nullptr;
      }
    } else if (at(TokenKind::Identifier)) {
      param = newNode(NodeType::Identifier);
      param->loc = current().start;
      param->name = current().text;
      advance();
    } else {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected parameter", current().start});
      allocator_->destroy(fn);
      return nullptr;
    }
    /* 默认值 a = 1（M5.1a）：AssignmentPattern{a, 1} */
    if (isSymbol(current(), "=")) {
      ASTNode *def = parseDefaultValue(param);
      if (def == nullptr) {
        allocator_->destroy(fn);
        return nullptr;
      }
      param = def;
    }
    fn->children.push_back(param);
    if (!isSymbol(current(), ",") && !isSymbol(current(), ")")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ',' or ')' in parameter list",
                  current().start});
      allocator_->destroy(fn);
      return nullptr;
    }
  }
  advance(); /* ) */

  /* 函数体：{ 块语句 }。async 函数（M5.4）：async_depth_ 递增，await 合法。 */
  if (!isSymbol(current(), "{")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '{' before function body",
                current().start});
    allocator_->destroy(fn);
    return nullptr;
  }
  ++function_depth_;
  if (is_async) ++async_depth_;
  int outer_super = super_depth_;
  super_depth_ = 0; /* 普通函数不继承 super 上下文（M5.1d-b） */
  bool outer_strict = strict_;
  ASTNode *body = parseBlock(/*detect_strict=*/true);
  bool strict_params = strict_; /* body 解析后 strict_ 已含 body directive 影响 */
  strict_ = outer_strict; /* 恢复外层严格状态（body 内 directive 不泄漏） */
  super_depth_ = outer_super;
  if (is_async) --async_depth_;
  --function_depth_;
  if (body == nullptr) {
    allocator_->destroy(fn);
    return nullptr;
  }
  fn->children.push_back(body);
  /* 严格模式参数检查（strict_params 已含 body directive 影响） */
  if (checkStrictParams(fn, strict_params, has_name,
                        static_cast<int>(fn->children.size()) - 1)) {
    allocator_->destroy(fn);
    return nullptr;
  }
  return fn;
}

/* 对象方法函数（M5.1c）：(params) { body }（method / getter / setter）。
 * 与函数表达式同构，仅无 function 关键字；产出 FunctionExpression
 *（fn_has_name=false，children[0] 为首参数）。 */
ASTNode *Parser::parseMethodFunction(bool is_async) {
  ASTNode *fn = newNode(NodeType::FunctionExpression);
  fn->loc = current().start;
  fn->fn_has_name = false;
  fn->fn_is_async = is_async;

  /* 参数列表：(a, b, ...rest)，当前已位于 '('。 */
  advance(); /* ( */
  while (!isSymbol(current(), ")")) {
    if (atEof()) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated parameter list", fn->loc});
      allocator_->destroy(fn);
      return nullptr;
    }
    if (isSymbol(current(), ",")) {
      advance();
      if (isSymbol(current(), ")")) break;
      continue;
    }
    ASTNode *param;
    if (isSymbol(current(), "...")) {
      advance();
      ASTNode *rest = newNode(NodeType::RestElement);
      rest->loc = current().start;
      ASTNode *target;
      if (isSymbol(current(), "{") || isSymbol(current(), "[")) {
        target = isSymbol(current(), "{") ? parseObjectPattern() : parseArrayPattern();
      } else if (at(TokenKind::Identifier)) {
        target = newNode(NodeType::Identifier);
        target->loc = current().start;
        target->name = current().text;
        advance();
      } else {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected parameter name",
                    current().start});
        allocator_->destroy(rest);
        allocator_->destroy(fn);
        return nullptr;
      }
      rest->children.push_back(target);
      param = rest;
    } else if (isSymbol(current(), "{") || isSymbol(current(), "[")) {
      param = isSymbol(current(), "{") ? parseObjectPattern() : parseArrayPattern();
      if (param == nullptr) {
        allocator_->destroy(fn);
        return nullptr;
      }
    } else if (at(TokenKind::Identifier)) {
      param = newNode(NodeType::Identifier);
      param->loc = current().start;
      param->name = current().text;
      advance();
    } else {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected parameter", current().start});
      allocator_->destroy(fn);
      return nullptr;
    }
    if (isSymbol(current(), "=")) {
      ASTNode *def = parseDefaultValue(param);
      if (def == nullptr) {
        allocator_->destroy(fn);
        return nullptr;
      }
      param = def;
    }
    fn->children.push_back(param);
    if (!isSymbol(current(), ",") && !isSymbol(current(), ")")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ',' or ')' in parameter list",
                  current().start});
      allocator_->destroy(fn);
      return nullptr;
    }
  }
  advance(); /* ) */

  /* 函数体：{ 块语句 }。async 方法（M5.4）：async_depth_ 递增，await 合法。 */
  if (!isSymbol(current(), "{")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '{' before method body",
                current().start});
    allocator_->destroy(fn);
    return nullptr;
  }
  ++function_depth_;
  if (is_async) ++async_depth_;
  bool outer_strict = strict_;
  ASTNode *body = parseBlock(/*detect_strict=*/true);
  bool strict_params = strict_;
  strict_ = outer_strict;
  if (is_async) --async_depth_;
  --function_depth_;
  if (body == nullptr) {
    allocator_->destroy(fn);
    return nullptr;
  }
  fn->children.push_back(body);
  if (checkStrictParams(fn, strict_params, /*has_name=*/false,
                        static_cast<int>(fn->children.size()) - 1)) {
    allocator_->destroy(fn);
    return nullptr;
  }
  return fn;
}

/* class 声明/表达式共享：解析 class 头（名字/extends）与类体元素。
 * is_declaration=true（声明）时名字必填；false（表达式）时名字可空。
 *
 * children 布局：
 *   [0] = name（Identifier；声明必有，表达式可无）
 *   [1] = extends 子句表达式（可无，无则省略）
 *   [2..n] = 类元素（Property 节点：Method/Get/Set；prop.is_static 标 static）
 *
 * 类元素解析（M5.1d-a 方法集）：
 *   static 前缀：static m(){} / static get x(){} / static x
 *   constructor 方法：key 为 "constructor" 的 Method（构造器，仅一个）
 *   get/set 前缀探测同对象字面量（get x(){} / set x(v){}）
 *   [expr] 计算键：方法/访问器
 *   key(){} 方法：普通/static
 *   其余（字段 x = v / static x = v）M5.1d-d 支持，本阶段报语法错误 */
ASTNode *Parser::parseClassDeclaration() {
  return parseClassTail(true);
}

ASTNode *Parser::parseClassExpression() {
  return parseClassTail(false);
}

ASTNode *Parser::parseClassTail(bool is_declaration) {
  ASTNode *cls = newNode(is_declaration ? NodeType::ClassDeclaration
                                        : NodeType::ClassExpression);
  cls->loc = current().start;
  advance(); /* class */

  /* 名字：声明必填；表达式可空（匿名 class） */
  bool has_name = false;
  if (at(TokenKind::Identifier)) {
    ASTNode *id = newNode(NodeType::Identifier);
    id->loc = current().start;
    id->name = current().text;
    advance();
    cls->children.push_back(id);
    has_name = true;
  } else if (is_declaration) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "class declaration requires a name",
                current().start});
    allocator_->destroy(cls);
    return nullptr;
  }
  (void)has_name;

  /* extends 子句：class C extends Base { ... } */
  bool has_extends = false;
  if (isKeyword(current(), "extends")) {
    advance();
    setValueContext();
    ASTNode *base = parseLeftHandSide(); /* extends 必须是 LeftHandSideExpression */
    if (base == nullptr) {
      allocator_->destroy(cls);
      return nullptr;
    }
    cls->children.push_back(base);
    has_extends = true;
  }

  /* 类体：{ element... } */
  if (!isSymbol(current(), "{")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '{' before class body",
                current().start});
    allocator_->destroy(cls);
    return nullptr;
  }
  advance(); /* { */
  /* M5.1d-c1：类体解析期间标记当前类是否有 extends（super() 合法条件）。 */
  bool outer_has_extends = class_has_extends_;
  class_has_extends_ = has_extends;
  while (!isSymbol(current(), "}")) {
    if (atEof()) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated class body", cls->loc});
      allocator_->destroy(cls);
      return nullptr;
    }
    /* 空元素：分号 } */
    if (isSymbol(current(), ";")) {
      advance();
      continue;
    }
    ASTNode *elem = parseClassElement();
    if (elem == nullptr) {
      class_has_extends_ = outer_has_extends;
      allocator_->destroy(cls);
      return nullptr;
    }
    cls->children.push_back(elem);
  }
  advance(); /* } */
  class_has_extends_ = outer_has_extends;
  return cls;
}

/* 单个类元素（M5.1d-a）：static 前缀探测 + 方法/访问器解析。
 * 产出 Property 节点（prop.is_static 标 static；kind=Method/Get/Set）。 */
ASTNode *Parser::parseClassElement() {
  /* static 前缀：static 后跟方法/字段。static 是普通 Identifier，
   * 需 save/restore + 前瞻判断（static {} 块 / static x / static m(){}）。 */
  bool is_static = false;
  if (at(TokenKind::Identifier) && current().text == "static") {
    Lexer::State st = lexer_.save();
    advance(); /* static */
    /* static 后跟 {（static 块，M5.1d-d）、方法/字段起始才视为 static 修饰。
     * static 后跟 ( ) = 等则 static 是普通方法名（class { static(){} }）。 */
    if (isSymbol(current(), "{")) {
      /* M5.1d-d static 块：static { stmts } —— 类定义时按序执行。 */
      advance(); /* { */
      ASTNode *sb = newNode(NodeType::StaticBlock);
      sb->loc = current().start;
      while (!isSymbol(current(), "}")) {
        if (atEof()) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR",
                      "unterminated static block", sb->loc});
          allocator_->destroy(sb);
          return nullptr;
        }
        ASTNode *stmt = parseStatement();
        if (stmt == nullptr) {
          allocator_->destroy(sb);
          return nullptr;
        }
        sb->children.push_back(stmt);
      }
      advance(); /* } */
      if (isSymbol(current(), ";")) advance();
      return sb;
    }
    if (isSymbol(current(), "[") || isSymbol(current(), "*") ||
        isSymbol(current(), "#") ||
        at(TokenKind::Identifier) || at(TokenKind::String) ||
        at(TokenKind::Number)) {
      is_static = true;
    } else {
      lexer_.restore(st); /* 不是 static 修饰：static 是普通方法名 */
    }
  }

  /* get/set 前缀（仅方法形态；static get x(){} / get x(){}）。 */
  bool getset_prefix = false;
  bool is_get = false;
  if (at(TokenKind::Identifier) &&
      (current().text == "get" || current().text == "set")) {
    const std::string getset_word = current().text;
    Lexer::State st = lexer_.save();
    advance(); /* get/set */
    if (at(TokenKind::Identifier)) {
      getset_prefix = isSymbol(peek(), "(");
    } else if (isSymbol(current(), "[")) {
      getset_prefix = true;
    } else {
      getset_prefix = false;
    }
    if (getset_prefix) {
      is_get = getset_word == "get";
    } else {
      lexer_.restore(st);
    }
  }

  /* async 前缀（M5.4）：async [no LineTerminator here] key(...)。
   * async 后跟方法名起始（Identifier/String/Number/[/#）且后跟 ( 才是
   * async 方法（async m(){}）；async(){} 的 async 是普通方法名（后跟 (）。
   * 与 get/set 互斥（async get 不合法）；static async m(){} 已由 static
   * 前缀消费，current 现指向 async。 */
  bool async_prefix = false;
  if (at(TokenKind::Identifier) && current().text == "async") {
    Lexer::State st = lexer_.save();
    advance(); /* async */
    bool is_method_start = at(TokenKind::Identifier) || at(TokenKind::String) ||
                           at(TokenKind::Number) || isSymbol(current(), "[") ||
                           isSymbol(current(), "#");
    if (is_method_start && isSymbol(peek(), "(")) {
      async_prefix = true;
    } else {
      lexer_.restore(st);
    }
  }

  /* 键：Identifier / [expr] 计算键 / String / Number。 */
  ASTNode *prop = newNode(NodeType::Property);
  prop->loc = current().start;
  prop->prop.is_static = is_static;
  ASTNode *key = nullptr;
  bool is_computed = false;
  if (at(TokenKind::Identifier)) {
    key = newNode(NodeType::Identifier);
    key->loc = current().start;
    key->name = current().text;
    advance();
  } else if (isSymbol(current(), "[")) {
    is_computed = true;
    advance(); /* [ */
    setValueContext();
    ASTNode *inner = parseExpression();
    if (inner == nullptr) {
      allocator_->destroy(prop);
      return nullptr;
    }
    if (!isSymbol(current(), "]")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ']' after computed key",
                  current().start});
      allocator_->destroy(inner);
      allocator_->destroy(prop);
      return nullptr;
    }
    advance(); /* ] */
    key = inner;
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
  } else if (isSymbol(current(), "#")) {
    /* M5.1d-c2 私有字段/方法：键 = PrivateIdentifier（#name）。 */
    advance(); /* # */
    if (!at(TokenKind::Identifier)) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR",
                  "expected private field name after '#'", current().start});
      allocator_->destroy(prop);
      return nullptr;
    }
    key = newNode(NodeType::PrivateIdentifier);
    key->loc = current().start;
    key->name = current().text;
    advance();
  } else {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid class element key",
                current().start});
    allocator_->destroy(prop);
    return nullptr;
  }
  prop->prop.is_computed = is_computed;

  /* 形态分派：get/set 访问器 / method / 字段（M5.1d-d）
   * M5.1d-b super：class 方法体解析期间 super_depth_ 递增（super 合法）。
   * 对象字面量 method 不经过此路径（parseObjectPattern），不受影响。 */
  if (getset_prefix) {
    prop->prop.kind = is_get ? PropertyKind::Get : PropertyKind::Set;
    ++super_depth_;
    ASTNode *fn = parseMethodFunction();
    --super_depth_;
    if (fn == nullptr) {
      allocator_->destroy(key);
      allocator_->destroy(prop);
      return nullptr;
    }
    prop->children.push_back(key);
    prop->children.push_back(fn);
  } else if (isSymbol(current(), "(")) {
    prop->prop.kind = PropertyKind::Method;
    ++super_depth_;
    /* M5.1d-c1：派生类构造器体解析期间 derived_ctor_depth_ 递增
     * （super() 仅此上下文合法）。构造器 = 非 static + key 为
     * "constructor" + 当前类有 extends。 */
    bool is_ctor = (!is_static && key->type == NodeType::Identifier &&
                    key->name == "constructor");
    if (is_ctor && class_has_extends_) ++derived_ctor_depth_;
    ASTNode *fn = parseMethodFunction(/*is_async=*/async_prefix);
    if (is_ctor && class_has_extends_) --derived_ctor_depth_;
    --super_depth_;
    if (fn == nullptr) {
      allocator_->destroy(key);
      allocator_->destroy(prop);
      return nullptr;
    }
    prop->children.push_back(key);
    prop->children.push_back(fn);
  } else {
    /* 类字段（M5.1d-d）：#x; / #x = init; / x; / x = init;（public/static）。
     * 键可为 PrivateIdentifier（私有字段，M5.1d-c2）或 Identifier/计算键
     * （public 字段，M5.1d-d）。初始化表达式可缺省（= undefined）。 */
    prop->prop.kind = PropertyKind::Init;
    prop->children.push_back(key);
    if (isSymbol(current(), "=")) {
      advance(); /* = */
      setValueContext();
      ASTNode *init = parseAssignment();
      if (init == nullptr) {
        allocator_->destroy(key);
        allocator_->destroy(prop);
        return nullptr;
      }
      prop->children.push_back(init);
    }
  }

  /* 方法后分号（可省）：method(){}; */
  if (isSymbol(current(), ";")) advance();
  return prop;
}

/* 返回语句（M1.5 ④）：return [expr]。ECMA-262 §14.4.3。
 * children[0] 为返回值（可缺省：return;）。
 * restricted production：return 后遇换行立即插入分号（return\nx → return; x）。
 * 顶层 return（function_depth_ == 0）是语法错误。 */
ASTNode *Parser::parseReturnStatement() {
  ASTNode *stmt = newNode(NodeType::ReturnStatement);
  stmt->loc = current().start;
  advance(); /* return */

  /* 顶层 return 非法（仅在函数体内合法） */
  if (function_depth_ == 0) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "return outside function", stmt->loc});
    allocator_->destroy(stmt);
    return nullptr;
  }

  /* 返回值可省：; / 换行（restricted production）/ } / EOF 均视为 return; */
  if (!isSymbol(current(), ";") && !isSymbol(current(), "}") && !atEof() &&
      !current().has_newline_before) {
    setValueContext();
    ASTNode *arg = parseExpression();
    if (arg == nullptr) {
      allocator_->destroy(stmt);
      return nullptr;
    }
    stmt->children.push_back(arg);
  }
  /* 分号（ASI）：显式 ; 或换行/}/EOF 隐式插入 */
  if (isSymbol(current(), ";")) advance();
  return stmt;
}

/* 跳出语句（M1.5 ⑤）：break [label]。ECMA-262 §14.8.1。
 * children[0] 为标签引用（可缺省）；label 名存 name。
 * restricted production：break 后换行立即插入分号（break\nx → break; x）。
 * 校验：无标签时须在循环/switch 内（loop_depth_）；有标签时须存在（label_stack_）。
 * 标签名不能是函数边界外的（break 标签不能跨函数，但本实现按语句解析）。 */
ASTNode *Parser::parseBreakStatement() {
  ASTNode *stmt = newNode(NodeType::BreakStatement);
  stmt->loc = current().start;
  advance(); /* break */

  /* 可选标签：同一行紧跟 Identifier 才是标签引用 */
  if (at(TokenKind::Identifier) && !current().has_newline_before) {
    /* 校验标签存在性（label_stack_ 内的活跃标签） */
    bool found = false;
    for (const std::string &l : label_stack_) {
      if (l == current().text) {
        found = true;
        break;
      }
    }
    if (!found) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "undefined label '" + current().text + "'",
                  current().start});
      allocator_->destroy(stmt);
      return nullptr;
    }
    stmt->name = current().text;
    advance();
  } else if (loop_depth_ == 0) {
    /* 无标签 break 须在循环/switch 内 */
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "illegal break statement", stmt->loc});
    allocator_->destroy(stmt);
    return nullptr;
  }
  /* 分号（ASI）：显式 ; 或换行/}/EOF 隐式插入 */
  if (isSymbol(current(), ";")) advance();
  return stmt;
}

/* 续环语句（M1.5 ⑤）：continue [label]。ECMA-262 §14.8.2。
 * 校验比 break 严格：无标签时必须直接处于循环内（loop_depth_ > 0）；
 * 有标签时标签必须指向一个循环（label 指向非循环语句是非法的）。
 * 此处简化：有标签时仅校验标签存在；循环指向校验留待后续（需记录标签目标类型）。
 * restricted production：continue 后换行立即插入分号。 */
ASTNode *Parser::parseContinueStatement() {
  ASTNode *stmt = newNode(NodeType::ContinueStatement);
  stmt->loc = current().start;
  advance(); /* continue */

  if (at(TokenKind::Identifier) && !current().has_newline_before) {
    bool found = false;
    for (const std::string &l : label_stack_) {
      if (l == current().text) {
        found = true;
        break;
      }
    }
    if (!found) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "undefined label '" + current().text + "'",
                  current().start});
      allocator_->destroy(stmt);
      return nullptr;
    }
    stmt->name = current().text;
    advance();
  } else if (loop_depth_ == 0) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "illegal continue statement", stmt->loc});
    allocator_->destroy(stmt);
    return nullptr;
  }
  if (isSymbol(current(), ";")) advance();
  return stmt;
}

/* 抛出语句（M1.5 ⑥）：throw expr。ECMA-262 §14.15。
 * children[0] 为被抛出的表达式（必须存在）。
 * restricted production：throw 后换行即语法错误（无表达式）。 */
ASTNode *Parser::parseThrowStatement() {
  ASTNode *stmt = newNode(NodeType::ThrowStatement);
  stmt->loc = current().start;
  advance(); /* throw */

  /* throw 后换行 → 语法错误（restricted production 不允许空 throw） */
  if (current().has_newline_before) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "illegal newline after throw", stmt->loc});
    allocator_->destroy(stmt);
    return nullptr;
  }
  setValueContext();
  ASTNode *arg = parseExpression();
  if (arg == nullptr) {
    allocator_->destroy(stmt);
    return nullptr;
  }
  stmt->children.push_back(arg);
  /* 分号（ASI）：显式 ; 或换行/}/EOF 隐式插入 */
  if (isSymbol(current(), ";")) advance();
  return stmt;
}

/* 异常处理语句（M1.5 ⑥）：try block [catch] [finally]。ECMA-262 §14.16。
 * children 布局：[block, catch?, finally?]（catch/finally 可缺省，至少一个）。
 * catch 子句：CatchClause[ param?, block ]。
 *   - catch (e) { ... }：param 为绑定（Identifier/模式）
 *   - catch { ... }（ES2019）：无绑定，param 缺省
 * finally 子句：块语句。 */
ASTNode *Parser::parseTryStatement() {
  ASTNode *stmt = newNode(NodeType::TryStatement);
  stmt->loc = current().start;
  advance(); /* try */

  /* try 块 */
  if (!isSymbol(current(), "{")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '{' after 'try'", current().start});
    allocator_->destroy(stmt);
    return nullptr;
  }
  ASTNode *block = parseBlock();
  if (block == nullptr) {
    allocator_->destroy(stmt);
    return nullptr;
  }
  stmt->children.push_back(block);

  /* catch 子句（可选） */
  if (isKeyword(current(), "catch")) {
    ASTNode *clause = newNode(NodeType::CatchClause);
    clause->loc = current().start;
    advance(); /* catch */
    if (isSymbol(current(), "(")) {
      advance(); /* ( */
      /* 绑定参数：Identifier / 解构模式 */
      ASTNode *param;
      if (isSymbol(current(), "{") || isSymbol(current(), "[")) {
        if (isSymbol(current(), "{")) {
          param = parseObjectPattern();
        } else {
          param = parseArrayPattern();
        }
        if (param == nullptr) {
          allocator_->destroy(stmt);
          allocator_->destroy(clause);
          return nullptr;
        }
      } else if (at(TokenKind::Identifier)) {
        /* 严格模式禁止 eval / arguments 作 catch 绑定（ECMA-262 §14.15.1） */
        if (strict_ && (current().text == "eval" || current().text == "arguments")) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR",
                      "unexpected eval or arguments in strict mode", current().start});
          allocator_->destroy(stmt);
          allocator_->destroy(clause);
          return nullptr;
        }
        param = newNode(NodeType::Identifier);
        param->loc = current().start;
        param->name = current().text;
        advance();
      } else {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected catch binding",
                    current().start});
        allocator_->destroy(stmt);
        allocator_->destroy(clause);
        return nullptr;
      }
      clause->children.push_back(param);
      if (!isSymbol(current(), ")")) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ')' after catch binding",
                    current().start});
        allocator_->destroy(stmt);
        allocator_->destroy(clause);
        return nullptr;
      }
      advance(); /* ) */
    }
    if (!isSymbol(current(), "{")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '{' after 'catch'",
                  current().start});
      allocator_->destroy(stmt);
      allocator_->destroy(clause);
      return nullptr;
    }
    ASTNode *catch_body = parseBlock();
    if (catch_body == nullptr) {
      allocator_->destroy(stmt);
      allocator_->destroy(clause);
      return nullptr;
    }
    clause->children.push_back(catch_body);
    stmt->children.push_back(clause);
  }

  /* finally 子句（可选） */
  if (isKeyword(current(), "finally")) {
    advance(); /* finally */
    if (!isSymbol(current(), "{")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '{' after 'finally'",
                  current().start});
      allocator_->destroy(stmt);
      return nullptr;
    }
    ASTNode *finally_body = parseBlock();
    if (finally_body == nullptr) {
      allocator_->destroy(stmt);
      return nullptr;
    }
    stmt->children.push_back(finally_body);
  }

  /* 至少一个 catch 或 finally */
  if (stmt->children.size() < 2) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "missing catch or finally after try",
                stmt->loc});
    allocator_->destroy(stmt);
    return nullptr;
  }
  return stmt;
}

/* with 语句（M1.5 ⑦）：with (obj) stmt。ECMA-262 §14.18。
 * children 布局：[object, body]。
 * 废弃语法：非严格模式发 Warning（DEP-0001），不中止解析；
 * 严格模式报 SyntaxError（ECMA-262 §14.1.2）。 */
ASTNode *Parser::parseWithStatement() {
  ASTNode *stmt = newNode(NodeType::WithStatement);
  stmt->loc = current().start;
  if (strict_) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "'with' statement is not allowed in strict mode",
                stmt->loc});
    allocator_->destroy(stmt);
    return nullptr;
  }
  sink_.emit({Severity::Warning, "DEP-0001",
              "'with' statement is deprecated and forbidden in strict mode", stmt->loc});
  advance(); /* with */

  if (!isSymbol(current(), "(")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected '(' after 'with'", current().start});
    allocator_->destroy(stmt);
    return nullptr;
  }
  advance(); /* ( */
  setValueContext();
  ASTNode *object = parseExpression();
  if (object == nullptr) {
    allocator_->destroy(stmt);
    return nullptr;
  }
  if (!isSymbol(current(), ")")) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected ')' after with object",
                current().start});
    allocator_->destroy(stmt);
    allocator_->destroy(object);
    return nullptr;
  }
  advance(); /* ) */
  stmt->children.push_back(object);
  ASTNode *body = parseStatement();
  if (body == nullptr) {
    allocator_->destroy(stmt);
    return nullptr;
  }
  stmt->children.push_back(body);
  return stmt;
}

/* debugger 语句（M1.5 ⑦）：debugger;。无子节点。
 * 编译开关关闭时（调试器不可用）由 Compiler 丢弃，Parser 恒产出 AST 节点。 */
ASTNode *Parser::parseDebuggerStatement() {
  ASTNode *stmt = newNode(NodeType::DebuggerStatement);
  stmt->loc = current().start;
  advance(); /* debugger */
  /* 分号（ASI）：显式 ; 或换行/}/EOF 隐式插入 */
  if (isSymbol(current(), ";")) advance();
  return stmt;
}

/* import 声明（M1.6 ③）：ECMA-262 §16.3。
 * 全形式：
 *   import d from 'm'                      → [ImportDefaultSpecifier[local], source]
 *   import {a, b as c} from 'm'            → [ImportSpecifier[imported, local]...]
 *   import * as ns from 'm'                → [ImportNamespaceSpecifier[local]]
 *   import 'm'                             → 仅 source（无 specifier）
 *   import d, {a} from 'm' / import d, * as ns from 'm'（default + 其余）
 * children 布局：specifiers..., source（Literal 字符串节点）。 */
ASTNode *Parser::parseImportDeclaration() {
  ASTNode *stmt = newNode(NodeType::ImportDeclaration);
  stmt->loc = current().start;
  advance(); /* import */

  bool seen_default = false;
  for (;;) {
    if (isSymbol(current(), ";") || atEof()) break;
    if (isKeyword(current(), "from")) break;
    if (at(TokenKind::String)) break; /* 裸 import 'm' 直接到 source */
    if (isSymbol(current(), "{")) {
      /* 具名导入：{ a, b as c } */
      advance(); /* { */
      while (!isSymbol(current(), "}")) {
        if (atEof()) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated import specifier list",
                      stmt->loc});
          allocator_->destroy(stmt);
          return nullptr;
        }
        if (isSymbol(current(), ",")) {
          advance();
          continue;
        }
        if (!at(TokenKind::Identifier)) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected imported binding",
                      current().start});
          allocator_->destroy(stmt);
          return nullptr;
        }
        ASTNode *spec = newNode(NodeType::ImportSpecifier);
        spec->loc = current().start;
        spec->imported = current().text; /* 导入名 */
        advance();
        ASTNode *local = nullptr;
        if (isKeyword(current(), "as")) {
          advance();
          if (!at(TokenKind::Identifier)) {
            sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected local binding after 'as'",
                        current().start});
            allocator_->destroy(spec);
            allocator_->destroy(stmt);
            return nullptr;
          }
          local = newNode(NodeType::Identifier);
          local->loc = current().start;
          local->name = current().text;
          advance();
        } else {
          /* 未重命名：imported == local */
          local = newNode(NodeType::Identifier);
          local->loc = spec->loc;
          local->name = spec->imported;
        }
        spec->children.push_back(local);
        stmt->children.push_back(spec);
      }
      advance(); /* } */
    } else if (isSymbol(current(), "*")) {
      /* 命名空间导入：* as ns */
      ASTNode *spec = newNode(NodeType::ImportNamespaceSpecifier);
      spec->loc = current().start;
      advance(); /* * */
      if (!isKeyword(current(), "as")) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected 'as' after '*'", current().start});
        allocator_->destroy(spec);
        allocator_->destroy(stmt);
        return nullptr;
      }
      advance(); /* as */
      if (!at(TokenKind::Identifier)) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected namespace binding after 'as'",
                    current().start});
        allocator_->destroy(spec);
        allocator_->destroy(stmt);
        return nullptr;
      }
      ASTNode *local = newNode(NodeType::Identifier);
      local->loc = current().start;
      local->name = current().text;
      advance();
      spec->children.push_back(local);
      stmt->children.push_back(spec);
    } else if (at(TokenKind::Identifier) && !seen_default) {
      /* 默认导入：import d from 'm' */
      ASTNode *spec = newNode(NodeType::ImportDefaultSpecifier);
      spec->loc = current().start;
      ASTNode *local = newNode(NodeType::Identifier);
      local->loc = current().start;
      local->name = current().text;
      advance();
      spec->children.push_back(local);
      stmt->children.push_back(spec);
      seen_default = true;
    } else {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unexpected token in import declaration",
                  current().start});
      allocator_->destroy(stmt);
      return nullptr;
    }
    /* 默认导入后可跟 , { } 或 , * as */
    if (isSymbol(current(), ",")) {
      advance();
      continue;
    }
    break;
  }

  /* 模块名：from 'm' 或裸 import 'm' */
  if (isKeyword(current(), "from")) {
    advance(); /* from */
  }
  if (!at(TokenKind::String)) {
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected module specifier string",
                current().start});
    allocator_->destroy(stmt);
    return nullptr;
  }
  ASTNode *source = newNode(NodeType::Literal);
  source->loc = current().start;
  source->literal.is_string = true;
  source->literal.str = current().str_value;
  source->literal.text = current().text;
  advance();
  stmt->children.push_back(source);

  /* 分号（ASI） */
  if (isSymbol(current(), ";")) advance();
  return stmt;
}

/* export 声明（M1.6 ③）：ECMA-262 §16.4。
 * 全形式：
 *   export {a, b as c}                        → ExportNamedDeclaration[ExportSpecifier...]
 *   export {a} from 'm'                       → 同上 + source
 *   export * from 'm'                         → ExportAllDeclaration[source]
 *   export * as ns from 'm'                   → ExportAllDeclaration[source]（exported=ns）
 *   export default expr                       → ExportDefaultDeclaration[expr]
 *   export default function f() {} / class    → 同上（函数/类声明体）
 *   export const/let/var ... / export function f / export class C
 *                                            → ExportNamedDeclaration[declaration]
 *   export async function ...                 → 同上 */
ASTNode *Parser::parseExportDeclaration() {
  ASTNode *stmt = newNode(NodeType::ExportNamedDeclaration);
  stmt->loc = current().start;
  advance(); /* export */

  /* export default ... */
  if (isKeyword(current(), "default")) {
    stmt->type = NodeType::ExportDefaultDeclaration;
    advance(); /* default */
    /* export default function f() {} / export default async function ...
     * 注意：export default 允许匿名函数声明（ECMA-262 §16.4.2）。 */
    if (isKeyword(current(), "function")) {
      ASTNode *fn = parseFunctionDeclaration(/*allow_anonymous=*/true);
      if (fn == nullptr) {
        allocator_->destroy(stmt);
        return nullptr;
      }
      stmt->children.push_back(fn);
      return stmt;
    }
    /* export default async function f(){} / async function(){}（M5.4） */
    if (at(TokenKind::Identifier) && current().text == "async") {
      Lexer::State st = lexer_.save();
      advance(); /* async */
      bool is_async_fn = isKeyword(current(), "function");
      lexer_.restore(st);
      if (is_async_fn) {
        advance(); /* async */
        ASTNode *fn = parseFunctionDeclaration(/*allow_anonymous=*/true,
                                               /*is_async=*/true);
        if (fn == nullptr) {
          allocator_->destroy(stmt);
          return nullptr;
        }
        stmt->children.push_back(fn);
        return stmt;
      }
    }
    ASTNode *expr = parseAssignment();
    if (expr == nullptr) {
      allocator_->destroy(stmt);
      return nullptr;
    }
    stmt->children.push_back(expr);
    if (isSymbol(current(), ";")) advance();
    return stmt;
  }

  /* export * ... */
  if (isSymbol(current(), "*")) {
    stmt->type = NodeType::ExportAllDeclaration;
    advance(); /* * */
    if (isKeyword(current(), "as")) {
      /* export * as ns from 'm' */
      advance(); /* as */
      if (!at(TokenKind::Identifier)) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected namespace export name",
                    current().start});
        allocator_->destroy(stmt);
        return nullptr;
      }
      stmt->exported = current().text;
      advance();
    }
    if (!isKeyword(current(), "from")) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected 'from' after '*'", current().start});
      allocator_->destroy(stmt);
      return nullptr;
    }
    advance(); /* from */
    if (!at(TokenKind::String)) {
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected module specifier string",
                  current().start});
      allocator_->destroy(stmt);
      return nullptr;
    }
    ASTNode *source = newNode(NodeType::Literal);
    source->loc = current().start;
    source->literal.is_string = true;
    source->literal.str = current().str_value;
    source->literal.text = current().text;
    advance();
    stmt->children.push_back(source);
    if (isSymbol(current(), ";")) advance();
    return stmt;
  }

  /* export { a, b as c } [from 'm'] */
  if (isSymbol(current(), "{")) {
    advance(); /* { */
    while (!isSymbol(current(), "}")) {
      if (atEof()) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated export specifier list",
                    stmt->loc});
        allocator_->destroy(stmt);
        return nullptr;
      }
      if (isSymbol(current(), ",")) {
        advance();
        continue;
      }
      if (!at(TokenKind::Identifier)) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected exported binding",
                    current().start});
        allocator_->destroy(stmt);
        return nullptr;
      }
      ASTNode *spec = newNode(NodeType::ExportSpecifier);
      spec->loc = current().start;
      ASTNode *local = newNode(NodeType::Identifier);
      local->loc = current().start;
      local->name = current().text;
      advance();
      spec->children.push_back(local); /* local */
      if (isKeyword(current(), "as")) {
        advance();
        if (!at(TokenKind::Identifier)) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected exported name after 'as'",
                      current().start});
          allocator_->destroy(spec);
          allocator_->destroy(stmt);
          return nullptr;
        }
        spec->exported = current().text;
        advance();
      } else {
        spec->exported = spec->children[0]->name;
      }
      stmt->children.push_back(spec);
    }
    advance(); /* } */
    /* 可选 from 'm'（re-export） */
    if (isKeyword(current(), "from")) {
      advance();
      if (!at(TokenKind::String)) {
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "expected module specifier string",
                    current().start});
        allocator_->destroy(stmt);
        return nullptr;
      }
      ASTNode *source = newNode(NodeType::Literal);
      source->loc = current().start;
      source->literal.is_string = true;
      source->literal.str = current().str_value;
      source->literal.text = current().text;
      advance();
      stmt->children.push_back(source);
    }
    if (isSymbol(current(), ";")) advance();
    return stmt;
  }

  /* export const/let/var / export function / export class（声明导出） */
  ASTNode *decl = parseStatement();
  if (decl == nullptr) {
    allocator_->destroy(stmt);
    return nullptr;
  }
  stmt->children.push_back(decl);
  return stmt;
}

/* 块语句（M1.5 ②）：{ stmt... }。空块合法。
 * 注意：读值位置的 { 是对象字面量/模式（parsePrimary），由调用方区分；
 * 语句位置的 { 在此进入块。
 * detect_strict=true（函数体）：块开头是 Directive Prologue，检测
 * "use strict" 指令并设置 strict_（函数体严格模式覆盖外层）。 */
ASTNode *Parser::parseBlock(bool detect_strict) {
  ASTNode *block = newNode(NodeType::BlockStatement);
  block->loc = current().start;
  advance(); /* { */
  if (detect_strict) {
    /* 函数体 Directive Prologue：开头连续的字符串字面量语句 */
    while (!atEof() && at(TokenKind::String)) {
      ASTNode *stmt = parseExpressionStatement();
      if (stmt == nullptr) {
        allocator_->destroy(block);
        return nullptr;
      }
      ASTNode *expr = stmt->children[0];
      if (expr->type == NodeType::Literal && expr->literal.is_string) {
        stmt->directive = expr->literal.str;
        if (stmt->directive == "use strict") strict_ = true;
      }
      block->children.push_back(stmt);
    }
  }
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
