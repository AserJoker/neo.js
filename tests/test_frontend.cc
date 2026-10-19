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

/* -------------------------------------------------------------------------- */
/* M1.2：ICU 接入 + Unicode 换行 + 完整字面量                                  */
/* -------------------------------------------------------------------------- */

/* Unicode 标识符：中文（ID_Start/ID_Continue 需 ICU 属性表） */
TEST(Frontend, UnicodeIdentifier) {
  neojs::DiagnosticSink sink;
  neojs::Lexer lexer("\xE5\x8F\x98\xE9\x87\x8F", 6, sink); /* UTF-8：变(3B) 量(3B) */

  const neojs::Token &t = lexer.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::Identifier);
  EXPECT_EQ(t.text, "\xE5\x8F\x98\xE9\x87\x8F");
  EXPECT_EQ(lexer.next().kind, neojs::TokenKind::EndOfFile);
  EXPECT_FALSE(sink.hasErrors());
}

/* Unicode 标识符：组合后续字符（é 由 e + U+0301 组成） */
TEST(Frontend, UnicodeIdentifierCombiningMark) {
  neojs::DiagnosticSink sink;
  /* "e\u0301"（e + 组合重音号 U+0301）——U+0301 是 ID_Continue */
  const char *src = "\x65\xCC\x81"; /* e + U+0301 */
  neojs::Lexer lexer(src, 3, sink);

  const neojs::Token &t = lexer.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::Identifier);
  EXPECT_EQ(t.text, std::string("\x65\xCC\x81"));
  EXPECT_FALSE(sink.hasErrors());
}

/* Unicode 标识符：ZWNJ/ZWJ 作为继续字符（波斯语/阿拉伯语） */
TEST(Frontend, IdentifierZwnjZwj) {
  neojs::DiagnosticSink sink;
  /* "a\u200Cb"（a + ZWNJ + b） */
  const char *src = "\x61\xE2\x80\x8C\x62";
  neojs::Lexer lexer(src, 5, sink);

  const neojs::Token &t = lexer.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::Identifier);
  EXPECT_EQ(t.text, std::string("\x61\xE2\x80\x8C\x62"));
  EXPECT_FALSE(sink.hasErrors());
}

/* 标识符 \u 转义：\u4E2D（中） */
TEST(Frontend, IdentifierUnicodeEscape) {
  neojs::DiagnosticSink sink;
  neojs::Lexer lexer("\\u4E2D\\u6587", 12, sink);

  const neojs::Token &t = lexer.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::Identifier);
  EXPECT_EQ(t.text, "\\u4E2D\\u6587");
  EXPECT_FALSE(sink.hasErrors());
}

/* Unicode 换行：U+2028 / U+2029 都是 LineTerminator，使 line 递增 */
TEST(Frontend, UnicodeLineTerminator) {
  neojs::DiagnosticSink sink;
  /* "1\u2028+ 2"：+ 在第二行 */
  const char *src = "1\xE2\x80\xA8+ 2";
  neojs::Lexer lexer(src, 7, sink);

  const neojs::Token &t1 = lexer.next();
  EXPECT_EQ(t1.kind, neojs::TokenKind::Number);
  const neojs::Token &t2 = lexer.next();
  EXPECT_EQ(t2.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(t2.text, "+");
  EXPECT_EQ(t2.start.line, 2u);
  EXPECT_EQ(t2.start.col, 1u);
  EXPECT_FALSE(sink.hasErrors());
}

/* \r\n 计单个换行（\n 不再 ++line） */
TEST(Frontend, CrLfSingleLineTerminator) {
  neojs::DiagnosticSink sink;
  neojs::Lexer lexer("1\r\n+ 2", 6, sink);

  const neojs::Token &t1 = lexer.next();
  EXPECT_EQ(t1.kind, neojs::TokenKind::Number);
  const neojs::Token &t2 = lexer.next();
  EXPECT_EQ(t2.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(t2.start.line, 2u);
}

/* Unicode 空白：U+00A0（NBSP）是 White_Space，被跳过 */
TEST(Frontend, UnicodeWhitespaceSkipped) {
  neojs::DiagnosticSink sink;
  /* "1\u00A0+ 2"：NBSP 分隔 */
  const char *src = "1\xC2\xA0+ 2";
  neojs::Lexer lexer(src, 6, sink);

  const neojs::Token &t1 = lexer.next();
  EXPECT_EQ(t1.kind, neojs::TokenKind::Number);
  EXPECT_EQ(t1.text, "1");
  const neojs::Token &t2 = lexer.next();
  EXPECT_EQ(t2.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(t2.text, "+");
  EXPECT_EQ(t2.start.col, 3u);
}

/* 数字：十六进制 + 二进制 + 八进制 */
TEST(Frontend, NumberHexBinaryOctal) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l1("0x1F", 4, sink);
  const neojs::Token &t1 = l1.next();
  EXPECT_EQ(t1.kind, neojs::TokenKind::Number);
  EXPECT_DOUBLE_EQ(t1.number, 31.0);

  neojs::Lexer l2("0b1010", 6, sink);
  const neojs::Token &t2 = l2.next();
  EXPECT_EQ(t2.kind, neojs::TokenKind::Number);
  EXPECT_DOUBLE_EQ(t2.number, 10.0);

  neojs::Lexer l3("0o17", 4, sink);
  const neojs::Token &t3 = l3.next();
  EXPECT_EQ(t3.kind, neojs::TokenKind::Number);
  EXPECT_DOUBLE_EQ(t3.number, 15.0);

  EXPECT_FALSE(sink.hasErrors());
}

/* 数字：科学计数 + 小数 */
TEST(Frontend, NumberExponentAndFraction) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l1("1.5e3", 5, sink);
  const neojs::Token &t1 = l1.next();
  EXPECT_EQ(t1.kind, neojs::TokenKind::Number);
  EXPECT_DOUBLE_EQ(t1.number, 1500.0);

  neojs::Lexer l2("2.5", 3, sink);
  const neojs::Token &t2 = l2.next();
  EXPECT_EQ(t2.kind, neojs::TokenKind::Number);
  EXPECT_DOUBLE_EQ(t2.number, 2.5);
}

/* 数字：_ 分隔符 */
TEST(Frontend, NumberSeparator) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l("1_000_000", 9, sink);
  const neojs::Token &t = l.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::Number);
  EXPECT_DOUBLE_EQ(t.number, 1000000.0);

  /* 非法分隔符：1__000 报错 */
  neojs::DiagnosticSink sink2;
  neojs::Lexer l2("1__000", 6, sink2);
  const neojs::Token &t2 = l2.next();
  EXPECT_EQ(t2.kind, neojs::TokenKind::Error);
  EXPECT_TRUE(sink2.hasErrors());
}

