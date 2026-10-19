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

/* AST 节点类型（M1.4 表达式体系：Literal/Identifier/模式先行） */
enum class NodeType : uint8_t {
  Program,     /* 根：语句列表 */
  ExprStmt,    /* 表达式语句 */
  Literal,     /* 字面量：数字/字符串/布尔/null */
  Identifier,  /* 标识符引用 */
  PrivateIdentifier, /* 私有字段引用：#x */
  RegExpLiteral,     /* 正则字面量 */
  TemplateLiteral,   /* 模板串（M1.3 插值后） */
  ObjectPattern,     /* 对象解构模式（声明/赋值左侧） */
  ArrayPattern,      /* 数组解构模式 */
  AssignmentPattern, /* 解构默认值：p = 1 */
  RestElement,       /* 展开/剩余：...rest */
  Property,          /* 对象字面量成员 */
  SpreadElement,     /* 展开元素：...expr（数组/调用/解构） */
  UnaryOp,     /* 一元运算 */
  BinaryOp,    /* 二元运算 */
  Conditional, /* 条件表达式：cond ? a : b（右结合） */
  Assignment,  /* 赋值：lhs = rhs（含复合赋值） */
  Sequence,    /* 逗号序列：a, b, c（最低优先级） */
  MemberExpression, /* 成员访问：obj.prop（.）或 obj[expr]（[]） */
  CallExpression,   /* 函数调用：callee(args) */
  NewExpression,    /* new 构造：new Foo(args) 或 new Foo */
  ArrowFunctionExpression, /* 箭头函数：params => body（M1.4 ③-4 表达式体） */
  UpdateExpression, /* 自增自减：++x / x++ / --x / x--（M1.4 ③-6） */
  EmptyStatement,   /* 空语句：单分号 ;（M1.5 语句体系） */
  BlockStatement,   /* 块语句：{ stmt... }（M1.5 语句体系） */
  LabeledStatement, /* 标签语句：label: stmt（M1.5，独立成语句） */
  IfStatement,      /* 条件语句：if (expr) stmt [else stmt]（M1.5 ⑤） */
  WhileStatement,   /* 循环：while (expr) stmt（M1.5 ⑥） */
  DoWhileStatement, /* 循环：do stmt while (expr)（M1.5 ⑥） */
  VariableDeclaration, /* 声明：var/let/const 语句（M1.5 ①） */
  VariableDeclarator,  /* 单个声明器：id = init（M1.5 ①） */
};

/* 声明关键字类型（M1.5 ①）：var / let / const */
enum class VarKind : uint8_t {
  Var,   /* var：函数作用域 */
  Let,   /* let：块作用域 */
  Const, /* const：块作用域，须初始化 */
};

/* 一元运算类型（M1.4：先支持 ! ~ + - 与关键字型 typeof/void/delete/await） */
enum class UnaryOpKind : uint8_t {
  Pos,       /* +x */
  Neg,       /* -x */
  Not,       /* !x */
  BitNot,    /* ~x */
  Typeof,    /* typeof x */
  Void,      /* void x */
  Delete,    /* delete x */
  Await,     /* await x */
};

/* 二元运算类型（M1.4 全套，按优先级分组） */
enum class BinaryOpKind : uint8_t {
  Add,        /* + */
  Sub,        /* - */
  Mul,        /* * */
  Div,        /* / */
  Mod,        /* % */
  Pow,        /* ** */
  LShift,     /* << */
  RShift,     /* >> */
  URShift,    /* >>> */
  Lt,         /* < */
  Le,         /* <= */
  Gt,         /* > */
  Ge,         /* >= */
  In,         /* in */
  Instanceof, /* instanceof */
  Eq,         /* == */
  Ne,         /* != */
  StrictEq,   /* === */
  StrictNe,   /* !== */
  BitAnd,     /* & */
  BitXor,     /* ^ */
  BitOr,      /* | */
  LogicalAnd, /* && */
  LogicalOr,  /* || */
  Nullish,    /* ?? */
};

