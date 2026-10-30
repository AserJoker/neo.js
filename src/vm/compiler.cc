#include "neojs/vm/compiler.h"

namespace neojs {
namespace vm {

namespace {

using neojs::ASTNode;
using neojs::BinaryOpKind;
using neojs::NodeType;
using neojs::UnaryOpKind;

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
 * 布局：compile lhs; jump_if_xx END; compile rhs; END: */
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
      /* 函数调用/赋值/成员/对象字面量等：M4 不实现 */
      return false;
  }
}

}  // namespace vm
}  // namespace neojs
