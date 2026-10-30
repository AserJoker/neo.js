#ifndef NEOJS_FRONTEND_PARSER_H
#define NEOJS_FRONTEND_PARSER_H

/*
 * 前端解析器（M1.1 骨架：最小子集）。
 *
 * 协同模式（docs/milestones.md M1.1）：Parser 按需向 Lexer 拉取 token
 * （advance()/peek()），并在解析表达式时显式设置 Lexer 上下文（读值/读操作），
 * 供 `/` 等歧义符号判定。
 *
 * M1.1 只覆盖最小解析：Program → 表达式语句列表；表达式 → 二元运算
 * （+ - * / %，左结合，优先级：* / % > + -）。AST 节点经 new 分配（继承
 * core::AllocatedObject），由返回的 AST 统一持有并在析构时释放（见 ast.h）。
 */

#include <memory>
#include <string>
#include <vector>

#include "neojs/frontend/ast.h"
#include "neojs/frontend/diagnostics.h"
#include "neojs/frontend/lexer.h"

namespace neojs {

/* 判断 token 是否为指定符号文本（如 "+"、"("）。内联定义，供
 * parser.cc（表达式）与 parser_stmt.cc（语句）共享。 */
inline bool isSymbol(const Token &tok, const char *sym) {
  return tok.kind == TokenKind::Symbol && tok.text == sym;
}

/* 判断 token 是否为指定关键字文本（关键字也是 Identifier kind） */
inline bool isKeyword(const Token &tok, const char *kw) {
  return tok.kind == TokenKind::Identifier && tok.text == kw;
}

class Parser {
 public:
  Parser(const char *source, size_t source_len,
         DiagnosticSink &sink, bool module = false);

  /* 解析整个 Program（或 Module）；失败（语法错误）返回 nullptr（错误在 sink）。 */
  AST *parse();

 private:
  /* 按需拉取 token（协同模式核心）：advance 推进，peek 前瞻不推进 */
  void advance() {
    lexer_.next();
    has_peeked_ = false;
  }
  const Token &current() const { return lexer_.current(); }
  const Token &peek() { return lexer_.peek(); }
  bool at(TokenKind k) const { return current().kind == k; }
  bool atEof() const { return at(TokenKind::EndOfFile); }

  /* 设置 Lexer 上下文（读值/读操作）：在解析表达式时显式调用 */
  void setValueContext() { lexer_.setContext(LexContext::Value); }
  void setOperatorContext() { lexer_.setContext(LexContext::Operator); }

  /* 最小递归下降：语句（parser_stmt.cc） */
  ASTNode *parseStatementList();
  ASTNode *parseStatement();      /* 语句总入口：按首 token 特征分派（含嵌套深度计数） */
  ASTNode *parseStatementInner(); /* parseStatement 的分派实现（import/export 仅嵌套深度 1 时合法） */
  ASTNode *parseBlock(bool detect_strict = false); /* 块语句：{ stmt... } */
  ASTNode *parseLabeledStatement(); /* 标签语句：label: stmt */
  ASTNode *parseIfStatement();      /* 条件语句：if (expr) stmt [else stmt] */
  ASTNode *parseWhileStatement();   /* while (expr) stmt / do stmt while (expr) */
  ASTNode *parseVariableDeclaration(bool consume_semicolon = true,
                                   bool allow_missing_const_init = false); /* 声明（M1.5 ①） */
  ASTNode *parseForStatement();        /* for/for-in/for-of/for await（M1.5 ③） */
  ASTNode *parseFunctionDeclaration(bool allow_anonymous = false, bool is_async = false, bool is_generator = false); /* 函数声明：function name(params) { body }（M1.5 ④）；is_async=async function（M5.4）；is_generator=function*（M5.5a）；export default 允许匿名 */
  ASTNode *parseFunctionExpression(bool is_async = false, bool is_generator = false);  /* 函数表达式：function [name](params) { body }（M1.5 ④）；is_async=async function（M5.4）；is_generator=function*（M5.5a） */
  ASTNode *parseMethodFunction(bool is_async = false, bool is_generator = false);      /* 对象方法函数：(params) { body }（M5.1c method/getter/setter）；is_async=async 方法（M5.4）；is_generator=* 方法（M5.5a） */
  ASTNode *parseReturnStatement();     /* 返回语句：return [expr]（M1.5 ④） */
  ASTNode *parseBreakStatement();      /* 跳出：break [label]（M1.5 ⑤） */
  ASTNode *parseContinueStatement();   /* 续环：continue [label]（M1.5 ⑤） */
  ASTNode *parseThrowStatement();      /* 抛出：throw expr（M1.5 ⑥） */
  ASTNode *parseTryStatement();        /* try/catch/finally（M1.5 ⑥） */
  ASTNode *parseWithStatement();       /* with：with (obj) stmt（M1.5 ⑦） */
  ASTNode *parseDebuggerStatement();   /* debugger（M1.5 ⑦） */
  ASTNode *parseImportDeclaration();   /* import 声明（M1.6 ③，module 上下文） */
  ASTNode *parseExportDeclaration();   /* export 声明（M1.6 ③，module 上下文） */
  ASTNode *parseClassDeclaration();    /* class 声明：class C { ... }（M5.1d-a） */
  ASTNode *parseClassExpression();     /* class 表达式：class [name] { ... }（M5.1d-a） */
  ASTNode *parseClassTail(bool is_declaration); /* class 头/体共享解析（M5.1d-a） */
  ASTNode *parseClassElement();        /* 单个类元素（方法/访问器，M5.1d-a） */
  ASTNode *parseExpressionStatement();
  /* super 属性访问（M5.1d-b）：super.x / super[expr]。仅 class 方法上下文
   * （super_depth_ > 0）合法；super()（构造调用）留 M5.1d-c。 */
  ASTNode *parseSuperProperty();