/* 数字：BigInt 后缀 */
TEST(Frontend, NumberBigInt) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l("123n", 4, sink);
  const neojs::Token &t = l.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::Number);
  EXPECT_TRUE(t.is_bigint);
  EXPECT_DOUBLE_EQ(t.number, 123.0);
}

/* 数字：成员访问 1.toString() 不应把 . 当小数点 */
TEST(Frontend, NumberDotNotFraction) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l("1.toString", 10, sink);
  const neojs::Token &t1 = l.next();
  EXPECT_EQ(t1.kind, neojs::TokenKind::Number);
  EXPECT_EQ(t1.text, "1");
  const neojs::Token &t2 = l.next();
  EXPECT_EQ(t2.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(t2.text, ".");
}

/* 字符串：转义序列解码 */
TEST(Frontend, StringEscapeDecoded) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l("\"a\\nb\\t\\u4E2D\"", 15, sink); /* "a\nb\t\u4E2D" */
  const neojs::Token &t = l.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::String);
  EXPECT_EQ(t.str_value, "a\nb\t中");
  EXPECT_FALSE(sink.hasErrors());
}

/* 字符串：废弃八进制转义打警告 */
TEST(Frontend, StringLegacyOctalEscapeWarns) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l("\"\\1\"", 4, sink);
  const neojs::Token &t = l.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::String);
  EXPECT_FALSE(sink.hasErrors());
  EXPECT_EQ(sink.diagnostics().size(), 1u);
  EXPECT_EQ(sink.diagnostics()[0].severity, neojs::Severity::Warning);
  EXPECT_EQ(sink.diagnostics()[0].code, "DEP-0003");
}

/* 字符串：未闭合报错 */
TEST(Frontend, StringUnterminatedErrors) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l("\"abc", 4, sink);
  const neojs::Token &t = l.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::Error);
  EXPECT_TRUE(sink.hasErrors());
}

/* 模板串：无插值完整片段 → TemplateStart（has_interpolation=false） */
TEST(Frontend, TemplateLiteral) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l("`hello`", 7, sink);
  const neojs::Token &t = l.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::TemplateStart);
  EXPECT_EQ(t.str_value, "hello");
  EXPECT_FALSE(t.has_interpolation);
  EXPECT_EQ(l.next().kind, neojs::TokenKind::EndOfFile);
}

/* 模板串头：${ 标记 has_interpolation=true（M1.3 处理插值） */
TEST(Frontend, TemplateHeadMarked) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l("`a${1}`", 7, sink);
  const neojs::Token &t = l.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::TemplateStart);
  EXPECT_EQ(t.str_value, "a");
  EXPECT_TRUE(t.has_interpolation);
}

/* HTML 风格注释：<!-- 注释到行尾（Annex B） */
TEST(Frontend, HtmlCommentSkipped) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l("1 <!-- x\n2", 10, sink);
  const neojs::Token &t1 = l.next();
  EXPECT_EQ(t1.kind, neojs::TokenKind::Number);
  const neojs::Token &t2 = l.next();
  EXPECT_EQ(t2.kind, neojs::TokenKind::Number);
  EXPECT_EQ(t2.start.line, 2u);
  EXPECT_FALSE(sink.hasErrors());
}

/* Parser：字符串字面量 primary */
TEST(Frontend, ParseStringLiteral) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "\"hi\"", 4, sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());

  neojs::ASTNode *lit = ast->root->children[0]->children[0];
  EXPECT_EQ(lit->type, neojs::NodeType::Literal);
  EXPECT_TRUE(lit->literal.is_string);
  EXPECT_EQ(lit->literal.str, "hi");

  a.destroy(ast);
}

/* -------------------------------------------------------------------------- */
/* M1.4 表达式体系：① Literal + ② Identifier/模式                           */
/* -------------------------------------------------------------------------- */

/* 布尔字面量：true / false */
TEST(Frontend, LiteralBoolean) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "true + false", 12, sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());

  neojs::ASTNode *bin = ast->root->children[0]->children[0];
  EXPECT_EQ(bin->type, neojs::NodeType::BinaryOp);
  neojs::ASTNode *l = bin->children[0];
  EXPECT_EQ(l->type, neojs::NodeType::Literal);
  EXPECT_TRUE(l->literal.is_boolean);
  EXPECT_TRUE(l->literal.bool_value);
  neojs::ASTNode *r = bin->children[1];
  EXPECT_TRUE(r->literal.is_boolean);
  EXPECT_FALSE(r->literal.bool_value);

  a.destroy(ast);
}

/* null 字面量 */
TEST(Frontend, LiteralNull) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "null", 4, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *lit = ast->root->children[0]->children[0];
  EXPECT_EQ(lit->type, neojs::NodeType::Literal);
  EXPECT_TRUE(lit->literal.is_null);
  a.destroy(ast);
}

/* 正则字面量：pattern + flags */
TEST(Frontend, LiteralRegExp) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "/ab+c/gi", 8, sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *re = ast->root->children[0]->children[0];
  EXPECT_EQ(re->type, neojs::NodeType::RegExpLiteral);
  EXPECT_EQ(re->regexp.pattern, "ab+c");
  EXPECT_EQ(re->regexp.flags, "gi");
  a.destroy(ast);
}

