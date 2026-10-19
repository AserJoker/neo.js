/*
 * 前端流水线单元测试（M1.1：Lexer/Parser/Diagnostics 骨架）。
 * 验证：token 最小流、AST 最小树、语法错误位置、协同模式上下文传递、
 * 注释贪婪消解（多字符符号回退单 /）、JSAllocator 内存管理（无泄漏）。
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

#include "neojs/frontend/diagnostics.h"
#include "neojs/frontend/frontend.h"
#include "neojs/frontend/lexer.h"
#include "neojs/frontend/parser.h"
#include "neojs/runtime/allocator.h"

namespace {

/* 栈上分配器：自身内存由宿主（此处栈）分配，测试结束自动回收。
 * 内部对象（AST/节点）经其 allocate/destroy 路由到 malloc/free。 */
void *testAlloc(size_t size, void *) { return std::malloc(size); }
void testFree(void *ptr, void *) { std::free(ptr); }

neojs::JSAllocator makeAllocator() {
  return neojs::JSAllocator(testAlloc, testFree, nullptr);
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* Lexer：最小 token 流 + 注释贪婪消解                                        */
/* -------------------------------------------------------------------------- */

TEST(Frontend, TokenStreamMinimal) {
  neojs::DiagnosticSink sink;
  neojs::Lexer lexer("1 + 2", 5, sink);

  const neojs::Token &t1 = lexer.next();
  EXPECT_EQ(t1.kind, neojs::TokenKind::Number);
  EXPECT_DOUBLE_EQ(t1.number, 1.0);
  EXPECT_EQ(t1.text, "1");

  const neojs::Token &t2 = lexer.next();
  EXPECT_EQ(t2.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(t2.text, "+");

  const neojs::Token &t3 = lexer.next();
  EXPECT_EQ(t3.kind, neojs::TokenKind::Number);
  EXPECT_DOUBLE_EQ(t3.number, 2.0);

  const neojs::Token &t4 = lexer.next();
  EXPECT_EQ(t4.kind, neojs::TokenKind::EndOfFile);
}

/* 符号表贪婪匹配：多字符符号优先（最长匹配） */
TEST(Frontend, SymbolGreedyLongestMatch) {
  neojs::DiagnosticSink sink;

  /* 三字符 `===` 优先于二字符 `==`、单字符 `=` */
  neojs::Lexer lexer1("===", 3, sink);
  const neojs::Token &s1 = lexer1.next();
  EXPECT_EQ(s1.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(s1.text, "===");
  EXPECT_EQ(lexer1.next().kind, neojs::TokenKind::EndOfFile);

  /* 二字符 `>>` 优先于单字符 `>`；三字符 `>>>` 优先于二字符 `>>` */
  neojs::Lexer lexer2(">>>=", 4, sink);
  const neojs::Token &s2 = lexer2.next();
  EXPECT_EQ(s2.text, ">>>="); /* 四字符 */
  EXPECT_EQ(lexer2.next().kind, neojs::TokenKind::EndOfFile);

  neojs::Lexer lexer3("a >>> b", 7, sink);
  EXPECT_EQ(lexer3.next().kind, neojs::TokenKind::Identifier);
  EXPECT_EQ(lexer3.next().text, ">>>");
  EXPECT_EQ(lexer3.next().kind, neojs::TokenKind::Identifier);

  /* 单字符仍正常 */
  neojs::Lexer lexer4("a > b", 5, sink);
  EXPECT_EQ(lexer4.next().kind, neojs::TokenKind::Identifier);
  EXPECT_EQ(lexer4.next().text, ">");
  EXPECT_EQ(lexer4.next().kind, neojs::TokenKind::Identifier);

  EXPECT_FALSE(sink.hasErrors());
}

/* 符号表支持 M1.4 符号（表驱动，无需新增 Token 类型） */
TEST(Frontend, SymbolTableExtensible) {
  neojs::DiagnosticSink sink;

  /* 箭头函数符号 */
  neojs::Lexer lexer1("=>", 2, sink);
  const neojs::Token &t1 = lexer1.next();
  EXPECT_EQ(t1.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(t1.text, "=>");

  /* 可选链 */
  neojs::Lexer lexer2("?.", 2, sink);
  const neojs::Token &t2 = lexer2.next();
  EXPECT_EQ(t2.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(t2.text, "?.");

  /* 展开运算符 */
  neojs::Lexer lexer3("...", 3, sink);
  const neojs::Token &t3 = lexer3.next();
  EXPECT_EQ(t3.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(t3.text, "...");

  EXPECT_FALSE(sink.hasErrors());
}

TEST(Frontend, LineCommentSkipped) {
  neojs::DiagnosticSink sink;
  neojs::Lexer lexer("1 // comment\n2", 14, sink);

  const neojs::Token &t1 = lexer.next();
  EXPECT_EQ(t1.kind, neojs::TokenKind::Number);
  EXPECT_EQ(t1.text, "1");
  EXPECT_EQ(t1.start.line, 1u);
  EXPECT_EQ(t1.start.col, 1u);

  const neojs::Token &t2 = lexer.next();
  EXPECT_EQ(t2.kind, neojs::TokenKind::Number);
  EXPECT_EQ(t2.text, "2");
  EXPECT_EQ(t2.start.line, 2u);
  EXPECT_EQ(t2.start.col, 1u);
}

TEST(Frontend, BlockCommentSkipped) {
  neojs::DiagnosticSink sink;
  neojs::Lexer lexer("1 /* x */ 2", 11, sink);

  EXPECT_EQ(lexer.next().kind, neojs::TokenKind::Number);
  EXPECT_EQ(lexer.next().kind, neojs::TokenKind::Number);
  EXPECT_EQ(lexer.next().kind, neojs::TokenKind::EndOfFile);
  EXPECT_FALSE(sink.hasErrors());
}

TEST(Frontend, UnterminatedBlockCommentErrors) {
  neojs::DiagnosticSink sink;
  neojs::Lexer lexer("1 /* x", 6, sink);

  EXPECT_EQ(lexer.next().kind, neojs::TokenKind::Number);
  EXPECT_EQ(lexer.next().kind, neojs::TokenKind::EndOfFile);
  EXPECT_TRUE(sink.hasErrors());
  EXPECT_EQ(sink.diagnostics().size(), 1u);
  EXPECT_EQ(sink.diagnostics()[0].severity, neojs::Severity::Error);
}

TEST(Frontend, ContextTracksValueOperator) {
  neojs::DiagnosticSink sink;
  neojs::Lexer lexer("a / b", 5, sink);

  /* 初始上下文为读值；Parser 在解析时会显式切换（M1.3 用于判定除法/正则） */
  EXPECT_EQ(lexer.context(), neojs::LexContext::Value);
  lexer.setContext(neojs::LexContext::Operator);
  EXPECT_EQ(lexer.context(), neojs::LexContext::Operator);

  lexer.next(); /* identifier a */
  EXPECT_EQ(lexer.context(), neojs::LexContext::Operator);
}

TEST(Frontend, PeekDoesNotAdvanceCurrent) {
  neojs::DiagnosticSink sink;
  neojs::Lexer lexer("1 + 2", 5, sink);

  EXPECT_EQ(lexer.next().kind, neojs::TokenKind::Number);
  const neojs::Token &p = lexer.peek();
  EXPECT_EQ(p.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(p.text, "+");
  /* current 仍是 Number */
  EXPECT_EQ(lexer.current().kind, neojs::TokenKind::Number);
  /* 后续 next 正常推进 */
  const neojs::Token &n = lexer.next();
  EXPECT_EQ(n.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(n.text, "+");
}

/* -------------------------------------------------------------------------- */
/* Parser：最小 AST + 优先级 + 括号 + 错误                                     */
/* -------------------------------------------------------------------------- */

TEST(Frontend, ParseMinimalExpression) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "1 + 2", 5, sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());

  /* Program → ExprStmt → BinaryOp(+) → [Literal(1), Literal(2)] */
  ASSERT_NE(ast->root, nullptr);
  EXPECT_EQ(ast->root->type, neojs::NodeType::Program);
  ASSERT_EQ(ast->root->children.size(), 1u);
  neojs::ASTNode *stmt = ast->root->children[0];
  EXPECT_EQ(stmt->type, neojs::NodeType::ExprStmt);
  ASSERT_EQ(stmt->children.size(), 1u);
  neojs::ASTNode *bin = stmt->children[0];
  EXPECT_EQ(bin->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(bin->binop, neojs::BinaryOpKind::Add);
  ASSERT_EQ(bin->children.size(), 2u);
  EXPECT_EQ(bin->children[0]->type, neojs::NodeType::Literal);
  EXPECT_DOUBLE_EQ(bin->children[0]->literal.number, 1.0);
  EXPECT_EQ(bin->children[1]->type, neojs::NodeType::Literal);
  EXPECT_DOUBLE_EQ(bin->children[1]->literal.number, 2.0);

  a.destroy(ast);
}

TEST(Frontend, ParsePrecedence) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 1 + 2 * 3 → BinaryOp(+)[1, BinaryOp(*)[2,3]] */
  neojs::AST *ast = neojs::parseSource(&a, "1 + 2 * 3", 9, sink);
  ASSERT_NE(ast, nullptr);

  neojs::ASTNode *bin = ast->root->children[0]->children[0];
  EXPECT_EQ(bin->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(bin->binop, neojs::BinaryOpKind::Add);
  neojs::ASTNode *rhs = bin->children[1];
  EXPECT_EQ(rhs->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(rhs->binop, neojs::BinaryOpKind::Mul);

  a.destroy(ast);
}

TEST(Frontend, ParseLeftAssociativity) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 1 - 2 - 3 → BinaryOp(-)[BinaryOp(-)[1,2],3]（左结合） */
  neojs::AST *ast = neojs::parseSource(&a, "1 - 2 - 3", 9, sink);
  ASSERT_NE(ast, nullptr);

  neojs::ASTNode *bin = ast->root->children[0]->children[0];
  EXPECT_EQ(bin->binop, neojs::BinaryOpKind::Sub);
  EXPECT_EQ(bin->children[0]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(bin->children[0]->binop, neojs::BinaryOpKind::Sub);
  EXPECT_EQ(bin->children[1]->type, neojs::NodeType::Literal);

  a.destroy(ast);
}

TEST(Frontend, ParseParenthesized) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* (1 + 2) * 3 → BinaryOp(*)[BinaryOp(+)[1,2],3] */
  neojs::AST *ast = neojs::parseSource(&a, "(1 + 2) * 3", 11, sink);
  ASSERT_NE(ast, nullptr);

  neojs::ASTNode *bin = ast->root->children[0]->children[0];
  EXPECT_EQ(bin->binop, neojs::BinaryOpKind::Mul);
  EXPECT_EQ(bin->children[0]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(bin->children[0]->binop, neojs::BinaryOpKind::Add);

  a.destroy(ast);
}

TEST(Frontend, ParseIdentifier) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "foo", 3, sink);
  ASSERT_NE(ast, nullptr);

  neojs::ASTNode *id = ast->root->children[0]->children[0];
  EXPECT_EQ(id->type, neojs::NodeType::Identifier);
  EXPECT_EQ(id->name, "foo");

  a.destroy(ast);
}

TEST(Frontend, ParseSyntaxError) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "1 +", 3, sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  EXPECT_EQ(sink.diagnostics()[0].severity, neojs::Severity::Error);
  /* 错误位置：1:4（+ 已消费，EOF 处期待 primary） */
  EXPECT_EQ(sink.diagnostics()[0].loc.line, 1u);
  EXPECT_EQ(sink.diagnostics()[0].loc.col, 4u);
}

TEST(Frontend, ParseMissingRParen) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "(1 + 2", 6, sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  EXPECT_EQ(sink.diagnostics()[0].severity, neojs::Severity::Error);
}

TEST(Frontend, ParseSemicolonTerminated) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 多条表达式语句 + 分号：1; 2 */
  neojs::AST *ast = neojs::parseSource(&a, "1; 2", 4, sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 2u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(ast->root->children[1]->type, neojs::NodeType::ExprStmt);

  a.destroy(ast);
}

/* -------------------------------------------------------------------------- */
/* Dump 输出                                                                   */
/* -------------------------------------------------------------------------- */

TEST(Frontend, DumpTokensOutput) {
  neojs::DiagnosticSink sink;
  std::string out = neojs::dumpTokens("1 + 2", 5, sink);
  EXPECT_NE(out.find("number '1' @1:1"), std::string::npos);
  EXPECT_NE(out.find("symbol '+' @1:3"), std::string::npos);
  EXPECT_NE(out.find("number '2' @1:5"), std::string::npos);
  EXPECT_NE(out.find("eof ''"), std::string::npos);
}

TEST(Frontend, DumpAstOutput) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "1 + 2", 5, sink);
  ASSERT_NE(ast, nullptr);
  std::string out = neojs::dumpAst(*ast);
  EXPECT_NE(out.find("Program"), std::string::npos);
  EXPECT_NE(out.find("ExprStmt"), std::string::npos);
  EXPECT_NE(out.find("BinaryOp(+)"), std::string::npos);
  EXPECT_NE(out.find("Literal(1)"), std::string::npos);
  EXPECT_NE(out.find("Literal(2)"), std::string::npos);

  a.destroy(ast);
}
