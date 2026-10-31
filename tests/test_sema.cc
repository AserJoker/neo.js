/*
 * sema 独立阶段单元测试（M5-B-1：AST 后、compile 前，scope-redesign.md §10.8）。
 *
 * 验证：
 *   - 变量提升：var → 最近函数级 scope；let/const → 当前块 scope
 *   - 标识符解析：identifier 节点挂定义点（node->resolved）
 *   - 函数捕获列表：嵌套函数引用外层变量 → 函数节点 captures
 *   - 块作用域遮蔽：同名变量内外层解析到不同定义
 */

#include <gtest/gtest.h>

#include <cstring>

#include "neojs/frontend/diagnostics.h"
#include "neojs/frontend/frontend.h"
#include "neojs/frontend/sema.h"

namespace {

using neojs::ASTNode;
using neojs::NodeType;
using neojs::VarKind;
using neojs::sema::ShadowScope;
using neojs::sema::ShadowValue;

/* 解析 + sema。返回 AST*（调用方释放）。 */
neojs::AST *parse_and_sema(const char *source) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource(source, std::strlen(source), sink);
  if (ast == nullptr) {
    ADD_FAILURE() << "parse failed: " << source;
    return nullptr;
  }
  neojs::sema::Result result;
  if (!neojs::sema::analyze(ast->root, result)) {
    ADD_FAILURE() << "sema failed: " << source;
    delete ast;
    return nullptr;
  }
  return ast;
}

/* 深度优先找第一个指定类型的节点（从 node 自身开始） */
ASTNode *find_node(ASTNode *node, NodeType type) {
  if (node == nullptr) return nullptr;
  if (node->type == type) return node;
  for (ASTNode *c : node->children) {
    ASTNode *r = find_node(c, type);
    if (r != nullptr) return r;
  }
  return nullptr;
}

/* 从 node 的子节点中深度优先找第一个指定类型的节点（跳过 node 自身） */
ASTNode *find_child_node(ASTNode *node, NodeType type) {
  if (node == nullptr) return nullptr;
  for (ASTNode *c : node->children) {
    if (c->type == type) return c;
    ASTNode *r = find_child_node(c, type);
    if (r != nullptr) return r;
  }
  return nullptr;
}

