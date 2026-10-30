/*
 * 前端流水线单元测试（M1.1：Lexer/Parser/Diagnostics 骨架）。
 * 验证：token 最小流、AST 最小树、语法错误位置、协同模式上下文传递、
 * 注释贪婪消解（多字符符号回退单 /）、AST 内存管理（new/delete，无泄漏）。
 */

#include <gtest/gtest.h>

#include <cstring>

#include "neojs/frontend/diagnostics.h"
#include "neojs/frontend/frontend.h"
#include "neojs/frontend/lexer.h"
#include "neojs/frontend/parser.h"

namespace {

/* （迁移到 core::AllocatedObject：AST 经 new/delete 分配，无需显式分配器） */

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
  neojs::AST *ast = neojs::parseSource("1 + 2", 5, sink);
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

  delete ast;
}

TEST(Frontend, ParsePrecedence) {
  neojs::DiagnosticSink sink;
  /* 1 + 2 * 3 → BinaryOp(+)[1, BinaryOp(*)[2,3]] */
  neojs::AST *ast = neojs::parseSource("1 + 2 * 3", 9, sink);
  ASSERT_NE(ast, nullptr);

  neojs::ASTNode *bin = ast->root->children[0]->children[0];
  EXPECT_EQ(bin->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(bin->binop, neojs::BinaryOpKind::Add);
  neojs::ASTNode *rhs = bin->children[1];
  EXPECT_EQ(rhs->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(rhs->binop, neojs::BinaryOpKind::Mul);

  delete ast;
}

TEST(Frontend, ParseLeftAssociativity) {
  neojs::DiagnosticSink sink;
  /* 1 - 2 - 3 → BinaryOp(-)[BinaryOp(-)[1,2],3]（左结合） */
  neojs::AST *ast = neojs::parseSource("1 - 2 - 3", 9, sink);
  ASSERT_NE(ast, nullptr);

  neojs::ASTNode *bin = ast->root->children[0]->children[0];
  EXPECT_EQ(bin->binop, neojs::BinaryOpKind::Sub);
  EXPECT_EQ(bin->children[0]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(bin->children[0]->binop, neojs::BinaryOpKind::Sub);
  EXPECT_EQ(bin->children[1]->type, neojs::NodeType::Literal);

  delete ast;
}

TEST(Frontend, ParseParenthesized) {
  neojs::DiagnosticSink sink;
  /* (1 + 2) * 3 → BinaryOp(*)[BinaryOp(+)[1,2],3] */
  neojs::AST *ast = neojs::parseSource("(1 + 2) * 3", 11, sink);
  ASSERT_NE(ast, nullptr);

  neojs::ASTNode *bin = ast->root->children[0]->children[0];
  EXPECT_EQ(bin->binop, neojs::BinaryOpKind::Mul);
  EXPECT_EQ(bin->children[0]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(bin->children[0]->binop, neojs::BinaryOpKind::Add);

  delete ast;
}

TEST(Frontend, ParseIdentifier) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("foo", 3, sink);
  ASSERT_NE(ast, nullptr);

  neojs::ASTNode *id = ast->root->children[0]->children[0];
  EXPECT_EQ(id->type, neojs::NodeType::Identifier);
  EXPECT_EQ(id->name, "foo");

  delete ast;
}

TEST(Frontend, ParseSyntaxError) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("1 +", 3, sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  EXPECT_EQ(sink.diagnostics()[0].severity, neojs::Severity::Error);
  /* 错误位置：1:4（+ 已消费，EOF 处期待 primary） */
  EXPECT_EQ(sink.diagnostics()[0].loc.line, 1u);
  EXPECT_EQ(sink.diagnostics()[0].loc.col, 4u);
}

TEST(Frontend, ParseMissingRParen) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("(1 + 2", 6, sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  EXPECT_EQ(sink.diagnostics()[0].severity, neojs::Severity::Error);
}

TEST(Frontend, ParseSemicolonTerminated) {
  neojs::DiagnosticSink sink;
  /* 多条表达式语句 + 分号：1; 2 */
  neojs::AST *ast = neojs::parseSource("1; 2", 4, sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 2u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(ast->root->children[1]->type, neojs::NodeType::ExprStmt);

  delete ast;
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
  neojs::AST *ast = neojs::parseSource("1 + 2", 5, sink);
  ASSERT_NE(ast, nullptr);
  std::string out = neojs::dumpAst(*ast);
  EXPECT_NE(out.find("Program"), std::string::npos);
  EXPECT_NE(out.find("ExprStmt"), std::string::npos);
  EXPECT_NE(out.find("BinaryOp(+)"), std::string::npos);
  EXPECT_NE(out.find("Literal(1)"), std::string::npos);
  EXPECT_NE(out.find("Literal(2)"), std::string::npos);

  delete ast;
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

/* 数字：旧式八进制 0123 打废弃警告（DEP-0002，非严格模式；严格模式 Parser 报错） */
TEST(Frontend, NumberLegacyOctalLiteralWarns) {
  neojs::DiagnosticSink sink;
  neojs::Lexer l("0123", 4, sink);
  const neojs::Token &t = l.next();
  EXPECT_EQ(t.kind, neojs::TokenKind::Number);
  EXPECT_FALSE(sink.hasErrors());
  EXPECT_EQ(sink.diagnostics().size(), 1u);
  EXPECT_EQ(sink.diagnostics()[0].severity, neojs::Severity::Warning);
  EXPECT_EQ(sink.diagnostics()[0].code, "DEP-0002");
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
  neojs::AST *ast = neojs::parseSource("\"hi\"", 4, sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());

  neojs::ASTNode *lit = ast->root->children[0]->children[0];
  EXPECT_EQ(lit->type, neojs::NodeType::Literal);
  EXPECT_TRUE(lit->literal.is_string);
  EXPECT_EQ(lit->literal.str, "hi");

  delete ast;
}

/* -------------------------------------------------------------------------- */
/* M1.4 表达式体系：① Literal + ② Identifier/模式                           */
/* -------------------------------------------------------------------------- */

/* 布尔字面量：true / false */
TEST(Frontend, LiteralBoolean) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("true + false", 12, sink);
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

  delete ast;
}

/* null 字面量 */
TEST(Frontend, LiteralNull) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("null", 4, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *lit = ast->root->children[0]->children[0];
  EXPECT_EQ(lit->type, neojs::NodeType::Literal);
  EXPECT_TRUE(lit->literal.is_null);
  delete ast;
}

/* 正则字面量：pattern + flags */
TEST(Frontend, LiteralRegExp) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("/ab+c/gi", 8, sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *re = ast->root->children[0]->children[0];
  EXPECT_EQ(re->type, neojs::NodeType::RegExpLiteral);
  EXPECT_EQ(re->regexp.pattern, "ab+c");
  EXPECT_EQ(re->regexp.flags, "gi");
  delete ast;
}

/* 除法 vs 正则（re-scan）：a / b 是除法，/re/ 是正则 */
TEST(Frontend, SlashDivisionVsRegex) {
  neojs::DiagnosticSink sink;
  neojs::AST *div = neojs::parseSource("a / b", 5, sink);
  ASSERT_NE(div, nullptr);
  neojs::ASTNode *bin = div->root->children[0]->children[0];
  EXPECT_EQ(bin->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(bin->binop, neojs::BinaryOpKind::Div);
  delete div;

  neojs::DiagnosticSink sink2;
  neojs::AST *re = neojs::parseSource("/re/", 4, sink2);
  ASSERT_NE(re, nullptr);
  neojs::ASTNode *lit = re->root->children[0]->children[0];
  EXPECT_EQ(lit->type, neojs::NodeType::RegExpLiteral);
  delete re;
}

/* 私有字段：#x */
TEST(Frontend, PrivateIdentifier) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("#x", 2, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *id = ast->root->children[0]->children[0];
  EXPECT_EQ(id->type, neojs::NodeType::PrivateIdentifier);
  EXPECT_EQ(id->name, "x");
  delete ast;
}

/* 对象字面量：{ a: 1, b } */
TEST(Frontend, ObjectLiteral) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("({a: 1, b})", 11, sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *obj = ast->root->children[0]->children[0];
  EXPECT_EQ(obj->type, neojs::NodeType::ObjectLiteral);
  ASSERT_EQ(obj->children.size(), 2u);
  EXPECT_EQ(obj->children[0]->type, neojs::NodeType::Property);
  EXPECT_EQ(obj->children[0]->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(obj->children[0]->children[1]->type, neojs::NodeType::Literal);
  /* 简写属性 */
  EXPECT_EQ(obj->children[1]->type, neojs::NodeType::Property);
  EXPECT_EQ(obj->children[1]->prop.kind, neojs::PropertyKind::Shorthand);
  delete ast;
}

/* 数组字面量：[1, 2, ...rest] */
TEST(Frontend, ArrayLiteral) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("[1, 2, ...r]", 12, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *arr = ast->root->children[0]->children[0];
  EXPECT_EQ(arr->type, neojs::NodeType::ArrayLiteral);
  ASSERT_EQ(arr->children.size(), 3u);
  EXPECT_EQ(arr->children[0]->type, neojs::NodeType::Literal);
  EXPECT_EQ(arr->children[2]->type, neojs::NodeType::SpreadElement);
  delete ast;
}

/* 字符串键属性：{ "k" : 1 } */
TEST(Frontend, ObjectStringKey) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("({\"k\" : 1})", 11, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *obj = ast->root->children[0]->children[0];
  EXPECT_EQ(obj->type, neojs::NodeType::ObjectLiteral);
  EXPECT_EQ(obj->children[0]->children[0]->literal.str, "k");
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 一元运算（M1.4）                                                            */
/* -------------------------------------------------------------------------- */

TEST(Frontend, UnarySymbolOps) {
  neojs::DiagnosticSink sink;

  /* !x → UnaryOp(!)[x] */
  neojs::AST *ast = neojs::parseSource("!x", 2, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->type, neojs::NodeType::UnaryOp);
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Not);
  EXPECT_EQ(u->children[0]->type, neojs::NodeType::Identifier);
  delete ast;

  /* ~x */
  ast = neojs::parseSource("~x", 2, sink);
  ASSERT_NE(ast, nullptr);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::BitNot);
  delete ast;

  /* +x / -x */
  ast = neojs::parseSource("+x", 2, sink);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Pos);
  delete ast;
  ast = neojs::parseSource("-x", 2, sink);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Neg);
  delete ast;
}

TEST(Frontend, UnaryKeywordOps) {
  neojs::DiagnosticSink sink;

  neojs::AST *ast = neojs::parseSource("typeof x", 8, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->type, neojs::NodeType::UnaryOp);
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Typeof);
  delete ast;

  ast = neojs::parseSource("void 0", 6, sink);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Void);
  delete ast;

  ast = neojs::parseSource("delete x", 8, sink);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Delete);
  delete ast;

  /* 顶层 await 仅模块上下文合法（M1.6 ③），此处按 module 解析 */
  ast = neojs::parseSource("await x", std::strlen("await x"), sink, /*module=*/true);
  u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Await);
  delete ast;
}

