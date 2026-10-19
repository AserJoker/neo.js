#ifndef NEOJS_FRONTEND_AST_H
#define NEOJS_FRONTEND_AST_H

/*
 * 前端 AST 节点定义（M1.1 骨架：最小子集，随 M1.2-M1.6 扩充）。
 *
 * 设计要点（docs/milestones.md M1.1）：
 *   - ASTNode 基类 + NodeType 枚举 + 子节点容器 + 源码位置。
 *   - M1.1 只覆盖最小节点集（Program / ExprStmt / Literal / BinaryOp /
 *     Identifier），用于打通流水线；M1.4 表达式体系、M1.5 语句体系再逐节点扩充。
 *
 * 内存管理（规范 #5/#13）：AST 节点经 JSAllocator 分配，统一由持有者（Parser 产物）
 * 经 JSAllocator::destroy 释放。节点用轻量子节点指针 + 可选语义载荷。
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "neojs/runtime/allocator.h"

namespace neojs {

/* 源码位置（token / AST 节点共用）。line/col 从 1 起。 */
struct SourceLocation {
  size_t line = 1;  /* 1-based */
  size_t col = 1;   /* 1-based */
  size_t offset = 0; /* 0-based 字节偏移 */
};

/* AST 节点类型（M1.1 最小子集；后续阶段扩充） */
enum class NodeType : uint8_t {
  Program,     /* 根：语句列表 */
  Literal,     /* 字面量：数字/字符串/布尔/null */
  Identifier,  /* 标识符引用 */
  BinaryOp,    /* 二元运算 */
  ExprStmt,    /* 表达式语句（M1.1 最小语句节点；M1.5 扩充语句体系） */
};

/* 二元运算类型（M1.1 先支持算术；M1.4 扩充全部） */
enum class BinaryOpKind : uint8_t {
  Add,   /* + */
  Sub,   /* - */
  Mul,   /* * */
  Div,   /* / */
  Mod,   /* % */
};

/* 字面量载荷（M1.2：数字 + 字符串；其余后续） */
struct LiteralPayload {
  double number = 0.0;  /* 数字字面量值 */
  bool is_number = false;
  bool is_string = false;
  std::string str;      /* 字符串字面量值（转义已解码） */
  bool is_bigint = false; /* BigInt 字面量（M2 用） */
  std::string text;     /* 原始文本（供 dump） */
};

/* AST 节点：基类 + 子节点（扁平化子节点向量，避免手写析构树） */
struct ASTNode {
  NodeType type = NodeType::Program;
  SourceLocation loc;

  /* 子节点：Program → 语句/表达式列表；BinaryOp → 左右操作数 */
  std::vector<ASTNode *> children;

  /* 语义载荷（按 type 使用） */
  LiteralPayload literal;
  std::string name;           /* Identifier：名称 */
  BinaryOpKind binop = BinaryOpKind::Add;

  /* 构造：仅由 Parser 经 JSAllocator::create 创建（见 parser.h） */
  explicit ASTNode(NodeType t) : type(t) {}
};

/* AST 树：持有所有节点，析构时统一释放（P1：AST 生命周期独立于运行时） */
struct AST {
  ASTNode *root = nullptr;    /* Program 根节点 */
  JSAllocator *allocator = nullptr;

  ~AST() {
    /* children 为裸指针，~ASTNode 不递归释放；必须整棵子树深度优先释放 */
    if (root != nullptr && allocator != nullptr) freeSubtree(root, allocator);
    root = nullptr;
  }

  /* 深度优先释放整棵子树（ASTNode::children 为裸指针） */
  void freeSubtree(ASTNode *node, JSAllocator *a) {
    if (node == nullptr) return;
    for (ASTNode *child : node->children) freeSubtree(child, a);
    a->destroy(node);
  }
};

}  // namespace neojs

#endif  // NEOJS_FRONTEND_AST_H