/* 除法 vs 正则（re-scan）：a / b 是除法，/re/ 是正则 */
TEST(Frontend, SlashDivisionVsRegex) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *div = neojs::parseSource(&a, "a / b", 5, sink);
  ASSERT_NE(div, nullptr);
  neojs::ASTNode *bin = div->root->children[0]->children[0];
  EXPECT_EQ(bin->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(bin->binop, neojs::BinaryOpKind::Div);
  a.destroy(div);

  neojs::DiagnosticSink sink2;
  neojs::AST *re = neojs::parseSource(&a, "/re/", 4, sink2);
  ASSERT_NE(re, nullptr);
  neojs::ASTNode *lit = re->root->children[0]->children[0];
  EXPECT_EQ(lit->type, neojs::NodeType::RegExpLiteral);
  a.destroy(re);
}

/* 私有字段：#x */
TEST(Frontend, PrivateIdentifier) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "#x", 2, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *id = ast->root->children[0]->children[0];
  EXPECT_EQ(id->type, neojs::NodeType::PrivateIdentifier);
  EXPECT_EQ(id->name, "x");
  a.destroy(ast);
}

/* 对象字面量：{ a: 1, b } */
TEST(Frontend, ObjectLiteral) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "({a: 1, b})", 11, sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *obj = ast->root->children[0]->children[0];
  EXPECT_EQ(obj->type, neojs::NodeType::ObjectPattern);
  ASSERT_EQ(obj->children.size(), 2u);
  EXPECT_EQ(obj->children[0]->type, neojs::NodeType::Property);
  EXPECT_EQ(obj->children[0]->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(obj->children[0]->children[1]->type, neojs::NodeType::Literal);
  /* 简写属性 */
  EXPECT_EQ(obj->children[1]->type, neojs::NodeType::Property);
  EXPECT_EQ(obj->children[1]->prop.kind, neojs::PropertyKind::Shorthand);
  a.destroy(ast);
}

/* 数组字面量：[1, 2, ...rest] */
TEST(Frontend, ArrayLiteral) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "[1, 2, ...r]", 12, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *arr = ast->root->children[0]->children[0];
  EXPECT_EQ(arr->type, neojs::NodeType::ArrayPattern);
  ASSERT_EQ(arr->children.size(), 3u);
  EXPECT_EQ(arr->children[0]->type, neojs::NodeType::Literal);
  EXPECT_EQ(arr->children[2]->type, neojs::NodeType::SpreadElement);
  a.destroy(ast);
}

/* 字符串键属性：{ "k" : 1 } */
TEST(Frontend, ObjectStringKey) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  neojs::AST *ast = neojs::parseSource(&a, "({\"k\" : 1})", 11, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *obj = ast->root->children[0]->children[0];
  EXPECT_EQ(obj->type, neojs::NodeType::ObjectPattern);
  EXPECT_EQ(obj->children[0]->children[0]->literal.str, "k");
  a.destroy(ast);
}

/* -------------------------------------------------------------------------- */
/* 一元运算（M1.4）                                                            */
/* -------------------------------------------------------------------------- */

TEST(Frontend, UnarySymbolOps) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();

  /* !x → UnaryOp(!)[x] */
  neojs::AST *ast = neojs::parseSource(&a, "!x", 2, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->type, neojs::NodeType::UnaryOp);
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Not);
  EXPECT_EQ(u->children[0]->type, neojs::NodeType::Identifier);
  a.destroy(ast);

  /* ~x */
  ast = neojs::parseSource(&a, "~x", 2, sink);
  ASSERT_NE(ast, nullptr);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::BitNot);
  a.destroy(ast);

  /* +x / -x */
  ast = neojs::parseSource(&a, "+x", 2, sink);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Pos);
  a.destroy(ast);
  ast = neojs::parseSource(&a, "-x", 2, sink);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Neg);
  a.destroy(ast);
}

TEST(Frontend, UnaryKeywordOps) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();

  neojs::AST *ast = neojs::parseSource(&a, "typeof x", 8, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->type, neojs::NodeType::UnaryOp);
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Typeof);
  a.destroy(ast);

  ast = neojs::parseSource(&a, "void 0", 6, sink);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Void);
  a.destroy(ast);

  ast = neojs::parseSource(&a, "delete x", 8, sink);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Delete);
  a.destroy(ast);

  ast = neojs::parseSource(&a, "await x", 7, sink);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Await);
  a.destroy(ast);
}

TEST(Frontend, UnaryChainedRightAssoc) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* !!!x → !(!(!x)) 右结合嵌套 */
  neojs::AST *ast = neojs::parseSource(&a, "!!!x", 4, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Not);
  EXPECT_EQ(u->children[0]->unop, neojs::UnaryOpKind::Not);
  EXPECT_EQ(u->children[0]->children[0]->unop, neojs::UnaryOpKind::Not);
  EXPECT_EQ(u->children[0]->children[0]->children[0]->type,
            neojs::NodeType::Identifier);
  a.destroy(ast);
}

/* -------------------------------------------------------------------------- */
/* 二元运算全套（M1.4）                                                        */
/* -------------------------------------------------------------------------- */

