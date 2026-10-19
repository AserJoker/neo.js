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
 * （+ - * / %，左结合，优先级：* / % > + -）。AST 节点经 JSAllocator 分配，
 * 由返回的 AST 统一持有并在析构时释放（见 ast.h）。
 */

#include <memory>
#include <string>

#include "neojs/frontend/ast.h"
#include "neojs/frontend/diagnostics.h"
#include "neojs/frontend/lexer.h"
#include "neojs/runtime/allocator.h"

namespace neojs {

class Parser {
 public:
  Parser(JSAllocator *allocator, const char *source, size_t source_len,
         DiagnosticSink &sink);

  /* 解析整个 Program；失败（语法错误）返回 nullptr（错误在 sink）。 */
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

  /* 最小递归下降 */
  ASTNode *parseStatementList();
  ASTNode *parseStatement();      /* 语句总入口：按首 token 特征分派 */
  ASTNode *parseBlock();          /* 块语句：{ stmt... } */
  ASTNode *parseLabeledStatement(); /* 标签语句：label: stmt */
  ASTNode *parseExpressionStatement();
  ASTNode *parseExpression();      /* 逗号序列（最低优先级，M1.4） */
  ASTNode *parseAssignment();      /* 赋值：= 与复合赋值（右结合） */
  ASTNode *parseArrowFunction(bool parenthesized); /* 箭头函数（表达式体） */
  ASTNode *parseConditional();     /* 条件表达式：?:（右结合） */
  ASTNode *parseUnary();           /* 一元运算：! ~ + - typeof/void/delete/await */
  ASTNode *parseLeftHandSide();    /* 左手边：成员/调用链（M1.4 ③-3） */
  ASTNode *parseNewExpression();   /* new 构造 */
  ASTNode *parseArguments();       /* 实参列表：(a, b, ...rest) */
  ASTNode *parseBinary(int min_prec);
  ASTNode *parsePrimary();         /* 数字 / 标识符 / 括号 / 字面量 */
  ASTNode *parseLiteral();         /* 数字/字符串/布尔/null/BigInt/正则 */
  ASTNode *parseTemplateLiteral(); /* 模板串（含 ${expr} 插值，M1.4 ③-5） */
  ASTNode *parseObjectPattern();   /* { ... } 模式（对象字面量/解构共用） */
  ASTNode *parseArrayPattern();    /* [ ... ] 模式（数组字面量/解构共用） */
  ASTNode *newNode(NodeType t);

  JSAllocator *allocator_;
  Lexer lexer_;
  DiagnosticSink &sink_;
  bool has_peeked_ = false;
};

}  // namespace neojs

#endif  // NEOJS_FRONTEND_PARSER_H
