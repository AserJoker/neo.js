#include "neojs/vm/compiler.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "neojs/frontend/sema.h"

namespace neojs {
namespace vm {

namespace {

using neojs::ASTNode;
using neojs::BinaryOpKind;
using neojs::NodeType;
using neojs::UnaryOpKind;
using neojs::VarKind;

/* 编译上下文：函数收集器（函数体延迟到主流程编译完再追加到尾部）。
 * 两阶段编译（§5.1）：
 *   - 阶段1：主流程编译，函数节点只预留 functions[idx] + 发 CreateFunction。
 *   - 阶段2：compile_function_bodies 统一把函数体追加到 bc 尾部回填 offset。
 * 主流程末尾必须发 Halt，防止顺序执行流进函数体区。 */
struct CompilerCtx {
  /* 待编译函数体：{函数节点, functions 表 idx}（阶段2 遍历编译）。 */
  std::vector<std::pair<ASTNode *, size_t>> funcs;
};

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

/* 递归编译表达式。ctx 收集函数体（延迟到主流程后统一编译）。
 * compile_expression（公开 M4 入口）是它的薄封装。 */
bool compile_expr_inner(ASTNode *node, Bytecode &bc, CompilerCtx *ctx);

/* 编译短路逻辑：lhs op rhs。op 为短路判定跳转指令。 */
bool compile_short_circuit(ASTNode *node, Bytecode &bc, CompilerCtx *ctx,
                           Opcode jump_op) {
  if (node->children.size() < 2) return false;
  if (!compile_expr_inner(node->children[0], bc, ctx)) return false;
  size_t jump_at = bc.size();
  bc.emit(jump_op); /* 占位，回填 */
  bc.emit(Opcode::Pop); /* 非跳转路径：丢掉 lhs */
  if (!compile_expr_inner(node->children[1], bc, ctx)) return false;
  bc.patch_jump(jump_at, bc.size());
  return true;
}

/* 递归编译语句。ctx 收集函数体。 */
bool compile_stmt_inner(ASTNode *node, Bytecode &bc, CompilerCtx *ctx);
bool compile_stmt_list(ASTNode *node, Bytecode &bc, CompilerCtx *ctx);

/* 阶段1：函数节点占位。
 * 布局（主流程内）：
 *   CreateFunction{idx} → 压函数值
 *   LoadVar{cap} + SetClosure{cap}（每个捕获名，§10.8 op_set_closure）
 * 函数体字节码延迟到阶段2 追加到尾部。 */
bool compile_function_placeholder(ASTNode *node, Bytecode &bc,
                                  CompilerCtx *ctx) {
  /* 预留函数表项（offset 阶段2 回填；param_count/params 现在填） */
  size_t idx = bc.functions.size();
  FunctionInfo info;
  /* children = [name?, params..., body]（fn_has_name 标记 name 存在） */
  size_t nparams = node->children.size() - 1 - (node->fn_has_name ? 1 : 0);
  info.param_count = nparams;
  for (size_t i = 0; i < nparams; ++i) {
    size_t pi = node->fn_has_name ? i + 1 : i;
    ASTNode *param = node->children[pi];
    ASTNode *id = param;
    if (param->type == NodeType::AssignmentPattern && !param->children.empty()) {
      id = param->children[0]; /* 默认值 a = 1 → 取 a */
    }
    if (id->type == NodeType::Identifier) {
      info.params.push_back(bc.add_string(id->name));
    } else {
      info.params.push_back(bc.add_string("")); /* 解构 M5-B 不做 */
    }
  }
  bc.functions.push_back(info);
  if (ctx != nullptr) ctx->funcs.push_back({node, idx});

  bc.emit(Opcode::CreateFunction, static_cast<uint32_t>(idx));
  /* 捕获挂载：函数节点 captures 列表。 */
  for (const std::string &cap : node->captures) {
    bc.emit(Opcode::LoadVar, bc.add_string(cap));
    bc.emit(Opcode::SetClosure, bc.add_string(cap));
  }
  return true;
}

/* 作用域入口：按 directives 清单发 EnableOption（指令序言，先于变量定义生效）。
 * 选项是作用域属性：设到当前 scope，scope 销毁自动恢复，无需 DisableOption。 */
void compile_option_entry(const std::vector<std::string> &directives,
                          Bytecode &bc) {
  for (const std::string &d : directives) {
    bc.emit(Opcode::EnableOption, bc.add_string(d));
  }
}

/* 阶段2：编译函数体（追加到 bc 尾部）。
 * 布局：body scope 变量定义（var/let/const 提升清单）→ 函数体语句 → Ret。
 * 函数体块不 PushScope（Call 已建 body scope）；编译器保证末尾 Ret。 */
bool compile_function_body(ASTNode *node, Bytecode &bc, CompilerCtx *ctx) {
  ASTNode *body = node->children.empty() ? nullptr : node->children.back();
  if (body == nullptr) return false;

  /* 函数体指令序言：入口 EnableOption（先于变量定义生效）。
   * 选项设到 body scope：函数返回 body scope 销毁自动恢复，无需 DisableOption。 */
  compile_option_entry(node->directives, bc);

  /* 按 sema 挂载的 body_scope（node->scope_info）变量清单生成定义 */
  sema::ShadowScope *ss = node->scope_info;
  if (ss != nullptr) {
    for (sema::ShadowValue *sv : ss->values) {
      if (sv->kind == VarKind::Var) {
        bc.emit(Opcode::PushUndefined);
      } else {
        bc.emit(Opcode::PushUninitialized);
      }
      bc.emit(Opcode::DefineVar, bc.add_string(sv->name));
    }
  }

  if (body->type == NodeType::BlockStatement) {
    for (ASTNode *stmt : body->children) {
      if (!compile_stmt_inner(stmt, bc, ctx)) return false;
    }
  } else {
    /* 箭头函数表达式体：求值 → Ret */
    if (!compile_expr_inner(body, bc, ctx)) return false;
    bc.emit(Opcode::Ret);
    return true;
  }
  /* 无显式 return：PushUndefined + Ret（compiler 保证函数末尾 Ret） */
  bc.emit(Opcode::PushUndefined);
  bc.emit(Opcode::Ret);
  return true;
}

/* 阶段2：遍历收集的函数，逐个编译函数体到尾部并回填 offset。
 * 嵌套函数（函数体内收集）会在遍历过程中追加到 ctx->funcs，循环自然处理。 */
bool compile_function_bodies(CompilerCtx &ctx, Bytecode &bc) {
  for (size_t i = 0; i < ctx.funcs.size(); ++i) {
    ASTNode *fn = ctx.funcs[i].first;
    size_t idx = ctx.funcs[i].second;
    size_t offset = bc.size();
    if (!compile_function_body(fn, bc, &ctx)) return false;
    bc.functions[idx].offset = offset;
  }
  return true;
}

/* 作用域入口：进入挂有 shadow_scope 的节点（Program/Block）时，
 * 按变量清单生成定义（var=undefined、let/const=UNINITIALIZED 哨兵，§10.8）。
 * 返回 true 表示该节点产生作用域。 */
bool compile_scope_entry(ASTNode *node, Bytecode &bc) {
  sema::ShadowScope *ss = node->scope_info;
  if (ss == nullptr) return false;
  for (sema::ShadowValue *sv : ss->values) {
    if (sv->kind == VarKind::Var) {
      bc.emit(Opcode::PushUndefined);
    } else {
      bc.emit(Opcode::PushUninitialized);
    }
    bc.emit(Opcode::DefineVar, bc.add_string(sv->name));
  }
  return true;
}

/* ---------- 表达式编译（内部版本，ctx 贯穿） ---------- */

bool compile_expr_inner(ASTNode *node, Bytecode &bc, CompilerCtx *ctx) {
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

    case NodeType::FunctionExpression:
    case NodeType::ArrowFunctionExpression: {
      /* 函数表达式：CreateFunction + 捕获挂载 → 栈顶函数值。 */
      return compile_function_placeholder(node, bc, ctx);
    }

    case NodeType::CallExpression: {
      /* 函数调用：compile callee → args → Call{argc}（§7）。 */
      if (node->children.empty()) return false;
      if (!compile_expr_inner(node->children[0], bc, ctx)) return false;
      size_t argc = node->children.size() - 1;
      for (size_t i = 1; i < node->children.size(); ++i) {
        if (!compile_expr_inner(node->children[i], bc, ctx)) return false;
      }
      bc.emit(Opcode::Call, static_cast<uint32_t>(argc));
      return true;
    }

    case NodeType::Assignment: {
      /* 赋值：lhs = rhs。M5-B 仅支持 lhs 为 Identifier。 */
      if (node->children.size() < 2) return false;
      ASTNode *lhs = node->children[0];
      if (lhs->type != NodeType::Identifier) return false; /* 成员/解构 M5-B */
      if (!compile_expr_inner(node->children[1], bc, ctx)) return false;
      bc.emit(Opcode::StoreVar, bc.add_string(lhs->name));
      return true;
    }

    case NodeType::UnaryOp: {
      if (node->children.size() < 1) return false;
      Opcode op;
      if (!map_unary(node->unop, op)) return false;
      if (!compile_expr_inner(node->children[0], bc, ctx)) return false;
      bc.emit(op);
      return true;
    }

    case NodeType::BinaryOp: {
      if (node->children.size() < 2) return false;
      /* 短路逻辑单独处理 */
      if (node->binop == BinaryOpKind::LogicalAnd) {
        return compile_short_circuit(node, bc, ctx, Opcode::JumpIfFalse);
      }
      if (node->binop == BinaryOpKind::LogicalOr) {
        return compile_short_circuit(node, bc, ctx, Opcode::JumpIfTrue);
      }
      if (node->binop == BinaryOpKind::Nullish) {
        return compile_short_circuit(node, bc, ctx, Opcode::JumpIfNotNullish);
      }
      Opcode op;
      if (!map_binary(node->binop, op)) return false;
      if (!compile_expr_inner(node->children[0], bc, ctx)) return false;
      if (!compile_expr_inner(node->children[1], bc, ctx)) return false;
      bc.emit(op);
      return true;
    }

    case NodeType::Conditional: {
      /* cond ? a : b */
      if (node->children.size() < 3) return false;
      if (!compile_expr_inner(node->children[0], bc, ctx)) return false;
      size_t jump_false_at = bc.size();
      bc.emit(Opcode::JumpIfFalse);
      if (!compile_expr_inner(node->children[1], bc, ctx)) return false;
      size_t jump_end_at = bc.size();
      bc.emit(Opcode::Jump);
      bc.patch_jump(jump_false_at, bc.size());
      bc.emit(Opcode::Pop); /* 丢掉 cond */
      if (!compile_expr_inner(node->children[2], bc, ctx)) return false;
      bc.patch_jump(jump_end_at, bc.size());
      return true;
    }

    default:
      /* 成员/对象字面量等：后续 */
      return false;
  }
}

/* ---------- 语句编译（内部版本，ctx 贯穿） ---------- */

bool compile_stmt_inner(ASTNode *node, Bytecode &bc, CompilerCtx *ctx) {
  if (node == nullptr) return false;
  switch (node->type) {
    case NodeType::ExprStmt: {
      /* 表达式语句：求值后丢弃结果 */
      if (node->children.empty()) return false;
      if (!compile_expr_inner(node->children[0], bc, ctx)) return false;
      bc.emit(Opcode::Pop);
      return true;
    }

    case NodeType::BlockStatement: {
      /* 块：进入新作用域，按 sema 变量清单发定义（TDZ 哨兵），编译子语句，离开。 */
      bc.emit(Opcode::PushScope);
      compile_scope_entry(node, bc);
      if (!compile_stmt_list(node, bc, ctx)) return false;
      bc.emit(Opcode::PopScope);
      return true;
    }

    case NodeType::VariableDeclaration: {
      /* 声明：var/let/const。sema 已把变量提升到作用域入口（作用域进入时
       * 已创建绑定），此处退化为一条赋值语句（§10.8 变量提升消减）。
       *   有 init → 求值 init → StoreVar（保留栈顶=赋值结果）
       *   无 init → PushUndefined → StoreVar（let 无 init = undefined） */
      for (ASTNode *declarator : node->children) {
        if (declarator->type != NodeType::VariableDeclarator) continue;
        if (declarator->children.empty()) return false;
        ASTNode *id = declarator->children[0];
        if (id->type != NodeType::Identifier) return false; /* 解构 M5-B 不做 */
        ASTNode *init = declarator->children.size() > 1
                            ? declarator->children[1]
                            : nullptr;
        if (init != nullptr) {
          if (!compile_expr_inner(init, bc, ctx)) return false;
        } else {
          bc.emit(Opcode::PushUndefined);
        }
        bc.emit(Opcode::StoreVar, bc.add_string(id->name));
      }
      return true;
    }

    case NodeType::FunctionDeclaration: {
      /* 函数声明：编译函数值 → define 到当前作用域。
       * 提升绑定已在作用域入口创建（undefined），StoreVar 写入函数值。 */
      if (node->children.empty()) return false;
      ASTNode *name_node = node->children[0];
      if (name_node->type != NodeType::Identifier) return false;
      if (!compile_function_placeholder(node, bc, ctx)) return false;
      bc.emit(Opcode::StoreVar, bc.add_string(name_node->name));
      bc.emit(Opcode::Pop); /* 函数声明语句不保留值 */
      return true;
    }

    case NodeType::IfStatement: {
      /* if (cond) then [else]： */
      if (node->children.size() < 2) return false;
      if (!compile_expr_inner(node->children[0], bc, ctx)) return false;
      size_t jump_else_at = bc.size();
      bc.emit(Opcode::JumpIfFalsePop);
      if (!compile_stmt_inner(node->children[1], bc, ctx)) return false;
      size_t jump_end_at = 0;
      if (node->children.size() >= 3 && node->children[2] != nullptr) {
        jump_end_at = bc.size();
        bc.emit(Opcode::Jump);
      }
      bc.patch_jump(jump_else_at, bc.size());
      if (node->children.size() >= 3 && node->children[2] != nullptr) {
        if (!compile_stmt_inner(node->children[2], bc, ctx)) return false;
        bc.patch_jump(jump_end_at, bc.size());
      }
      return true;
    }

    case NodeType::WhileStatement: {
      if (node->children.size() < 2) return false;
      size_t start = bc.size();
      if (!compile_expr_inner(node->children[0], bc, ctx)) return false;
      size_t jump_end_at = bc.size();
      bc.emit(Opcode::JumpIfFalsePop);
      if (!compile_stmt_inner(node->children[1], bc, ctx)) return false;
      bc.emit(Opcode::Jump, static_cast<uint32_t>(start));
      bc.patch_jump(jump_end_at, bc.size());
      return true;
    }

    case NodeType::DoWhileStatement: {
      if (node->children.size() < 2) return false;
      size_t start = bc.size();
      if (!compile_stmt_inner(node->children[0], bc, ctx)) return false;
      if (!compile_expr_inner(node->children[1], bc, ctx)) return false;
      bc.emit(Opcode::JumpIfTruePop, static_cast<uint32_t>(start));
      return true;
    }

    case NodeType::ForStatement: {
      /* for(init;cond;update) body：for-init 的 let/const 声明在 for 作用域
       * （sema 挂 node->scope_info=init_scope）。进入时 PushScope + 发定义，
       * 循环结束 PopScope。per-iteration 闭包（每次迭代新绑定）留后续。 */
      if (node->children.size() < 4) return false;
      ASTNode *init = node->children[0];
      ASTNode *cond = node->children[1];
      ASTNode *update = node->children[2];
      ASTNode *body = node->children[3];
      bool has_scope = node->scope_info != nullptr;
      if (has_scope) bc.emit(Opcode::PushScope);
      if (has_scope) compile_scope_entry(node, bc);
      if (init != nullptr) {
        if (init->type == NodeType::VariableDeclaration) {
          if (!compile_stmt_inner(init, bc, ctx)) return false;
        } else {
          if (!compile_expr_inner(init, bc, ctx)) return false;
          bc.emit(Opcode::Pop);
        }
      }
      size_t start = bc.size();
      if (cond != nullptr) {
        if (!compile_expr_inner(cond, bc, ctx)) return false;
      } else {
        bc.emit(Opcode::PushTrue); /* 恒真 */
      }
      size_t jump_end_at = bc.size();
      bc.emit(Opcode::JumpIfFalsePop);
      if (body != nullptr) {
        if (!compile_stmt_inner(body, bc, ctx)) return false;
      }
      if (update != nullptr) {
        if (!compile_expr_inner(update, bc, ctx)) return false;
        bc.emit(Opcode::Pop);
      }
      bc.emit(Opcode::Jump, static_cast<uint32_t>(start));
      bc.patch_jump(jump_end_at, bc.size());
      if (has_scope) bc.emit(Opcode::PopScope);
      return true;
    }

    case NodeType::ReturnStatement: {
      /* return [expr]：expr/undefined 压栈 → Ret（产生 interrupt）。
       * 选项是作用域属性：body scope 销毁自动恢复，return 无需显式关闭。 */
      if (!node->children.empty() && node->children[0] != nullptr) {
        if (!compile_expr_inner(node->children[0], bc, ctx)) return false;
      } else {
        bc.emit(Opcode::PushUndefined);
      }
      bc.emit(Opcode::Ret);
      return true;
    }

    default:
      /* break/continue/throw/try 等：后续 */
      return false;
  }
}

bool compile_stmt_list(ASTNode *node, Bytecode &bc, CompilerCtx *ctx) {
  for (ASTNode *child : node->children) {
    if (!compile_stmt_inner(child, bc, ctx)) return false;
  }
  return true;
}

}  // namespace

/* ========================================================================== */
/* 公开 API                                                                  */
/* ========================================================================== */

/* 编译表达式子树到 bc（M4 兼容入口）。返回 true 成功。
 * 表达式含函数时：主流程发 CreateFunction + Halt，函数体追加到尾部。 */
bool compile_expression(ASTNode *node, Bytecode &bc) {
  CompilerCtx ctx;
  if (!compile_expr_inner(node, bc, &ctx)) return false;
  bc.emit(Opcode::Halt); /* 隔离函数体区 */
  return compile_function_bodies(ctx, bc);
}

/* 编译单条语句到 bc。顶层是隐式块作用域（sema 变量清单发定义），
 * 末尾发 Halt 隔离函数体区。 */
bool compile_statement(ASTNode *node, Bytecode &bc) {
  if (node == nullptr) return false;
  CompilerCtx ctx;
  if (!compile_stmt_inner(node, bc, &ctx)) return false;
  bc.emit(Opcode::Halt);
  return compile_function_bodies(ctx, bc);
}

/* 编译 Program（顶层语句列表）到 bc。返回 true 成功。
 * 顶层（Program）是隐式块作用域：按 sema 变量清单发定义，末尾 Halt。 */
bool compile_program(ASTNode *node, Bytecode &bc) {
  if (node == nullptr) return false;
  /* 顶层指令序言：Program/Module 的 directives（"use strict" 等）。
   * 选项设到 current_scope（顶层=root_scope，脚本级严格；后续模块体系再迁
   * 到模块 globalScope）。选项是作用域属性，无需 DisableOption。 */
  compile_option_entry(node->directives, bc);
  compile_scope_entry(node, bc);
  CompilerCtx ctx;
  if (!compile_stmt_list(node, bc, &ctx)) return false;
  bc.emit(Opcode::Halt); /* 阻止 main 区顺序流进函数体区 */
  return compile_function_bodies(ctx, bc);
}

}  // namespace vm
}  // namespace neojs