TEST(Frontend, BinaryFullOps) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();

  struct Case {
    const char *src;
    neojs::BinaryOpKind expect;
  } cases[] = {
      {"a + b", neojs::BinaryOpKind::Add},        {"a - b", neojs::BinaryOpKind::Sub},
      {"a * b", neojs::BinaryOpKind::Mul},        {"a / b", neojs::BinaryOpKind::Div},
      {"a % b", neojs::BinaryOpKind::Mod},        {"a ** b", neojs::BinaryOpKind::Pow},
      {"a << b", neojs::BinaryOpKind::LShift},    {"a >> b", neojs::BinaryOpKind::RShift},
      {"a >>> b", neojs::BinaryOpKind::URShift},  {"a < b", neojs::BinaryOpKind::Lt},
      {"a <= b", neojs::BinaryOpKind::Le},        {"a > b", neojs::BinaryOpKind::Gt},
      {"a >= b", neojs::BinaryOpKind::Ge},        {"a in b", neojs::BinaryOpKind::In},
      {"a instanceof b", neojs::BinaryOpKind::Instanceof},
      {"a == b", neojs::BinaryOpKind::Eq},        {"a != b", neojs::BinaryOpKind::Ne},
      {"a === b", neojs::BinaryOpKind::StrictEq}, {"a !== b", neojs::BinaryOpKind::StrictNe},
      {"a & b", neojs::BinaryOpKind::BitAnd},     {"a ^ b", neojs::BinaryOpKind::BitXor},
      {"a | b", neojs::BinaryOpKind::BitOr},      {"a && b", neojs::BinaryOpKind::LogicalAnd},
      {"a || b", neojs::BinaryOpKind::LogicalOr}, {"a ?? b", neojs::BinaryOpKind::Nullish},
  };
  for (const auto &c : cases) {
    neojs::AST *ast = neojs::parseSource(&a, c.src, std::strlen(c.src), sink);
    ASSERT_NE(ast, nullptr) << c.src;
    neojs::ASTNode *bin = ast->root->children[0]->children[0];
    EXPECT_EQ(bin->type, neojs::NodeType::BinaryOp) << c.src;
    EXPECT_EQ(bin->binop, c.expect) << c.src;
    a.destroy(ast);
  }
}

TEST(Frontend, BinaryPowerRightAssoc) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 2 ** 3 ** 2 → 2 ** (3 ** 2)（右结合） */
  neojs::AST *ast = neojs::parseSource(&a, "2 ** 3 ** 2", 11, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *bin = ast->root->children[0]->children[0];
  EXPECT_EQ(bin->binop, neojs::BinaryOpKind::Pow);
  EXPECT_EQ(bin->children[0]->type, neojs::NodeType::Literal);
  EXPECT_EQ(bin->children[1]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(bin->children[1]->binop, neojs::BinaryOpKind::Pow);
  a.destroy(ast);
}

TEST(Frontend, BinaryFullPrecedence) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* !x && y || z → (!x && y) || z：一元 > && > || */
  neojs::AST *ast = neojs::parseSource(&a, "!x && y || z", 12, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->binop, neojs::BinaryOpKind::LogicalOr);
  EXPECT_EQ(top->children[0]->binop, neojs::BinaryOpKind::LogicalAnd);
  EXPECT_EQ(top->children[0]->children[0]->type, neojs::NodeType::UnaryOp);
  a.destroy(ast);

  /* a | b << c → a | (b << c)：<< > | */
  ast = neojs::parseSource(&a, "a | b << c", 10, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->binop, neojs::BinaryOpKind::BitOr);
  EXPECT_EQ(top->children[1]->binop, neojs::BinaryOpKind::LShift);
  a.destroy(ast);

  /* 1 < 2 == true → (1 < 2) == true：< > == */
  ast = neojs::parseSource(&a, "1 < 2 == true", 13, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->binop, neojs::BinaryOpKind::Eq);
  EXPECT_EQ(top->children[0]->binop, neojs::BinaryOpKind::Lt);
  a.destroy(ast);

  /* a + b * c → a + (b * c)：* > + */
  ast = neojs::parseSource(&a, "a + b * c", 9, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->binop, neojs::BinaryOpKind::Add);
  EXPECT_EQ(top->children[1]->binop, neojs::BinaryOpKind::Mul);
  a.destroy(ast);
}

/* -------------------------------------------------------------------------- */
/* 三元 / 赋值 / 序列（M1.4 ③-2）                                              */
/* -------------------------------------------------------------------------- */

TEST(Frontend, AssignmentRightAssoc) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* a = b = c → Assignment[=][a, Assignment[=][b, c]] 右结合 */
  neojs::AST *ast = neojs::parseSource(&a, "a = b = c", 9, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Assignment);
  EXPECT_EQ(top->assign_op, neojs::AssignmentOpKind::Assign);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Assignment);
  EXPECT_EQ(top->children[1]->assign_op, neojs::AssignmentOpKind::Assign);
  a.destroy(ast);
}

TEST(Frontend, CompoundAssignments) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();

  struct Case {
    const char *src;
    neojs::AssignmentOpKind expect;
  } cases[] = {
      {"a = b", neojs::AssignmentOpKind::Assign},
      {"a += b", neojs::AssignmentOpKind::AddAssign},
      {"a -= b", neojs::AssignmentOpKind::SubAssign},
      {"a *= b", neojs::AssignmentOpKind::MulAssign},
      {"a /= b", neojs::AssignmentOpKind::DivAssign},
      {"a %= b", neojs::AssignmentOpKind::ModAssign},
      {"a **= b", neojs::AssignmentOpKind::PowAssign},
      {"a <<= b", neojs::AssignmentOpKind::LShiftAssign},
      {"a >>= b", neojs::AssignmentOpKind::RShiftAssign},
      {"a >>>= b", neojs::AssignmentOpKind::URShiftAssign},
      {"a &= b", neojs::AssignmentOpKind::BitAndAssign},
      {"a ^= b", neojs::AssignmentOpKind::BitXorAssign},
      {"a |= b", neojs::AssignmentOpKind::BitOrAssign},
      {"a &&= b", neojs::AssignmentOpKind::LogicalAndAssign},
      {"a ||= b", neojs::AssignmentOpKind::LogicalOrAssign},
      {"a ?" "?= b", neojs::AssignmentOpKind::NullishAssign}, /* 拼接避免 trigraph */
  };
  for (const auto &c : cases) {
    neojs::AST *ast = neojs::parseSource(&a, c.src, std::strlen(c.src), sink);
    ASSERT_NE(ast, nullptr) << c.src;
    neojs::ASTNode *top = ast->root->children[0]->children[0];
    EXPECT_EQ(top->type, neojs::NodeType::Assignment) << c.src;
    EXPECT_EQ(top->assign_op, c.expect) << c.src;
    a.destroy(ast);
  }
}

