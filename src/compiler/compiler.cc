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
  locals_.clear();
  next_slot_ = 0;
  builder_ = BytecodeBuilder();

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
  builder_.set_num_locals(next_slot_);
  result.ok = true;
  result.fn = builder_.build();
  return result;
}

CompileResult Compiler::compileFunction(const ASTNode &fn) {
  CompileResult result;
  failed_ = false;
  error_.clear();
  locals_.clear();
  next_slot_ = 0;

  /* 函数名（可空）：FunctionDeclaration 恒有名；
   * FunctionExpression 可匿名（children[0] 为名字或参数）。
   * 布局：children[0] = 名字（有名时），之后参数，最后 body（BlockStatement）。 */
  size_t start = 0;
  std::string fn_name;
  if (fn.type == NodeType::FunctionDeclaration && !fn.children.empty() &&
      fn.children[0]->type == NodeType::Identifier) {
    fn_name = fn.children[0]->name;
    start = 1;
  } else if (fn.type == NodeType::FunctionExpression &&
             fn.children.size() >= 2 &&
             fn.children.back()->type == NodeType::BlockStatement &&
             fn.children[0]->type == NodeType::Identifier) {
    /* 有名函数表达式：children[0] 是名字（后面至少还有参数+body）。 */
    fn_name = fn.children[0]->name;
    start = 1;
  }
  builder_ = BytecodeBuilder(fn_name);

  /* 参数入局部槽 0..n-1（按声明顺序）。body 是最后那个 BlockStatement。 */
  size_t body_index = fn.children.size();
  for (size_t i = start; i < fn.children.size(); ++i) {
    const ASTNode *p = fn.children[i];
    if (p->type == NodeType::BlockStatement) {
      body_index = i;
      break;
    }
    if (p->type != NodeType::Identifier) {
      fail("not implemented: destructuring/default/rest parameter");
      return failResult();
    }
    declareLocal(p->name);
  }

  /* 函数体：块语句逐条编译。 */
  if (body_index < fn.children.size()) {
    const ASTNode *body = fn.children[body_index];
    for (const ASTNode *stmt : body->children) {
      if (stmt == nullptr) continue;
      if (stmt->type == NodeType::FunctionDeclaration) {
        fail("not implemented: nested function declaration (M2.5 闭包)");
        return failResult();
      }
      if (!compileStmt(*stmt)) return failResult();
    }
  }

  /* 函数默认返回 undefined。 */
  builder_.push_op(Opcode::PushUndefined);
  builder_.ret();

  builder_.set_num_locals(next_slot_);
  result.ok = true;
  result.fn = builder_.build();
  return result;
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
      fail("not implemented: function declaration (M2.4 ③)");
      return false;
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
    case NodeType::Identifier: return compileIdentifier(node);
    case NodeType::UnaryOp: return compileUnary(node);
    case NodeType::BinaryOp: return compileBinary(node);
    case NodeType::Conditional: return compileConditional(node);
    case NodeType::Assignment: return compileAssignment(node);
    case NodeType::CallExpression: return compileCall(node);
    case NodeType::Sequence: return compileSequence(node);
    case NodeType::MemberExpression:
      fail("not implemented: member expression");
      return false;
    case NodeType::NewExpression:
      fail("not implemented: new expression");
      return false;
    case NodeType::FunctionExpression:
    case NodeType::ArrowFunctionExpression:
      fail("not implemented: function expression (M2.4 ③)");
      return false;
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
  uint16_t slot = 0;
  if (resolveLocal(node.name, &slot)) {
    builder_.load_var(slot);
  } else {
    builder_.load_global(node.name); /* 未声明标识符 -> 全局对象属性读取 */
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
    uint16_t tmp = next_slot_++; /* 临时槽（不入 locals_） */
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

  /* 赋值目标：本阶段仅支持局部标识符（成员/解构在后续实现）。 */
  if (lhs->type != NodeType::Identifier) {
    fail("not implemented: assignment to member/destructuring");
    return false;
  }
  uint16_t slot = 0;
  if (!resolveLocal(lhs->name, &slot)) {
    fail("not implemented: assignment to undeclared global '" + lhs->name + "'");
    return false;
  }

  /* 逻辑复合赋值（&&= / ||= / ??=）：需短路语义，本阶段暂不支持。 */
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
    builder_.load_var(slot); /* 旧值 */
    if (!compileExpr(*rhs)) return false;
    builder_.push_op(op);
    builder_.store_var(slot); /* 存回（弹栈） */
    builder_.load_var(slot);  /* 赋值表达式值 = 新值 */
    return true;
  }

  /* 简单赋值：a = expr。赋值表达式值 = rhs。 */
  if (!compileExpr(*rhs)) return false;
  builder_.store_var(slot); /* 弹栈 */
  builder_.load_var(slot);  /* 保留赋值结果在栈顶 */
  return true;
}

bool Compiler::compileCall(const ASTNode &node) {
  if (node.children.empty()) {
    fail("call without callee");
    return false;
  }
  /* callee 先入栈，再压 argc 个参数，Call {argc}。 */
  if (!compileExpr(*node.children[0])) return false;
  size_t argc = node.children.size() - 1;
  for (size_t i = 1; i < node.children.size(); ++i) {
    if (!compileExpr(*node.children[i])) return false;
  }
  builder_.call(static_cast<uint16_t>(argc));
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
  auto it = locals_.find(name);
  if (it != locals_.end()) return it->second; /* 已声明：复用槽 */
  uint16_t slot = next_slot_++;
  locals_[name] = slot;
  return slot;
}

bool Compiler::resolveLocal(const std::string &name, uint16_t *slot) {
  auto it = locals_.find(name);
  if (it == locals_.end()) return false;
  *slot = it->second;
  return true;
}

bool Compiler::isSimpleAssignTarget(const ASTNode &node) const {
  return node.type == NodeType::Identifier;
}

}  // namespace neojs