TEST(Frontend, UnaryChainedRightAssoc) {
  neojs::DiagnosticSink sink;
  /* !!!x → !(!(!x)) 右结合嵌套 */
  neojs::AST *ast = neojs::parseSource("!!!x", 4, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *u = ast->root->children[0]->children[0];
  EXPECT_EQ(u->unop, neojs::UnaryOpKind::Not);
  EXPECT_EQ(u->children[0]->unop, neojs::UnaryOpKind::Not);
  EXPECT_EQ(u->children[0]->children[0]->unop, neojs::UnaryOpKind::Not);
  EXPECT_EQ(u->children[0]->children[0]->children[0]->type,
            neojs::NodeType::Identifier);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 二元运算全套（M1.4）                                                        */
/* -------------------------------------------------------------------------- */

TEST(Frontend, BinaryFullOps) {
  neojs::DiagnosticSink sink;

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
    neojs::AST *ast = neojs::parseSource(c.src, std::strlen(c.src), sink);
    ASSERT_NE(ast, nullptr) << c.src;
    neojs::ASTNode *bin = ast->root->children[0]->children[0];
    EXPECT_EQ(bin->type, neojs::NodeType::BinaryOp) << c.src;
    EXPECT_EQ(bin->binop, c.expect) << c.src;
    delete ast;
  }
}

TEST(Frontend, BinaryPowerRightAssoc) {
  neojs::DiagnosticSink sink;
  /* 2 ** 3 ** 2 → 2 ** (3 ** 2)（右结合） */
  neojs::AST *ast = neojs::parseSource("2 ** 3 ** 2", 11, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *bin = ast->root->children[0]->children[0];
  EXPECT_EQ(bin->binop, neojs::BinaryOpKind::Pow);
  EXPECT_EQ(bin->children[0]->type, neojs::NodeType::Literal);
  EXPECT_EQ(bin->children[1]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(bin->children[1]->binop, neojs::BinaryOpKind::Pow);
  delete ast;
}

TEST(Frontend, BinaryFullPrecedence) {
  neojs::DiagnosticSink sink;
  /* !x && y || z → (!x && y) || z：一元 > && > || */
  neojs::AST *ast = neojs::parseSource("!x && y || z", 12, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->binop, neojs::BinaryOpKind::LogicalOr);
  EXPECT_EQ(top->children[0]->binop, neojs::BinaryOpKind::LogicalAnd);
  EXPECT_EQ(top->children[0]->children[0]->type, neojs::NodeType::UnaryOp);
  delete ast;

  /* a | b << c → a | (b << c)：<< > | */
  ast = neojs::parseSource("a | b << c", 10, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->binop, neojs::BinaryOpKind::BitOr);
  EXPECT_EQ(top->children[1]->binop, neojs::BinaryOpKind::LShift);
  delete ast;

  /* 1 < 2 == true → (1 < 2) == true：< > == */
  ast = neojs::parseSource("1 < 2 == true", 13, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->binop, neojs::BinaryOpKind::Eq);
  EXPECT_EQ(top->children[0]->binop, neojs::BinaryOpKind::Lt);
  delete ast;

  /* a + b * c → a + (b * c)：* > + */
  ast = neojs::parseSource("a + b * c", 9, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->binop, neojs::BinaryOpKind::Add);
  EXPECT_EQ(top->children[1]->binop, neojs::BinaryOpKind::Mul);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 三元 / 赋值 / 序列（M1.4 ③-2）                                              */
/* -------------------------------------------------------------------------- */

TEST(Frontend, AssignmentRightAssoc) {
  neojs::DiagnosticSink sink;
  /* a = b = c → Assignment[=][a, Assignment[=][b, c]] 右结合 */
  neojs::AST *ast = neojs::parseSource("a = b = c", 9, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Assignment);
  EXPECT_EQ(top->assign_op, neojs::AssignmentOpKind::Assign);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Assignment);
  EXPECT_EQ(top->children[1]->assign_op, neojs::AssignmentOpKind::Assign);
  delete ast;
}

TEST(Frontend, CompoundAssignments) {
  neojs::DiagnosticSink sink;

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
    neojs::AST *ast = neojs::parseSource(c.src, std::strlen(c.src), sink);
    ASSERT_NE(ast, nullptr) << c.src;
    neojs::ASTNode *top = ast->root->children[0]->children[0];
    EXPECT_EQ(top->type, neojs::NodeType::Assignment) << c.src;
    EXPECT_EQ(top->assign_op, c.expect) << c.src;
    delete ast;
  }
}

TEST(Frontend, InvalidAssignmentTarget) {
  neojs::DiagnosticSink sink;
  /* 1 = x → 非法左值 */
  neojs::AST *ast = neojs::parseSource("1 = x", 5, sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  delete ast;
}

TEST(Frontend, ConditionalExpression) {
  neojs::DiagnosticSink sink;
  /* x ? y : z → Conditional[x, y, z] */
  neojs::AST *ast = neojs::parseSource("x ? y : z", 9, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Conditional);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::Identifier);
  delete ast;

  /* 右结合：a ? b : c ? d : e → Conditional[a, b, Conditional[c, d, e]] */
  ast = neojs::parseSource("a ? b : c ? d : e", 17, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Conditional);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::Conditional);
  delete ast;
}

TEST(Frontend, SequenceExpression) {
  neojs::DiagnosticSink sink;
  /* a, b, c → Sequence[Sequence[a, b], c] 左结合 */
  neojs::AST *ast = neojs::parseSource("a, b, c", 7, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Sequence);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Sequence);
  EXPECT_EQ(top->children[0]->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[0]->children[1]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  delete ast;
}

TEST(Frontend, SequenceInsideParens) {
  neojs::DiagnosticSink sink;
  /* (x = 1, y = 2) → 括号内序列，元素为赋值 */
  neojs::AST *ast = neojs::parseSource("(x = 1, y = 2)", 14, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Sequence);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Assignment);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Assignment);
  delete ast;
}