TEST(Frontend, InvalidAssignmentTarget) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 1 = x → 非法左值 */
  neojs::AST *ast = neojs::parseSource(&a, "1 = x", 5, sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  a.destroy(ast);
}

TEST(Frontend, ConditionalExpression) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* x ? y : z → Conditional[x, y, z] */
  neojs::AST *ast = neojs::parseSource(&a, "x ? y : z", 9, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Conditional);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::Identifier);
  a.destroy(ast);

  /* 右结合：a ? b : c ? d : e → Conditional[a, b, Conditional[c, d, e]] */
  ast = neojs::parseSource(&a, "a ? b : c ? d : e", 17, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Conditional);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::Conditional);
  a.destroy(ast);
}

TEST(Frontend, SequenceExpression) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* a, b, c → Sequence[Sequence[a, b], c] 左结合 */
  neojs::AST *ast = neojs::parseSource(&a, "a, b, c", 7, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Sequence);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Sequence);
  EXPECT_EQ(top->children[0]->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[0]->children[1]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  a.destroy(ast);
}

TEST(Frontend, SequenceInsideParens) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* (x = 1, y = 2) → 括号内序列，元素为赋值 */
  neojs::AST *ast = neojs::parseSource(&a, "(x = 1, y = 2)", 14, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Sequence);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Assignment);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Assignment);
  a.destroy(ast);
}

TEST(Frontend, LiteralCommaNotSequence) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 协同回归：数组/对象字面量内部逗号是分隔符，不被序列吞掉 */
  neojs::AST *ast = neojs::parseSource(&a, "[1, 2, 3]", 9, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *arr = ast->root->children[0]->children[0];
  EXPECT_EQ(arr->type, neojs::NodeType::ArrayPattern);
  EXPECT_EQ(arr->children.size(), 3u);
  a.destroy(ast);

  ast = neojs::parseSource(&a, "({a: 1, b: 2})", 14, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *obj = ast->root->children[0]->children[0];
  EXPECT_EQ(obj->type, neojs::NodeType::ObjectPattern);
  EXPECT_EQ(obj->children.size(), 2u);
  a.destroy(ast);
}

/* -------------------------------------------------------------------------- */
/* 成员访问 / 调用 / new（M1.4 ③-3）                                           */
/* -------------------------------------------------------------------------- */

TEST(Frontend, MemberAccess) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* a.b.c → Member(Member(a, b), c) */
  neojs::AST *ast = neojs::parseSource(&a, "a.b.c", 5, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_FALSE(top->member.computed);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  a.destroy(ast);
}

TEST(Frontend, ComputedMember) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* arr[i + 1] → Member[] [arr, BinaryOp(+)] */
  neojs::AST *ast = neojs::parseSource(&a, "arr[i + 1]", std::strlen("arr[i + 1]"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.computed);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BinaryOp);
  a.destroy(ast);
}

TEST(Frontend, CallExpression) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* f(x, y) → Call[f, x, y] */
  neojs::AST *ast = neojs::parseSource(&a, "f(x, y)", 7, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::CallExpression);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  ASSERT_EQ(top->children.size(), 3u);
  a.destroy(ast);

  /* 链式：a.b.c(d).e[0] */
  ast = neojs::parseSource(&a, "a.b.c(d).e[0]", std::strlen("a.b.c(d).e[0]"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.computed);
  a.destroy(ast);
}

TEST(Frontend, NewExpression) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* new Foo() → New[Foo] */
  neojs::AST *ast = neojs::parseSource(&a, "new Foo()", std::strlen("new Foo()"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::NewExpression);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  a.destroy(ast);

  /* new Foo 无括号 */
  ast = neojs::parseSource(&a, "new Foo", 7, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::NewExpression);
  EXPECT_EQ(top->children.size(), 1u);
  a.destroy(ast);

  /* new a.b(1, 2).c：callee 吃成员 a.b，参数 (1,2)，.c 在外层 */
  ast = neojs::parseSource(&a, "new a.b(1, 2).c", 15, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::NewExpression);
  a.destroy(ast);
}

TEST(Frontend, CallSpreadArgs) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* f(...xs, 1) → Call[f, Spread[xs], 1] */
  neojs::AST *ast = neojs::parseSource(&a, "f(...xs, 1)", std::strlen("f(...xs, 1)"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::CallExpression);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::SpreadElement);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::Literal);
  a.destroy(ast);
}

TEST(Frontend, UnaryBelowMember) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* -a.b → UnaryOp(-)[Member(a, b)]：成员优先级高于一元 */
  neojs::AST *ast = neojs::parseSource(&a, "-a.b", 4, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UnaryOp);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  a.destroy(ast);
}

TEST(Frontend, MemberAssignmentTarget) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* a.b = 1 → Assignment[=][Member(a, b), 1]：成员是合法左值 */
  neojs::AST *ast = neojs::parseSource(&a, "a.b = 1", 7, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Assignment);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  a.destroy(ast);
}

TEST(Frontend, PrivateMemberAccess) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* obj.#x → Member(obj, PrivateIdentifier(x)) */
  neojs::AST *ast = neojs::parseSource(&a, "obj.#x", 6, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::PrivateIdentifier);
  a.destroy(ast);
}

TEST(Frontend, AwaitCallAndDeleteMember) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* await foo() → UnaryOp(await)[Call(foo)] */
  neojs::AST *ast = neojs::parseSource(&a, "await foo()", 11, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UnaryOp);
  EXPECT_EQ(top->unop, neojs::UnaryOpKind::Await);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::CallExpression);
  a.destroy(ast);

  /* delete obj.x → UnaryOp(delete)[Member(obj, x)] */
  ast = neojs::parseSource(&a, "delete obj.x", 12, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UnaryOp);
  EXPECT_EQ(top->unop, neojs::UnaryOpKind::Delete);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  a.destroy(ast);
}

