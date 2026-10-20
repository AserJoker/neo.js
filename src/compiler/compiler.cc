/*
 * 内部 C++ 实现：Compiler（AST → 字节码）。
 *
 * M2.4 ① 编译器骨架 + 表达式编译。
 *   - 表达式：字面量 / 标识符 / 一元 / 二元 / 逻辑短路 / 条件 / 赋值 /
 *     调用 / 序列。
 *   - 语句：表达式语句 + 变量声明（简单标识符；解构在②③扩展）。
 *     控制流（if/while/for/return/块）与函数编译在 M2.4 ②③。
 */

#include "neojs/compiler/compiler.h"

#include <utility>

#include "neojs/frontend/ast.h"
#include "neojs/runtime/conversion.h"

namespace neojs {

namespace {

/* 二元运算符 -> 指令。返回 false 表示不支持（逻辑短路/特殊运算符单独处理）。 */
bool binaryOpcode(BinaryOpKind op, Opcode *out) {
  switch (op) {
    case BinaryOpKind::Add: *out = Opcode::Add; return true;
    case BinaryOpKind::Sub: *out = Opcode::Sub; return true;
    case BinaryOpKind::Mul: *out = Opcode::Mul; return true;
    case BinaryOpKind::Div: *out = Opcode::Div; return true;
    case BinaryOpKind::Mod: *out = Opcode::Mod; return true;
    case BinaryOpKind::Pow: *out = Opcode::Pow; return true;
    case BinaryOpKind::LShift: *out = Opcode::LShift; return true;
    case BinaryOpKind::RShift: *out = Opcode::RShift; return true;
    case BinaryOpKind::URShift: *out = Opcode::URShift; return true;
    case BinaryOpKind::Lt: *out = Opcode::Lt; return true;
    case BinaryOpKind::Le: *out = Opcode::Le; return true;
    case BinaryOpKind::Gt: *out = Opcode::Gt; return true;
    case BinaryOpKind::Ge: *out = Opcode::Ge; return true;
    case BinaryOpKind::Eq: *out = Opcode::Eq; return true;
    case BinaryOpKind::Ne: *out = Opcode::Ne; return true;
    case BinaryOpKind::StrictEq: *out = Opcode::StrictEq; return true;
    case BinaryOpKind::StrictNe: *out = Opcode::StrictNe; return true;
    case BinaryOpKind::BitAnd: *out = Opcode::BitAnd; return true;
    case BinaryOpKind::BitXor: *out = Opcode::BitXor; return true;
    case BinaryOpKind::BitOr: *out = Opcode::BitOr; return true;
    default: return false;
  }
}

/* 复合赋值运算符 -> 对应二元运算符。 */
bool compoundAssignToBinary(AssignmentOpKind aop, BinaryOpKind *bop) {
  switch (aop) {
    case AssignmentOpKind::AddAssign: *bop = BinaryOpKind::Add; return true;
    case AssignmentOpKind::SubAssign: *bop = BinaryOpKind::Sub; return true;
    case AssignmentOpKind::MulAssign: *bop = BinaryOpKind::Mul; return true;
    case AssignmentOpKind::DivAssign: *bop = BinaryOpKind::Div; return true;
    case AssignmentOpKind::ModAssign: *bop = BinaryOpKind::Mod; return true;
    case AssignmentOpKind::PowAssign: *bop = BinaryOpKind::Pow; return true;
    case AssignmentOpKind::LShiftAssign: *bop = BinaryOpKind::LShift; return true;
    case AssignmentOpKind::RShiftAssign: *bop = BinaryOpKind::RShift; return true;
    case AssignmentOpKind::URShiftAssign: *bop = BinaryOpKind::URShift; return true;
    case AssignmentOpKind::BitAndAssign: *bop = BinaryOpKind::BitAnd; return true;
    case AssignmentOpKind::BitXorAssign: *bop = BinaryOpKind::BitXor; return true;
    case AssignmentOpKind::BitOrAssign: *bop = BinaryOpKind::BitOr; return true;
    default: return false;
  }
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 顶层编译                                                                    */
/* -------------------------------------------------------------------------- */

CompileResult Compiler::compile(const AST &ast) {
  CompileResult result;
  failed_ = false;
  error_.clear();
  for (CompilerScope *s : scopes_) delete s;
  scopes_.clear();
  scope_ = nullptr;
  builder_ = BytecodeBuilder();

  /* 顶层作用域帧。 */
  CompilerScope *top = new CompilerScope;
  scopes_.push_back(top);
  scope_ = top;

  if (ast.root != nullptr) {
    const auto &stmts = ast.root->children;
    if (!stmts.empty()) {
      /* 除最后一条外：逐条编译（语句自身管理栈平衡，值 Pop）。
       * FunctionDeclaration 已在顶层收集阶段实例化挂到 global，此处跳过。 */
      for (size_t i = 0; i + 1 < stmts.size(); ++i) {
        if (stmts[i] == nullptr) continue;
        if (stmts[i]->type == NodeType::FunctionDeclaration) continue;
        if (!compileStmt(*stmts[i])) return failResult();
      }
      /* 最后一条：若是表达式语句，其值作为 completion value（不 Pop）；
       * FunctionDeclaration 已单独实例化（completion value = undefined）；
       * 否则编译后补 undefined。 */
      const ASTNode *last = stmts.back();
      if (last != nullptr && last->type == NodeType::ExprStmt &&
          !last->children.empty()) {
        if (!compileExpr(*last->children[0])) return failResult();
        builder_.ret(); /* 栈顶即 completion value */
      } else {
        if (last != nullptr && last->type != NodeType::FunctionDeclaration &&
            !compileStmt(*last))
          return failResult();
        builder_.push_op(Opcode::PushUndefined);
        builder_.ret();
      }
    } else {
      /* 空程序：completion value = undefined。 */
      builder_.push_op(Opcode::PushUndefined);
      builder_.ret();
    }
  } else {
    /* 空程序：completion value = undefined。 */
    builder_.push_op(Opcode::PushUndefined);
    builder_.ret();
  }

  /* 局部变量槽数（含临时槽）写入字节码。 */
  builder_.set_num_locals(scope_->next_slot);
  result.ok = true;
  result.fn = builder_.build();
  return result;
}

CompileResult Compiler::compileFunction(const ASTNode &fn) {
  CompileResult result;
  failed_ = false;
  error_.clear();
  for (CompilerScope *s : scopes_) delete s;
  scopes_.clear();
  scope_ = nullptr;

  /* 函数名（可空）：fn_has_name 标记 children[0] 是否为函数名。 */
  std::string fn_name;
  if (fn.fn_has_name && !fn.children.empty()) {
    fn_name = fn.children[0]->name;
  }
  builder_ = BytecodeBuilder(fn_name);

  /* 顶层作用域帧（单函数编译：无外层，捕获解析到全局）。 */
  CompilerScope *top = new CompilerScope;
  scopes_.push_back(top);
  scope_ = top;

  if (!compileFunctionBody(fn)) return failResult();

  builder_.set_num_locals(scope_->next_slot);
  result.ok = true;
  result.fn = builder_.build();
  return result;
}

/* 函数体编译共享实现：解析参数（入本函数作用域帧）+ 编译 body。
 * 调用方已建立本函数作用域帧（scope_ 指向本函数）。 */
bool Compiler::compileFunctionBody(const ASTNode &fn) {
  /* 布局：children[start..n-2] = 参数，children[n-1] = body。
   * fn_has_name = true 时 children[0] 是函数名，start = 1；否则 start = 0。 */
  size_t start = fn.fn_has_name ? 1 : 0;
  size_t body_index = fn.children.size() - 1;

  /* 参数入局部槽 0..n-1（按声明顺序）。 */
  for (size_t i = start; i < body_index; ++i) {
    const ASTNode *p = fn.children[i];
    if (p->type != NodeType::Identifier) {
      fail("not implemented: destructuring/default/rest parameter");
      return false;
    }
    declareLocal(p->name);
  }

  /* 函数体：块语句逐条编译，或箭头函数表达式体（求值后 Ret）。 */
  const ASTNode *body = fn.children[body_index];
  if (body->type == NodeType::BlockStatement) {
    for (const ASTNode *stmt : body->children) {
      if (stmt == nullptr) continue;
      if (!compileStmt(*stmt)) return false;
    }
  } else {
    /* 箭头函数表达式体：求值后作为返回值。 */
    if (!compileExpr(*body)) return false;
    builder_.ret();
  }

  /* 函数默认返回 undefined（块体自然结束）。 */
  builder_.push_op(Opcode::PushUndefined);
  builder_.ret();
  return true;
}

/* 编译子函数到独立 builder（共享参数/body 解析）。
 * 调用方负责保存/恢复外层 builder_ 与作用域帧。 */
bool Compiler::compileChildFunction(const ASTNode &fn, FunctionBytecode *out) {
  /* 函数名（可空）：fn_has_name 标记 children[0] 是否为函数名。 */
  std::string fn_name;
  if (fn.fn_has_name && !fn.children.empty()) {
    fn_name = fn.children[0]->name;
  }

  /* 建立子函数作用域帧：parent = 当前（捕获解析到外层）。 */
  CompilerScope *child = new CompilerScope;
  child->parent = scope_;
  scopes_.push_back(child);
  scope_ = child;

  /* 子函数自己的 builder（与当前 builder_ 分离）。 */
  BytecodeBuilder saved_builder;
  saved_builder = std::move(builder_); /* 保存外层 builder */
  builder_ = BytecodeBuilder(fn_name);

  bool ok = compileFunctionBody(fn);

  /* 恢复外层 builder_；子函数字节码交给调用方。
   * 注意：子函数局部槽数（参数 + var）必须写入字节码，否则 VM 栈底
   * 大小（numLocals）为 0，参数 LoadVar/StoreVar 越界报错或错位。 */
  builder_.set_num_locals(scope_->next_slot);
  FunctionBytecode child_fn = builder_.build();
  builder_ = std::move(saved_builder);
  scope_ = child->parent;
  /* 释放子作用域帧（scopes_ 中最后一个） */
  scopes_.pop_back();
  delete child;

  if (!ok) return false;
  *out = std::move(child_fn);
  return true;
}

/* 嵌套函数编译（M2.5）：编译子函数字节码加入当前 builder 的 nested 表，
 * 发射 MakeFunction 指令。声明时挂到函数名局部变量；表达式时留栈顶。 */
bool Compiler::compileNestedFunction(const ASTNode &fn, bool is_declaration) {
  FunctionBytecode child;
  if (!compileChildFunction(fn, &child)) return false;

  uint16_t index = static_cast<uint16_t>(builder_.add_nested(std::move(child)));
  builder_.make_function(index);

  if (is_declaration) {
    /* 函数声明：按函数名声明局部槽并存储。 */
    if (fn.fn_has_name && !fn.children.empty()) {
      uint16_t slot = declareLocal(fn.children[0]->name);
      builder_.store_var(slot);
    }
  }
  return true;
}

std::vector<const ASTNode *> Compiler::collectTopLevelFunctions(const AST &ast) {
  std::vector<const ASTNode *> out;
  if (ast.root == nullptr) return out;
  for (const ASTNode *stmt : ast.root->children) {
    if (stmt != nullptr && stmt->type == NodeType::FunctionDeclaration) {
      out.push_back(stmt);
    }
  }
  return out;
}

CompileResult Compiler::failResult() {
  CompileResult result;
  result.ok = false;
  result.error = error_;
  return result;
}

/* 变量声明：每个声明器分配局部槽 + 编译初始化表达式（声明值 = undefined）。 */
bool Compiler::compileVarDecl(const ASTNode &decl) {
  for (const ASTNode *declarator : decl.children) {
    if (declarator == nullptr || declarator->type != NodeType::VariableDeclarator) {
      fail("invalid variable declarator");
      return false;
    }
    /* 仅支持简单标识符绑定（解构在②③扩展）。 */
    if (declarator->children.empty() ||
        declarator->children[0]->type != NodeType::Identifier) {
      fail("not implemented: destructuring declaration");
      return false;
    }
    const ASTNode *id = declarator->children[0];
    uint16_t slot = declareLocal(id->name);

    /* 初始化：= expr（未初始化时槽位为 undefined，由 runBytecode 填充）。 */
    if (declarator->children.size() >= 2) {
      if (!compileExpr(*declarator->children[1])) return false;
      builder_.store_var(slot);
    }
  }
  return true;
}

/* -------------------------------------------------------------------------- */
/* 语句编译（M2.4 ②）                                                        */
/* -------------------------------------------------------------------------- */

bool Compiler::compileStmt(const ASTNode &node) {
  switch (node.type) {
    case NodeType::ExprStmt:
      if (!node.children.empty() && !compileExpr(*node.children[0])) return false;
      builder_.pop(); /* 表达式语句值丢弃 */
      return true;
    case NodeType::VariableDeclaration:
      return compileVarDecl(node);
    case NodeType::EmptyStatement:
      return true;
    case NodeType::BlockStatement:
      return compileBlock(node);
    case NodeType::IfStatement:
      return compileIf(node);
    case NodeType::WhileStatement:
      return compileWhile(node);
    case NodeType::DoWhileStatement:
      return compileDoWhile(node);
    case NodeType::ForStatement:
      return compileFor(node);
    case NodeType::ReturnStatement:
      return compileReturn(node);
    case NodeType::FunctionDeclaration:
      /* 嵌套函数声明（M2.5 闭包）：编译子函数字节码，发射 MakeFunction
       * 实例化闭包并挂到当前函数局部变量。 */
      return compileNestedFunction(node);
    case NodeType::BreakStatement:
    case NodeType::ContinueStatement:
      fail("not implemented: break/continue (M2.4 ③ 循环控制)");
      return false;
    default:
      fail("not implemented: statement type " +
           std::to_string(static_cast<int>(node.type)));
      return false;
  }
}

bool Compiler::compileBlock(const ASTNode &node) {
  /* 块语句：逐条编译（值均丢弃）。本阶段不引入新作用域。 */
  for (const ASTNode *stmt : node.children) {
    if (stmt == nullptr) continue;
    if (!compileStmt(*stmt)) return false;
  }
  return true;
}

bool Compiler::compileIf(const ASTNode &node) {
  /* if (cond) then [else]
   *   [cond]
   *   JumpIfFalse else_label
   *   [then]
   *   Jump end_label
   * else_label:
   *   [else]        （无 else 时为空）
   * end_label: */
  if (node.children.empty()) {
    fail("if without condition");
    return false;
  }
  if (!compileExpr(*node.children[0])) return false;
  BcLabel else_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  builder_.jump_if_false(&else_label);
  if (node.children.size() >= 2 && !compileStmt(*node.children[1])) return false;
  builder_.jump(&end_label);
  builder_.bind_label(else_label);
  if (node.children.size() >= 3 && !compileStmt(*node.children[2])) return false;
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileWhile(const ASTNode &node) {
  /* while (cond) body
   * loop_label:
   *   [cond]
   *   JumpIfFalse end_label
   *   [body]
   *   Jump loop_label
   * end_label: */
  if (node.children.size() < 2) {
    fail("while without condition/body");
    return false;
  }
  BcLabel loop_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  builder_.bind_label(loop_label);
  if (!compileExpr(*node.children[0])) return false;
  builder_.jump_if_false(&end_label);
  if (!compileStmt(*node.children[1])) return false;
  builder_.jump(&loop_label);
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileDoWhile(const ASTNode &node) {
  /* do body while (cond)
   * loop_label:
   *   [body]
   *   [cond]
   *   JumpIfTrue loop_label
   * end_label:
   * 注意：VM 无 JumpIfTrue，用 Not + JumpIfFalse：
   *   cond falsy -> 结束；cond truthy -> 循环。
   *   [cond] Not JumpIfFalse loop_label  —— cond falsy(Not=true) 不跳(错误！)
   * 需重新推导：跳回 loop 当 cond truthy。
   *   等价：cond falsy 时结束。用 JumpIfFalse 直接跳 end：
   *   [cond] JumpIfFalse end_label；Jump loop_label。
   *   loop_label:
   *   [body]
   *   [cond]
   *   JumpIfFalse end_label
   *   Jump loop_label
   * end_label: */
  if (node.children.size() < 2) {
    fail("do-while without body/condition");
    return false;
  }
  BcLabel loop_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  builder_.bind_label(loop_label);
  if (!compileStmt(*node.children[0])) return false;
  if (!compileExpr(*node.children[1])) return false;
  builder_.jump_if_false(&end_label);
  builder_.jump(&loop_label);
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileFor(const ASTNode &node) {
  /* for (init; cond; update) body
   *   [init]        （可空）
   * loop_label:
   *   [cond]        （可空；空 = true）
   *   JumpIfFalse end_label
   *   [body]
   * update_label:
   *   [update]      （可空）
   *   Jump loop_label
   * end_label: */
  if (node.children.size() < 4) {
    fail("for without clauses");
    return false;
  }
  /* init */
  if (node.children[0] != nullptr) {
    if (node.children[0]->type == NodeType::VariableDeclaration) {
      if (!compileVarDecl(*node.children[0])) return false;
    } else {
      /* init 表达式：求值后丢弃。 */
      if (!compileExpr(*node.children[0])) return false;
      builder_.pop();
    }
  }
  BcLabel loop_label = builder_.new_label();
  BcLabel update_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  builder_.bind_label(loop_label);
  /* cond */
  if (node.children[1] != nullptr) {
    if (!compileExpr(*node.children[1])) return false;
    builder_.jump_if_false(&end_label);
  }
  /* body */
  if (node.children[3] != nullptr && !compileStmt(*node.children[3])) return false;
  /* update */
  builder_.bind_label(update_label);
  if (node.children[2] != nullptr) {
    if (!compileExpr(*node.children[2])) return false;
    builder_.pop(); /* update 值丢弃 */
  }
  builder_.jump(&loop_label);
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileReturn(const ASTNode &node) {
  /* return [expr]
   *   [expr]        （可空 -> undefined）
   *   Ret */
  if (node.children.empty()) {
    builder_.push_op(Opcode::PushUndefined);
  } else if (!compileExpr(*node.children[0])) {
    return false;
  }
  builder_.ret();
  return true;
}

/* -------------------------------------------------------------------------- */
/* 表达式编译                                                                  */
/* -------------------------------------------------------------------------- */

bool Compiler::compileExpr(const ASTNode &node) {
  switch (node.type) {
    case NodeType::Literal: return compileLiteral(node);
    case NodeType::ObjectLiteral: return compileObjectLiteral(node);
    case NodeType::ArrayLiteral: return compileArrayLiteral(node);
    case NodeType::Identifier: return compileIdentifier(node);
    case NodeType::UnaryOp: return compileUnary(node);
    case NodeType::BinaryOp: return compileBinary(node);
    case NodeType::Conditional: return compileConditional(node);
    case NodeType::Assignment: return compileAssignment(node);
    case NodeType::CallExpression: return compileCall(node);
    case NodeType::Sequence: return compileSequence(node);
    case NodeType::MemberExpression: return compileMemberLoad(node);
    case NodeType::NewExpression: return compileNew(node);
    case NodeType::FunctionExpression:
    case NodeType::ArrowFunctionExpression:
      /* 函数表达式/箭头函数（M2.5 闭包）：编译子函数，MakeFunction 留栈顶。 */
      return compileNestedFunction(node, /*is_declaration=*/false);
    case NodeType::UpdateExpression:
      fail("not implemented: update expression");
      return false;
    default:
      fail("not implemented: expression type " +
           std::to_string(static_cast<int>(node.type)));
      return false;
  }
}

bool Compiler::compileLiteral(const ASTNode &node) {
  const LiteralPayload &lit = node.literal;
  if (lit.is_number) {
    builder_.push_number(lit.number);
    return true;
  }
  if (lit.is_string) {
    builder_.push_const(lit.str);
    return true;
  }
  if (lit.is_boolean) {
    builder_.push_op(lit.bool_value ? Opcode::PushTrue : Opcode::PushFalse);
    return true;
  }
  if (lit.is_null) {
    builder_.push_op(Opcode::PushNull);
    return true;
  }
  if (lit.is_bigint) {
    fail("not implemented: BigInt literal");
    return false;
  }
  builder_.push_op(Opcode::PushUndefined);
  return true;
}

bool Compiler::compileIdentifier(const ASTNode &node) {
  return emitLoadIdentifier(node.name);
}

/* 对象字面量键 -> 字符串（M3.1）：
 *   Identifier 键 {a: 1}      -> 名字
 *   String 字面量键 {"k": 1}  -> 解码串
 *   Number 字面量键 {1: 1}    -> 数值最短表示（§6.1.6.1.20）
 * 计算键 [expr] / Symbol 键 / getter/setter/method 留后续阶段。 */
bool Compiler::objectKeyToString(const ASTNode *key, std::string *out) {
  if (key->type == NodeType::Identifier) {
    *out = key->name;
    return true;
  }
  if (key->type == NodeType::Literal) {
    const LiteralPayload &lit = key->literal;
    if (lit.is_string) {
      *out = lit.str;
      return true;
    }
    if (lit.is_number) {
      *out = numberToString(lit.number);
      return true;
    }
  }
  return false; /* 计算键/其他：本阶段不支持 */
}

/* 对象字面量（M3.1）：{ key: value, shorthand, ...spread }
 *   NewObject; StoreVar tmp
 *   对每属性：LoadVar tmp; [value]; StoreProp "key"; Pop
 *   最后 LoadVar tmp（结果 = 对象）
 * 简写 {b}：值 = 读 b（emitLoadIdentifier）。 */
bool Compiler::compileObjectLiteral(const ASTNode &node) {
  builder_.new_object();
  uint16_t tmp = scope_->next_slot++;
  builder_.store_var(tmp);

  for (const ASTNode *prop : node.children) {
    if (prop == nullptr) continue;
    if (prop->type == NodeType::SpreadElement) {
      fail("not implemented: spread in object literal");
      return false;
    }
    if (prop->type != NodeType::Property) {
      fail("invalid object literal member");
      return false;
    }
    /* 计算键 [expr]：本阶段不支持。 */
    if (prop->prop.is_computed) {
      fail("not implemented: computed property key");
      return false;
    }
    if (prop->prop.kind != PropertyKind::Init &&
        prop->prop.kind != PropertyKind::Shorthand) {
      fail("not implemented: getter/setter/method in object literal");
      return false;
    }
    if (prop->children.empty()) {
      fail("invalid property");
      return false;
    }
    std::string key;
    if (!objectKeyToString(prop->children[0], &key)) {
      fail("not implemented: computed property key");
      return false;
    }
    builder_.load_var(tmp); /* obj */
    if (prop->prop.kind == PropertyKind::Shorthand) {
      /* {b}：值 = 读 b（简写属性值取自同名绑定）。 */
      if (prop->children[0]->type != NodeType::Identifier) {
        fail("invalid shorthand property");
        return false;
      }
      if (!emitLoadIdentifier(prop->children[0]->name)) return false;
    } else {
      if (prop->children.size() < 2 || !compileExpr(*prop->children[1])) {
        return false;
      }
    }
    builder_.store_prop(key);
    builder_.pop(); /* 丢弃赋值结果 */
  }
  builder_.load_var(tmp); /* 结果 = 对象 */
  return true;
}

/* 数组字面量（M3.1）：[ elem, ...spread ]
 *   NewArray; StoreVar tmp
 *   对每元素（索引 i）：LoadVar tmp; PushInt i; [elem]; StoreElem; Pop
 *   最后 LoadVar tmp（结果 = 数组）
 * 稀疏空洞（[1,,3]）本阶段按紧凑存储（元素个数 = 声明元素数）。 */
bool Compiler::compileArrayLiteral(const ASTNode &node) {
  builder_.new_array();
  uint16_t tmp = scope_->next_slot++;
  builder_.store_var(tmp);

  uint16_t index = 0;
  for (const ASTNode *elem : node.children) {
    if (elem == nullptr) continue;
    if (elem->type == NodeType::SpreadElement) {
      fail("not implemented: spread in array literal");
      return false;
    }
    builder_.load_var(tmp);   /* arr */
    builder_.push_int(index); /* arr, i */
    if (!compileExpr(*elem)) return false;
    builder_.store_elem();
    builder_.pop(); /* 丢弃赋值结果 */
    ++index;
  }
  builder_.load_var(tmp); /* 结果 = 数组 */
  return true;
}

/* 成员访问读取（M3.1）：obj.prop -> LoadProp；obj[expr] -> LoadElem。
 * 先编译对象表达式（留栈顶），再编译属性名/计算键，发射读取指令。
 * 可选链 ?. / ?.[ 本阶段不支持（M5 语义）。 */
bool Compiler::compileMemberLoad(const ASTNode &node) {
  if (node.children.size() < 2) {
    fail("member expression without property");
    return false;
  }
  if (node.member.optional) {
    fail("not implemented: optional chaining");
    return false;
  }
  if (!compileExpr(*node.children[0])) return false;
  if (!node.member.computed) {
    /* obj.prop：属性名为 Identifier 或 PrivateIdentifier。 */
    const ASTNode *prop = node.children[1];
    if (prop->type == NodeType::PrivateIdentifier) {
      fail("not implemented: private member access");
      return false;
    }
    if (prop->type != NodeType::Identifier) {
      fail("invalid member property name");
      return false;
    }
    builder_.load_prop(prop->name);
    return true;
  }
  /* obj[expr]：编译计算键表达式，LoadElem。 */
  if (!compileExpr(*node.children[1])) return false;
  builder_.load_elem();
  return true;
}

/* 成员赋值（M3.2）：obj.prop = rhs / obj[expr] = rhs（含复合 op=）。
 * lhs = node.children[0]（对象表达式），rhs = node.children[1]。
 *
 * 简单赋值：obj.prop = rhs
 *   [obj] [rhs] StoreProp "prop"   -> 栈顶 val
 *   obj[expr] = rhs：
 *   [obj] [expr] [rhs] StoreElem   -> 栈顶 val
 *
 * 复合赋值：obj.prop += rhs（对象表达式只求值一次，暂存临时槽）
 *   [obj] StoreVar t_obj
 *   LoadVar t_obj LoadProp "prop"  -> 旧值
 *   [rhs] Add                       -> 新值
 *   LoadVar t_obj [新值] StoreProp "prop" -> 栈顶 val
 *   obj[expr] += rhs：额外暂存 key：
 *   [obj] StoreVar t_obj [expr] StoreVar t_key
 *   LoadVar t_obj LoadVar t_key LoadElem -> 旧值
 *   [rhs] Add
 *   LoadVar t_obj LoadVar t_key [新值] StoreElem
 *
 * 逻辑复合赋值（&&= / ||= / ??=）需短路 + 重复读引用，本阶段不支持。 */
bool Compiler::compileMemberAssign(const ASTNode &node) {
  if (node.children.size() < 2) {
    fail("member assignment without operands");
    return false;
  }
  const ASTNode *lhs = node.children[0];
  const ASTNode *rhs = node.children[1];
  if (lhs->type != NodeType::MemberExpression) {
    fail("invalid member assignment target");
    return false;
  }
  if (lhs->member.optional) {
    fail("not implemented: optional chaining assignment");
    return false;
  }
  /* 逻辑复合赋值：短路语义留后续（与标识符目标一致）。 */
  if (node.assign_op == AssignmentOpKind::LogicalAndAssign ||
      node.assign_op == AssignmentOpKind::LogicalOrAssign ||
      node.assign_op == AssignmentOpKind::NullishAssign) {
    fail("not implemented: logical compound assignment");
    return false;
  }

  const bool computed = lhs->member.computed;
  const ASTNode *obj_expr = lhs->children[0];
  const ASTNode *key_expr = computed ? lhs->children[1] : nullptr;

  /* 非计算成员：属性名（Identifier；PrivateIdentifier 不支持）。 */
  std::string prop_name;
  if (!computed) {
    const ASTNode *prop = lhs->children[1];
    if (prop->type == NodeType::PrivateIdentifier) {
      fail("not implemented: private member assignment");
      return false;
    }
    if (prop->type != NodeType::Identifier) {
      fail("invalid member property name");
      return false;
    }
    prop_name = prop->name;
  }

  const bool compound = (node.assign_op != AssignmentOpKind::Assign);
  if (!compound) {
    /* 简单赋值：对象表达式求值一次，留栈顶供 StoreProp/StoreElem 消费。 */
    if (!compileExpr(*obj_expr)) return false;
    if (computed && !compileExpr(*key_expr)) return false;
    if (!compileExpr(*rhs)) return false;
    if (computed) {
      builder_.store_elem();
    } else {
      builder_.store_prop(prop_name);
    }
    return true;
  }

  /* 复合赋值：映射到二元运算。 */
  BinaryOpKind bop = BinaryOpKind::Add;
  if (!compoundAssignToBinary(node.assign_op, &bop)) {
    fail("not implemented: compound assignment operator");
    return false;
  }
  Opcode op = Opcode::Nop;
  if (!binaryOpcode(bop, &op)) {
    fail("not implemented: compound assignment operator");
    return false;
  }

  /* 暂存对象（和计算键）到临时槽，保证对象表达式只求值一次。 */
  if (!compileExpr(*obj_expr)) return false;
  uint16_t t_obj = scope_->next_slot++;
  builder_.store_var(t_obj);
  uint16_t t_key = 0;
  if (computed) {
    if (!compileExpr(*key_expr)) return false;
    t_key = scope_->next_slot++;
    builder_.store_var(t_key);
  }

  /* 读旧值。 */
  builder_.load_var(t_obj);
  if (computed) {
    builder_.load_var(t_key);
    builder_.load_elem();
  } else {
    builder_.load_prop(prop_name);
  }
  /* 运算：新值 = 旧值 op rhs。 */
  if (!compileExpr(*rhs)) return false;
  builder_.push_op(op);

  /* 写回：StoreProp/StoreElem 期望 obj 在下、新值在栈顶。
   * 新值已在栈顶，先暂存到临时槽，再压 obj（和 key）+ 新值。 */
  uint16_t t_val = scope_->next_slot++;
  builder_.store_var(t_val);
  builder_.load_var(t_obj);
  if (computed) builder_.load_var(t_key);
  builder_.load_var(t_val);
  if (computed) {
    builder_.store_elem();
  } else {
    builder_.store_prop(prop_name);
  }
  return true;
}

bool Compiler::compileUnary(const ASTNode &node) {
  if (node.children.empty()) {
    fail("unary op without operand");
    return false;
  }
  if (!compileExpr(*node.children[0])) return false;
  switch (node.unop) {
    case UnaryOpKind::Neg: builder_.push_op(Opcode::Neg); return true;
    case UnaryOpKind::Not: builder_.push_op(Opcode::Not); return true;
    case UnaryOpKind::Typeof: builder_.push_op(Opcode::Typeof); return true;
    case UnaryOpKind::Void:
      /* void x：求值 x（副作用），结果 undefined。 */
      builder_.pop();
      builder_.push_op(Opcode::PushUndefined);
      return true;
    case UnaryOpKind::Pos:
      fail("not implemented: unary + (need ToNumber instruction)");
      return false;
    case UnaryOpKind::BitNot:
      fail("not implemented: unary ~ (need bitwise-not instruction)");
      return false;
    case UnaryOpKind::Delete:
    case UnaryOpKind::Await:
      fail("not implemented: unary delete/await");
      return false;
  }
  return true;
}

bool Compiler::compileBinary(const ASTNode &node) {
  if (node.children.size() < 2) {
    fail("binary op without operands");
    return false;
  }
  const ASTNode *lhs = node.children[0];
  const ASTNode *rhs = node.children[1];

  /* 逻辑短路（&&/||）：短路路径保留 lhs，继续路径为 lhs op rhs。
   * VM 的 JumpIfFalse 会弹栈顶，故先把 lhs 暂存到临时局部槽：
   *   [lhs] store tmp
   *   &&:  load tmp; JumpIfFalse short_circuit   (lhs falsy -> 结果 = lhs)
   *       [rhs]
   *       Jump end
   *       short_circuit: load tmp   (结果 = lhs)
   *       end:
   *   ||:  load tmp; Not; JumpIfFalse short_circuit  (lhs truthy -> 结果 = lhs)
   *       [rhs]
   *       Jump end
   *       short_circuit: load tmp
   *       end: */
  if (node.binop == BinaryOpKind::LogicalAnd ||
      node.binop == BinaryOpKind::LogicalOr) {
    if (!compileExpr(*lhs)) return false;
    uint16_t tmp = scope_->next_slot++; /* 临时槽（不入 locals） */
    builder_.store_var(tmp);
    BcLabel short_circuit = builder_.new_label();
    BcLabel end = builder_.new_label();
    builder_.load_var(tmp);
    if (node.binop == BinaryOpKind::LogicalAnd) {
      /* &&：lhs falsy -> 结果 = lhs（短路）。 */
      builder_.jump_if_false(&short_circuit);
    } else {
      /* ||：lhs truthy -> 结果 = lhs（短路）。VM 无 JumpIfTrue，
       * 用 Not + JumpIfFalse 组合：lhs truthy -> Not -> false -> 跳。 */
      builder_.push_op(Opcode::Not);
      builder_.jump_if_false(&short_circuit);
    }
    if (!compileExpr(*rhs)) return false;
    builder_.jump(&end);
    builder_.bind_label(short_circuit);
    builder_.load_var(tmp); /* 短路路径：结果 = lhs */
    builder_.bind_label(end);
    return true;
  }

  if (node.binop == BinaryOpKind::In || node.binop == BinaryOpKind::Instanceof ||
      node.binop == BinaryOpKind::Nullish) {
    fail("not implemented: in/instanceof/?? operator");
    return false;
  }

  Opcode op = Opcode::Nop;
  if (!binaryOpcode(node.binop, &op)) {
    fail("not implemented: binary operator");
    return false;
  }
  if (!compileExpr(*lhs)) return false;
  if (!compileExpr(*rhs)) return false;
  builder_.push_op(op);
  return true;
}

bool Compiler::compileConditional(const ASTNode &node) {
  if (node.children.size() < 3) {
    fail("conditional without branches");
    return false;
  }
  /* cond ? then : else
   *   [cond]
   *   JumpIfFalse else_label
   *   [then]
   *   Jump end_label
   * else_label:
   *   [else]
   * end_label: */
  if (!compileExpr(*node.children[0])) return false;
  BcLabel else_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  builder_.jump_if_false(&else_label);
  if (!compileExpr(*node.children[1])) return false;
  builder_.jump(&end_label);
  builder_.bind_label(else_label);
  if (!compileExpr(*node.children[2])) return false;
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileAssignment(const ASTNode &node) {
  if (node.children.size() < 2) {
    fail("assignment without operands");
    return false;
  }
  const ASTNode *lhs = node.children[0];
  const ASTNode *rhs = node.children[1];

  /* 赋值目标：局部标识符 / 捕获的外层变量 / 成员（obj.prop / obj[expr]）。 */
  if (lhs->type == NodeType::MemberExpression) {
    /* 逻辑复合赋值（&&= / ||= / ??=）在成员分支统一处理（M3.2）。 */
    return compileMemberAssign(node);
  }
  if (lhs->type != NodeType::Identifier) {
    fail("not implemented: assignment to destructuring");
    return false;
  }
  uint16_t slot = 0;
  uint16_t capture = 0;
  bool is_upvalue = false;
  if (!resolveAssignTarget(lhs->name, &slot, &capture, &is_upvalue)) {
    fail("not implemented: assignment to undeclared global '" + lhs->name + "'");
    return false;
  }

  /* 逻辑复合赋值（&&= / ||= / ??=）：需短路语义，本阶段暂不支持（标识符目标）。 */
  if (node.assign_op == AssignmentOpKind::LogicalAndAssign ||
      node.assign_op == AssignmentOpKind::LogicalOrAssign ||
      node.assign_op == AssignmentOpKind::NullishAssign) {
    fail("not implemented: logical compound assignment");
    return false;
  }

  /* 复合赋值：a op= b -> a = a op b（先读旧值，运算后存回）。 */
  if (node.assign_op != AssignmentOpKind::Assign) {
    BinaryOpKind bop = BinaryOpKind::Add;
    if (!compoundAssignToBinary(node.assign_op, &bop)) {
      fail("not implemented: compound assignment operator");
      return false;
    }
    Opcode op = Opcode::Nop;
    if (!binaryOpcode(bop, &op)) {
      fail("not implemented: compound assignment operator");
      return false;
    }
    if (is_upvalue) {
      builder_.load_upvalue(capture); /* 旧值 */
    } else {
      builder_.load_var(slot);
    }
    if (!compileExpr(*rhs)) return false;
    builder_.push_op(op);
    if (is_upvalue) {
      builder_.store_upvalue(capture); /* 存回（弹栈） */
      builder_.load_upvalue(capture);  /* 赋值表达式值 = 新值 */
    } else {
      builder_.store_var(slot);
      builder_.load_var(slot);
    }
    return true;
  }

  /* 简单赋值：a = expr。赋值表达式值 = rhs。 */
  if (!compileExpr(*rhs)) return false;
  if (is_upvalue) {
    builder_.store_upvalue(capture); /* 弹栈 */
    builder_.load_upvalue(capture);  /* 保留赋值结果在栈顶 */
  } else {
    builder_.store_var(slot);
    builder_.load_var(slot);
  }
  return true;
}

bool Compiler::compileCall(const ASTNode &node) {
  if (node.children.empty()) {
    fail("call without callee");
    return false;
  }
  const ASTNode *callee = node.children[0];
  size_t argc = node.children.size() - 1;

  /* 成员方法调用 obj.method(args)（M4.1）：this 绑定接收者。
   *   [obj] Dup LoadProp "method" [args...] CallMethod {argc}
   * 栈布局：obj, func, args...（CallMethod 以 obj 为 this）。 */
  if (callee->type == NodeType::MemberExpression) {
    if (callee->member.optional || callee->children.size() < 2) {
      fail("not implemented: optional chaining in call");
      return false;
    }
    const ASTNode *prop = callee->children[1];
    if (prop->type == NodeType::PrivateIdentifier) {
      fail("not implemented: private member call");
      return false;
    }
    if (!compileExpr(*callee->children[0])) return false; /* obj */
    builder_.dup();                                        /* obj, obj */
    if (!callee->member.computed) {
      if (prop->type != NodeType::Identifier) {
        fail("invalid member property name");
        return false;
      }
      builder_.load_prop(prop->name); /* obj, func */
    } else {
      if (!compileExpr(*prop)) return false; /* obj, obj, key */
      builder_.load_elem();                  /* obj, func */
    }
    for (size_t i = 1; i < node.children.size(); ++i) {
      if (!compileExpr(*node.children[i])) return false;
    }
    builder_.call_method(static_cast<uint16_t>(argc));
    return true;
  }

  /* 普通调用：callee 先入栈，再压 argc 个参数，Call {argc}。 */
  if (!compileExpr(*callee)) return false;
  for (size_t i = 1; i < node.children.size(); ++i) {
    if (!compileExpr(*node.children[i])) return false;
  }
  builder_.call(static_cast<uint16_t>(argc));
  return true;
}

/* new 构造（M4.2）：new Foo(args) / new Foo。
 *   ctor, args... -> Construct {argc}
 * new 语义下 this 是新对象，callee 只需求值（成员构造 new a.b() 的
 * this 与 a 无关，直接 LoadProp 取构造器即可，无需保留接收者）。 */
bool Compiler::compileNew(const ASTNode &node) {
  if (node.children.empty()) {
    fail("new without callee");
    return false;
  }
  const ASTNode *callee = node.children[0];
  if (callee->type == NodeType::MemberExpression) {
    /* new a.b(...)：求值 a 并取 b（this 语义不影响 new）。 */
    if (callee->member.optional || callee->children.size() < 2) {
      fail("not implemented: optional chaining in new");
      return false;
    }
    if (!compileExpr(*callee->children[0])) return false;
    const ASTNode *prop = callee->children[1];
    if (prop->type == NodeType::PrivateIdentifier) {
      fail("not implemented: private member in new");
      return false;
    }
    if (!callee->member.computed) {
      if (prop->type != NodeType::Identifier) {
        fail("invalid member property name");
        return false;
      }
      builder_.load_prop(prop->name);
    } else {
      if (!compileExpr(*prop)) return false;
      builder_.load_elem();
    }
  } else {
    if (!compileExpr(*callee)) return false;
  }
  size_t argc = node.children.size() - 1;
  for (size_t i = 1; i < node.children.size(); ++i) {
    if (!compileExpr(*node.children[i])) return false;
  }
  builder_.construct(static_cast<uint16_t>(argc));
  return true;
}

bool Compiler::compileSequence(const ASTNode &node) {
  /* 逗号序列：逐个求值，保留最后一个。 */
  if (node.children.empty()) {
    builder_.push_op(Opcode::PushUndefined);
    return true;
  }
  for (size_t i = 0; i + 1 < node.children.size(); ++i) {
    if (!compileExpr(*node.children[i])) return false;
    builder_.pop(); /* 丢弃中间值 */
  }
  return compileExpr(*node.children.back());
}

/* -------------------------------------------------------------------------- */
/* 局部变量槽分配与解析                                                        */
/* -------------------------------------------------------------------------- */

uint16_t Compiler::declareLocal(const std::string &name) {
  if (scope_ == nullptr) return 0;
  auto it = scope_->locals.find(name);
  if (it != scope_->locals.end()) return it->second; /* 已声明：复用槽 */
  uint16_t slot = scope_->next_slot++;
  scope_->locals[name] = slot;
  builder_.set_local_name(slot, name); /* M2.5：记录槽名（环境同步） */
  return slot;
}

bool Compiler::resolveLocal(const std::string &name, uint16_t *slot) {
  if (scope_ == nullptr) return false;
  auto it = scope_->locals.find(name);
  if (it == scope_->locals.end()) return false;
  *slot = it->second;
  return true;
}

/* 标识符读取（M2.5）：本函数局部 -> LoadVar；外层局部 -> 捕获 LoadUpvalue；
 * 否则 LoadGlobal。返回 true 表示已发射 load 指令。 */
bool Compiler::emitLoadIdentifier(const std::string &name) {
  uint16_t slot = 0;
  if (resolveLocal(name, &slot)) {
    builder_.load_var(slot);
    return true;
  }

  /* 沿作用域链查找外层函数是否声明该变量。找到则捕获为 upvalue。
   * depth = 外层函数作用域帧层数（0 = 直接外层）。 */
  int depth = 0;
  for (CompilerScope *s = scope_->parent; s != nullptr; s = s->parent, ++depth) {
    auto it = s->locals.find(name);
    if (it != s->locals.end()) {
      uint16_t idx = builder_.add_capture(static_cast<uint16_t>(depth), name);
      builder_.load_upvalue(idx);
      return true;
    }
  }

  builder_.load_global(name);
  return true;
}

/* 赋值目标解析（M2.5）：本函数局部 -> slot；外层局部 -> capture upvalue；
 * 否则返回 false（全局赋值本阶段不支持，见 compileAssignment）。 */
bool Compiler::resolveAssignTarget(const std::string &name, uint16_t *slot,
                                   uint16_t *capture, bool *is_upvalue) {
  *is_upvalue = false;
  if (resolveLocal(name, slot)) return true;

  int depth = 0;
  for (CompilerScope *s = scope_->parent; s != nullptr; s = s->parent, ++depth) {
    auto it = s->locals.find(name);
    if (it != s->locals.end()) {
      *capture = builder_.add_capture(static_cast<uint16_t>(depth), name);
      *is_upvalue = true;
      return true;
    }
  }
  return false; /* 全局 */
}

bool Compiler::isSimpleAssignTarget(const ASTNode &node) const {
  return node.type == NodeType::Identifier;
}

}  // namespace neojs
