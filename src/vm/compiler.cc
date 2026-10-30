#include "neojs/vm/compiler.h"

namespace neojs {
namespace vm {

namespace {

using neojs::ASTNode;
using neojs::BinaryOpKind;
using neojs::NodeType;
using neojs::UnaryOpKind;
using neojs::VarKind;

/* 二元运算（非短路）→ Opcode。返回 true 命中。 */
bool map_binary(BinaryOpKind k, Opcode &op) {
  switch (k) {
    case BinaryOpKind::Add: op = Opcode::Add; return true;
    case BinaryOpKind::Sub: op = Opcode::Sub; return true;
    case BinaryOpKind::Mul: op = Opcode::Mul; return true;
    case BinaryOpKind::Div: op = Opcode::Div; return true;
    case BinaryOpKind::Mod: op = Opcode::Mod; return true;
    case BinaryOpKind::Pow: op = Opcode::Pow; return true;
    case BinaryOpKind::LShift: op = Opcode::Shl; return true;
    case BinaryOpKind::RShift: op = Opcode::Sar; return true;
    case BinaryOpKind::URShift: op = Opcode::Shr; return true;
    case BinaryOpKind::BitAnd: op = Opcode::BitAnd; return true;
    case BinaryOpKind::BitXor: op = Opcode::BitXor; return true;
    case BinaryOpKind::BitOr: op = Opcode::BitOr; return true;
    case BinaryOpKind::Lt: op = Opcode::Lt; return true;
    case BinaryOpKind::Le: op = Opcode::Le; return true;
    case BinaryOpKind::Gt: op = Opcode::Gt; return true;
    case BinaryOpKind::Ge: op = Opcode::Ge; return true;
    case BinaryOpKind::Eq: op = Opcode::Eq; return true;
    case BinaryOpKind::Ne: op = Opcode::Ne; return true;
    case BinaryOpKind::StrictEq: op = Opcode::StrictEq; return true;
    case BinaryOpKind::StrictNe: op = Opcode::StrictNe; return true;
    default: return false; /* In/Instanceof/LogicalAnd/LogicalOr/Nullish */
  }
}

/* 一元运算 → Opcode。返回 true 命中。 */
bool map_unary(UnaryOpKind k, Opcode &op) {
  switch (k) {
    case UnaryOpKind::Pos: op = Opcode::Pos; return true;
    case UnaryOpKind::Neg: op = Opcode::Neg; return true;
    case UnaryOpKind::Not: op = Opcode::Not; return true;
    case UnaryOpKind::BitNot: op = Opcode::BitNot; return true;
    case UnaryOpKind::Typeof: op = Opcode::Typeof; return true;
    case UnaryOpKind::Void: op = Opcode::Void; return true;
    default: return false; /* Delete/Await（M4 不实现） */
  }
}

/* 编译短路逻辑：lhs op rhs。op 为短路判定跳转指令。
 * 布局：compile lhs; jump_if_xx END; pop(丢掉 lhs); compile rhs; END:
 *   - 条件命中（jump_op 跳转）：lhs 即结果，保留栈顶跳到 END。
 *   - 条件不命中：pop 掉 lhs，计算 rhs 得结果。
 * 例：
 *   &&（JumpIfFalse）：lhs 假 → 跳 END 返回 lhs；真 → pop 后算 rhs。
 *   ||（JumpIfTrue）：lhs 真 → 跳 END 返回 lhs；假 → pop 后算 rhs。
 *   ??（JumpIfNotNullish）：lhs 非 null/undefined → 跳 END 返回 lhs；
 *       nullish → pop 后算 rhs。 */
bool compile_short_circuit(ASTNode *node, Bytecode &bc, Opcode jump_op) {
  if (node->children.size() < 2) return false;
  if (!compile_expression(node->children[0], bc)) return false;
  size_t jump_at = bc.size();
  bc.emit(jump_op); /* 占位，回填 */
  bc.emit(Opcode::Pop); /* 非跳转路径：丢掉 lhs */
  if (!compile_expression(node->children[1], bc)) return false;
  bc.patch_jump(jump_at, bc.size());
  return true;
}

}  // namespace

/* ========================================================================== */
/* 表达式编译（M4 已有）                                                      */
/* ========================================================================== */

bool compile_expression(ASTNode *node, Bytecode &bc) {
  if (node == nullptr) return false;
  switch (node->type) {
    case NodeType::Literal: {
      const auto &lit = node->literal;
      if (lit.is_number) {
        bc.emit(Opcode::PushConst, bc.add_number(lit.number));
        return true;
      }
      if (lit.is_string) {
        bc.emit(Opcode::PushConst, bc.add_string(lit.str));
        return true;
      }
      if (lit.is_boolean) {
        bc.emit(lit.bool_value ? Opcode::PushTrue : Opcode::PushFalse);
        return true;
      }
      if (lit.is_null) {
        bc.emit(Opcode::PushNull);
        return true;
      }
      /* bigint 等未支持 */
      return false;
    }

    case NodeType::Identifier: {
      bc.emit(Opcode::LoadVar, bc.add_string(node->name));
      return true;
    }

    case NodeType::Assignment: {
      /* 赋值：lhs = rhs。M5-A 仅支持 lhs 为 Identifier。
       * 布局：compile rhs; StoreVar{name}（保留栈顶 = 赋值表达式结果）。 */
      if (node->children.size() < 2) return false;
      ASTNode *lhs = node->children[0];
      if (lhs->type != NodeType::Identifier) return false; /* 成员/解构 M5-B */
      if (!compile_expression(node->children[1], bc)) return false;
      bc.emit(Opcode::StoreVar, bc.add_string(lhs->name));
      return true;
    }

    case NodeType::UnaryOp: {
      if (node->children.size() < 1) return false;
      Opcode op;
      if (!map_unary(node->unop, op)) return false;
      if (!compile_expression(node->children[0], bc)) return false;
      bc.emit(op);
      return true;
    }

    case NodeType::BinaryOp: {
      if (node->children.size() < 2) return false;
      /* 短路逻辑单独处理 */
      if (node->binop == BinaryOpKind::LogicalAnd) {
        return compile_short_circuit(node, bc, Opcode::JumpIfFalse);
      }
      if (node->binop == BinaryOpKind::LogicalOr) {
        return compile_short_circuit(node, bc, Opcode::JumpIfTrue);
      }
      if (node->binop == BinaryOpKind::Nullish) {
        /* ??：lhs 非 nullish 返回 lhs（跳 END），nullish 计算 rhs。
         * 与 &&(JumpIfFalse)/||(JumpIfTrue) 对称：跳转 = 保留左操作数。 */
        return compile_short_circuit(node, bc, Opcode::JumpIfNotNullish);
      }
      Opcode op;
      if (!map_binary(node->binop, op)) return false;
      if (!compile_expression(node->children[0], bc)) return false;
      if (!compile_expression(node->children[1], bc)) return false;
      bc.emit(op);
      return true;
    }

    case NodeType::Conditional: {
      /* cond ? a : b；M4 表达式支持。
       * 布局：compile cond; jump_if_false ELSE; compile a; jump END;
       *       ELSE: pop(丢掉 cond); compile b; END:
       * 注意：conditional 的 jump_if_false 与短路不同，需要 pop 掉 cond
       * （短路保留栈顶作为结果，conditional 跳去求值另一分支）。 */
      if (node->children.size() < 3) return false;
      if (!compile_expression(node->children[0], bc)) return false;
      size_t jump_false_at = bc.size();
      bc.emit(Opcode::JumpIfFalse);
      if (!compile_expression(node->children[1], bc)) return false;
      size_t jump_end_at = bc.size();
      bc.emit(Opcode::Jump);
      bc.patch_jump(jump_false_at, bc.size());
      bc.emit(Opcode::Pop); /* 丢掉 cond */
      if (!compile_expression(node->children[2], bc)) return false;
      bc.patch_jump(jump_end_at, bc.size());
      return true;
    }

    default:
      /* 函数调用/成员/对象字面量等：M4/M5-B 不实现 */
      return false;
  }
}

/* ========================================================================== */
/* 语句编译（M5-A）                                                           */
/* ========================================================================== */

namespace {

/* 收集块内直接子语句中的 let/const 声明名（不递归嵌套块）。
 * 用于块级 TDZ：进入块时先为这些名字发哨兵定义。 */
void collect_tdz_names(ASTNode *block, std::vector<std::string> &names) {
  for (ASTNode *stmt : block->children) {
    if (stmt->type != NodeType::VariableDeclaration) continue;
    if (stmt->varkind == VarKind::Var) continue; /* var 无 TDZ */
    for (ASTNode *d : stmt->children) {
      if (d->type != NodeType::VariableDeclarator) continue;
      if (d->children.empty()) continue;
      ASTNode *id = d->children[0];
      if (id->type == NodeType::Identifier) names.push_back(id->name);
    }
  }
}

bool compile_statement_inner(ASTNode *node, Bytecode &bc,
                             const std::vector<std::string> *declared);

/* 编译语句列表。declared = 本层已预声明的 let/const 名字集合（可能为空）。 */
bool compile_statement_list(ASTNode *node, Bytecode &bc,
                            const std::vector<std::string> *declared) {
  for (ASTNode *child : node->children) {
    if (!compile_statement_inner(child, bc, declared)) return false;
  }
  return true;
}

bool compile_statement_inner(ASTNode *node, Bytecode &bc,
                             const std::vector<std::string> *declared) {
  if (node == nullptr) return false;
  switch (node->type) {
    case NodeType::ExprStmt: {
      /* 表达式语句：求值后丢弃结果 */
      if (node->children.empty()) return false;
      if (!compile_expression(node->children[0], bc)) return false;
      bc.emit(Opcode::Pop);
      return true;
    }

    case NodeType::BlockStatement: {
      /* 块：进入新作用域，先为直接 let/const 发 TDZ 哨兵定义，编译子语句，离开。 */
      std::vector<std::string> tdz;
      collect_tdz_names(node, tdz);
      bc.emit(Opcode::PushScope);
      for (const std::string &name : tdz) {
        bc.emit(Opcode::PushUninitialized);
        bc.emit(Opcode::DefineVar, bc.add_string(name));
      }
      if (!compile_statement_list(node, bc, &tdz)) return false;
      bc.emit(Opcode::PopScope);
      return true;
    }

    case NodeType::VariableDeclaration: {
      /* 声明：var/let/const。
       * var：函数级作用域（顶层=全局），无 TDZ，直接 define 初始值。
       * let/const：块作用域。若名字已在块顶预声明（TDZ 哨兵已发），
       *   只求值 init → StoreVar；否则（for init 等）先发哨兵+DefineVar。 */
      for (ASTNode *declarator : node->children) {
        if (declarator->type != NodeType::VariableDeclarator) continue;
        /* children[0] = id（仅支持 Identifier，解构 M5-B） */
        if (declarator->children.empty()) return false;
        ASTNode *id = declarator->children[0];
        if (id->type != NodeType::Identifier) return false;
        ASTNode *init = declarator->children.size() > 1
                            ? declarator->children[1]
                            : nullptr;
        const std::string &name = id->name;

        if (node->varkind == VarKind::Var) {
          /* var：无 TDZ，直接 define 到 var 目标作用域（函数级/全局，
           * 非块作用域——块内 var 提升）。无 init 则 undefined。 */
          if (init != nullptr) {
            if (!compile_expression(init, bc)) return false;
          } else {
            bc.emit(Opcode::PushUndefined);
          }
          bc.emit(Opcode::DefineVarGlobal, bc.add_string(name));
          continue;
        }

        /* let/const */
        bool predeclared = false;
        if (declared != nullptr) {
          for (const std::string &n : *declared) {
            if (n == name) { predeclared = true; break; }
          }
        }
        if (!predeclared) {
          /* 未预声明（for init 等）：先发 TDZ 哨兵 + DefineVar */
          bc.emit(Opcode::PushUninitialized);
          bc.emit(Opcode::DefineVar, bc.add_string(name));
        }
        if (init != nullptr) {
          if (!compile_expression(init, bc)) return false;
        } else {
          bc.emit(Opcode::PushUndefined);
        }
        bc.emit(Opcode::StoreVar, bc.add_string(name));
      }
      return true;
    }

    case NodeType::IfStatement: {
      /* if (cond) then [else]：
       *   cond; JumpIfFalsePop ELSE; then; Jump END; ELSE: else; END:
       * JumpIfFalsePop 消费栈顶（cond），两条路径栈均干净。 */
      if (node->children.size() < 2) return false;
      if (!compile_expression(node->children[0], bc)) return false;
      size_t jump_else_at = bc.size();
      bc.emit(Opcode::JumpIfFalsePop);
      if (!compile_statement_inner(node->children[1], bc, declared)) return false;
      /* then 分支结束：跳过 else */
      size_t jump_end_at = 0;
      if (node->children.size() >= 3 && node->children[2] != nullptr) {
        jump_end_at = bc.size();
        bc.emit(Opcode::Jump);
      }
      bc.patch_jump(jump_else_at, bc.size());
      if (node->children.size() >= 3 && node->children[2] != nullptr) {
        if (!compile_statement_inner(node->children[2], bc, declared))
          return false;
        bc.patch_jump(jump_end_at, bc.size());
      }
      return true;
    }

    case NodeType::WhileStatement: {
      /* while (cond) body：
       *   START: cond; JumpIfFalsePop END; body; Jump START; END:
       * JumpIfFalsePop 消费 cond，两条路径栈均干净。 */
      if (node->children.size() < 2) return false;
      size_t start = bc.size();
      if (!compile_expression(node->children[0], bc)) return false;
      size_t jump_end_at = bc.size();
      bc.emit(Opcode::JumpIfFalsePop);
      if (!compile_statement_inner(node->children[1], bc, declared))
        return false;
      bc.emit(Opcode::Jump, static_cast<uint32_t>(start));
      bc.patch_jump(jump_end_at, bc.size());
      return true;
    }

    case NodeType::DoWhileStatement: {
      /* do body while (cond)：
       *   START: body; cond; JumpIfTruePop START
       * JumpIfTruePop 消费 cond：为真消费并跳回 START（栈干净）；为假
       * 不跳（cond 已消费），自然落到 END。 */
      if (node->children.size() < 2) return false;
      size_t start = bc.size();
      if (!compile_statement_inner(node->children[0], bc, declared))
        return false;
      if (!compile_expression(node->children[1], bc)) return false;
      bc.emit(Opcode::JumpIfTruePop, static_cast<uint32_t>(start));
      return true;
    }

    case NodeType::ForStatement: {
      /* for (init; cond; update) body：
       *   init; START: cond; JumpIfFalsePop END; body; update; Jump START; END:
       * init/cond/update 可为空（缺省 undefined/恒真/空）。 */
      if (node->children.size() < 4) return false;
      ASTNode *init = node->children[0];
      ASTNode *cond = node->children[1];
      ASTNode *update = node->children[2];
      ASTNode *body = node->children[3];
      if (init != nullptr) {
        /* init 是声明或表达式 */
        if (init->type == NodeType::VariableDeclaration) {
          if (!compile_statement_inner(init, bc, declared)) return false;
        } else {
          if (!compile_expression(init, bc)) return false;
          bc.emit(Opcode::Pop);
        }
      }
      size_t start = bc.size();
      if (cond != nullptr) {
        if (!compile_expression(cond, bc)) return false;
      } else {
        bc.emit(Opcode::PushTrue); /* 恒真 */
      }
      size_t jump_end_at = bc.size();
      bc.emit(Opcode::JumpIfFalsePop);
      if (body != nullptr) {
        if (!compile_statement_inner(body, bc, declared)) return false;
      }
      if (update != nullptr) {
        if (!compile_expression(update, bc)) return false;
        bc.emit(Opcode::Pop);
      }
      bc.emit(Opcode::Jump, static_cast<uint32_t>(start));
      bc.patch_jump(jump_end_at, bc.size());
      return true;
    }

    case NodeType::ReturnStatement: {
      /* return [expr]：expr/undefined 压栈 → Ret（产生 interrupt） */
      if (!node->children.empty() && node->children[0] != nullptr) {
        if (!compile_expression(node->children[0], bc)) return false;
      } else {
        bc.emit(Opcode::PushUndefined);
      }
      bc.emit(Opcode::Ret);
      return true;
    }

    default:
      /* 函数声明/break/continue/throw/try 等：M5-B/后续 */
      return false;
  }
}

}  // namespace

/* 编译单条语句到 bc（不带预声明：单语句不是块，let/const 自声明自初始化）。 */
bool compile_statement(ASTNode *node, Bytecode &bc) {
  return compile_statement_inner(node, bc, nullptr);
}

/* 编译 Program（顶层语句列表）。返回 true 成功。
 * 顶层（Program）是隐式块作用域：先为直接 let/const 发 TDZ 哨兵定义。 */
bool compile_program(ASTNode *node, Bytecode &bc) {
  if (node == nullptr) return false;
  std::vector<std::string> tdz;
  collect_tdz_names(node, tdz);
  for (const std::string &name : tdz) {
    bc.emit(Opcode::PushUninitialized);
    bc.emit(Opcode::DefineVar, bc.add_string(name));
  }
  return compile_statement_list(node, bc, &tdz);
}

}  // namespace vm
}  // namespace neojs