/* -------------------------------------------------------------------------- */
/* 箭头函数（M1.4 ③-4，仅表达式体）                                            */
/* -------------------------------------------------------------------------- */

TEST(Frontend, ArrowSingleParam) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* x => x + 1 → Arrow[Identifier(x), BinaryOp(+)] */
  neojs::AST *ast = neojs::parseSource(&a, "x => x + 1", std::strlen("x => x + 1"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[0]->name, "x");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->children[1]->binop, neojs::BinaryOpKind::Add);
  a.destroy(ast);
}

TEST(Frontend, ArrowMultiParam) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* (a, b) => a + b → Arrow[Identifier(a), Identifier(b), BinaryOp(+)] */
  neojs::AST *ast = neojs::parseSource(&a, "(a, b) => a + b", std::strlen("(a, b) => a + b"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[0]->name, "a");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[1]->name, "b");
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::BinaryOp);
  a.destroy(ast);
}

TEST(Frontend, ArrowNoParam) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* () => 42 → Arrow[Literal(42)] */
  neojs::AST *ast = neojs::parseSource(&a, "() => 42", std::strlen("() => 42"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 1u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Literal);
  a.destroy(ast);
}

TEST(Frontend, ArrowRightAssocNested) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* x => y => x + y → Arrow[x, Arrow[y, BinaryOp(+)]（右结合嵌套） */
  neojs::AST *ast =
      neojs::parseSource(&a, "x => y => x + y", std::strlen("x => y => x + y"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[0]->name, "x");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::ArrowFunctionExpression);
  neojs::ASTNode *inner = top->children[1];
  EXPECT_EQ(inner->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(inner->children[0]->name, "y");
  EXPECT_EQ(inner->children[1]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(inner->children[1]->binop, neojs::BinaryOpKind::Add);
  a.destroy(ast);
}

TEST(Frontend, ArrowRestParam) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* (...xs) => xs → Arrow[RestElement[Identifier(xs)], Identifier(xs)] */
  neojs::AST *ast = neojs::parseSource(&a, "(...xs) => xs", std::strlen("(...xs) => xs"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::RestElement);
  EXPECT_EQ(top->children[0]->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[0]->children[0]->name, "xs");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  a.destroy(ast);
}

TEST(Frontend, ArrowVsParenNotConfused) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 协同回归：(a + b) * c 是括号表达式，不得误判为箭头 */
  neojs::AST *ast =
      neojs::parseSource(&a, "(a + b) * c", std::strlen("(a + b) * c"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->binop, neojs::BinaryOpKind::Mul);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->children[0]->binop, neojs::BinaryOpKind::Add);
  a.destroy(ast);
}

/* -------------------------------------------------------------------------- */
/* 模板串插值（M1.4 ③-5）                                                     */
/* -------------------------------------------------------------------------- */

TEST(Frontend, TemplateLiteralNoInterp) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* `hello` → TemplateLiteral[Literal("hello")] 单 quasi */
  neojs::AST *ast = neojs::parseSource(&a, "`hello`", std::strlen("`hello`"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::TemplateLiteral);
  ASSERT_EQ(top->children.size(), 1u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Literal);
  EXPECT_EQ(top->children[0]->literal.str, "hello");
  a.destroy(ast);
}

TEST(Frontend, TemplateSingleInterp) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* `a${1}` → TemplateLiteral[Literal("a"), Literal(1), Literal("")] */
  neojs::AST *ast = neojs::parseSource(&a, "`a${1}`", std::strlen("`a${1}`"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::TemplateLiteral);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Literal);
  EXPECT_EQ(top->children[0]->literal.str, "a");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Literal);
  EXPECT_TRUE(top->children[1]->literal.is_number);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::Literal);
  EXPECT_EQ(top->children[2]->literal.str, "");
  a.destroy(ast);
}

TEST(Frontend, TemplateMultiInterp) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* `a${x}b${y}c` → [quasi a, Identifier x, quasi b, Identifier y, quasi c] */
  neojs::AST *ast =
      neojs::parseSource(&a, "`a${x}b${y}c`", std::strlen("`a${x}b${y}c`"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::TemplateLiteral);
  ASSERT_EQ(top->children.size(), 5u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Literal);
  EXPECT_EQ(top->children[0]->literal.str, "a");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[1]->name, "x");
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::Literal);
  EXPECT_EQ(top->children[2]->literal.str, "b");
  EXPECT_EQ(top->children[3]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[3]->name, "y");
  EXPECT_EQ(top->children[4]->type, neojs::NodeType::Literal);
  EXPECT_EQ(top->children[4]->literal.str, "c");
  a.destroy(ast);
}

TEST(Frontend, TemplateInterpExpression) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* `sum=${a + b}` → [quasi "sum=", BinaryOp(+), quasi ""] */
  neojs::AST *ast =
      neojs::parseSource(&a, "`sum=${a + b}`", std::strlen("`sum=${a + b}`"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::TemplateLiteral);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[0]->literal.str, "sum=");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->children[1]->binop, neojs::BinaryOpKind::Add);
  a.destroy(ast);
}

TEST(Frontend, TemplateNested) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 嵌套模板：`n=${`in${x}`}` → 插值内又含 TemplateLiteral */
  neojs::AST *ast =
      neojs::parseSource(&a, "`n=${`in${x}`}`", std::strlen("`n=${`in${x}`}`"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::TemplateLiteral);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::TemplateLiteral);
  a.destroy(ast);
}