/* 自增自减类型（M1.4 ③-6）：++x / x++ / --x / x-- */
enum class UpdateOpKind : uint8_t {
  Inc, /* ++ */
  Dec, /* -- */
};

/* 赋值运算类型（M1.4：= 与全部复合赋值；右结合） */
enum class AssignmentOpKind : uint8_t {
  Assign,      /* = */
  AddAssign,   /* += */
  SubAssign,   /* -= */
  MulAssign,   /* *= */
  DivAssign,   /* /= */
  ModAssign,   /* %= */
  PowAssign,   /* **= */
  LShiftAssign, /* <<= */
  RShiftAssign, /* >>= */
  URShiftAssign, /* >>>= */
  BitAndAssign, /* &= */
  BitXorAssign, /* ^= */
  BitOrAssign,  /* |= */
  LogicalAndAssign, /* &&= */
  LogicalOrAssign,  /* ||= */
  NullishAssign,    /* ??= */
};

/* 字面量载荷（M1.4：数字 + 字符串 + 布尔/null） */
struct LiteralPayload {
  double number = 0.0;  /* 数字字面量值 */
  bool is_number = false;
  bool is_string = false;
  std::string str;      /* 字符串字面量值（转义已解码） */
  bool is_boolean = false;
  bool bool_value = false;  /* is_boolean 时的值 */
  bool is_null = false;  /* null 字面量 */
  bool is_bigint = false; /* BigInt 字面量（M2 用） */
  std::string text;     /* 原始文本（供 dump） */
};

/* 正则字面量载荷（pattern 不含斜杠；flags 如 "gi"） */
struct RegExpPayload {
  std::string pattern;
  std::string flags;
};

/* 对象成员类型：普通键值 / getter / setter / 简写 / 方法 */
enum class PropertyKind : uint8_t {
  Init,     /* key: value */
  Get,      /* get key() {} */
  Set,      /* set key(v) {} */
  Shorthand, /* { key } */
  Method,   /* key() {} */
};

/* 对象成员载荷 */
struct PropertyPayload {
  PropertyKind kind = PropertyKind::Init;
  bool is_computed = false; /* [expr] 计算键 */
};

/* 成员访问载荷：true = 计算成员 obj[expr]；false = 点成员 obj.prop。
 * optional：可选链 ?. / ?.[ / ?.(（M1.4 ③-6）。 */
struct MemberPayload {
  bool computed = false;
  bool optional = false;
};

/* AST 节点：基类 + 子节点（扁平化子节点向量，避免手写析构树） */
struct ASTNode {
  NodeType type = NodeType::Program;
  SourceLocation loc;

  /* 子节点：Program → 语句/表达式列表；BinaryOp → 左右操作数 */
  std::vector<ASTNode *> children;

  /* 语义载荷（按 type 使用） */
  LiteralPayload literal;
  RegExpPayload regexp;       /* RegExpLiteral */
  PropertyPayload prop;       /* Property */
  std::string name;           /* Identifier：名称 */
  UnaryOpKind unop = UnaryOpKind::Pos;
  BinaryOpKind binop = BinaryOpKind::Add;
  AssignmentOpKind assign_op = AssignmentOpKind::Assign;
  UpdateOpKind update_op = UpdateOpKind::Inc; /* UpdateExpression：++/-- */
  VarKind varkind = VarKind::Var; /* VariableDeclaration：var/let/const */
  std::string directive;  /* ExprStmt：指令序言标记（非空 = "use strict" 等） */
  bool prefix = false;        /* UpdateExpression：true = 前缀 ++x；false = 后缀 x++ */
  bool optional = false;      /* CallExpression/MemberExpression：可选链 ?. 标记 */
  MemberPayload member;       /* MemberExpression：computed/optional 标记 */

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