TEST(Frontend, LiteralCommaNotSequence) {
  neojs::DiagnosticSink sink;
  /* 协同回归：数组/对象字面量内部逗号是分隔符，不被序列吞掉 */
  neojs::AST *ast = neojs::parseSource("[1, 2, 3]", 9, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *arr = ast->root->children[0]->children[0];
  EXPECT_EQ(arr->type, neojs::NodeType::ArrayLiteral);
  EXPECT_EQ(arr->children.size(), 3u);
  delete ast;

  ast = neojs::parseSource("({a: 1, b: 2})", 14, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *obj = ast->root->children[0]->children[0];
  EXPECT_EQ(obj->type, neojs::NodeType::ObjectLiteral);
  EXPECT_EQ(obj->children.size(), 2u);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 成员访问 / 调用 / new（M1.4 ③-3）                                           */
/* -------------------------------------------------------------------------- */

TEST(Frontend, MemberAccess) {
  neojs::DiagnosticSink sink;
  /* a.b.c → Member(Member(a, b), c) */
  neojs::AST *ast = neojs::parseSource("a.b.c", 5, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_FALSE(top->member.computed);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  delete ast;
}

TEST(Frontend, ComputedMember) {
  neojs::DiagnosticSink sink;
  /* arr[i + 1] → Member[] [arr, BinaryOp(+)] */
  neojs::AST *ast = neojs::parseSource("arr[i + 1]", std::strlen("arr[i + 1]"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.computed);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BinaryOp);
  delete ast;
}

TEST(Frontend, CallExpression) {
  neojs::DiagnosticSink sink;
  /* f(x, y) → Call[f, x, y] */
  neojs::AST *ast = neojs::parseSource("f(x, y)", 7, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::CallExpression);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  ASSERT_EQ(top->children.size(), 3u);
  delete ast;

  /* 链式：a.b.c(d).e[0] */
  ast = neojs::parseSource("a.b.c(d).e[0]", std::strlen("a.b.c(d).e[0]"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.computed);
  delete ast;
}

TEST(Frontend, NewExpression) {
  neojs::DiagnosticSink sink;
  /* new Foo() → New[Foo] */
  neojs::AST *ast = neojs::parseSource("new Foo()", std::strlen("new Foo()"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::NewExpression);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  delete ast;

  /* new Foo 无括号 */
  ast = neojs::parseSource("new Foo", 7, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::NewExpression);
  EXPECT_EQ(top->children.size(), 1u);
  delete ast;

  /* new a.b(1, 2).c：callee 吃成员 a.b，参数 (1,2)，.c 在外层 */
  ast = neojs::parseSource("new a.b(1, 2).c", 15, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::NewExpression);
  delete ast;
}

TEST(Frontend, CallSpreadArgs) {
  neojs::DiagnosticSink sink;
  /* f(...xs, 1) → Call[f, Spread[xs], 1] */
  neojs::AST *ast = neojs::parseSource("f(...xs, 1)", std::strlen("f(...xs, 1)"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::CallExpression);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::SpreadElement);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::Literal);
  delete ast;
}

TEST(Frontend, UnaryBelowMember) {
  neojs::DiagnosticSink sink;
  /* -a.b → UnaryOp(-)[Member(a, b)]：成员优先级高于一元 */
  neojs::AST *ast = neojs::parseSource("-a.b", 4, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UnaryOp);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  delete ast;
}

TEST(Frontend, MemberAssignmentTarget) {
  neojs::DiagnosticSink sink;
  /* a.b = 1 → Assignment[=][Member(a, b), 1]：成员是合法左值 */
  neojs::AST *ast = neojs::parseSource("a.b = 1", 7, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::Assignment);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  delete ast;
}

TEST(Frontend, PrivateMemberAccess) {
  neojs::DiagnosticSink sink;
  /* obj.#x → Member(obj, PrivateIdentifier(x)) */
  neojs::AST *ast = neojs::parseSource("obj.#x", 6, sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::PrivateIdentifier);
  delete ast;
}

TEST(Frontend, AwaitCallAndDeleteMember) {
  neojs::DiagnosticSink sink;
  /* await foo() → UnaryOp(await)[Call(foo)]（顶层 await 仅模块上下文合法） */
  neojs::AST *ast = neojs::parseSource("await foo()", std::strlen("await foo()"), sink,
                                       /*module=*/true);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UnaryOp);
  EXPECT_EQ(top->unop, neojs::UnaryOpKind::Await);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::CallExpression);
  delete ast;

  /* delete obj.x → UnaryOp(delete)[Member(obj, x)] */
  ast = neojs::parseSource("delete obj.x", 12, sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UnaryOp);
  EXPECT_EQ(top->unop, neojs::UnaryOpKind::Delete);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 箭头函数（M1.4 ③-4，仅表达式体）                                            */
/* -------------------------------------------------------------------------- */

TEST(Frontend, ArrowSingleParam) {
  neojs::DiagnosticSink sink;
  /* x => x + 1 → Arrow[Identifier(x), BinaryOp(+)] */
  neojs::AST *ast = neojs::parseSource("x => x + 1", std::strlen("x => x + 1"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[0]->name, "x");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->children[1]->binop, neojs::BinaryOpKind::Add);
  delete ast;
}

TEST(Frontend, ArrowMultiParam) {
  neojs::DiagnosticSink sink;
  /* (a, b) => a + b → Arrow[Identifier(a), Identifier(b), BinaryOp(+)] */
  neojs::AST *ast = neojs::parseSource("(a, b) => a + b", std::strlen("(a, b) => a + b"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[0]->name, "a");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[1]->name, "b");
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::BinaryOp);
  delete ast;
}

TEST(Frontend, ArrowNoParam) {
  neojs::DiagnosticSink sink;
  /* () => 42 → Arrow[Literal(42)] */
  neojs::AST *ast = neojs::parseSource("() => 42", std::strlen("() => 42"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 1u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Literal);
  delete ast;
}

TEST(Frontend, ArrowRightAssocNested) {
  neojs::DiagnosticSink sink;
  /* x => y => x + y → Arrow[x, Arrow[y, BinaryOp(+)]（右结合嵌套） */
  neojs::AST *ast =
      neojs::parseSource("x => y => x + y", std::strlen("x => y => x + y"), sink);
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
  delete ast;
}

TEST(Frontend, ArrowRestParam) {
  neojs::DiagnosticSink sink;
  /* (...xs) => xs → Arrow[RestElement[Identifier(xs)], Identifier(xs)] */
  neojs::AST *ast = neojs::parseSource("(...xs) => xs", std::strlen("(...xs) => xs"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::RestElement);
  EXPECT_EQ(top->children[0]->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[0]->children[0]->name, "xs");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  delete ast;
}

TEST(Frontend, ArrowVsParenNotConfused) {
  neojs::DiagnosticSink sink;
  /* 协同回归：(a + b) * c 是括号表达式，不得误判为箭头 */
  neojs::AST *ast =
      neojs::parseSource("(a + b) * c", std::strlen("(a + b) * c"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->binop, neojs::BinaryOpKind::Mul);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->children[0]->binop, neojs::BinaryOpKind::Add);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 模板串插值（M1.4 ③-5）                                                     */
/* -------------------------------------------------------------------------- */

TEST(Frontend, TemplateLiteralNoInterp) {
  neojs::DiagnosticSink sink;
  /* `hello` → TemplateLiteral[Literal("hello")] 单 quasi */
  neojs::AST *ast = neojs::parseSource("`hello`", std::strlen("`hello`"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::TemplateLiteral);
  ASSERT_EQ(top->children.size(), 1u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Literal);
  EXPECT_EQ(top->children[0]->literal.str, "hello");
  delete ast;
}

TEST(Frontend, TemplateSingleInterp) {
  neojs::DiagnosticSink sink;
  /* `a${1}` → TemplateLiteral[Literal("a"), Literal(1), Literal("")] */
  neojs::AST *ast = neojs::parseSource("`a${1}`", std::strlen("`a${1}`"), sink);
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
  delete ast;
}

TEST(Frontend, TemplateMultiInterp) {
  neojs::DiagnosticSink sink;
  /* `a${x}b${y}c` → [quasi a, Identifier x, quasi b, Identifier y, quasi c] */
  neojs::AST *ast =
      neojs::parseSource("`a${x}b${y}c`", std::strlen("`a${x}b${y}c`"), sink);
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
  delete ast;
}

TEST(Frontend, TemplateInterpExpression) {
  neojs::DiagnosticSink sink;
  /* `sum=${a + b}` → [quasi "sum=", BinaryOp(+), quasi ""] */
  neojs::AST *ast =
      neojs::parseSource("`sum=${a + b}`", std::strlen("`sum=${a + b}`"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::TemplateLiteral);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[0]->literal.str, "sum=");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->children[1]->binop, neojs::BinaryOpKind::Add);
  delete ast;
}

TEST(Frontend, TemplateNested) {
  neojs::DiagnosticSink sink;
  /* 嵌套模板：`n=${`in${x}`}` → 插值内又含 TemplateLiteral */
  neojs::AST *ast =
      neojs::parseSource("`n=${`in${x}`}`", std::strlen("`n=${`in${x}`}`"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::TemplateLiteral);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::TemplateLiteral);
  delete ast;
}

TEST(Frontend, TemplateUnterminated) {
  neojs::DiagnosticSink sink;
  /* `unterminated → 报错，ast 为 nullptr */
  neojs::AST *ast =
      neojs::parseSource("`unterminated", std::strlen("`unterminated"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  delete ast;
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
  /* ++x → Update(++, prefix)[x] */
  neojs::AST *ast = neojs::parseSource("++x", std::strlen("++x"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UpdateExpression);
  EXPECT_EQ(top->update_op, neojs::UpdateOpKind::Inc);
  EXPECT_TRUE(top->prefix);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  delete ast;

  /* x-- → Update(--, postfix)[x] */
  ast = neojs::parseSource("x--", std::strlen("x--"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UpdateExpression);
  EXPECT_EQ(top->update_op, neojs::UpdateOpKind::Dec);
  EXPECT_FALSE(top->prefix);
  delete ast;
}

TEST(Frontend, UpdateMemberOperand) {
  neojs::DiagnosticSink sink;
  /* a.b++ → Update(++, postfix)[Member]：成员是合法操作数 */
  neojs::AST *ast = neojs::parseSource("a.b++", std::strlen("a.b++"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UpdateExpression);
  EXPECT_FALSE(top->prefix);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  delete ast;

  /* --a[0] → Update(--, prefix)[Member[]] */
  ast = neojs::parseSource("--a[0]", std::strlen("--a[0]"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::UpdateExpression);
  EXPECT_EQ(top->update_op, neojs::UpdateOpKind::Dec);
  EXPECT_TRUE(top->prefix);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->children[0]->member.computed);
  delete ast;
}

TEST(Frontend, UpdateInvalidOperand) {
  neojs::DiagnosticSink sink;
  /* 1++ → 非法操作数 */
  neojs::AST *ast = neojs::parseSource("1++", std::strlen("1++"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  delete ast;
}

TEST(Frontend, OptionalChainMember) {
  neojs::DiagnosticSink sink;
  /* a?.b → MemberExpression.?（optional=true，computed=false） */
  neojs::AST *ast = neojs::parseSource("a?.b", std::strlen("a?.b"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional);
  EXPECT_FALSE(top->member.computed);
  delete ast;

  /* a?.[x] → MemberExpression[]?（optional=true，computed=true） */
  ast = neojs::parseSource("a?.[x]", std::strlen("a?.[x]"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional);
  EXPECT_TRUE(top->member.computed);
  delete ast;

  /* a?.(1) → CallExpression? */
  ast = neojs::parseSource("a?.(1)", std::strlen("a?.(1)"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::CallExpression);
  EXPECT_TRUE(top->optional);
  delete ast;
}

TEST(Frontend, OptionalChainShortCircuit) {
  neojs::DiagnosticSink sink;
  /* a?.b.c → 一旦可选，后续 .c 也 optional（短路语义） */
  neojs::AST *ast = neojs::parseSource("a?.b.c", std::strlen("a?.b.c"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional); /* .c 也 optional */
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->children[0]->member.optional); /* a?.b */
  delete ast;

  /* a?.b?.c → 多级可选链 */
  ast = neojs::parseSource("a?.b?.c", std::strlen("a?.b?.c"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional);
  delete ast;

  /* a.b?.c → 普通成员后才可选 */
  ast = neojs::parseSource("a.b?.c", std::strlen("a.b?.c"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::MemberExpression);
  EXPECT_FALSE(top->children[0]->member.optional); /* a.b 非可选 */
  delete ast;
}

TEST(Frontend, OptionalChainNotInNewCallee) {
  neojs::DiagnosticSink sink;
  /* new a?.b → 语法错误（可选链不能作 new 的 callee） */
  neojs::AST *ast = neojs::parseSource("new a?.b", std::strlen("new a?.b"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  delete ast;

  /* new Foo()?.b → 合法：new 结果上可选链 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("new Foo()?.b", std::strlen("new Foo()?.b"), sink2);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::MemberExpression);
  EXPECT_TRUE(top->member.optional);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::NewExpression);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 语句体系地基（M1.5）：hashbang + directives + 空语句 + 表达式兜底          */
/* -------------------------------------------------------------------------- */

TEST(Frontend, EmptyStatement) {
  neojs::DiagnosticSink sink;
  /* ; → EmptyStatement；;;; → 3 个 */
  neojs::AST *ast = neojs::parseSource(";", std::strlen(";"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::EmptyStatement);
  delete ast;

  ast = neojs::parseSource(";;;", std::strlen(";;;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 3u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::EmptyStatement);
  EXPECT_EQ(ast->root->children[1]->type, neojs::NodeType::EmptyStatement);
  EXPECT_EQ(ast->root->children[2]->type, neojs::NodeType::EmptyStatement);
  delete ast;
}

TEST(Frontend, StatementFallbackExpression) {
  neojs::DiagnosticSink sink;
  /* 兜底：非特征值首 token → 表达式语句 */
  neojs::AST *ast = neojs::parseSource("a = 1;", std::strlen("a = 1;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(ast->root->children[0]->children[0]->type, neojs::NodeType::Assignment);
  delete ast;

  /* 空语句与表达式混合 */
  ast = neojs::parseSource(";a;b;", std::strlen(";a;b;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 3u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::EmptyStatement);
  EXPECT_EQ(ast->root->children[1]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(ast->root->children[2]->type, neojs::NodeType::ExprStmt);
  delete ast;
}

TEST(Frontend, DirectivePrologue) {
  neojs::DiagnosticSink sink;
  /* 序言：开头字符串字面量表达式语句标记 directive */
  neojs::AST *ast =
      neojs::parseSource("\"use strict\";", std::strlen("\"use strict\";"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  neojs::ASTNode *stmt = ast->root->children[0];
  EXPECT_EQ(stmt->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(stmt->directive, "use strict");
  delete ast;

  /* 序言后遇到非字符串字面量即退出：后续字符串不再标记 */
  ast = neojs::parseSource("1 + 2; \"later\";",
                           std::strlen("1 + 2; \"later\";"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 2u);
  EXPECT_TRUE(ast->root->children[0]->directive.empty());
  EXPECT_TRUE(ast->root->children[1]->directive.empty());
  delete ast;
}

TEST(Frontend, HashbangSkip) {
  neojs::DiagnosticSink sink;
  /* 入口级 hashbang：整行跳过，后续正常解析 */
  neojs::AST *ast = neojs::parseSource(
"#!/usr/bin/env node\na = 1;", std::strlen("#!/usr/bin/env node\na = 1;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::ExprStmt);
  delete ast;

  /* hashbang + directive 序言 + 空语句混合 */
  ast = neojs::parseSource("#!x\n\"use strict\";\n;f();",
                           std::strlen("#!x\n\"use strict\";\n;f();"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 3u);
  EXPECT_EQ(ast->root->children[0]->directive, "use strict");
  EXPECT_EQ(ast->root->children[1]->type, neojs::NodeType::EmptyStatement);
  EXPECT_EQ(ast->root->children[2]->type, neojs::NodeType::ExprStmt);
  delete ast;

  /* 非 #! 开头：无操作 */
  ast = neojs::parseSource("a;", std::strlen("a;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 块语句（M1.5 ②）：{ stmt... } + 箭头函数块体                              */
/* -------------------------------------------------------------------------- */

TEST(Frontend, BlockStatement) {
  neojs::DiagnosticSink sink;
  /* {} → 空块 */
  neojs::AST *ast = neojs::parseSource("{}", std::strlen("{}"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::BlockStatement);
  EXPECT_TRUE(ast->root->children[0]->children.empty());
  delete ast;

  /* { a; b; } → 块内两条语句 */
  ast = neojs::parseSource("{ a; b; }", std::strlen("{ a; b; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *block = ast->root->children[0];
  EXPECT_EQ(block->type, neojs::NodeType::BlockStatement);
  ASSERT_EQ(block->children.size(), 2u);
  EXPECT_EQ(block->children[0]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(block->children[1]->type, neojs::NodeType::ExprStmt);
  delete ast;

  /* 嵌套块：{ { {} } } */
  ast = neojs::parseSource("{ { {} } }", std::strlen("{ { {} } }"), sink);
  ASSERT_NE(ast, nullptr);
  block = ast->root->children[0];
  EXPECT_EQ(block->type, neojs::NodeType::BlockStatement);
  EXPECT_EQ(block->children[0]->type, neojs::NodeType::BlockStatement);
  EXPECT_EQ(block->children[0]->children[0]->type, neojs::NodeType::BlockStatement);
  delete ast;

  /* 未闭合块报错 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("{ a;", std::strlen("{ a;"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());
  delete ast;
}

TEST(Frontend, BlockVsObjectLiteral) {
  neojs::DiagnosticSink sink;
  /* 协同回归：语句位置 {a} 是块；读值位置 ({a: 1}) 是对象字面量 */
  neojs::AST *ast = neojs::parseSource("{a}", std::strlen("{a}"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::BlockStatement);
  delete ast;

  ast = neojs::parseSource("({a: 1})", std::strlen("({a: 1})"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_EQ(ast->root->children[0]->children[0]->type, neojs::NodeType::ObjectLiteral);
  delete ast;
}

TEST(Frontend, ArrowBlockBody) {
  neojs::DiagnosticSink sink;
  /* x => { ... } → 箭头函数块体（M1.5 ② 解锁，③-4 预留） */
  neojs::AST *ast =
      neojs::parseSource("x => { x; }", std::strlen("x => { x; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BlockStatement);
  delete ast;

  /* 多参 + 块体：(a, b) => { s = a + b; } */
  ast = neojs::parseSource(
"(a, b) => { s = a + b; }", std::strlen("(a, b) => { s = a + b; }"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::BlockStatement);
  delete ast;

  /* 表达式体仍可用：x => x */
  ast = neojs::parseSource("x => x", std::strlen("x => x"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ArrowFunctionExpression);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 标签语句（M1.5 ③）：label: stmt，独立成语句                              */
/* -------------------------------------------------------------------------- */

TEST(Frontend, LabeledStatement) {
  neojs::DiagnosticSink sink;
  /* label: { x; } → LabeledStatement(label)[Block] */
  neojs::AST *ast =
      neojs::parseSource("label: { x; }", std::strlen("label: { x; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::LabeledStatement);
  EXPECT_EQ(top->name, "label");
  ASSERT_EQ(top->children.size(), 1u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::BlockStatement);
  delete ast;
}

TEST(Frontend, LabeledNested) {
  neojs::DiagnosticSink sink;
  /* a: b: c: d; → 嵌套 label 链 */
  neojs::AST *ast = neojs::parseSource("a: b: c: d;", std::strlen("a: b: c: d;"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::LabeledStatement);
  EXPECT_EQ(top->name, "a");
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::LabeledStatement);
  EXPECT_EQ(top->children[0]->name, "b");
  EXPECT_EQ(top->children[0]->children[0]->type, neojs::NodeType::LabeledStatement);
  EXPECT_EQ(top->children[0]->children[0]->name, "c");
  EXPECT_EQ(top->children[0]->children[0]->children[0]->type, neojs::NodeType::ExprStmt);
  delete ast;
}

TEST(Frontend, LabelVsTernaryNotConfused) {
  neojs::DiagnosticSink sink;
  /* 协同回归：a ? b : c 是三元，不是 label（? 触发 :，无 ? 则 : 不触发） */
  neojs::AST *ast = neojs::parseSource("a ? b : c", std::strlen("a ? b : c"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_EQ(ast->root->children[0]->children[0]->type, neojs::NodeType::Conditional);
  delete ast;

  /* x: y ? z : w → label(x) 包裹三元 */
  ast = neojs::parseSource("x: y ? z : w", std::strlen("x: y ? z : w"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::LabeledStatement);
  EXPECT_EQ(top->name, "x");
  EXPECT_EQ(top->children[0]->children[0]->type, neojs::NodeType::Conditional);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 表达式语句 ASI（M1.5 ④）：自动分号插入                                      */
/* -------------------------------------------------------------------------- */

TEST(Frontend, AsiExplicitSemicolon) {
  neojs::DiagnosticSink sink;
  /* 显式分号：a = 1; b = 2; → 两条 ExprStmt */
  neojs::AST *ast =
      neojs::parseSource("a = 1; b = 2;", std::strlen("a = 1; b = 2;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 2u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(ast->root->children[1]->type, neojs::NodeType::ExprStmt);
  delete ast;
}

TEST(Frontend, AsiInsertOnNewline) {
  neojs::DiagnosticSink sink;
  /* 规则①：换行后遇到非法 token → 插入分号（a = 1\nb = 2） */
  neojs::AST *ast = neojs::parseSource(
"a = 1\nb = 2", std::strlen("a = 1\nb = 2"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 2u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(ast->root->children[1]->type, neojs::NodeType::ExprStmt);
  delete ast;

  /* 块内换行同样插入：{ a = 1\nb = 2 } */
  ast = neojs::parseSource(
"{ a = 1\nb = 2 }", std::strlen("{ a = 1\nb = 2 }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *block = ast->root->children[0];
  ASSERT_EQ(block->type, neojs::NodeType::BlockStatement);
  ASSERT_EQ(block->children.size(), 2u);
  EXPECT_EQ(block->children[0]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(block->children[1]->type, neojs::NodeType::ExprStmt);
  delete ast;

  /* 换行后标识符同样插入：a = 1\nb */
  ast = neojs::parseSource("a = 1\nb", std::strlen("a = 1\nb"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 2u);
  delete ast;
}

TEST(Frontend, AsiInsertOnBraceAndEof) {
  neojs::DiagnosticSink sink;
  /* 规则②：遇到 } → 插入分号（块内语句无分号） */
  neojs::AST *ast = neojs::parseSource("{ a }", std::strlen("{ a }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *block = ast->root->children[0];
  ASSERT_EQ(block->type, neojs::NodeType::BlockStatement);
  ASSERT_EQ(block->children.size(), 1u);
  EXPECT_EQ(block->children[0]->type, neojs::NodeType::ExprStmt);
  delete ast;

  /* 规则③：EOF → 插入分号（a 无分号） */
  ast = neojs::parseSource("a", std::strlen("a"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  delete ast;
}

TEST(Frontend, AsiMissingSemicolonError) {
  neojs::DiagnosticSink sink;
  /* 同一行无法继续的 token：a b → 缺分号语法错误 */
  neojs::AST *ast = neojs::parseSource("a b", std::strlen("a b"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  delete ast;
}

TEST(Frontend, AsiPostfixUpdateNewline) {
  neojs::DiagnosticSink sink;
  /* Restricted production：a\n++b → a; ++b（后置换行转前缀） */
  neojs::AST *ast = neojs::parseSource("a\n++b", std::strlen("a\n++b"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 2u);
  neojs::ASTNode *first = ast->root->children[0];
  neojs::ASTNode *second = ast->root->children[1];
  EXPECT_EQ(first->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(first->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(second->children[0]->type, neojs::NodeType::UpdateExpression);
  EXPECT_TRUE(second->children[0]->prefix);
  EXPECT_EQ(second->children[0]->update_op, neojs::UpdateOpKind::Inc);
  delete ast;

  /* 同行仍作后缀：a++ */
  ast = neojs::parseSource("a++", std::strlen("a++"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *node = ast->root->children[0]->children[0];
  EXPECT_EQ(node->type, neojs::NodeType::UpdateExpression);
  EXPECT_FALSE(node->prefix);
  delete ast;

  /* a\n--b → a; --b */
  ast = neojs::parseSource("a\n--b", std::strlen("a\n--b"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 2u);
  second = ast->root->children[1];
  EXPECT_EQ(second->children[0]->type, neojs::NodeType::UpdateExpression);
  EXPECT_TRUE(second->children[0]->prefix);
  EXPECT_EQ(second->children[0]->update_op, neojs::UpdateOpKind::Dec);
  delete ast;
}

TEST(Frontend, AsiNewlineCallNotSplit) {
  neojs::DiagnosticSink sink;
  /* 换行后 ( 能继续表达式（调用），不触发 ASI：a\n(b) → Call(a, b) */
  neojs::AST *ast = neojs::parseSource("a\n(b)", std::strlen("a\n(b)"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 1u);
  neojs::ASTNode *expr = ast->root->children[0]->children[0];
  EXPECT_EQ(expr->type, neojs::NodeType::CallExpression);
  EXPECT_EQ(expr->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(expr->children[1]->type, neojs::NodeType::Identifier);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* if/else 条件语句（M1.5 ⑤）                                                 */
/* -------------------------------------------------------------------------- */

TEST(Frontend, IfStatement) {
  neojs::DiagnosticSink sink;
  /* if (a) b; → IfStatement[cond, then]（无 else） */
  neojs::AST *ast = neojs::parseSource("if (a) b;", std::strlen("if (a) b;"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::IfStatement);
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier); /* cond */
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::ExprStmt);    /* then */
  delete ast;

  /* if (a) b; else c; → IfStatement[cond, then, else] */
  ast = neojs::parseSource("if (a) b; else c;", std::strlen("if (a) b; else c;"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::IfStatement);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::ExprStmt); /* else */
  delete ast;

  /* 条件为二元表达式：if (a / b) c;（读值位置 / 是除法） */
  ast = neojs::parseSource("if (a / b) c;", std::strlen("if (a / b) c;"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::IfStatement);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->children[0]->binop, neojs::BinaryOpKind::Div);
  delete ast;
}

TEST(Frontend, IfDanglingElse) {
  neojs::DiagnosticSink sink;
  /* 悬挂 else：else 与最近未匹配 if 结合 → 内层 if 有 else */
  neojs::AST *ast =
      neojs::parseSource("if (a) if (b) c; else d;", std::strlen("if (a) if (b) c; else d;"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *outer = ast->root->children[0];
  EXPECT_EQ(outer->type, neojs::NodeType::IfStatement);
  ASSERT_EQ(outer->children.size(), 2u); /* 外层无 else */
  neojs::ASTNode *inner = outer->children[1];
  EXPECT_EQ(inner->type, neojs::NodeType::IfStatement);
  ASSERT_EQ(inner->children.size(), 3u); /* 内层有 else */
  delete ast;
}

TEST(Frontend, IfElseIfChain) {
  neojs::DiagnosticSink sink;
  /* else if 链：if (a) b; else if (c) d; else e; → else 分支嵌套 IfStatement */
  neojs::AST *ast = neojs::parseSource(
"if (a) b; else if (c) d; else e;",
      std::strlen("if (a) b; else if (c) d; else e;"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::IfStatement);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::IfStatement); /* else → if */
  neojs::ASTNode *second = top->children[2];
  ASSERT_EQ(second->children.size(), 3u);
  EXPECT_EQ(second->children[2]->type, neojs::NodeType::ExprStmt);
  delete ast;
}

TEST(Frontend, IfBlockBranches) {
  neojs::DiagnosticSink sink;
  /* if (a) { b; } else { c; } → 块分支 */
  neojs::AST *ast = neojs::parseSource(
"if (a) { b; } else { c; }", std::strlen("if (a) { b; } else { c; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::IfStatement);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BlockStatement);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::BlockStatement);
  delete ast;
}

TEST(Frontend, IfEmptyThen) {
  neojs::DiagnosticSink sink;
  /* if (a); → 空 then 分支（EmptyStatement） */
  neojs::AST *ast = neojs::parseSource("if (a);", std::strlen("if (a);"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::IfStatement);
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::EmptyStatement);
  delete ast;
}

TEST(Frontend, IfMissingParenError) {
  neojs::DiagnosticSink sink;
  /* if 缺左括号 / 缺右括号 / 缺条件 → 语法错误 */
  neojs::AST *ast = neojs::parseSource("if a) b;", std::strlen("if a) b;"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  delete ast;

  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("if (a b;", std::strlen("if (a b;"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* while/do-while 循环语句（M1.5 ⑥）                                          */
/* -------------------------------------------------------------------------- */

TEST(Frontend, WhileStatement) {
  neojs::DiagnosticSink sink;
  /* while (a) b; → WhileStatement[cond, body] */
  neojs::AST *ast =
      neojs::parseSource("while (a) b;", std::strlen("while (a) b;"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::WhileStatement);
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier); /* cond */
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::ExprStmt);    /* body */
  delete ast;

  /* 块体：while (a) { b; } */
  ast = neojs::parseSource("while (a) { b; }", std::strlen("while (a) { b; }"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::WhileStatement);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BlockStatement);
  delete ast;

  /* 条件为除法：while (a / b) c; */
  ast = neojs::parseSource("while (a / b) c;", std::strlen("while (a / b) c;"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::WhileStatement);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->children[0]->binop, neojs::BinaryOpKind::Div);
  delete ast;
}

TEST(Frontend, DoWhileStatement) {
  neojs::DiagnosticSink sink;
  /* do b; while (a); → DoWhileStatement[body, cond]（显式分号） */
  neojs::AST *ast =
      neojs::parseSource("do b; while (a);", std::strlen("do b; while (a);"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::DoWhileStatement);
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::ExprStmt); /* body */
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier); /* cond */
  delete ast;

  /* 块体 + 无分号（ASI 补全）：do { b; } while (a) */
  ast = neojs::parseSource("do { b; } while (a)", std::strlen("do { b; } while (a)"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::DoWhileStatement);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::BlockStatement);
  delete ast;

  /* 尾分号 ASI：do b; while (a)\nc; → do-while + 独立 c; */
  ast = neojs::parseSource("do b; while (a)\nc;", std::strlen("do b; while (a)\nc;"), sink);
  ASSERT_NE(ast, nullptr);
  ASSERT_EQ(ast->root->children.size(), 2u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::DoWhileStatement);
  EXPECT_EQ(ast->root->children[1]->type, neojs::NodeType::ExprStmt);
  delete ast;
}

TEST(Frontend, LoopNestedAndError) {
  neojs::DiagnosticSink sink;
  /* 嵌套 while */
  neojs::AST *ast =
      neojs::parseSource("while (a) while (b) c;", std::strlen("while (a) while (b) c;"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *outer = ast->root->children[0];
  EXPECT_EQ(outer->type, neojs::NodeType::WhileStatement);
  EXPECT_EQ(outer->children[1]->type, neojs::NodeType::WhileStatement);
  delete ast;

  /* do 缺 while 报错 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("do b;", std::strlen("do b;"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());
  delete ast;

  /* while 缺 ) 报错 */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("while (a b;", std::strlen("while (a b;"), sink3);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink3.hasErrors());
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 声明语句（M1.5 ①）：var / let / const（含解构）                             */
/* -------------------------------------------------------------------------- */

TEST(Frontend, VarDeclaration) {
  neojs::DiagnosticSink sink;
  /* var a = 1; → VariableDeclaration(var)[VariableDeclarator[a, init]] */
  neojs::AST *ast = neojs::parseSource("var a = 1;", std::strlen("var a = 1;"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *decl = ast->root->children[0];
  EXPECT_EQ(decl->type, neojs::NodeType::VariableDeclaration);
  EXPECT_EQ(decl->varkind, neojs::VarKind::Var);
  ASSERT_EQ(decl->children.size(), 1u);
  neojs::ASTNode *d = decl->children[0];
  EXPECT_EQ(d->type, neojs::NodeType::VariableDeclarator);
  EXPECT_EQ(d->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(d->children[0]->name, "a");
  ASSERT_EQ(d->children.size(), 2u); /* id + init */
  EXPECT_EQ(d->children[1]->type, neojs::NodeType::Literal);
  delete ast;

  /* 多声明器 + 无初始化：var a, b = 2; */
  ast = neojs::parseSource("var a, b = 2;", std::strlen("var a, b = 2;"), sink);
  ASSERT_NE(ast, nullptr);
  decl = ast->root->children[0];
  ASSERT_EQ(decl->children.size(), 2u);
  EXPECT_EQ(decl->children[0]->children.size(), 1u); /* a 无 init */
  EXPECT_EQ(decl->children[1]->children.size(), 2u); /* b 有 init */
  delete ast;
}

TEST(Frontend, LetConstDeclaration) {
  neojs::DiagnosticSink sink;
  /* let a = 1, b; */
  neojs::AST *ast = neojs::parseSource("let a = 1, b;", std::strlen("let a = 1, b;"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *decl = ast->root->children[0];
  EXPECT_EQ(decl->type, neojs::NodeType::VariableDeclaration);
  EXPECT_EQ(decl->varkind, neojs::VarKind::Let);
  ASSERT_EQ(decl->children.size(), 2u);
  delete ast;

  /* const c = 2; */
  ast = neojs::parseSource("const c = 2;", std::strlen("const c = 2;"), sink);
  ASSERT_NE(ast, nullptr);
  decl = ast->root->children[0];
  EXPECT_EQ(decl->varkind, neojs::VarKind::Const);
  delete ast;

  /* const 缺初始化报错 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("const c;", std::strlen("const c;"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());
  delete ast;
}

TEST(Frontend, LetAmbiguity) {
  neojs::DiagnosticSink sink;
  /* let 后跟 Identifier → 声明 */
  neojs::AST *ast = neojs::parseSource("let a = 1;", std::strlen("let a = 1;"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::VariableDeclaration);
  delete ast;

  /* let = 5 → 表达式赋值（let 作标识符） */
  ast = neojs::parseSource("let = 5;", std::strlen("let = 5;"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::ExprStmt);
  EXPECT_EQ(ast->root->children[0]->children[0]->type, neojs::NodeType::Assignment);
  delete ast;

  /* let[0] = 1 → 表达式（成员赋值） */
  ast = neojs::parseSource("let[0] = 1;", std::strlen("let[0] = 1;"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::ExprStmt);
  delete ast;
}

TEST(Frontend, DestructuringDeclaration) {
  neojs::DiagnosticSink sink;
  /* var {x, y} = o, [p, q] = arr; */
  neojs::AST *ast = neojs::parseSource(
"var {x, y} = o, [p, q] = arr;", std::strlen("var {x, y} = o, [p, q] = arr;"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *decl = ast->root->children[0];
  ASSERT_EQ(decl->type, neojs::NodeType::VariableDeclaration);
  ASSERT_EQ(decl->children.size(), 2u);
  EXPECT_EQ(decl->children[0]->children[0]->type, neojs::NodeType::ObjectPattern);
  EXPECT_EQ(decl->children[1]->children[0]->type, neojs::NodeType::ArrayPattern);
  delete ast;

  /* 对象解构重命名：let {p: q} = obj; */
  ast = neojs::parseSource("let {p: q} = obj;", std::strlen("let {p: q} = obj;"), sink);
  ASSERT_NE(ast, nullptr);
  decl = ast->root->children[0];
  neojs::ASTNode *pat = decl->children[0]->children[0];
  EXPECT_EQ(pat->type, neojs::NodeType::ObjectPattern);
  ASSERT_EQ(pat->children.size(), 1u);
  EXPECT_EQ(pat->children[0]->type, neojs::NodeType::Property);
  EXPECT_EQ(pat->children[0]->children[0]->name, "p");
  EXPECT_EQ(pat->children[0]->children[1]->name, "q");
  delete ast;

  /* 数组默认值 + rest：let [a, b = 2, ...rest] = arr; */
  ast = neojs::parseSource(
"let [a, b = 2, ...rest] = arr;", std::strlen("let [a, b = 2, ...rest] = arr;"), sink);
  ASSERT_NE(ast, nullptr);
  decl = ast->root->children[0];
  pat = decl->children[0]->children[0];
  EXPECT_EQ(pat->type, neojs::NodeType::ArrayPattern);
  ASSERT_EQ(pat->children.size(), 3u);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* for / for-in / for-of 循环（M1.5 ③）                                        */
/* -------------------------------------------------------------------------- */

TEST(Frontend, ForStatement) {
  neojs::DiagnosticSink sink;
  /* for (i = 0; i < 10; i++) { x; } → ForStatement[init, cond, update, body] */
  neojs::AST *ast = neojs::parseSource(
"for (i = 0; i < 10; i++) { x; }",
      std::strlen("for (i = 0; i < 10; i++) { x; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ForStatement);
  ASSERT_EQ(top->children.size(), 4u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Assignment); /* init */
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BinaryOp);   /* cond */
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::UpdateExpression); /* update */
  EXPECT_EQ(top->children[3]->type, neojs::NodeType::BlockStatement);   /* body */
  delete ast;

  /* 空段：for (;;) {} */
  ast = neojs::parseSource("for (;;) {}", std::strlen("for (;;) {}"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ForStatement);
  ASSERT_EQ(top->children.size(), 4u);
  EXPECT_EQ(top->children[0], nullptr); /* init 空 */
  EXPECT_EQ(top->children[1], nullptr); /* cond 空 */
  EXPECT_EQ(top->children[2], nullptr); /* update 空 */
  delete ast;

  /* 声明 init：for (var i = 0; i < n; i++) f(i); */
  ast = neojs::parseSource(
"for (var i = 0; i < n; i++) f(i);",
      std::strlen("for (var i = 0; i < n; i++) f(i);"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ForStatement);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::VariableDeclaration);
  delete ast;
}

TEST(Frontend, ForInOfStatement) {
  neojs::DiagnosticSink sink;
  /* for (var k in obj) {} → ForInStatement[decl, right, body] */
  neojs::AST *ast = neojs::parseSource(
"for (var k in obj) {}", std::strlen("for (var k in obj) {}"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ForInStatement);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::VariableDeclaration);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier); /* right */
  delete ast;

  /* for (let x of arr) g(x); → ForOfStatement */
  ast = neojs::parseSource(
"for (let x of arr) g(x);", std::strlen("for (let x of arr) g(x);"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ForOfStatement);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::VariableDeclaration);
  delete ast;

  /* 表达式 left：for (k in obj) {} */
  ast = neojs::parseSource("for (k in obj) {}", std::strlen("for (k in obj) {}"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ForInStatement);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  delete ast;
}

TEST(Frontend, ForOfAwait) {
  neojs::DiagnosticSink sink;
  /* for await (const x of stream) h(x); → ForOfStatement(await) */
  neojs::AST *ast = neojs::parseSource(
"for await (const x of stream) h(x);",
      std::strlen("for await (const x of stream) h(x);"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ForOfStatement);
  EXPECT_TRUE(top->is_await);
  delete ast;
}

/* M5.5a-1 生成器语法：function-star / yield / yield-star 前端解析。
 * 验证：generator 标记、yield 表达式（值/裸/yield*）、async generator、
 * 对象/class generator 方法、restricted production（换行 → 裸 yield）。 */
TEST(Frontend, GeneratorFunctionDeclaration) {
  neojs::DiagnosticSink sink;
  /* function* g() { yield 1; } → FunctionDeclaration[generator] */
  neojs::AST *ast = neojs::parseSource(
"function* g() { yield 1; }",
      std::strlen("function* g() { yield 1; }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *fn = ast->root->children[0];
  EXPECT_EQ(fn->type, neojs::NodeType::FunctionDeclaration);
  EXPECT_TRUE(fn->fn_is_generator);
  ASSERT_EQ(fn->children.size(), 2u); /* id + body（无参数） */
  EXPECT_EQ(fn->children[0]->name, "g");
  /* body[0] = ExprStmt(yield 1) */
  neojs::ASTNode *stmt = fn->children[1]->children[0];
  EXPECT_EQ(stmt->type, neojs::NodeType::ExprStmt);
  neojs::ASTNode *yield = stmt->children[0];
  EXPECT_EQ(yield->type, neojs::NodeType::YieldExpression);
  EXPECT_FALSE(yield->yield_delegate);
  ASSERT_EQ(yield->children.size(), 1u);
  EXPECT_EQ(yield->children[0]->type, neojs::NodeType::Literal);
  delete ast;

  /* function* g() { yield; } → 裸 yield（children[0]=nullptr） */
  sink = neojs::DiagnosticSink();
  ast = neojs::parseSource("function* g() { yield; }",
                           std::strlen("function* g() { yield; }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  fn = ast->root->children[0];
  EXPECT_TRUE(fn->fn_is_generator);
  yield = fn->children[1]->children[0]->children[0];
  EXPECT_EQ(yield->type, neojs::NodeType::YieldExpression);
  EXPECT_FALSE(yield->yield_delegate);
  ASSERT_EQ(yield->children.size(), 1u);
  EXPECT_EQ(yield->children[0], nullptr);
  delete ast;

  /* function* g() { yield * [1,2,3]; } → yield* 委托 */
  sink = neojs::DiagnosticSink();
  ast = neojs::parseSource("function* g() { yield * [1,2,3]; }",
                           std::strlen("function* g() { yield * [1,2,3]; }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  fn = ast->root->children[0];
  yield = fn->children[1]->children[0]->children[0];
  EXPECT_EQ(yield->type, neojs::NodeType::YieldExpression);
  EXPECT_TRUE(yield->yield_delegate);
  ASSERT_EQ(yield->children.size(), 1u);
  EXPECT_EQ(yield->children[0]->type, neojs::NodeType::ArrayLiteral);
  delete ast;
}

TEST(Frontend, GeneratorFunctionExpression) {
  neojs::DiagnosticSink sink;
  /* var g = function*() { yield 1; }; → FunctionExpression[generator] */
  neojs::AST *ast = neojs::parseSource(
"var g = function*() { yield 1; };",
      std::strlen("var g = function*() { yield 1; };"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *init = ast->root->children[0]->children[0]->children[1];
  EXPECT_EQ(init->type, neojs::NodeType::FunctionExpression);
  EXPECT_TRUE(init->fn_is_generator);
  EXPECT_FALSE(init->fn_has_name);
  delete ast;

  /* 命名 generator 函数表达式：var g = function* gen() { yield 1; }; */
  sink = neojs::DiagnosticSink();
  ast = neojs::parseSource("var g = function* gen() { yield 1; };",
                           std::strlen("var g = function* gen() { yield 1; };"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  init = ast->root->children[0]->children[0]->children[1];
  EXPECT_EQ(init->type, neojs::NodeType::FunctionExpression);
  EXPECT_TRUE(init->fn_is_generator);
  EXPECT_TRUE(init->fn_has_name);
  EXPECT_EQ(init->children[0]->name, "gen");
  delete ast;
}

TEST(Frontend, AsyncGeneratorFunction) {
  neojs::DiagnosticSink sink;
  /* async function* ag() { yield 1; } → FunctionDeclaration[async+generator] */
  neojs::AST *ast = neojs::parseSource(
"async function* ag() { yield 1; }",
      std::strlen("async function* ag() { yield 1; }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *fn = ast->root->children[0];
  EXPECT_EQ(fn->type, neojs::NodeType::FunctionDeclaration);
  EXPECT_TRUE(fn->fn_is_async);
  EXPECT_TRUE(fn->fn_is_generator);
  EXPECT_EQ(fn->children[0]->name, "ag");
  delete ast;

  /* async function* 表达式：var ag = async function*() { yield 1; }; */
  sink = neojs::DiagnosticSink();
  ast = neojs::parseSource("var ag = async function*() { yield 1; };",
                           std::strlen("var ag = async function*() { yield 1; };"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *init = ast->root->children[0]->children[0]->children[1];
  EXPECT_EQ(init->type, neojs::NodeType::FunctionExpression);
  EXPECT_TRUE(init->fn_is_async);
  EXPECT_TRUE(init->fn_is_generator);
  delete ast;
}

TEST(Frontend, GeneratorMethodSyntax) {
  neojs::DiagnosticSink sink;
  /* 对象 generator 方法：let o = { *m() { yield 1; } }; */
  neojs::AST *ast = neojs::parseSource(
"let o = { *m() { yield 1; } };",
      std::strlen("let o = { *m() { yield 1; } };"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *obj = ast->root->children[0]->children[0]->children[1];
  EXPECT_EQ(obj->type, neojs::NodeType::ObjectLiteral);
  ASSERT_EQ(obj->children.size(), 1u);
  neojs::ASTNode *prop = obj->children[0];
  EXPECT_EQ(prop->type, neojs::NodeType::Property);
  EXPECT_EQ(prop->prop.kind, neojs::PropertyKind::Method);
  neojs::ASTNode *fn = prop->children[1];
  EXPECT_EQ(fn->type, neojs::NodeType::FunctionExpression);
  EXPECT_TRUE(fn->fn_is_generator);
  delete ast;

  /* class generator 方法：class A { *m() { yield 1; } } */
  sink = neojs::DiagnosticSink();
  ast = neojs::parseSource("class A { *m() { yield 1; } }",
                           std::strlen("class A { *m() { yield 1; } }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *cls = ast->root->children[0];
  EXPECT_EQ(cls->type, neojs::NodeType::ClassDeclaration);
  /* children[1] = 第一个元素（Property） */
  neojs::ASTNode *method = cls->children[1];
  EXPECT_EQ(method->type, neojs::NodeType::Property);
  EXPECT_EQ(method->prop.kind, neojs::PropertyKind::Method);
  EXPECT_FALSE(method->prop.is_static);
  neojs::ASTNode *mfn = method->children[1];
  EXPECT_EQ(mfn->type, neojs::NodeType::FunctionExpression);
  EXPECT_TRUE(mfn->fn_is_generator);
  delete ast;

  /* class static generator 方法：class A { static *m() { yield 1; } } */
  sink = neojs::DiagnosticSink();
  ast = neojs::parseSource("class A { static *m() { yield 1; } }",
                           std::strlen("class A { static *m() { yield 1; } }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  cls = ast->root->children[0];
  method = cls->children[1];
  EXPECT_EQ(method->type, neojs::NodeType::Property);
  EXPECT_TRUE(method->prop.is_static);
  mfn = method->children[1];
  EXPECT_TRUE(mfn->fn_is_generator);
  delete ast;

  /* class async generator 方法：class A { async *m() { yield 1; } } */
  sink = neojs::DiagnosticSink();
  ast = neojs::parseSource("class A { async *m() { yield 1; } }",
                           std::strlen("class A { async *m() { yield 1; } }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  cls = ast->root->children[0];
  method = cls->children[1];
  EXPECT_EQ(method->type, neojs::NodeType::Property);
  EXPECT_FALSE(method->prop.is_static);
  mfn = method->children[1];
  EXPECT_EQ(mfn->type, neojs::NodeType::FunctionExpression);
  EXPECT_TRUE(mfn->fn_is_async);
  EXPECT_TRUE(mfn->fn_is_generator);
  delete ast;
}

TEST(Frontend, YieldRestrictedProduction) {
  neojs::DiagnosticSink sink;
  /* yield 后换行 → 裸 yield（restricted production，§14.4） */
  neojs::AST *ast = neojs::parseSource("function* g() { yield\n5; }",
                                       std::strlen("function* g() { yield\n5; }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *fn = ast->root->children[0];
  /* body 两条语句：ExprStmt(yield) / ExprStmt(5) */
  ASSERT_EQ(fn->children[1]->children.size(), 2u);
  neojs::ASTNode *yield_stmt = fn->children[1]->children[0];
  neojs::ASTNode *yield = yield_stmt->children[0];
  EXPECT_EQ(yield->type, neojs::NodeType::YieldExpression);
  EXPECT_FALSE(yield->yield_delegate);
  ASSERT_EQ(yield->children.size(), 1u);
  EXPECT_EQ(yield->children[0], nullptr); /* 裸 yield */
  neojs::ASTNode *five_stmt = fn->children[1]->children[1];
  EXPECT_EQ(five_stmt->children[0]->type, neojs::NodeType::Literal);
  delete ast;

  /* yield 作为参数：function* g() { f(yield); } */
  sink = neojs::DiagnosticSink();
  ast = neojs::parseSource("function* g() { f(yield); }",
                           std::strlen("function* g() { f(yield); }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  fn = ast->root->children[0];
  neojs::ASTNode *call = fn->children[1]->children[0]->children[0];
  EXPECT_EQ(call->type, neojs::NodeType::CallExpression);
  ASSERT_EQ(call->children.size(), 2u); /* callee + arg */
  neojs::ASTNode *arg = call->children[1];
  EXPECT_EQ(arg->type, neojs::NodeType::YieldExpression);
  EXPECT_FALSE(arg->yield_delegate);
  delete ast;

  /* return yield 1; → return (yield 1) */
  sink = neojs::DiagnosticSink();
  ast = neojs::parseSource("function* g() { return yield 1; }",
                           std::strlen("function* g() { return yield 1; }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  fn = ast->root->children[0];
  neojs::ASTNode *ret = fn->children[1]->children[0];
  EXPECT_EQ(ret->type, neojs::NodeType::ReturnStatement);
  ASSERT_EQ(ret->children.size(), 1u);
  EXPECT_EQ(ret->children[0]->type, neojs::NodeType::YieldExpression);
  delete ast;

  /* 嵌套 yield：function* g() { var x = yield yield 1; } */
  sink = neojs::DiagnosticSink();
  ast = neojs::parseSource("function* g() { var x = yield yield 1; }",
                           std::strlen("function* g() { var x = yield yield 1; }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  fn = ast->root->children[0];
  neojs::ASTNode *decl = fn->children[1]->children[0];
  neojs::ASTNode *outer_yield = decl->children[0]->children[1];
  EXPECT_EQ(outer_yield->type, neojs::NodeType::YieldExpression);
  ASSERT_EQ(outer_yield->children.size(), 1u);
  neojs::ASTNode *inner_yield = outer_yield->children[0];
  EXPECT_EQ(inner_yield->type, neojs::NodeType::YieldExpression);
  delete ast;
}

TEST(Frontend, YieldOutsideGeneratorIsIdentifier) {
  /* 非生成器函数内 yield 是普通标识符（§14.4 仅 generator 体内合法）。
   * function g() { var x = yield; } —— yield 作为标识符赋值给 x。 */
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource(
"function g() { var x = yield; }",
      std::strlen("function g() { var x = yield; }"), sink);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *fn = ast->root->children[0];
  EXPECT_FALSE(fn->fn_is_generator);
  neojs::ASTNode *decl = fn->children[1]->children[0];
  neojs::ASTNode *init = decl->children[0]->children[1];
  /* yield 作为标识符（非 YieldExpression） */
  EXPECT_EQ(init->type, neojs::NodeType::Identifier);
  EXPECT_EQ(init->name, "yield");
  delete ast;
}

TEST(Frontend, FunctionDeclaration) {
  neojs::DiagnosticSink sink;
  /* function add(a, b) { return a + b; } → FunctionDeclaration[id, a, b, body] */
  neojs::AST *ast = neojs::parseSource(
"function add(a, b) { return a + b; }",
      std::strlen("function add(a, b) { return a + b; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::FunctionDeclaration);
  ASSERT_EQ(top->children.size(), 4u); /* id + 2 params + body */
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[0]->name, "add");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  EXPECT_EQ(top->children[3]->type, neojs::NodeType::BlockStatement);
  neojs::ASTNode *ret = top->children[3]->children[0];
  EXPECT_EQ(ret->type, neojs::NodeType::ReturnStatement);
  ASSERT_EQ(ret->children.size(), 1u); /* 有返回值 */
  EXPECT_EQ(ret->children[0]->type, neojs::NodeType::BinaryOp);
  delete ast;

  /* 函数表达式：var g = function(x) { return x * 2; }; */
  ast = neojs::parseSource(
"var g = function(x) { return x * 2; };",
      std::strlen("var g = function(x) { return x * 2; };"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  ASSERT_EQ(top->children.size(), 1u);
  neojs::ASTNode *init = top->children[0]->children[1];
  EXPECT_EQ(init->type, neojs::NodeType::FunctionExpression);
  delete ast;

  /* rest 参数：function f(...args) { return args; } */
  ast = neojs::parseSource(
"function f(...args) { return args; }",
      std::strlen("function f(...args) { return args; }"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  ASSERT_EQ(top->children.size(), 3u); /* id + rest + body */
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::RestElement);
  delete ast;
}

TEST(Frontend, ReturnStatement) {
  neojs::DiagnosticSink sink;
  /* return; 无值 */
  neojs::AST *ast = neojs::parseSource(
"function f() { return; }", std::strlen("function f() { return; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *ret = ast->root->children[0]->children[1]->children[0];
  EXPECT_EQ(ret->type, neojs::NodeType::ReturnStatement);
  EXPECT_EQ(ret->children.size(), 0u); /* 无返回值 */
  delete ast;

  /* 顶层 return 报错 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("return 42;", std::strlen("return 42;"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());
}

TEST(Frontend, ReturnNewline) {
  neojs::DiagnosticSink sink;
  /* return 后换行 → restricted production 插入分号：return; x; */
  neojs::AST *ast = neojs::parseSource(
"function f() { return\nx; }", std::strlen("function f() { return\nx; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *body = ast->root->children[0]->children[1];
  ASSERT_EQ(body->children.size(), 2u); /* return; + x; */
  EXPECT_EQ(body->children[0]->type, neojs::NodeType::ReturnStatement);
  EXPECT_EQ(body->children[0]->children.size(), 0u);
  EXPECT_EQ(body->children[1]->type, neojs::NodeType::ExprStmt);
  delete ast;
}

TEST(Frontend, BreakStatement) {
  neojs::DiagnosticSink sink;
  /* while (true) { break; } → BreakStatement 无标签 */
  neojs::AST *ast = neojs::parseSource(
"while (true) { break; }", std::strlen("while (true) { break; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *body = ast->root->children[0]->children[1];
  ASSERT_EQ(body->children.size(), 1u);
  EXPECT_EQ(body->children[0]->type, neojs::NodeType::BreakStatement);
  EXPECT_TRUE(body->children[0]->name.empty()); /* 无标签 */
  delete ast;

  /* 带标签 break：outer: for(...) { break outer; } */
  ast = neojs::parseSource(
"outer: for (var i = 0; i < 10; i++) { break outer; }",
      std::strlen("outer: for (var i = 0; i < 10; i++) { break outer; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *outer = ast->root->children[0];
  EXPECT_EQ(outer->type, neojs::NodeType::LabeledStatement);
  EXPECT_EQ(outer->name, "outer");
  neojs::ASTNode *brk = outer->children[0]->children[3]->children[0];
  EXPECT_EQ(brk->type, neojs::NodeType::BreakStatement);
  EXPECT_EQ(brk->name, "outer");
  delete ast;

  /* 顶层 break 报错 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("break;", std::strlen("break;"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());

  /* 未定义标签报错 */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("while (true) { break nope; }",
                           std::strlen("while (true) { break nope; }"), sink3);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink3.hasErrors());
}

TEST(Frontend, ContinueStatement) {
  neojs::DiagnosticSink sink;
  /* for (var i = 0; i < 10; i++) { continue; } */
  neojs::AST *ast = neojs::parseSource(
"for (var i = 0; i < 10; i++) { continue; }",
      std::strlen("for (var i = 0; i < 10; i++) { continue; }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *body = ast->root->children[0]->children[3];
  ASSERT_EQ(body->children.size(), 1u);
  EXPECT_EQ(body->children[0]->type, neojs::NodeType::ContinueStatement);
  delete ast;

  /* 顶层 continue 报错 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("continue;", std::strlen("continue;"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());

  /* continue 后换行 → restricted production：continue; + x; */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("for (var i = 0; i < 10; i++) { continue\nx; }",
                           std::strlen("for (var i = 0; i < 10; i++) { continue\nx; }"), sink3);
  ASSERT_NE(ast, nullptr);
  body = ast->root->children[0]->children[3];
  ASSERT_EQ(body->children.size(), 2u);
  EXPECT_EQ(body->children[0]->type, neojs::NodeType::ContinueStatement);
  EXPECT_EQ(body->children[1]->type, neojs::NodeType::ExprStmt);
  delete ast;
}

TEST(Frontend, ThrowStatement) {
  neojs::DiagnosticSink sink;
  /* throw new Error("boom"); → ThrowStatement[NewExpression] */
  neojs::AST *ast = neojs::parseSource(
"throw new Error(\"boom\");", std::strlen("throw new Error(\"boom\");"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::ThrowStatement);
  ASSERT_EQ(top->children.size(), 1u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::NewExpression);
  delete ast;

  /* throw 后换行 → 语法错误 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("throw\nx;", std::strlen("throw\nx;"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());
}

TEST(Frontend, TryCatchFinally) {
  neojs::DiagnosticSink sink;
  /* try { f(); } catch (e) { g(e); } finally { h(); }
   * → TryStatement[block, CatchClause[e, block], block] */
  neojs::AST *ast = neojs::parseSource(
"try { f(); } catch (e) { g(e); } finally { h(); }",
      std::strlen("try { f(); } catch (e) { g(e); } finally { h(); }"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::TryStatement);
  ASSERT_EQ(top->children.size(), 3u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::BlockStatement);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::CatchClause);
  EXPECT_EQ(top->children[1]->children[0]->type, neojs::NodeType::Identifier); /* e */
  EXPECT_EQ(top->children[1]->children[0]->name, "e");
  EXPECT_EQ(top->children[1]->children[1]->type, neojs::NodeType::BlockStatement);
  EXPECT_EQ(top->children[2]->type, neojs::NodeType::BlockStatement); /* finally */
  delete ast;

  /* catch 无绑定（ES2019）：try { f(); } catch { g(); } */
  ast = neojs::parseSource("try { f(); } catch { g(); }",
                           std::strlen("try { f(); } catch { g(); }"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::CatchClause);
  EXPECT_EQ(top->children[1]->children.size(), 1u); /* 仅 block，无绑定 */
  delete ast;

  /* 仅 finally：try { f(); } finally { g(); } */
  ast = neojs::parseSource("try { f(); } finally { g(); }",
                           std::strlen("try { f(); } finally { g(); }"), sink);
  ASSERT_NE(ast, nullptr);
  top = ast->root->children[0];
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::BlockStatement);
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BlockStatement);
  delete ast;

  /* try 缺 catch/finally → 报错 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("try { f(); }", std::strlen("try { f(); }"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());
}

TEST(Frontend, WithStatement) {
  neojs::DiagnosticSink sink;
  /* with (obj) { x = 1; } → WithStatement[obj, block] + 废弃警告（不中止） */
  neojs::AST *ast = neojs::parseSource(
"with (obj) { x = 1; }", std::strlen("with (obj) { x = 1; }"), sink);
  ASSERT_NE(ast, nullptr); /* 警告不中止解析 */
  EXPECT_FALSE(sink.hasErrors());
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::WithStatement);
  ASSERT_EQ(top->children.size(), 2u);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::Identifier); /* obj */
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::BlockStatement);
  /* 确认发出废弃警告（DEP-0001） */
  bool warned = false;
  for (const neojs::Diagnostic &d : sink.diagnostics()) {
    if (d.severity == neojs::Severity::Warning && d.code == "DEP-0001") warned = true;
  }
  EXPECT_TRUE(warned);
  delete ast;
}

TEST(Frontend, DebuggerStatement) {
  neojs::DiagnosticSink sink;
  /* debugger; → DebuggerStatement */
  neojs::AST *ast =
      neojs::parseSource("debugger;", std::strlen("debugger;"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::DebuggerStatement);
  EXPECT_TRUE(top->children.empty());
  delete ast;
}

TEST(Frontend, PrivateInExpression) {
  neojs::DiagnosticSink sink;
  /* #x in obj → BinaryOp(in)[PrivateIdentifier, Identifier] */
  neojs::AST *ast = neojs::parseSource("#x in obj", std::strlen("#x in obj"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->children[0]->type, neojs::NodeType::PrivateIdentifier);
  EXPECT_EQ(top->children[0]->name, "x");
  EXPECT_EQ(top->children[1]->type, neojs::NodeType::Identifier);
  delete ast;

  /* 链式 #x in a in b → 非法 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("#x in a in b", std::strlen("#x in a in b"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());
}

TEST(Frontend, RegExpFlags) {
  neojs::DiagnosticSink sink;
  /* /ab/gi → 合法，flags=gi */
  neojs::AST *ast = neojs::parseSource("/ab/gi", std::strlen("/ab/gi"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::RegExpLiteral);
  EXPECT_EQ(top->regexp.flags, "gi");
  delete ast;

  /* 重复 flag /ab/gg → 报错 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("/ab/gg", std::strlen("/ab/gg"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());

  /* 非法 flag /ab/z → 报错 */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("/ab/z", std::strlen("/ab/z"), sink3);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink3.hasErrors());

  /* u/v 互斥 /ab/uv → 报错 */
  neojs::DiagnosticSink sink4;
  ast = neojs::parseSource("/ab/uv", std::strlen("/ab/uv"), sink4);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink4.hasErrors());
}

TEST(Frontend, NullishCoalesceIsolation) {
  neojs::DiagnosticSink sink;
  /* a ?? b ?? c → 合法（?? 链式） */
  neojs::AST *ast = neojs::parseSource("a ?? b ?? c", std::strlen("a ?? b ?? c"), sink);
  ASSERT_NE(ast, nullptr);
  neojs::ASTNode *top = ast->root->children[0]->children[0];
  EXPECT_EQ(top->type, neojs::NodeType::BinaryOp);
  EXPECT_EQ(top->binop, neojs::BinaryOpKind::Nullish);
  delete ast;

  /* (a ?? b) || c → 合法（括号隔离） */
  ast = neojs::parseSource("(a ?? b) || c", std::strlen("(a ?? b) || c"), sink);
  ASSERT_NE(ast, nullptr);
  delete ast;

  /* a ?? b || c → 报错（?? 与 || 混用） */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("a ?? b || c", std::strlen("a ?? b || c"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());

  /* a || b ?? c → 报错 */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("a || b ?? c", std::strlen("a || b ?? c"), sink3);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink3.hasErrors());
}

/* ==================== M1.6 ② 严格模式全量 ==================== */

/* 严格模式 with → SyntaxError（ECMA-262 §14.1.2） */
TEST(Frontend, StrictWithError) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast =
      neojs::parseSource("\"use strict\"; with (o) x;",
                         std::strlen("\"use strict\"; with (o) x;"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());

  /* 函数体 directive 触发严格：function f() { "use strict"; with (o) x; } */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("function f() { \"use strict\"; with (o) x; }",
                           std::strlen("function f() { \"use strict\"; with (o) x; }"),
                           sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());

  /* 非严格 with 仍是警告（不中止） */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("with (o) x;", std::strlen("with (o) x;"), sink3);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink3.hasErrors());
  delete ast;
}

/* 严格模式旧式八进制字面量 0123 → SyntaxError */
TEST(Frontend, StrictLegacyOctalLiteralError) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast =
      neojs::parseSource("\"use strict\"; 0123;",
                         std::strlen("\"use strict\"; 0123;"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());

  /* 函数体 directive：function f() { "use strict"; return 0123; } */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("function f() { \"use strict\"; return 0123; }",
                           std::strlen("function f() { \"use strict\"; return 0123; }"),
                           sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());

  /* 非严格 0123 合法 */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("0123;", std::strlen("0123;"), sink3);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink3.hasErrors());
  delete ast;
}

/* 严格模式旧式八进制转义 \1 → SyntaxError */
TEST(Frontend, StrictLegacyOctalEscapeError) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast =
      neojs::parseSource("\"use strict\"; var s = \"\\1\";",
                         std::strlen("\"use strict\"; var s = \"\\1\";"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());

  /* 非严格 \1 合法（DEP-0003 警告，不中止） */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("var s = \"\\1\";", std::strlen("var s = \"\\1\";"), sink2);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink2.hasErrors());
  delete ast;
}

/* 严格模式 delete 裸标识符 → SyntaxError */
TEST(Frontend, StrictDeleteIdentifierError) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast =
      neojs::parseSource("\"use strict\"; delete x;",
                         std::strlen("\"use strict\"; delete x;"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());

  /* delete obj.prop 严格模式合法 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("\"use strict\"; delete obj.prop;",
                           std::strlen("\"use strict\"; delete obj.prop;"), sink2);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink2.hasErrors());
  delete ast;

  /* 非严格 delete x 合法 */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("delete x;", std::strlen("delete x;"), sink3);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink3.hasErrors());
  delete ast;
}

/* 严格模式重名参数 → SyntaxError（含 body directive 触发） */
TEST(Frontend, StrictDuplicateParamsError) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("\"use strict\"; function f(a, a) {}",
                                       std::strlen("\"use strict\"; function f(a, a) {}"),
                                       sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());

  /* body directive：function f(a, a) { "use strict"; } */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("function f(a, a) { \"use strict\"; }",
                           std::strlen("function f(a, a) { \"use strict\"; }"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());

  /* 函数表达式严格重名 */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("\"use strict\"; var f = function(a, a) {};",
                           std::strlen("\"use strict\"; var f = function(a, a) {};"),
                           sink3);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink3.hasErrors());

  /* 解构参数重名：function f({a}, a) {} */
  neojs::DiagnosticSink sink4;
  ast = neojs::parseSource("\"use strict\"; function f({a}, a) {}",
                           std::strlen("\"use strict\"; function f({a}, a) {}"), sink4);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink4.hasErrors());

  /* 非严格重名合法 */
  neojs::DiagnosticSink sink5;
  ast = neojs::parseSource("function f(a, a) {}", std::strlen("function f(a, a) {}"), sink5);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink5.hasErrors());
  delete ast;
}

/* 严格模式 eval / arguments 限制 */
TEST(Frontend, StrictEvalArgumentsError) {
  neojs::DiagnosticSink sink;
  /* 参数名 */
  neojs::AST *ast = neojs::parseSource("\"use strict\"; function f(eval) {}",
                                       std::strlen("\"use strict\"; function f(eval) {}"),
                                       sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());
  /* 绑定名（var） */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("\"use strict\"; var eval;",
                           std::strlen("\"use strict\"; var eval;"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());
  /* 赋值目标 */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("\"use strict\"; eval = 1;",
                           std::strlen("\"use strict\"; eval = 1;"), sink3);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink3.hasErrors());
  /* ++/-- 目标 */
  neojs::DiagnosticSink sink4;
  ast = neojs::parseSource("\"use strict\"; arguments++;",
                           std::strlen("\"use strict\"; arguments++;"), sink4);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink4.hasErrors());
  /* catch 绑定 */
  neojs::DiagnosticSink sink5;
  ast = neojs::parseSource("\"use strict\"; try {} catch (eval) {}",
                           std::strlen("\"use strict\"; try {} catch (eval) {}"), sink5);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink5.hasErrors());
  /* 标签名 */
  neojs::DiagnosticSink sink6;
  ast = neojs::parseSource("\"use strict\"; arguments: ;",
                           std::strlen("\"use strict\"; arguments: ;"), sink6);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink6.hasErrors());
  /* for-in/of left */
  neojs::DiagnosticSink sink6b;
  ast = neojs::parseSource("\"use strict\"; for (eval in obj) {}",
                           std::strlen("\"use strict\"; for (eval in obj) {}"), sink6b);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink6b.hasErrors());
  /* 非严格全部合法 */
  neojs::DiagnosticSink sink7;
  ast = neojs::parseSource("function f(eval) { return eval; }",
                           std::strlen("function f(eval) { return eval; }"), sink7);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink7.hasErrors());
  delete ast;
}

/* 严格模式状态传播：嵌套函数继承外层严格、body directive 不泄漏 */
TEST(Frontend, StrictPropagation) {
  neojs::DiagnosticSink sink;
  /* 外层严格 → 嵌套函数 g 的重名参数也报错 */
  neojs::AST *ast = neojs::parseSource(
"\"use strict\"; function f() { function g(a, a) {} }",
      std::strlen("\"use strict\"; function f() { function g(a, a) {} }"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());

  /* 外层非严格 + 外层函数 body directive → 嵌套函数继承严格 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("function outer() { \"use strict\"; function g(a, a) {} }",
                           std::strlen(
                               "function outer() { \"use strict\"; function g(a, a) {} }"),
                           sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());

  /* body directive 不泄漏：非严格函数内 strict directive 后 with 仍警告不报错 */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("function f() { \"use strict\"; } with (o) x;",
                           std::strlen("function f() { \"use strict\"; } with (o) x;"), sink3);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink3.hasErrors());
  delete ast;

  /* 箭头函数 body directive 触发参数检查 */
  neojs::DiagnosticSink sink4;
  ast = neojs::parseSource("(a, a) => { \"use strict\"; }",
                           std::strlen("(a, a) => { \"use strict\"; }"), sink4);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink4.hasErrors());
}

/* ===== M1.6 ③ 模块语法 ===== */

/* import 全形式：默认 / 具名（含 as 重命名）/ 命名空间 / 裸导入 / 组合 */
TEST(Frontend, ModuleImportForms) {
  /* 默认导入 */
  neojs::DiagnosticSink sink;
  neojs::AST *ast =
      neojs::parseSource("import d from 'm';", std::strlen("import d from 'm';"),
                         sink, /*module=*/true);
  ASSERT_NE(ast, nullptr);
  ASSERT_NE(ast->root, nullptr);
  EXPECT_EQ(ast->root->type, neojs::NodeType::Module);
  ASSERT_GE(ast->root->children.size(), 1u);
  EXPECT_EQ(ast->root->children[0]->type, neojs::NodeType::ImportDeclaration);
  ASSERT_GE(ast->root->children[0]->children.size(), 1u);
  EXPECT_EQ(ast->root->children[0]->children[0]->type,
            neojs::NodeType::ImportDefaultSpecifier);
  delete ast;

  /* 具名导入 + as 重命名 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("import {a, b as c} from 'm';",
                           std::strlen("import {a, b as c} from 'm';"), sink2,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  auto *imp = ast->root->children[0];
  EXPECT_EQ(imp->type, neojs::NodeType::ImportDeclaration);
  ASSERT_GE(imp->children.size(), 2u);
  EXPECT_EQ(imp->children[0]->type, neojs::NodeType::ImportSpecifier);
  EXPECT_EQ(imp->children[0]->imported, "a");
  EXPECT_EQ(imp->children[1]->type, neojs::NodeType::ImportSpecifier);
  EXPECT_EQ(imp->children[1]->imported, "b");
  ASSERT_GE(imp->children[1]->children.size(), 1u);
  EXPECT_EQ(imp->children[1]->children[0]->name, "c"); /* local 名 */
  delete ast;

  /* 命名空间导入 */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("import * as ns from 'm';",
                           std::strlen("import * as ns from 'm';"), sink3,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  imp = ast->root->children[0];
  EXPECT_EQ(imp->children[0]->type, neojs::NodeType::ImportNamespaceSpecifier);
  delete ast;

  /* 裸导入 */
  neojs::DiagnosticSink sink4;
  ast = neojs::parseSource("import 'm';", std::strlen("import 'm';"), sink4,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  imp = ast->root->children[0];
  ASSERT_EQ(imp->children.size(), 1u);
  EXPECT_EQ(imp->children[0]->type, neojs::NodeType::Literal);
  delete ast;

  /* 默认 + 具名组合 */
  neojs::DiagnosticSink sink5;
  ast = neojs::parseSource("import d, {a} from 'm';",
                           std::strlen("import d, {a} from 'm';"), sink5,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  imp = ast->root->children[0];
  ASSERT_GE(imp->children.size(), 2u);
  EXPECT_EQ(imp->children[0]->type, neojs::NodeType::ImportDefaultSpecifier);
  EXPECT_EQ(imp->children[1]->type, neojs::NodeType::ImportSpecifier);
  delete ast;

  /* 默认 + 命名空间组合 */
  neojs::DiagnosticSink sink6;
  ast = neojs::parseSource("import d, * as ns from 'm';",
                           std::strlen("import d, * as ns from 'm';"), sink6,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  imp = ast->root->children[0];
  ASSERT_GE(imp->children.size(), 2u);
  EXPECT_EQ(imp->children[0]->type, neojs::NodeType::ImportDefaultSpecifier);
  EXPECT_EQ(imp->children[1]->type, neojs::NodeType::ImportNamespaceSpecifier);
  delete ast;

  /* import 声明缺模块名 → 报错 */
  neojs::DiagnosticSink sink7;
  ast = neojs::parseSource("import x from;", std::strlen("import x from;"), sink7,
                           /*module=*/true);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink7.hasErrors());
}

/* export 全形式：具名 / as / re-export / all / all-as / default / 声明导出 */
TEST(Frontend, ModuleExportForms) {
  /* 具名导出 */
  neojs::DiagnosticSink sink;
  neojs::AST *ast =
      neojs::parseSource("export {a};", std::strlen("export {a};"), sink,
                         /*module=*/true);
  ASSERT_NE(ast, nullptr);
  auto *exp = ast->root->children[0];
  EXPECT_EQ(exp->type, neojs::NodeType::ExportNamedDeclaration);
  ASSERT_GE(exp->children.size(), 1u);
  EXPECT_EQ(exp->children[0]->type, neojs::NodeType::ExportSpecifier);
  EXPECT_EQ(exp->children[0]->exported, "a");
  delete ast;

  /* as 重命名 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("export {b as c};", std::strlen("export {b as c};"),
                           sink2, /*module=*/true);
  ASSERT_NE(ast, nullptr);
  exp = ast->root->children[0];
  EXPECT_EQ(exp->children[0]->exported, "c");
  delete ast;

  /* re-export：export {a} from 'm' */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("export {a} from 'm';",
                           std::strlen("export {a} from 'm';"), sink3,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  exp = ast->root->children[0];
  EXPECT_EQ(exp->type, neojs::NodeType::ExportNamedDeclaration);
  ASSERT_GE(exp->children.size(), 2u);
  EXPECT_EQ(exp->children[1]->type, neojs::NodeType::Literal);
  delete ast;

  /* export * from 'm' */
  neojs::DiagnosticSink sink4;
  ast = neojs::parseSource("export * from 'm';",
                           std::strlen("export * from 'm';"), sink4,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  exp = ast->root->children[0];
  EXPECT_EQ(exp->type, neojs::NodeType::ExportAllDeclaration);
  delete ast;

  /* export * as ns from 'm' */
  neojs::DiagnosticSink sink5;
  ast = neojs::parseSource("export * as ns from 'm';",
                           std::strlen("export * as ns from 'm';"), sink5,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  exp = ast->root->children[0];
  EXPECT_EQ(exp->type, neojs::NodeType::ExportAllDeclaration);
  EXPECT_EQ(exp->exported, "ns");
  delete ast;

  /* export default 表达式 */
  neojs::DiagnosticSink sink6;
  ast = neojs::parseSource("export default 1;", std::strlen("export default 1;"),
                           sink6, /*module=*/true);
  ASSERT_NE(ast, nullptr);
  exp = ast->root->children[0];
  EXPECT_EQ(exp->type, neojs::NodeType::ExportDefaultDeclaration);
  delete ast;

  /* export default 匿名函数声明 */
  neojs::DiagnosticSink sink7;
  ast = neojs::parseSource("export default function () {};",
                           std::strlen("export default function () {};"), sink7,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  exp = ast->root->children[0];
  EXPECT_EQ(exp->type, neojs::NodeType::ExportDefaultDeclaration);
  ASSERT_GE(exp->children.size(), 1u);
  EXPECT_EQ(exp->children[0]->type, neojs::NodeType::FunctionDeclaration);
  delete ast;

  /* export default 具名函数声明 */
  neojs::DiagnosticSink sink8;
  ast = neojs::parseSource("export default function f() {};",
                           std::strlen("export default function f() {};"), sink8,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  exp = ast->root->children[0];
  EXPECT_EQ(exp->type, neojs::NodeType::ExportDefaultDeclaration);
  delete ast;

  /* export const/let/var 声明导出 */
  neojs::DiagnosticSink sink9;
  ast = neojs::parseSource("export const x = 1;",
                           std::strlen("export const x = 1;"), sink9,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  exp = ast->root->children[0];
  EXPECT_EQ(exp->type, neojs::NodeType::ExportNamedDeclaration);
  ASSERT_GE(exp->children.size(), 1u);
  EXPECT_EQ(exp->children[0]->type, neojs::NodeType::VariableDeclaration);
  delete ast;

  /* export function 声明导出 */
  neojs::DiagnosticSink sink10;
  ast = neojs::parseSource("export function f() {}",
                           std::strlen("export function f() {}"), sink10,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  exp = ast->root->children[0];
  EXPECT_EQ(exp->type, neojs::NodeType::ExportNamedDeclaration);
  delete ast;

  /* export {} 空列表合法 */
  neojs::DiagnosticSink sink11;
  ast = neojs::parseSource("export {};", std::strlen("export {};"), sink11,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  delete ast;
}

/* 模块上下文语义：import/export 仅模块合法；顶层 await 仅模块合法；
 * import() 动态导入与 import.meta 行为；模块自动严格模式 */
TEST(Frontend, ModuleContextSemantics) {
  /* script 模式：import 声明报错 */
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource("import x from 'm';",
                                       std::strlen("import x from 'm';"), sink);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink.hasErrors());

  /* script 模式：export 声明报错 */
  neojs::DiagnosticSink sink2;
  ast = neojs::parseSource("export {a};", std::strlen("export {a};"), sink2);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink2.hasErrors());

  /* script 模式：顶层 await 报错 */
  neojs::DiagnosticSink sink3;
  ast = neojs::parseSource("await x;", std::strlen("await x;"), sink3);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink3.hasErrors());

  /* 模块模式：顶层 await 合法 */
  neojs::DiagnosticSink sink4;
  ast = neojs::parseSource("await x;", std::strlen("await x;"), sink4,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink4.hasErrors());
  delete ast;

  /* script 模式：import.meta 报错 */
  neojs::DiagnosticSink sink5;
  ast = neojs::parseSource("import.meta;", std::strlen("import.meta;"), sink5);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink5.hasErrors());

  /* 模块模式：import.meta 合法 */
  neojs::DiagnosticSink sink6;
  ast = neojs::parseSource("import.meta;", std::strlen("import.meta;"), sink6,
                           /*module=*/true);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink6.hasErrors());
  delete ast;

  /* import() 动态导入在 script 与模块均合法 */
  neojs::DiagnosticSink sink7;
  ast = neojs::parseSource("const p = import('m');",
                           std::strlen("const p = import('m');"), sink7);
  ASSERT_NE(ast, nullptr);
  EXPECT_FALSE(sink7.hasErrors());
  delete ast;

  /* 模块自动严格模式：with 语句报错 */
  neojs::DiagnosticSink sink8;
  ast = neojs::parseSource("with (o) x;", std::strlen("with (o) x;"), sink8,
                           /*module=*/true);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink8.hasErrors());

  /* import 多参（import(a, b)）报错：恰好一个参数 */
  neojs::DiagnosticSink sink9;
  ast = neojs::parseSource("import('a', 'b');",
                           std::strlen("import('a', 'b');"), sink9);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink9.hasErrors());

  /* import/export 仅模块顶层：嵌套在块/控制流体内报错 */
  neojs::DiagnosticSink sink10;
  ast = neojs::parseSource("if (x) import y from 'm';",
                           std::strlen("if (x) import y from 'm';"), sink10,
                           /*module=*/true);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink10.hasErrors());

  neojs::DiagnosticSink sink11;
  ast = neojs::parseSource("{ export {a}; }", std::strlen("{ export {a}; }"),
                           sink11, /*module=*/true);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink11.hasErrors());

  neojs::DiagnosticSink sink12;
  ast = neojs::parseSource("function f() { import x from 'm'; }",
                           std::strlen("function f() { import x from 'm'; }"), sink12,
                           /*module=*/true);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink12.hasErrors());

  neojs::DiagnosticSink sink13;
  ast = neojs::parseSource("for (;;) export {a};",
                           std::strlen("for (;;) export {a};"), sink13,
                           /*module=*/true);
  EXPECT_EQ(ast, nullptr);
  EXPECT_TRUE(sink13.hasErrors());
}