TEST(Frontend, TemplateUnterminated) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* `unterminated → 报错，ast 为 nullptr */
  neojs::AST *ast =
      neojs::parseSource(&a, "`unterminated", std::strlen("`unterminated"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  a.destroy(ast);
}

/* Lexer 级：续段重扫产出 TemplatePart / TemplateEnd（M1.4 ③-5） */
TEST(Frontend, TemplateContinuationTokens) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l("`a${x}b${y}c`", std::strlen("`a${x}b${y}c`"), sink);

  /* head：`a${ */
  const neojs::Token &h = l.next();
  EXPECT_EQ(h.kind, neojs::TokenKind::TemplateStart);
  EXPECT_EQ(h.str_value, "a");
  EXPECT_TRUE(h.has_interpolation);

  /* 模拟 Parser：表达式 x 后的 } 重扫为续段。先推进到 } */
  EXPECT_EQ(l.next().text, "x");
  const neojs::Token &close = l.next();
  EXPECT_EQ(close.kind, neojs::TokenKind::Symbol);
  EXPECT_EQ(close.text, "}");
  /* rescanTemplatePart 依赖 current 是 }，且需回退重扫；
   * 这里直接调用（current 已是 }，pos 在它之后） */
  l.rescanTemplatePart();
  const neojs::Token &mid = l.current();
  EXPECT_EQ(mid.kind, neojs::TokenKind::TemplatePart);
  EXPECT_EQ(mid.str_value, "b");
  EXPECT_TRUE(mid.has_interpolation);

  /* 第二个 } 后是尾段 }c` */
  EXPECT_EQ(l.next().text, "y");
  EXPECT_EQ(l.next().text, "}");
  l.rescanTemplatePart();
  const neojs::Token &tail = l.current();
  EXPECT_EQ(tail.kind, neojs::TokenKind::TemplateEnd);
  EXPECT_EQ(tail.str_value, "c");
  EXPECT_FALSE(tail.has_interpolation);

  EXPECT_EQ(l.next().kind, neojs::TokenKind::EndOfFile);
}

/* -------------------------------------------------------------------------- */
/* 自增自减 / 可选链（M1.4 ③-6）                                               */
/* -------------------------------------------------------------------------- */