  /* 最小递归下降：表达式（parser.cc） */
  ASTNode *parseExpression(bool allow_in = true); /* 逗号序列（最低优先级，M1.4） */
  ASTNode *parseAssignment(bool allow_in = true); /* 赋值：= 与复合赋值（右结合） */
  ASTNode *parseYieldExpression(bool allow_in = true); /* yield / yield* / 裸 yield（M5.5a，§14.4） */
  ASTNode *parseArrowFunction(bool parenthesized, bool is_async = false); /* 箭头函数（表达式体）；is_async=async 箭头（M5.4） */
  ASTNode *parseConditional(bool allow_in = true); /* 条件表达式：?:（右结合） */
  ASTNode *parseUnary();           /* 一元运算：! ~ + - typeof/void/delete/await */
  ASTNode *parseLeftHandSide();    /* 左手边：成员/调用链（M1.4 ③-3） */
  ASTNode *parseNewExpression();   /* new 构造 */
  ASTNode *parseArguments();       /* 实参列表：(a, b, ...rest) */
  ASTNode *parseBinary(int min_prec, bool allow_in = true,
                       bool no_coalesce = false); /* 二元（含 ?? 与 ||/&& 隔离） */
  ASTNode *parsePrimary();         /* 数字 / 标识符 / 括号 / 字面量 */
  ASTNode *parseLiteral();         /* 数字/字符串/布尔/null/BigInt/正则 */
  ASTNode *parseTemplateLiteral(); /* 模板串（含 ${expr} 插值，M1.4 ③-5） */
  ASTNode *parseObjectPattern(bool as_literal = false); /* { ... } 模式（解构/字面量共用） */
  ASTNode *parseArrayPattern(bool as_literal = false);  /* [ ... ] 模式（解构/字面量共用） */
  ASTNode *parseDefaultValue(ASTNode *target); /* 默认值：target = default（M5.1a） */
  /* 绑定位置受限目标（M5.1a）：Identifier / 嵌套模式 / ...rest。
   * 不消费 '='（默认值由调用方经 parseDefaultValue 处理）——
   * 避免 parseAssignment 把 [a, b=2] / {p: q=1} 吞成 Assignment 节点。 */
  ASTNode *parseBindingTarget();
  /* 字面量 → 解构模式（M5.1a）：把赋值左侧的 ArrayLiteral/ObjectLiteral
   * 递归转换为 ArrayPattern/ObjectPattern；元素/值中的 Assignment 节点转
   * AssignmentPattern，非绑定目标返回 false。原地修改 node。 */
  bool convertLiteralToPattern(ASTNode *node);
  ASTNode *newNode(NodeType t);  /* new ASTNode(t)，继承 core::AllocatedObject */

  /* 严格模式辅助（M1.6 ②） */
  void collectBindingNames(const ASTNode *node, std::vector<std::string> &names);
  /* 参数重名 / eval / arguments 检查（strict 显式传入——body 解析后 strict_ 会恢复外层值）；
   * 返回 true 表示已发错误（调用方须销毁 fn） */
  bool checkStrictParams(ASTNode *fn, bool strict, bool has_name, int body_index);

  Lexer lexer_;
  DiagnosticSink &sink_;
  bool module_ = false; /* 模块模式（sourceType=module）：import/export/顶层 await（M1.6 ③） */
  bool has_peeked_ = false;
  int loop_depth_ = 0; /* 循环嵌套深度（for/while/do-while；break/continue 校验） */
  int function_depth_ = 0; /* 函数嵌套深度（return 顶层校验，M1.5 ④） */
  int async_depth_ = 0; /* async 函数嵌套深度（M5.4 await 合法性：>0 时 await 可用） */
  int generator_depth_ = 0; /* generator 函数嵌套深度（M5.5a yield 合法性：>0 时 yield 可用） */
  int statement_nesting_ = 0; /* 语句嵌套深度（import/export 仅模块顶层，M1.6 ③） */
  std::vector<std::string> label_stack_; /* 活跃标签栈（break/continue 引用校验） */
  int super_depth_ = 0; /* class 方法上下文深度（M5.1d-b super：>0 时 super 合法） */
  /* M5.1d-c1 super()：派生类（extends）构造器内合法。
   * class_has_extends_ = 当前 class 是否有 extends 子句（parseClassTail
   *   进入类体前设置，退出恢复）；
   * derived_ctor_depth_ = 派生类构造器方法体解析深度（>0 时 super() 合法）。 */
  bool class_has_extends_ = false;
  int derived_ctor_depth_ = 0;
  bool strict_ = false; /* 当前作用域严格模式（"use strict" directive / module 自动严格） */
};

}  // namespace neojs

#endif  // NEOJS_FRONTEND_PARSER_H