/* 在指定 scope 找变量 */
ShadowValue *find_var(ShadowScope *scope, const std::string &name) {
  auto f = scope->named.find(name);
  return f == scope->named.end() ? nullptr : f->second;
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 变量提升                                                                    */
/* -------------------------------------------------------------------------- */

TEST(Sema, VarHoistToFunctionScope) {
  neojs::AST *ast = parse_and_sema("function f() { { var x = 1; } }");
  ASSERT_NE(ast, nullptr);
  /* 找函数节点 → body scope（函数级）应含 x */
  ASTNode *fn = find_node(ast->root, NodeType::FunctionDeclaration);
  ASSERT_NE(fn, nullptr);
  /* 函数节点的 scope_info 是 body scope（sema 挂的是最近函数 scope） */
  ASSERT_NE(fn->scope_info, nullptr);
  EXPECT_NE(find_var(fn->scope_info, "x"), nullptr);
  delete ast;
}

TEST(Sema, VarHoistToProgramTop) {
  neojs::AST *ast = parse_and_sema("var x = 1; { var y = 2; }");
  ASSERT_NE(ast, nullptr);
  /* Program 根 scope 应含 x 和 y（var 提升到顶层） */
  ASSERT_NE(ast->root->scope_info, nullptr);
  EXPECT_NE(find_var(ast->root->scope_info, "x"), nullptr);
  EXPECT_NE(find_var(ast->root->scope_info, "y"), nullptr);
  delete ast;
}

TEST(Sema, LetStaysInBlock) {
  neojs::AST *ast = parse_and_sema("let x = 1; { let y = 2; }");
  ASSERT_NE(ast, nullptr);
  /* Program scope 含 x，不含 y（y 在块 scope） */
  ASSERT_NE(ast->root->scope_info, nullptr);
  EXPECT_NE(find_var(ast->root->scope_info, "x"), nullptr);
  EXPECT_EQ(find_var(ast->root->scope_info, "y"), nullptr);
  /* 块节点 scope 含 y */
  ASTNode *block = find_node(ast->root, NodeType::BlockStatement);
  ASSERT_NE(block, nullptr);
  ASSERT_NE(block->scope_info, nullptr);
  EXPECT_NE(find_var(block->scope_info, "y"), nullptr);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 标识符解析                                                                  */
/* -------------------------------------------------------------------------- */

TEST(Sema, IdentifierResolveLocal) {
  neojs::AST *ast = parse_and_sema("var x = 1; x = x + 1;");
  ASSERT_NE(ast, nullptr);
  /* 找赋值 lhs 的 Identifier（第一个 x 是声明，第二个是赋值） */
  ASTNode *assign = find_node(ast->root, NodeType::Assignment);
  ASSERT_NE(assign, nullptr);
  ASSERT_GT(assign->children.size(), 0u);
  ASTNode *lhs = assign->children[0];
  ASSERT_EQ(lhs->type, NodeType::Identifier);
  EXPECT_NE(lhs->resolved, nullptr); /* 解析到局部定义 */
  delete ast;
}

TEST(Sema, IdentifierResolveGlobalNull) {
  neojs::AST *ast = parse_and_sema("x = 1;");
  ASSERT_NE(ast, nullptr);
  ASTNode *assign = find_node(ast->root, NodeType::Assignment);
  ASSERT_NE(assign, nullptr);
  ASTNode *lhs = assign->children[0];
  ASSERT_EQ(lhs->type, NodeType::Identifier);
  EXPECT_EQ(lhs->resolved, nullptr); /* 未定义 → 全局 */
  delete ast;
}

TEST(Sema, BlockShadowing) {
  neojs::AST *ast = parse_and_sema("var x = 1; { var x = 2; x = 3; }");
  ASSERT_NE(ast, nullptr);
  /* 块内赋值 x 解析到与外层同一个定义（var 提升到顶层，同名遮蔽） */
  ASTNode *block = find_node(ast->root, NodeType::BlockStatement);
  ASSERT_NE(block, nullptr);
  /* 块内最后一个赋值语句的 lhs */
  ASTNode *inner_assign = nullptr;
  for (ASTNode *c : block->children) {
    if (c->type == NodeType::ExprStmt && !c->children.empty() &&
        c->children[0]->type == NodeType::Assignment) {
      inner_assign = c->children[0];
    }
  }
  ASSERT_NE(inner_assign, nullptr);
  ASTNode *lhs = inner_assign->children[0];
  EXPECT_NE(lhs->resolved, nullptr);
  delete ast;
}

/* -------------------------------------------------------------------------- */
/* 函数捕获                                                                    */
/* -------------------------------------------------------------------------- */

TEST(Sema, ClosureCapture) {
  neojs::AST *ast = parse_and_sema(
      "function outer() { var a = 1; function inner() { return a; } }");
  ASSERT_NE(ast, nullptr);
  /* inner 函数节点应捕获 a */
  ASTNode *outer = find_node(ast->root, NodeType::FunctionDeclaration);
  ASSERT_NE(outer, nullptr);
  ASTNode *inner = find_child_node(outer, NodeType::FunctionDeclaration);
  ASSERT_NE(inner, nullptr);
  bool captured = false;
  for (const std::string &c : inner->captures) {
    if (c == "a") captured = true;
  }
  EXPECT_TRUE(captured);
  delete ast;
}

TEST(Sema, NoCaptureForOwnVar) {
  neojs::AST *ast = parse_and_sema(
      "function outer() { function inner() { var a = 1; return a; } }");
  ASSERT_NE(ast, nullptr);
  ASTNode *outer = find_node(ast->root, NodeType::FunctionDeclaration);
  ASSERT_NE(outer, nullptr);
  ASTNode *inner = find_child_node(outer, NodeType::FunctionDeclaration);
  ASSERT_NE(inner, nullptr);
  EXPECT_TRUE(inner->captures.empty()); /* 自己的变量不捕获 */
  delete ast;
}