TEST(Frontend, UpdatePrefixPostfix) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* ++x → Update(++, prefix)[x] */
  neojs::AST *ast = neojs::parseSource(&a, "++x", std::strlen("++x"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UpdateExpression);
  EXPECT_EQ(top->update_op, neojs::UpdateOpKind::Inc);
  EXPECT_TRUE(top->prefix);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  a.destroy(ast);

  /* x-- → Update(--, postfix)[x] */
  ast = neojs::parseSource(&a, "x--", std::strlen("x--"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UpdateExpression);
  EXPECT_EQ(top->update_op, neojs::UpdateOpKind::Dec);
  EXPECT_FALSE(top->prefix);
  a.destroy(ast);
}

TEST(Frontend, UpdateMemberOperand) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* a.b++ → Update(++, postfix)[Member]：成员是合法操作数 */
  neojs::AST *ast = neojs::parseSource(&a, "a.b++", std::strlen("a.b++"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UpdateExpression);
  EXPECT_FALSE(top->prefix);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  a.destroy(ast);

  /* --a[0] → Update(--, prefix)[Member[]] */
  ast = neojs::parseSource(&a, "--a[0]", std::strlen("--a[0]"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UpdateExpression);
  EXPECT_EQ(top->update_op, neojs::UpdateOpKind::Dec);
  EXPECT_TRUE(top->prefix);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->children[0]->member.computed);
  a.destroy(ast);
}

TEST(Frontend, UpdateInvalidOperand) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 1++ → 非法操作数 */
  neojs::AST *ast = neojs::parseSource(&a, "1++", std::strlen("1++"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  a.destroy(ast);
}

TEST(Frontend, OptionalChainMember) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* a?.b → MemberExpression.?（optional=true，computed=false） */
  neojs::AST *ast = neojs::parseSource(&a, "a?.b", std::strlen("a?.b"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional);
  EXPECT_FALSE(top->member.computed);
  a.destroy(ast);

  /* a?.[x] → MemberExpression[]?（optional=true，computed=true） */
  ast = neojs::parseSource(&a, "a?.[x]", std::strlen("a?.[x]"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional);
  EXPECT_TRUE(top->member.computed);
  a.destroy(ast);

  /* a?.(1) → CallExpression? */
  ast = neojs::parseSource(&a, "a?.(1)", std::strlen("a?.(1)"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::CallExpression);
  EXPECT_TRUE(top->optional);
  a.destroy(ast);
}

TEST(Frontend, OptionalChainShortCircuit) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* a?.b.c → 一旦可选，后续 .c 也 optional（短路语义） */
  neojs::AST *ast = neojs::parseSource(&a, "a?.b.c", std::strlen("a?.b.c"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional); /* .c 也 optional */
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->children[0]->member.optional); /* a?.b */
  a.destroy(ast);

  /* a?.b?.c → 多级可选链 */
  ast = neojs::parseSource(&a, "a?.b?.c", std::strlen("a?.b?.c"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional);
  a.destroy(ast);

  /* a.b?.c → 普通成员后才可选 */
  ast = neojs::parseSource(&a, "a.b?.c", std::strlen("a.b?.c"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  EXPECT_FALSE(top->children[0]->member.optional); /* a.b 非可选 */
  a.destroy(ast);
}

TEST(Frontend, OptionalChainNotInNewCallee) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* new a?.b → 语法错误（可选链不能作 new 的 callee） */
  neojs::AST *ast = neojs::parseSource(&a, "new a?.b", std::strlen("new a?.b"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  a.destroy(ast);

  /* new Foo()?.b → 合法：new 结果上可选链 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource(&a, "new Foo()?.b", std::strlen("new Foo()?.b"), sink2);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::NewExpression);
  a.destroy(ast);
}

/* -------------------------------------------------------------------------- */
/* 语句体系地基（M1.5）：hashbang + directives + 空语句 + 表达式兜底          */
/* -------------------------------------------------------------------------- */

TEST(Frontend, EmptyStatement) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* ; → EmptyStatement；;;; → 3 个 */
  neojs::AST *ast = neojs::parseSource(&a, ";", std::strlen(";"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::EmptyStatement);
  a.destroy(ast);

  ast = neojs::parseSource(&a, ";;;", std::strlen(";;;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 3u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::EmptyStatement);
  EXPECT_EQ(ast->root->children[1]->type, neojs::NodeType::EmptyStatement);
  EXPECT_EQ(ast->root->children[2]->type, neojs::NodeType::EmptyStatement);
  a.destroy(ast);
}

TEST(Frontend, StatementFallbackExpression) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 兜底：非特征值首 token → 表达式语句 */
  neojs::AST *ast = neojs::parseSource(&a, "a = 1;", std::strlen("a = 1;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(ast->root->children[0]->children[0]->type, neojs::NodeType::Assignment);
  a.destroy(ast);

  /* 空语句与表达式混合 */
  ast = neojs::parseSource(&a, ";a;b;", std::strlen(";a;b;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 3u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::EmptyStatement);
  EXPECT_EQ(ast->root->children[1]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(ast->root->children[2]->type, neojs::NodeType::ExprStmt);
  a.destroy(ast);
}

TEST(Frontend, DirectivePrologue) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 序言：开头字符串字面量表达式语句标记 directive */
  neojs::AST *ast =
      neojs::parseSource(&a, "\"use strict\";", std::strlen("\"use strict\";"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  neojs::ASTNode *stmt = ast->root->children[0];
  EXPECT_EQ(stmt->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(stmt->directive, "use strict");
  a.destroy(ast);

  /* 序言后遇到非字符串字面量即退出：后续字符串不再标记 */
  ast = neojs::parseSource(&a, "1 + 2; \"later\";",
                           std::strlen("1 + 2; \"later\";"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 2u);
  EXPECT_TRUE(ast->root->children[0]->directive.empty());
  EXPECT_TRUE(ast->root->children[1]->directive.empty());
  a.destroy(ast);
}

TEST(Frontend, HashbangSkip) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 入口级 hashbang：整行跳过，后续正常解析 */
  neojs::AST *ast = neojs::parseSource(
      &a, "#!/usr/bin/env node\na = 1;", std::strlen("#!/usr/bin/env node\na = 1;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::ExprStmt);
  a.destroy(ast);

  /* hashbang + directive 序言 + 空语句混合 */
  ast = neojs::parseSource(&a, "#!x\n\"use strict\";\n;f();",
                           std::strlen("#!x\n\"use strict\";\n;f();"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 3u);
  EXPECT_EQ(ast->root->children[0]->directive, "use strict");
  EXPECT_EQ(ast->root->children[1]->type, neojs::NodeType::EmptyStatement);
  EXPECT_EQ(ast->root->children[2]->type, neojs::NodeType::ExprStmt);
  a.destroy(ast);

  /* 非 #! 开头：无操作 */
  ast = neojs::parseSource(&a, "a;", std::strlen("a;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  a.destroy(ast);
}

/* -------------------------------------------------------------------------- */
/* 块语句（M1.5 ②）：{ stmt... } + 箭头函数块体                              */
/* -------------------------------------------------------------------------- */

TEST(Frontend, BlockStatement) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* {} → 空块 */
  neojs::AST *ast = neojs::parseSource(&a, "{}", std::strlen("{}"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::BlockStatement);
  EXPECT_TRUE(ast->root->children[0]->children.empty());
  a.destroy(ast);

  /* { a; b; } → 块内两条语句 */
  ast = neojs::parseSource(&a, "{ a; b; }", std::strlen("{ a; b; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *block = ast->root->children[0];
  EXPECT_EQ(block->type, neojs::NodeType::BlockStatement);
  ASSERT_EQ(block->children.size(), 2u);
  EXPECT_EQ(block->children[0]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(block->children[1]->type, neojs::NodeType::ExprStmt);
  a.destroy(ast);

  /* 嵌套块：{ { {} } } */
  ast = neojs::parseSource(&a, "{ { {} } }", std::strlen("{ { {} } }"), sink);
  ASSERT_NE(ast, nullptr);
  block = ast->root->children[0];
  EXPECT_EQ(block->type, neojs::NodeType::BlockStatement);
  EXPECT_EQ(block->children[0]->type, neojs::NodeType::BlockStatement);
  EXPECT_EQ(block->children[0]->children[0]->type, neojs::NodeType::BlockStatement);
  a.destroy(ast);

  /* 未闭合块报错 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource(&a, "{ a;", std::strlen("{ a;"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());
  a.destroy(ast);
}

TEST(Frontend, BlockVsObjectLiteral) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* 协同回归：语句位置 {a} 是块；读值位置 ({a: 1}) 是对象字面量 */
  neojs::AST *ast = neojs::parseSource(&a, "{a}", std::strlen("{a}"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::BlockStatement);
  a.destroy(ast);

  ast = neojs::parseSource(&a, "({a: 1})", std::strlen("({a: 1})"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_EQ(ast->root->children[0]->children[0]->type, neojs::NodeType::ObjectPattern);
  a.destroy(ast);
}

TEST(Frontend, ArrowBlockBody) {
  neojs::DiagnosticSink sink;
  neojs::JSAllocator a = makeAllocator();
  /* x => { ... } → 箭头函数块体（M1.5 ② 解锁，③-4 预留） */
  neojs::AST *ast =
      neojs::parseSource(&a, "x => { x; }", std::strlen("x => { x; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BlockStatement);
  a.destroy(ast);

  /* 多参 + 块体：(a, b) => { s = a + b; } */
  ast = neojs::parseSource(
      &a, "(a, b) => { s = a + b; }", std::strlen("(a, b) => { s = a + b; }"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::BlockStatement);
  a.destroy(ast);

  /* 表达式体仍可用：x => x */
  ast = neojs::parseSource(&a, "x => x", std::strlen("x => x"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  a.destroy(ast);
}
