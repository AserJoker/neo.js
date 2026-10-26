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
  loops_.clear(); /* M5.1e-c：顶层程序不在循环/标签内 */
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
  loops_.clear(); /* M5.1e-c：单函数编译无外层循环上下文 */

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
  /* M5.4 async 函数标记（JS_NewFunction 单函数编译路径）。 */
  result.fn.is_async = fn.fn_is_async;
  return result;
}

/* -------------------------------------------------------------------------- */
/* 模块编译（M5.2a）                                                          */
/* -------------------------------------------------------------------------- */

CompileResult Compiler::compileModule(const AST &ast) {
  CompileResult result;
  failed_ = false;
  error_.clear();
  for (CompilerScope *s : scopes_) delete s;
  scopes_.clear();
  scope_ = nullptr;
  loops_.clear();
  module_import_map_.clear();
  module_imports_.clear();
  module_exports_.clear();
  module_deps_.clear();
  builder_ = BytecodeBuilder();

  /* 顶层作用域帧（模块顶层：捕获解析到此，再外层为全局）。 */
  CompilerScope *top = new CompilerScope;
  scopes_.push_back(top);
  scope_ = top;

  if (ast.root == nullptr) {
    builder_.push_op(Opcode::PushUndefined);
    builder_.ret();
    builder_.set_num_locals(scope_->next_slot);
    result.ok = true;
    result.fn = builder_.build();
    result.module_exports = std::move(module_exports_);
    result.module_deps = std::move(module_deps_);
    return result;
  }

  const auto &stmts = ast.root->children;

  /* 第 1 遍：收集 import/export 元数据（不发射代码）。
   *   - import 声明：注册 module_imports_/module_deps_，绑定名不入局部槽。
   *   - export 声明：登记 module_exports_；声明导出（export const/function/
   *     class）返回被包装声明，第 2 遍编译其体。
   *   - 顶层 FunctionDeclaration/ClassDeclaration（含 export default 的）：
   *     提升收集，第 2 遍体开头发射 MakeFunction+StoreVar。
   * 两遍分离保证：import 绑定在编译任意语句前已入表（emitLoadIdentifier
   * 检查），提升函数在引用前已实例化。 */
  std::vector<const ASTNode *> hoisted_decls;
  for (const ASTNode *stmt : stmts) {
    if (stmt == nullptr) continue;
    if (stmt->type == NodeType::ImportDeclaration) {
      if (!compileModuleImport(*stmt)) return failResult();
    } else if (stmt->type == NodeType::ExportNamedDeclaration ||
               stmt->type == NodeType::ExportDefaultDeclaration ||
               stmt->type == NodeType::ExportAllDeclaration) {
      const ASTNode *decl = nullptr;
      /* 第 1 遍：登记 module_exports_（默认）。 */
      if (!compileModuleExport(*stmt, &decl)) return failResult();
      if (decl != nullptr) {
        if (decl->type == NodeType::FunctionDeclaration ||
            decl->type == NodeType::ClassDeclaration) {
          /* 具名函数/类提升；匿名 default 导出（无模块绑定名）不提升，
           * 源码位置按表达式编译存入 __default__（pass 3 处理）。 */
          bool has_name = decl->fn_has_name && !decl->children.empty() &&
                          decl->children[0]->type == NodeType::Identifier;
          if (has_name) hoisted_decls.push_back(decl);
        }
      }
    } else if (stmt->type == NodeType::FunctionDeclaration ||
               stmt->type == NodeType::ClassDeclaration) {
      hoisted_decls.push_back(stmt);
    }
  }

  /* 第 2 遍：体开头发射提升的顶层函数/类声明（源码位置跳过，不挂 global）。
   *   FunctionDeclaration 提升（§16.2.5）：MakeFunction + 声明槽 + StoreVar。
   *   ClassDeclaration 提升：TDZ 语义（M5.2b 细化），本阶段按声明即实例化。 */
  for (const ASTNode *decl : hoisted_decls) {
    if (decl->type == NodeType::FunctionDeclaration) {
      if (!compileNestedFunction(*decl, /*is_declaration=*/true)) {
        return failResult();
      }
    } else if (decl->type == NodeType::ClassDeclaration) {
      if (!compileClass(*decl, /*is_declaration=*/true)) {
        return failResult();
      }
    }
  }

  /* 第 3 遍：按源码序编译非 import/export 语句与 export 声明的体。
   * import/export 本身不发射代码（纯编译期登记）。 */
  for (const ASTNode *stmt : stmts) {
    if (stmt == nullptr) continue;
    if (stmt->type == NodeType::ImportDeclaration ||
        stmt->type == NodeType::ExportAllDeclaration) {
      continue;
    }
    if (stmt->type == NodeType::ExportNamedDeclaration ||
        stmt->type == NodeType::ExportDefaultDeclaration) {
      const ASTNode *decl = nullptr;
      /* 第 3 遍：不重复登记（register_exports=false），只取被包装声明体。 */
      if (!compileModuleExport(*stmt, &decl, /*register_exports=*/false))
        return failResult();
      if (decl == nullptr) continue; /* export {a}：无体，纯登记 */
      if (decl->type == NodeType::FunctionDeclaration ||
          decl->type == NodeType::ClassDeclaration) {
        /* 提升已处理：仅具名 fn/class（pass 1 收集 hoisted_decls、pass 2 发射）。
         * 匿名 default 导出（export default function(){}）pass 1 未提升，须
         * 落入下方 __default__ 表达式分支编译（MakeFunction + StoreVar）。 */
        bool has_name = decl->fn_has_name && !decl->children.empty() &&
                        decl->children[0]->type == NodeType::Identifier;
        if (has_name) continue;
      }
      if (stmt->type == NodeType::ExportDefaultDeclaration) {
        /* export default expr / 匿名函数/类：声明 __default__ 槽，求值 expr
         * 存入（live binding，导出表 "default" -> "__default__"）。
         * 匿名 fn/class 是 FunctionExpression 语义，compileExpr 走表达式路径
         * 留栈顶（不 declareLocal）。具名 fn/class 已提升（pass 3 跳过）。 */
        bool is_decl = decl->type == NodeType::FunctionDeclaration ||
                       decl->type == NodeType::ClassDeclaration;
        bool has_name = is_decl && decl->fn_has_name && !decl->children.empty() &&
                        decl->children[0]->type == NodeType::Identifier;
        if (is_decl && has_name) continue; /* 已提升 */
        uint16_t def_slot = declareLocal("__default__");
        if (decl->type == NodeType::FunctionDeclaration) {
          /* 匿名默认导出函数：MakeFunction 留栈顶（表达式语义）。 */
          if (!compileNestedFunction(*decl, /*is_declaration=*/false)) {
            return failResult();
          }
        } else if (decl->type == NodeType::ClassDeclaration) {
          /* 匿名默认导出类：编译类表达式，构造器留栈顶。 */
          if (!compileClass(*decl, /*is_declaration=*/false)) {
            return failResult();
          }
        } else if (!compileExpr(*decl)) {
          return failResult();
        }
        builder_.store_var(def_slot);
        continue;
      }
      if (!compileStmt(*decl)) return failResult();
      continue;
    }
    if (stmt->type == NodeType::FunctionDeclaration ||
        stmt->type == NodeType::ClassDeclaration) {
      continue; /* 提升已处理 */
    }
    if (!compileStmt(*stmt)) return failResult();
  }

  /* 模块求值函数默认返回 undefined（顶层无 completion value 语义）。 */
  builder_.push_op(Opcode::PushUndefined);
  builder_.ret();
  builder_.set_num_locals(scope_->next_slot);

  result.ok = true;
  result.fn = builder_.build();
  result.module_exports = std::move(module_exports_);
  result.module_deps = std::move(module_deps_);
  return result;
}

/* M5.2a：处理单条 import 声明。
 * AST 布局（parseImportDeclaration）：
 *   children = specifiers..., source（Literal 字符串 = 模块名 'm'）。
 *   ImportSpecifier        { imported, local }（children[0]=local，imported 字段）
 *   ImportDefaultSpecifier { local }（children[0]=local）
 *   ImportNamespaceSpecifier { local }（children[0]=local）
 * 裸 import 'm'（副作用导入）：仅 source，无 specifier。
 * 每个绑定：注册 module_imports_ + module_import_map_（读走 LoadImport，
 * 写走 StoreImport 报 TypeError）。依赖 specifier 去重入 module_deps_。 */
bool Compiler::compileModuleImport(const ASTNode &node) {
  if (node.children.empty()) {
    fail("SyntaxError: import declaration without source");
    return false;
  }
  const ASTNode *source = node.children.back();
  if (source->type != NodeType::Literal || !source->literal.is_string) {
    fail("SyntaxError: invalid module specifier");
    return false;
  }
  const std::string &specifier = source->literal.str;

  /* 依赖（副作用导入也构成依赖，须加载求值）。 */
  bool seen = false;
  for (const std::string &d : module_deps_) {
    if (d == specifier) {
      seen = true;
      break;
    }
  }
  if (!seen) module_deps_.push_back(specifier);

  /* specifiers（不含 source）。 */
  for (size_t i = 0; i + 1 < node.children.size(); ++i) {
    const ASTNode *spec = node.children[i];
    if (spec == nullptr) continue;
    ModuleImportEntry entry;
    entry.specifier = specifier;
    if (spec->type == NodeType::ImportSpecifier) {
      /* { a as b }：export_name = imported（a），local_name = b。
       * children[0] = local（b），imported 字段 = a。 */
      entry.export_name = spec->imported;
      if (spec->children.empty()) {
        fail("SyntaxError: invalid import specifier");
        return false;
      }
      entry.local_name = spec->children[0]->name;
    } else if (spec->type == NodeType::ImportDefaultSpecifier) {
      /* import d from 'm'：export_name = "default"。 */
      entry.export_name = "default";
      if (spec->children.empty()) {
        fail("SyntaxError: invalid default import");
        return false;
      }
      entry.local_name = spec->children[0]->name;
    } else if (spec->type == NodeType::ImportNamespaceSpecifier) {
      /* import * as ns：命名空间对象（M5.2d，§10.4.6 模块 namespace 镜像）。
       * 标记 is_namespace，编译/运行期走 LoadImportNamespace 生成导出镜像。 */
      if (spec->children.empty()) {
        fail("SyntaxError: invalid namespace import");
        return false;
      }
      entry.export_name.clear();
      entry.is_namespace = true;
      entry.local_name = spec->children[0]->name;
    } else {
      fail("SyntaxError: invalid import specifier");
      return false;
    }
    /* 去重注册（同 local 名唯一，builder 按 local_name 去重并返回下标）。 */
    uint16_t idx = builder_.add_import(entry);
    module_imports_.push_back(entry);
    module_import_map_[entry.local_name] = idx;
  }
  return true;
}

/* M5.2a：处理单条 export 声明，登记 module_exports_。
 * 返回的 decl（若非空）为被包装的声明节点（export const/function/class /
 * export default expr），调用方继续编译其体；纯导出（export {a}）decl=nullptr。
 *
 * AST 布局（parseExportDeclaration）：
 *   ExportNamedDeclaration：
 *     - export {a, b as c} [from 'm']：children = ExportSpecifier..., [source]
 *       ExportSpecifier { children[0]=local, exported 字段 = 导出名 }。
 *     - export const/let/var / function / class：children[0] = 声明节点。
 *   ExportDefaultDeclaration：children[0] = expr / FunctionDeclaration /
 *     ClassDeclaration（默认导出，导出名 "default"）。
 *   ExportAllDeclaration：children = [source]（export * from 'm'）；
 *     exported 字段非空 = export * as ns from 'm'（M5.2b 细化，本阶段登记转发）。
 *
 * 本地导出：export_name -> local_name（绑定名 = 模块 env 中的名字）。
 * re-export：export {a} from 'm' / export * from 'm' —— export_name 转发自
 * 源模块（from_specifier 记录，运行期 LoadImport 沿转发链找）。 */
bool Compiler::compileModuleExport(const ASTNode &node, const ASTNode **decl,
                                   bool register_exports) {
  if (decl != nullptr) *decl = nullptr;

  if (node.type == NodeType::ExportDefaultDeclaration) {
    /* export default <expr|fn|class>：导出名 "default"。
     * 声明体（fn/class）由调用方编译（提升或 compileStmt）；表达式默认
     * 导出需要绑定名：合成隐藏名 "default" 的本地槽承载（export default
     * expr 语义 = 求值 expr 存到模块级 default 绑定，live binding）。 */
    if (node.children.empty()) {
      fail("SyntaxError: export default without expression");
      return false;
    }
    const ASTNode *expr = node.children[0];
    ModuleExportEntry e;
    e.export_name = "default";
    if (expr->type == NodeType::FunctionDeclaration ||
        expr->type == NodeType::ClassDeclaration) {
      if (expr->fn_has_name && !expr->children.empty() &&
          expr->children[0]->type == NodeType::Identifier) {
        /* 具名默认导出：export default function f(){}。绑定名 = f（提升）。 */
        e.local_name = expr->children[0]->name;
        if (register_exports) module_exports_.push_back(std::move(e));
        if (decl != nullptr) *decl = expr;
        return true;
      }
      /* 匿名默认导出：export default function(){}。无模块绑定名，按
       * 表达式求值存入隐藏绑定 __default__（源码位置编译，不提升）。 */
      e.local_name = "__default__";
      if (register_exports) module_exports_.push_back(std::move(e));
      if (decl != nullptr) *decl = expr;
      return true;
    }
    /* 表达式默认导出：合成隐藏绑定名（不与用户标识符冲突）。 */
    e.local_name = "__default__";
    if (register_exports) module_exports_.push_back(std::move(e));
    if (decl != nullptr) *decl = expr;
    return true;
  }

  if (node.type == NodeType::ExportAllDeclaration) {
    /* export * from 'm'：全部具名导出转发（不含 default）。
     * 运行期 findExport 对 export_all 条目：查源模块 exports 全表。
     * export * as ns from 'm'：导出名 = ns，转发源模块全部具名。 */
    if (node.children.empty()) {
      fail("SyntaxError: export * without source");
      return false;
    }
    const ASTNode *source = node.children[0];
    if (source->type != NodeType::Literal || !source->literal.is_string) {
      fail("SyntaxError: invalid module specifier");
      return false;
    }
    ModuleExportEntry e;
    if (!node.exported.empty()) {
      /* export * as ns from 'm'：导出名 = ns。 */
      e.export_name = node.exported;
      e.local_name = node.exported;
    } else {
      e.export_name.clear(); /* export_all 标记（转发全部具名） */
      e.local_name.clear();
    }
    e.from_specifier = source->literal.str;
    e.export_all = true;
    /* 依赖：export * from 'm' 同样构成加载依赖。
     * 注意：须在 push_back(std::move(e)) 之前处理——move 后 e.from_specifier
     * 为空串，此处会误把空 specifier 登记为依赖。 */
    bool seen = false;
    for (const std::string &d : module_deps_) {
      if (d == e.from_specifier) {
        seen = true;
        break;
      }
    }
    if (!seen) module_deps_.push_back(e.from_specifier);
    if (register_exports) module_exports_.push_back(std::move(e));
    return true;
  }

  /* ExportNamedDeclaration。 */
  if (node.type == NodeType::ExportNamedDeclaration) {
    if (node.children.empty()) {
      fail("SyntaxError: empty export declaration");
      return false;
    }
    const ASTNode *first = node.children[0];
    if (first->type == NodeType::ExportSpecifier) {
      /* export { a, b as c } [from 'm']：纯 re-export 或本地导出。 */
      std::string from_spec;
      if (node.children.size() >= 2 &&
          node.children.back()->type == NodeType::Literal &&
          node.children.back()->literal.is_string) {
        from_spec = node.children.back()->literal.str;
        bool seen = false;
        for (const std::string &d : module_deps_) {
          if (d == from_spec) {
            seen = true;
            break;
          }
        }
        if (!seen) module_deps_.push_back(from_spec);
      }
      for (size_t i = 0; i < node.children.size(); ++i) {
        const ASTNode *spec = node.children[i];
        if (spec->type != NodeType::ExportSpecifier) continue;
        if (spec->children.empty()) {
          fail("SyntaxError: invalid export specifier");
          return false;
        }
        ModuleExportEntry e;
        e.export_name = spec->exported;
        e.local_name = spec->children[0]->name;
        e.from_specifier = from_spec;
        if (register_exports) module_exports_.push_back(std::move(e));
      }
      return true;
    }
    /* export const/let/var / function / class：children[0] = 声明。
     * 声明体内绑定名即导出名（export function f 导出 f；export const x 导出
     * x）。多声明器（export const a=1,b=2）在 compileStmt 的 declareLocal 时
     * 无法回查——本阶段导出声明中每个 VariableDeclarator 的标识符登记。 */
    if (first->type == NodeType::VariableDeclaration) {
      for (const ASTNode *declarator : first->children) {
        if (declarator == nullptr ||
            declarator->type != NodeType::VariableDeclarator) {
          continue;
        }
        if (declarator->children.empty()) continue;
        const ASTNode *target = declarator->children[0];
        if (target->type != NodeType::Identifier) {
          fail("SyntaxError: export of destructuring not supported");
          return false;
        }
        ModuleExportEntry e;
        e.export_name = target->name;
        e.local_name = target->name;
        if (register_exports) module_exports_.push_back(std::move(e));
      }
    } else if (first->type == NodeType::FunctionDeclaration ||
               first->type == NodeType::ClassDeclaration) {
      std::string bind_name;
      if (first->fn_has_name && !first->children.empty() &&
          first->children[0]->type == NodeType::Identifier) {
        bind_name = first->children[0]->name;
      }
      if (bind_name.empty()) {
        fail("SyntaxError: exported declaration requires name");
        return false;
      }
      ModuleExportEntry e;
      e.export_name = bind_name;
      e.local_name = bind_name;
      if (register_exports) module_exports_.push_back(std::move(e));
    } else {
      fail("SyntaxError: invalid exported declaration");
      return false;
    }
    if (decl != nullptr) *decl = first;
    return true;
  }

  fail("SyntaxError: invalid export declaration");
  return false;
}

/* 函数体编译共享实现：解析参数（入本函数作用域帧）+ 编译 body。
 * 调用方已建立本函数作用域帧（scope_ 指向本函数）。 */
bool Compiler::compileFunctionBody(const ASTNode &fn) {
  /* 布局：children[start..n-2] = 参数，children[n-1] = body。
   * fn_has_name = true 时 children[0] 是函数名，start = 1；否则 start = 0。 */
  size_t start = fn.fn_has_name ? 1 : 0;
  size_t body_index = fn.children.size() - 1;

  /* 参数槽分配（M5.1a）：每个参数占用一个源槽（顺序入槽，VM 按位置
   * 复制实参 args[i] -> 槽 i）。
   *   Identifier        -> 该槽即绑定
   *   AssignmentPattern -> 源槽 = 绑定槽（undefined 时求默认值）
   *   RestElement       -> 源槽 = rest 数组（RestParam 收集）
   *   解构模式          -> 源槽（无名）+ 内部绑定槽（declarePatternBindings）
   * 两遍：先声明全部绑定（槽位稳定），再发射入口初始化代码。 */
  struct ParamInfo {
    const ASTNode *node = nullptr;
    uint16_t slot = 0; /* 源槽 */
  };
  std::vector<ParamInfo> params;
  for (size_t i = start; i < body_index; ++i) {
    const ASTNode *p = fn.children[i];
    ParamInfo info;
    info.node = p;
    if (p->type == NodeType::Identifier) {
      info.slot = declareLocal(p->name);
    } else if (p->type == NodeType::AssignmentPattern) {
      if (p->children.empty()) {
        fail("invalid default parameter");
        return false;
      }
      const ASTNode *target = p->children[0];
      if (target->type == NodeType::Identifier) {
        info.slot = declareLocal(target->name);
      } else {
        /* 解构 + 默认值：源槽 = 无名临时槽，内部绑定预声明。 */
        info.slot = scope_->next_slot++;
        if (!declarePatternBindings(*target)) {
          fail("invalid destructuring default parameter");
          return false;
        }
      }
    } else if (p->type == NodeType::RestElement) {
      if (p->children.empty()) {
        fail("invalid rest parameter");
        return false;
      }
      const ASTNode *target = p->children[0];
      if (target->type == NodeType::Identifier) {
        info.slot = declareLocal(target->name);
      } else {
        info.slot = scope_->next_slot++;
        if (!declarePatternBindings(*target)) {
          fail("invalid rest parameter");
          return false;
        }
      }
    } else if (p->type == NodeType::ObjectPattern ||
               p->type == NodeType::ArrayPattern) {
      info.slot = scope_->next_slot++;
      if (!declarePatternBindings(*p)) {
        fail("invalid destructuring parameter");
        return false;
      }
    } else {
      fail("invalid parameter");
      return false;
    }
    params.push_back(info);
  }

  /* 入口初始化（按参数顺序）：默认值 / rest / 解构。 */
  for (size_t i = 0; i < params.size(); ++i) {
    const ParamInfo &info = params[i];
    const ASTNode *p = info.node;
    if (p->type == NodeType::Identifier) continue;
    if (p->type == NodeType::AssignmentPattern) {
      const ASTNode *target = p->children[0];
      if (target->type == NodeType::Identifier) {
        /* x = default：x 为 undefined 时求默认值（§9.2.12 步骤 20）。
         *   [slot] Dup; JumpIfUndefined use_default; Pop; Jump end;
         *   use_default: [default]; end: StoreVar slot
         * JumpIfUndefined 弹栈判断（弹 Dup 副本），原值保留：非 undefined
         * 路径栈顶仍是原值；undefined 路径 Pop 掉原值后求默认值。 */
        BcLabel use_default = builder_.new_label();
        BcLabel end = builder_.new_label();
        builder_.load_var(info.slot);
        builder_.dup();
        builder_.jump_if_undefined(&use_default);
        builder_.pop();
        builder_.jump(&end);
        builder_.bind_label(use_default);
        if (!compileExpr(*p->children[1])) return false;
        builder_.bind_label(end);
        builder_.store_var(info.slot);
      } else {
        /* 解构 + 默认值：源 undefined 时先求默认值存入源槽，再解构。 */
        BcLabel use_default = builder_.new_label();
        BcLabel end = builder_.new_label();
        builder_.load_var(info.slot);
        builder_.dup();
        builder_.jump_if_undefined(&use_default);
        builder_.pop();
        builder_.jump(&end);
        builder_.bind_label(use_default);
        if (!compileExpr(*p->children[1])) return false;
        builder_.bind_label(end);
        builder_.store_var(info.slot);
        if (!compilePattern(*target, info.slot, /*is_declaration=*/true)) {
          return false;
        }
      }
    } else if (p->type == NodeType::RestElement) {
      /* ...rest：RestParam {i} 收集 args[i..] 为数组，存源槽。 */
      builder_.rest_param(static_cast<uint16_t>(i));
      builder_.store_var(info.slot);
      const ASTNode *target = p->children[0];
      if (target->type != NodeType::Identifier) {
        if (!compilePattern(*target, info.slot, /*is_declaration=*/true)) {
          return false;
        }
      }
    } else {
      /* 解构参数：从源槽解构绑定（f({a}) / f([x])）。 */
      if (!compilePattern(*p, info.slot, /*is_declaration=*/true)) {
        return false;
      }
    }
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
 * 调用方负责保存/恢复外层 builder_ 与作用域帧。
 * is_ctor = true：该子函数是 class 构造器，body 编译前发射私有字段
 * 初始化（M5.1d-c2）；派生类构造器不在此发射（super() 后由
 * compileCall 的 SuperCall 分支发射）。
 * inherit_private：是否继承外层 class 的私有字段访问权。class 方法/
 * 构造器/访问器传 true；嵌套函数按规范（§8.1.1.2.3）只有箭头函数
 * 继承（普通 function 有独立 PrivateEnvironment，不跨边界），
 * 故 compileNestedFunction 传 fn.type == ArrowFunctionExpression。 */
bool Compiler::compileChildFunction(const ASTNode &fn, FunctionBytecode *out,
                                    bool is_ctor, bool inherit_private) {
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

  /* M5.2b：模块顶层 import 绑定传播到子函数。
   * 嵌套函数（如 export function a(){ return b()+1; }）内部引用 import
   * 绑定 b 时，emitLoadIdentifier 命中 module_import_map_ 发射 LoadImport，
   * 但 LoadImport 下标指向子 builder 的 imports 表——子 builder 未注册则
   * 运行期报 "LoadImport index out of range"。这里按 module_imports_
   * 顺序注册（add_import 按 local_name 去重，下标与模块求值函数一致），
   * 子函数 LoadImport 因此可正常查源模块 env（选项 A 动态查表）。 */
  for (const ModuleImportEntry &imp : module_imports_) {
    builder_.add_import(imp);
  }

  /* M5.1d-c2 私有字段编译状态：普通函数（非箭头、非 class 成员）不能
   * 访问外层 class 的私有字段，置空 current_private_names_（编译期
   * resolvePrivateName 报 SyntaxError）。箭头函数/class 成员继承。 */
  const std::vector<std::string> *saved_names = current_private_names_;
  const std::vector<const ASTNode *> *saved_inits = current_field_inits_;
  const void *saved_instance = current_instance_fields_;
  std::vector<LoopContext> saved_loops = std::move(loops_);
  loops_.clear(); /* M5.1e-c：break/continue 不能跨函数边界 */
  bool saved_ctor = current_is_ctor_;
  bool saved_derived = current_is_derived_;
  bool saved_field_emitted = field_init_emitted_;
  if (!inherit_private) {
    current_private_names_ = nullptr;
    current_field_inits_ = nullptr;
    current_instance_fields_ = nullptr; /* 非 class 成员：实例字段表不可见 */
  }
  current_is_ctor_ = is_ctor;
  field_init_emitted_ = false;
  /* M5.1d-c2 构造器私有字段初始化时序（§9.2.2.4 步骤 5/8）：
   *   - 基类构造器：body 编译前发射字段初始化（this 已就绪，步骤 5）。
   *   - 派生类构造器：不能 body 前发射（this 未初始化）；显式 super()
   *     由 compileCall 的 SuperCall 分支在 super() 后触发（步骤 8，
   *     field_init_emitted_ = true）。
   *   - 派生类默认构造器（空 body 无 super()）：super() 后字段初始化须在
   *     body 编译前发射——compileFunctionBody 末尾已发射 Ret，body 后补发
   *     会成为死代码（字段槽永不创建）。 */
  const ASTNode *body_node =
      fn.children.empty() ? nullptr : fn.children[fn.children.size() - 1];
  bool derived_empty_ctor =
      is_ctor && current_is_derived_ && !field_init_emitted_ &&
      body_node != nullptr && body_node->type == NodeType::BlockStatement &&
      body_node->children.empty();
  bool ok = true;
  if (is_ctor && !current_is_derived_ && !field_init_emitted_) {
    /* M5.1d-c2/d 构造器：字段初始化（含 public 字段）由本函数发射，
     * current_instance_fields_ 在继承路径未置空（class 成员传
     * inherit_private=true），compileClass 已设置指向统一表。 */
    ok = emitInstanceFieldInits();
  }
  if (ok && derived_empty_ctor) {
    /* 派生类默认构造器：constructor(...args) { super(...args); }
     * RestParam {0} 收集全部实参为数组，SuperCallArray 转发（§15.7.2）。
     * 字段初始化必须在 super(...args) 之后（§9.2.2.4 步骤 8）：super 前
     * this 未建立。 */
    builder_.rest_param(0);
    builder_.super_call_array();
    builder_.pop(); /* 丢弃 super 结果（this 已由 frame_this 维护） */
    ok = emitInstanceFieldInits();
  }
  if (ok) ok = compileFunctionBody(fn);
  current_private_names_ = saved_names;
  current_field_inits_ = saved_inits;
  current_instance_fields_ = saved_instance;
  current_is_ctor_ = saved_ctor;
  current_is_derived_ = saved_derived;
  field_init_emitted_ = saved_field_emitted;
  loops_ = std::move(saved_loops);

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
  /* M5.1d-b 箭头函数：super 上下文继承标记（MakeFunction 时复制外层
   * HomeObject，使箭头函数内 super 指向外层方法的 HomeObject）。 */
  child_fn.is_arrow = (fn.type == NodeType::ArrowFunctionExpression);
  /* M5.4 async 函数：async 标记（VM 调用时创建 Promise + await 挂起恢复）。 */
  child_fn.is_async = fn.fn_is_async;
  /* M5.1d-c2 私有字段：class 构造器/方法/访问器携带 class 词法可见的
   * 字段全名表（含 '#'，LoadPrivate/StorePrivate/HasPrivate 引用其下标）。 */
  if (inherit_private && current_private_names_ != nullptr) {
    child_fn.private_names = *current_private_names_;
  }
  *out = std::move(child_fn);
  return true;
}

/* 嵌套函数编译（M2.5）：编译子函数字节码加入当前 builder 的 nested 表，
 * 发射 MakeFunction 指令。声明时挂到函数名局部变量；表达式时留栈顶。 */
bool Compiler::compileNestedFunction(const ASTNode &fn, bool is_declaration) {
  FunctionBytecode child;
  /* M5.1d-c2：嵌套函数是否继承外层 class 私有字段访问权——仅箭头函数
   * 继承（普通函数有独立 PrivateEnvironment，§8.1.1.2.3）。 */
  const bool inherit_private =
      (fn.type == NodeType::ArrowFunctionExpression);
  if (!compileChildFunction(fn, &child, /*is_ctor=*/false, inherit_private)) {
    return false;
  }

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

/* 编译单个类方法/构造器函数（M5.1d-a）：
 *   fn       方法函数表达式（FunctionExpression，fn_has_name=false）
 *   fn_name  函数名（方法键名；构造器用类名）
 *   is_ctor  true = 构造器（is_constructor 标志）
 * 产出子函数字节码加入 nested 表并发射 MakeFunction，函数对象留栈顶。 */
bool Compiler::compileClassMemberFunction(const ASTNode &fn,
                                          const std::string &fn_name,
                                          bool is_ctor) {
  FunctionBytecode child;
  /* M5.1d-c2：class 成员（构造器/方法/访问器）继承 class 私有字段访问权。 */
  if (!compileChildFunction(fn, &child, is_ctor, /*inherit_private=*/true)) {
    return false;
  }
  child.name = fn_name;
  if (is_ctor) {
    child.is_constructor = true;
  } else {
    child.is_method = true;
  }
  uint16_t index = static_cast<uint16_t>(builder_.add_nested(std::move(child)));
  builder_.make_function(index);
  return true;
}

/* 编译类元素（M5.1d-a）：把方法/访问器挂到目标对象。
 * 目标：static -> 构造器（栈顶，Dup 保留）；实例 -> proto 槽。 */
bool Compiler::compileClassElementDef(const ASTNode &prop, uint16_t proto_slot,
                                      bool is_static) {
  if (prop.children.empty()) {
    fail("invalid class element");
    return false;
  }
  /* 键：计算键 [expr] 运行期求值；否则编译为常量串。 */
  std::string key;
  bool computed_key = prop.prop.is_computed;
  if (!computed_key && !objectKeyToString(prop.children[0], &key)) {
    fail("invalid class element key");
    return false;
  }
  const PropertyKind kind = prop.prop.kind;
  if (prop.children.size() < 2) {
    fail("invalid class element (missing function)");
    return false;
  }
  const ASTNode *fn = prop.children[1];

  if (is_static) {
    builder_.dup(); /* 构造器在栈顶，dup 保留原值 */
  } else {
    builder_.load_var(proto_slot);
  }
  if (computed_key) {
    if (!compileExpr(*prop.children[0])) return false;
  } else {
    builder_.push_const(key);
  }
  if (!compileClassMemberFunction(*fn, key, /*is_ctor=*/false)) return false;
  /* M5.1d-b super：设置方法 [[HomeObject]] = 定义时所在对象
   * （实例方法 = proto，static = 构造器）。栈 [obj, key, fn] 原地。 */
  builder_.set_home_object();
  if (kind == PropertyKind::Method) {
    builder_.define_prop();
  } else {
    builder_.define_accessor(kind == PropertyKind::Set ? 1 : 0);
  }
  builder_.pop(); /* 丢弃定义结果；static 时留下 dup 的构造器 */
  return true;
}

/* 实例字段统一表条目（M5.1d-d）：private + public 混合，源码序。
 * 构造器内按序初始化（基类 body 前 / 派生类 super() 后，§9.2.2.4）。
 * - is_private：InitPrivate {priv_idx}（私有槽建槽；priv_idx 指向 private_names）
 * - !is_private：StoreProp pub_key（public 字段 this.key = init）
 * init 为 FunctionExpression 时 = 私有方法（编译方法函数入槽）。 */
struct InstanceFieldEntry {
  bool is_private = false;
  size_t priv_idx = 0;      /* 私有字段：private_names 下标 */
  std::string pub_key;      /* public 字段：属性名（静态键） */
  const ASTNode *computed_key = nullptr; /* 计算键字段 [expr]（M5.1e-d）；非空时运行期求键 */
  const ASTNode *init = nullptr; /* 初始化表达式（nullptr -> undefined） */
};

/* static 元素（M5.1d-d）：类定义完成后按源码序执行，this = 构造器。
 * kind：Field（static 字段 Property）/ Block（static 块 StaticBlock）。
 * 字段可为 public（StoreProp 到构造器）或 private（InitPrivate 建槽）。 */
struct StaticElement {
  const ASTNode *node = nullptr; /* Property（字段）或 StaticBlock */
  bool is_private = false;
  size_t priv_idx = 0;
  std::string pub_key;
  const ASTNode *computed_key = nullptr; /* 计算键字段 [expr]（M5.1e-d） */
};

/* class 编译（M5.1d-a）：
 * children 布局：[name?, extends?, element...]（见 parser_stmt.cc parseClassTail）。
 *
 * 产物 = 构造器函数（栈顶；声明时挂类名局部变量）。执行序列：
 *   NewObject; StoreVar proto_tmp          ; prototype 对象
 *   [extends] 求值基类 -> StoreVar base_tmp
 *   MakeFunction ctor（构造器方法体或默认空构造器）
 *   Dup; "prototype"; proto_tmp; DefineProp; Pop  ; ctor.prototype = proto
 *   实例方法：LoadVar proto_tmp; key; fn; DefineProp/Accessor; Pop
 *   static 方法：Dup; key; fn; DefineProp/Accessor; Pop
 *   [extends] proto_tmp.[[Prototype]]=base.prototype; ctor.[[Prototype]]=base
 *   结果 = 构造器（栈顶） */
bool Compiler::compileClass(const ASTNode &node, bool is_declaration) {
  std::string class_name;
  size_t index = 0;
  if (!node.children.empty() && node.children[0]->type == NodeType::Identifier) {
    class_name = node.children[0]->name;
    index = 1;
  }
  const ASTNode *base = nullptr;
  if (index < node.children.size() &&
      node.children[index]->type != NodeType::Property) {
    base = node.children[index];
    ++index;
  }

  /* 类名作用域：class 声明时类名绑定。class 体内引用类名（自引用）留
   * M5.1d 后续阶段（需类名在构造器/方法作用域可见）。 */
  uint16_t class_slot = 0;
  if (is_declaration && !class_name.empty()) {
    class_slot = declareLocal(class_name);
  }

  /* 求值 extends 基类（若有），暂存槽。 */
  uint16_t base_slot = 0;
  if (base != nullptr) {
    if (!compileExpr(*base)) return false;
    base_slot = scope_->next_slot++;
    builder_.store_var(base_slot);
  }

  /* 1. prototype 对象。 */
  builder_.new_object();
  uint16_t proto_slot = scope_->next_slot++;
  builder_.store_var(proto_slot);

  /* 2. 编译构造器函数。
   *    找 key == "constructor" 的 Method 元素作为构造器体；否则默认空构造器。
   *    M5.1d-c2 私有字段：先收集 class 全部私有字段（#x 声明，含 static），
   *    设置 current_private_names_/current_field_inits_（class 体编译期间
   *    有效：构造器/方法/访问器/箭头函数内 #x 引用解析到该表）。 */
  const ASTNode *ctor_fn = nullptr;
  std::vector<const ASTNode *> elements;
  std::vector<std::string> private_names;      /* 私有字段全名表（含 '#'，按声明序） */
  std::vector<const ASTNode *> private_inits;  /* 私有字段初始化表达式（与表一一对应） */
  std::vector<InstanceFieldEntry> instance_fields; /* 实例字段统一表（源码序） */
  std::vector<StaticElement> static_elems;        /* static 元素（源码序） */
  for (size_t i = index; i < node.children.size(); ++i) {
    const ASTNode *elem = node.children[i];
    if (elem == nullptr) continue;
    if (elem->type == NodeType::StaticBlock) {
      /* M5.1d-d static 块：static { stmts }。 */
      StaticElement se;
      se.node = elem;
      static_elems.push_back(se);
      continue;
    }
    if (elem->type != NodeType::Property) continue;
    elements.push_back(elem);
    if (elem->prop.kind == PropertyKind::Method && !elem->prop.is_static &&
        elem->children.size() >= 2 &&
        elem->children[0]->type == NodeType::Identifier &&
        elem->children[0]->name == "constructor") {
      ctor_fn = elem->children[1];
    }
    /* M5.1d-c2/d 类字段/私有方法：
     *   - 字段（kind=Init）：key = PrivateIdentifier（私有）或
     *     Identifier/Literal（public），children[1] = 初始化表达式（或空）。
     *   - 私有方法/访问器（kind=Method/Get/Set + PrivateIdentifier 键）：
     *     方法函数存入私有槽（children[1]）。
     *   - 私有元素：收集进 private_names/private_inits（建槽表），实例字段
     *     统一表加私有条目；static 私有元素进 static 元素（构造器建槽）。
     *   - public 字段（实例/static）：分别进 instance_fields / static_elems。
     * 键重复（public 重名 / 私有重名）-> 编译错误。 */
    const bool is_field = elem->prop.kind == PropertyKind::Init;
    const bool is_private_elem =
        !elem->children.empty() &&
        elem->children[0]->type == NodeType::PrivateIdentifier &&
        (is_field || elem->prop.kind == PropertyKind::Method ||
         elem->prop.kind == PropertyKind::Get ||
         elem->prop.kind == PropertyKind::Set);
    if (is_field || is_private_elem) {
      const ASTNode *key = elem->children[0];
      const ASTNode *init =
          elem->children.size() >= 2 ? elem->children[1] : nullptr;
      if (key->type == NodeType::PrivateIdentifier) {
        const std::string full = "#" + key->name;
        for (const std::string &pn : private_names) {
          if (pn == full) {
            fail("SyntaxError: duplicate private field " + full);
            return false;
          }
        }
        size_t priv_idx = private_names.size();
        private_names.push_back(full);
        private_inits.push_back(init);
        /* 私有方法/访问器：非字段，跳过字段初始化表（构造器经
         * emitInstanceFieldInits 编译方法函数入槽）。 */
        if (!is_field) {
          InstanceFieldEntry fe;
          fe.is_private = true;
          fe.priv_idx = priv_idx;
          fe.init = init;
          if (elem->prop.is_static) {
            StaticElement se;
            se.node = elem;
            se.is_private = true;
            se.priv_idx = priv_idx;
            static_elems.push_back(se);
          } else {
            instance_fields.push_back(fe);
          }
          continue;
        }
        if (elem->prop.is_static) {
          /* static 私有字段：static_init 里构造器建槽。 */
          StaticElement se;
          se.node = elem;
          se.is_private = true;
          se.priv_idx = priv_idx;
          static_elems.push_back(se);
        } else {
          InstanceFieldEntry fe;
          fe.is_private = true;
          fe.priv_idx = priv_idx;
          fe.init = init;
          instance_fields.push_back(fe);
        }
      } else {
        /* public 字段：键为 Identifier/Literal（静态键）或 [expr]（计算键，
         * 运行期求键，M5.1e-d）。计算键存表达式，初始化时 DefineProp。 */
        std::string pkey;
        const ASTNode *computed_key = nullptr;
        if (elem->prop.is_computed) {
          /* 计算键 [expr]：key 即表达式节点（运行期求键）。 */
          computed_key = key;
        } else if (!objectKeyToString(key, &pkey)) {
          fail("invalid public field key");
          return false;
        }
        if (elem->prop.is_static) {
          StaticElement se;
          se.node = elem;
          se.pub_key = pkey;
          se.computed_key = computed_key;
          static_elems.push_back(se);
        } else {
          InstanceFieldEntry fe;
          fe.is_private = false;
          fe.pub_key = pkey;
          fe.computed_key = computed_key;
          fe.init = init;
          instance_fields.push_back(fe);
        }
      }
    }
  }
  const std::vector<std::string> *saved_names = current_private_names_;
  const std::vector<const ASTNode *> *saved_inits = current_field_inits_;
  const void *saved_instance = current_instance_fields_;
  const void *saved_static_elems = current_static_elems_;
  bool saved_derived = current_is_derived_;
  current_private_names_ = &private_names;
  current_field_inits_ = &private_inits;
  current_instance_fields_ = &instance_fields;
  current_static_elems_ = nullptr; /* static_init 编译时单独设置 */
  current_is_derived_ = (base != nullptr);
  if (ctor_fn != nullptr) {
    if (!compileClassMemberFunction(*ctor_fn, class_name, /*is_ctor=*/true)) {
      current_private_names_ = saved_names;
      current_field_inits_ = saved_inits;
      current_instance_fields_ = saved_instance;
      current_static_elems_ = saved_static_elems;
      current_is_derived_ = saved_derived;
      return false;
    }
  } else {
    /* 默认构造器：无参 + 空 body。复用 compileChildFunction（is_ctor=true）
     * 使其统一发射字段初始化（基类 body 前 / 派生类 body 后 super() + 初始化）
     * 并编译各字段的 init 表达式（§12.2.9）。用栈上临时节点表示空函数
     *（FunctionExpression，children = [空 BlockStatement]）。 */
    ASTNode empty_fn(NodeType::FunctionExpression);
    empty_fn.fn_has_name = false;
    ASTNode empty_body(NodeType::BlockStatement);
    empty_fn.children.push_back(&empty_body);
    FunctionBytecode child;
    if (!compileChildFunction(empty_fn, &child, /*is_ctor=*/true,
                              /*inherit_private=*/true)) {
      current_private_names_ = saved_names;
      current_field_inits_ = saved_inits;
      current_instance_fields_ = saved_instance;
      current_static_elems_ = saved_static_elems;
      current_is_derived_ = saved_derived;
      return false;
    }
    child.name = class_name;
    child.is_constructor = true;
    uint16_t ctor_index =
        static_cast<uint16_t>(builder_.add_nested(std::move(child)));
    builder_.make_function(ctor_index);
  }
  /* current_private_names_/current_field_inits_/current_is_derived_ 保持指向
   * 本 class 的私有字段状态，直到全部元素（方法/访问器，M5.1d-c2 需在编译
   * 方法体时解析 #x 引用）编译完成；恢复推迟到元素循环之后（见步骤 4 尾部）。 */

  /* 3. ctor.prototype = proto。栈上 [ctor]。
   *    M5.1d-b super：构造器 [[HomeObject]] = 构造器自身（static 语义），
   *    super.x 从 ctor.[[Prototype]] = base 读取（extends 后）。 */
  builder_.dup();
  builder_.set_home_object_self();
  /* set_home_object_self（arg=1）VM 端已把 obj 槽覆盖为 fn 并去重：
   * 栈 [ctor] -> [ctor]（self 形式 [obj, fn] -> [fn]），无需再 pop。 */
  builder_.dup();
  builder_.push_const("prototype");
  builder_.load_var(proto_slot);
  builder_.define_prop();
  builder_.pop();

  /* 4. 实例方法挂到 proto；static 方法挂到构造器（栈顶 dup）。
   *    构造器元素本身不重复挂载（constructor 是构造器，不是 prototype 方法）。
   *    M5.1d-c2：私有字段声明（PropertyKind::Init）不在 prototype 上定义——
   *    初始化在构造器里经 LoadPrivate/StorePrivate 发射（emitPrivateFieldInits），
   *    存储于对象私有槽表，此处跳过。 */
  for (const ASTNode *elem : elements) {
    /* M5.1d-c2：私有元素（字段声明 + 私有方法）不挂 prototype——
     * 私有字段在构造器建槽；私有方法值同样由 emitPrivateFieldInits 编译
     * 方法函数并存入槽（LoadPrivate 取方法值）。此处跳过。 */
    if (elem->prop.kind == PropertyKind::Init) continue;
    if (!elem->children.empty() &&
        elem->children[0]->type == NodeType::PrivateIdentifier) {
      continue; /* 私有方法 */
    }
    const ASTNode *elem_fn =
        (elem->children.size() >= 2) ? elem->children[1] : nullptr;
    if (ctor_fn != nullptr && elem_fn == ctor_fn) continue;
    if (!compileClassElementDef(*elem, proto_slot, elem->prop.is_static)) {
      current_private_names_ = saved_names;
      current_field_inits_ = saved_inits;
      current_instance_fields_ = saved_instance;
      current_static_elems_ = saved_static_elems;
      current_is_derived_ = saved_derived;
      return false;
    }
  }
  /* class 全部元素编译完成，恢复外层私有字段状态。 */
  current_private_names_ = saved_names;
  current_field_inits_ = saved_inits;
  current_instance_fields_ = saved_instance;
  current_static_elems_ = saved_static_elems;
  current_is_derived_ = saved_derived;

  /* 5. extends：proto_tmp.[[Prototype]] = base.prototype；
   *          ctor.[[Prototype]] = base。 */
  if (base != nullptr) {
    builder_.load_var(proto_slot);
    builder_.load_var(base_slot);
    builder_.load_prop("prototype");
    builder_.set_prototype();
    builder_.pop(); /* 丢弃 proto（SetPrototype 压回 obj） */
    builder_.dup();
    builder_.load_var(base_slot);
    builder_.set_prototype();
    builder_.pop(); /* 丢弃 dup 的构造器 */
  }

  /* 6. static 初始化（M5.1d-d）：类定义完成后按源码序执行 static 字段
   *    与 static 块（this = 构造器）。合成 static_init 函数调用，
   *    栈 [ctor] 保留（Dup 后调用）。
   *    注意：此处在元素编译完成、状态已恢复之后，故重新指向本 class
   *    的各类字段表（static_init 编译 static 块内 #x 引用与 static 私有
   *    字段初始化需解析到本 class 的私有字段表）。 */
  if (!static_elems.empty()) {
    current_static_elems_ = &static_elems;
    current_private_names_ = &private_names;
    current_field_inits_ = &private_inits;
    current_instance_fields_ = &instance_fields;
    builder_.dup();
    if (!emitStaticInit()) {
      current_static_elems_ = saved_static_elems;
      current_private_names_ = saved_names;
      current_field_inits_ = saved_inits;
      current_instance_fields_ = saved_instance;
      return false;
    }
    current_static_elems_ = saved_static_elems;
    current_private_names_ = saved_names;
    current_field_inits_ = saved_inits;
    current_instance_fields_ = saved_instance;
  }

  /* 7. 结果：构造器留栈顶。声明时挂类名局部变量。 */
  if (is_declaration && !class_name.empty()) {
    builder_.store_var(class_slot);
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

/* -------------------------------------------------------------------------- */
/* 私有字段（M5.1d-c2）                                                        */
/* -------------------------------------------------------------------------- */

/* 解析私有字段引用：字段名（不含 '#'）-> current_private_names_ 下标。
 * 不在表内（未声明或超出 class 词法作用域）-> 编译错误返回 -1。
 * 注意：本函数在字段名查表前先构造全名（含 '#'）。 */
int Compiler::resolvePrivateName(const std::string &name) {
  if (current_private_names_ == nullptr) {
    fail("SyntaxError: Private field '#" + name +
         "' must be declared in an enclosing class");
    return -1;
  }
  const std::string full = "#" + name;
  for (size_t i = 0; i < current_private_names_->size(); ++i) {
    if ((*current_private_names_)[i] == full) {
      return static_cast<int>(i);
    }
  }
  fail("SyntaxError: Private field '#" + name +
       "' must be declared in an enclosing class");
  return -1;
}

/* 发射构造器实例字段初始化（M5.1d-c2/d）：逐字段 LoadThis; init|undefined;
 * InitPrivate/StoreProp（统一表 current_instance_fields_，private+public 源码序）。
 * 仅构造器调用（current_is_ctor_ 保证；非构造器函数无此序列）。
 * 字段初始化表达式在类构造时求值（this 已就绪，基类已建好私有槽）。
 * 私有字段/私有方法：InitPrivate {priv_idx}（建槽）；public 字段：StoreProp。 */
bool Compiler::emitInstanceFieldInits() {
  if (current_instance_fields_ != nullptr) {
    const std::vector<InstanceFieldEntry> &fields =
        *static_cast<const std::vector<InstanceFieldEntry> *>(
            current_instance_fields_);
    for (const InstanceFieldEntry &fe : fields) {
      builder_.load_this();
      if (fe.is_private) {
        /* 私有字段/私有方法。 */
        if (fe.init != nullptr &&
            fe.init->type == NodeType::FunctionExpression) {
          /* 私有方法 #m(){}：编译方法函数（MakeFunction 留栈顶）存入槽。
           * 方法名为字段名（不含 '#'）；HomeObject 暂不设置（私有方法内
           * super 访问属 M5.1d-b 组合场景，本阶段不测试）。 */
          std::string mname = (*current_private_names_)[fe.priv_idx];
          if (!mname.empty() && mname[0] == '#') mname = mname.substr(1);
          if (!compileClassMemberFunction(*fe.init, mname, /*is_ctor=*/false)) {
            return false;
          }
        } else if (fe.init != nullptr) {
          if (!compileExpr(*fe.init)) return false;
        } else {
          builder_.push_op(Opcode::PushUndefined);
        }
        /* InitPrivate：首次创建槽（与 StorePrivate 的「必须已声明」区分）。 */
        builder_.init_private(static_cast<uint16_t>(fe.priv_idx));
        builder_.pop(); /* 丢弃字段值 */
        continue;
      }
      /* public 字段：this.key = init 或 this[key] = init（计算键）。
       * StoreProp 约定 [obj, val]（key 内嵌）；DefineProp 约定 [obj, key, val]。 */
      builder_.load_this();
      if (fe.computed_key != nullptr) {
        if (!compileExpr(*fe.computed_key)) return false; /* this, key */
        if (fe.init != nullptr) {
          if (!compileExpr(*fe.init)) return false;
        } else {
          builder_.push_op(Opcode::PushUndefined);
        }
        builder_.define_prop(); /* this, key, val -> val（运行期 ToPropertyKey） */
      } else {
        if (fe.init != nullptr) {
          if (!compileExpr(*fe.init)) return false;
        } else {
          builder_.push_op(Opcode::PushUndefined);
        }
        builder_.store_prop(fe.pub_key); /* this, val -> val */
      }
      builder_.pop(); /* 丢弃字段值（字段初始化是语句） */
    }
  }
  field_init_emitted_ = true;
  return true;
}

/* 编译并执行 static 初始化（M5.1d-d）：static 字段 + static 块，源码序，
 * this = 构造器（§15.7.14 ClassStaticBlockDefinition / §15.7.2.8 步骤 27）。
 *
 * 实现：合成 static_init 函数（无参空 body），经 compileChildFunction +
 * MakeFunction 实例化，this 绑定在调用时传入。static_init 继承当前 class
 * 私有字段访问权（inherit_private=true），其字节码按 static_elems 源码序
 * 发射：
 *   - static 块：编译块内语句（this 经 LoadThis 访问）。
 *   - static 字段（public）：this.key = init（init 缺省 undefined）。
 *   - static 字段（private）：this.#x = init（InitPrivate 建槽）。
 * 编译方式：static_init 的 body 是「虚拟空 Block」——compileChildFunction
 * 建立子函数 builder 后，我们直接在其上发射元素字节码（不经过 body 的
 * compileFunctionBody），随后补 PushUndefined; Ret 收尾。
 * 调用：外层 builder 栈顶 = 构造器（this_obj），Dup 后 CallMethod 以
 * this=构造器调用（栈 [ctor, fn, args...]），结果 Pop。 */
bool Compiler::emitStaticInit() {
  if (current_static_elems_ == nullptr) return true;
  const std::vector<StaticElement> &elems =
      *static_cast<const std::vector<StaticElement> *>(current_static_elems_);

  /* 建立 static_init 子函数作用域帧。 */
  CompilerScope *child = new CompilerScope;
  child->parent = scope_;
  scopes_.push_back(child);
  scope_ = child;

  /* 子函数自己的 builder。 */
  BytecodeBuilder saved_builder;
  saved_builder = std::move(builder_);
  builder_ = BytecodeBuilder("static_init");

  /* 保存/恢复状态：static_init 是 class 词法范围的合成函数，继承私有字段
   * 访问权（current_private_names_/current_field_inits_/current_instance_fields_
   * 保持指向本 class 的表）；current_is_ctor_ 置 false（非构造器）。 */
  bool saved_ctor = current_is_ctor_;
  bool saved_derived = current_is_derived_;
  bool saved_field_emitted = field_init_emitted_;
  current_is_ctor_ = false;
  current_is_derived_ = false;
  field_init_emitted_ = false;

  /* 逐元素发射 static 初始化字节码（源码序）。 */
  bool ok = true;
  for (const StaticElement &se : elems) {
    if (se.node->type == NodeType::StaticBlock) {
      /* static 块：块内语句按序编译（this 经 LoadThis 访问构造器）。 */
      for (const ASTNode *stmt : se.node->children) {
        if (stmt == nullptr) continue;
        if (!compileStmt(*stmt)) { ok = false; break; }
      }
      if (!ok) break;
    } else {
      /* static 字段：this = 构造器。
       *   public：this.key = init（缺省 undefined），StoreProp 消费 this 与值。
       *   private：this.#x = init（InitPrivate 建槽）。
       *   计算键 public：this[key] = init（DefineProp，运行期求键，M5.1e-d）。 */
      builder_.load_this();
      const ASTNode *init =
          se.node->children.size() >= 2 ? se.node->children[1] : nullptr;
      if (se.is_private) {
        if (init != nullptr) {
          if (init->type == NodeType::FunctionExpression) {
            /* static 私有方法：编译方法函数入槽。 */
            std::string mname = (*current_private_names_)[se.priv_idx];
            if (!mname.empty() && mname[0] == '#') mname = mname.substr(1);
            if (!compileClassMemberFunction(*init, mname, /*is_ctor=*/false)) {
              ok = false;
              break;
            }
          } else if (!compileExpr(*init)) {
            ok = false;
            break;
          }
        } else {
          builder_.push_op(Opcode::PushUndefined);
        }
        builder_.init_private(static_cast<uint16_t>(se.priv_idx));
        builder_.pop(); /* 丢弃字段值 */
        continue;
      }
      /* public 字段：this.key = init 或 this[key] = init（计算键）。
       * StoreProp 约定 [obj, val]（key 内嵌）；DefineProp 约定 [obj, key, val]。 */
      builder_.load_this();
      if (se.computed_key != nullptr) {
        if (!compileExpr(*se.computed_key)) {
          ok = false;
          break;
        }
        if (init != nullptr) {
          if (!compileExpr(*init)) {
            ok = false;
            break;
          }
        } else {
          builder_.push_op(Opcode::PushUndefined);
        }
        builder_.define_prop(); /* this, key, val -> val（运行期 ToPropertyKey） */
      } else {
        if (init != nullptr) {
          if (!compileExpr(*init)) {
            ok = false;
            break;
          }
        } else {
          builder_.push_op(Opcode::PushUndefined);
        }
        builder_.store_prop(se.pub_key); /* this, val -> val */
      }
      builder_.pop(); /* 丢弃字段值 */
    }
  }

  /* 收尾：默认返回 undefined。 */
  builder_.push_op(Opcode::PushUndefined);
  builder_.ret();

  /* 恢复外层状态。 */
  current_is_ctor_ = saved_ctor;
  current_is_derived_ = saved_derived;
  field_init_emitted_ = saved_field_emitted;

  /* 产出子函数字节码并入外层 builder。 */
  builder_.set_num_locals(scope_->next_slot);
  FunctionBytecode child_fn = builder_.build();
  /* M5.1d-d：static_init 继承 class 私有字段访问权，LoadPrivate/
   * InitPrivate/StorePrivate 的字段全名表须随 child_fn 携带（编译期
   * current_private_names_ 指向本 class 表，运行期经函数私有表索引）。 */
  if (current_private_names_ != nullptr) {
    child_fn.private_names = *current_private_names_;
  }
  builder_ = std::move(saved_builder);
  scope_ = child->parent;
  scopes_.pop_back();
  delete child;

  if (!ok) return false;
  child_fn.name = "static_init";
  uint16_t index =
      static_cast<uint16_t>(builder_.add_nested(std::move(child_fn)));
  builder_.make_function(index);
  builder_.call_method(0); /* [ctor, fn] -> 调用，this = ctor */
  builder_.pop();          /* 丢弃 static_init 返回值 */
  return true;
}

/* -------------------------------------------------------------------------- */
/* 解构编译（M5.1a）                                                          */
/* -------------------------------------------------------------------------- */

/* 预声明解构模式中的全部绑定名（声明语义：槽位分配稳定）。 */
bool Compiler::declarePatternBindings(const ASTNode &pattern) {
  if (pattern.type == NodeType::ObjectPattern) {
    for (const ASTNode *member : pattern.children) {
      if (member == nullptr) continue;
      if (member->type == NodeType::SpreadElement) {
        if (!member->children.empty() &&
            !declarePatternBindings(*member->children[0]))
          return false;
        continue;
      }
      if (member->type != NodeType::Property || member->children.empty()) {
        return false;
      }
      /* 简写 { p }：单 child（key 即 value），value = key。 */
      const ASTNode *value =
          member->children.size() >= 2 ? member->children[1] : member->children[0];
      if (!declarePatternBindings(*value)) return false;
    }
    return true;
  }
  if (pattern.type == NodeType::ArrayPattern) {
    for (const ASTNode *elem : pattern.children) {
      if (elem == nullptr) continue;
      if (elem->type == NodeType::SpreadElement) {
        if (!elem->children.empty() &&
            !declarePatternBindings(*elem->children[0]))
          return false;
        continue;
      }
      if (!declarePatternBindings(*elem)) return false;
    }
    return true;
  }
  if (pattern.type == NodeType::Identifier) {
    declareLocal(pattern.name);
    return true;
  }
  if (pattern.type == NodeType::AssignmentPattern) {
    /* 默认值：绑定左侧目标。 */
    if (!pattern.children.empty()) return declarePatternBindings(*pattern.children[0]);
    return false;
  }
  if (pattern.type == NodeType::RestElement) {
    if (!pattern.children.empty()) return declarePatternBindings(*pattern.children[0]);
    return false;
  }
  return false;
}

/* 绑定单个目标（值已在栈顶）：
 *   Identifier      -> 声明 declareLocal/store 或赋值 resolveAssignTarget/store
 *   AssignmentPattern -> 默认值：undefined 时求默认表达式
 *   嵌套模式        -> 存临时槽后递归 compilePattern
 * 绑定后值仍留栈顶（调用方 Pop 或复用）。 */
bool Compiler::compileBindTarget(const ASTNode &target, bool is_declaration) {
  if (target.type == NodeType::Identifier) {
    if (is_declaration) {
      uint16_t slot = declareLocal(target.name);
      builder_.store_var(slot);
      builder_.load_var(slot); /* 恢复绑定值到栈顶（外层 Pop 复用） */
    } else {
      uint16_t slot = 0, capture = 0;
      bool is_upvalue = false;
      bool is_global = false;
      if (!resolveAssignTarget(target.name, &slot, &capture, &is_upvalue)) {
        /* 解构赋值到未声明标识符（M5.1e-g）：非严格隐式全局。 */
        is_global = true;
      }
      if (is_global) {
        builder_.store_global(target.name); /* 保留绑定值在栈顶 */
      } else if (is_upvalue) {
        builder_.store_upvalue(capture);
        builder_.load_upvalue(capture); /* 保留绑定值在栈顶 */
      } else {
        builder_.store_var(slot);
        builder_.load_var(slot);
      }
    }
    return true;
  }

  if (target.type == NodeType::AssignmentPattern) {
    /* 默认值：value = (value === undefined) ? default : value
     *   [value] Dup; JumpIfUndefined use_default; Jump end
     *   use_default: Pop; [default]     ; end:
     * JumpIfUndefined 弹栈判断（弹的是 Dup 副本），原值保留在栈顶：
     * 非 undefined 路径栈顶仍是原值；undefined 路径 Pop 掉原值后
     * 求默认值。选定后递归绑定左侧目标（值在栈顶）。 */
    if (target.children.size() < 2) {
      fail("invalid default value pattern");
      return false;
    }
    BcLabel use_default = builder_.new_label();
    BcLabel end = builder_.new_label();
    builder_.dup();
    builder_.jump_if_undefined(&use_default);
    builder_.jump(&end);
    builder_.bind_label(use_default);
    builder_.pop(); /* 清掉保留的原值 */
    if (!compileExpr(*target.children[1])) return false;
    builder_.bind_label(end);
    /* 递归绑定左侧目标（Identifier / 嵌套模式；绑定后值留栈顶）。 */
    return compileBindTarget(*target.children[0], is_declaration);
  }

  if (target.type == NodeType::ObjectPattern ||
      target.type == NodeType::ArrayPattern) {
    /* 嵌套模式：值暂存临时槽后递归，恢复源值到栈顶（外层 Pop 复用）。 */
    uint16_t tmp = scope_->next_slot++;
    builder_.store_var(tmp);
    if (!compilePattern(target, tmp, is_declaration)) return false;
    builder_.load_var(tmp);
    return true;
  }

  if (target.type == NodeType::MemberExpression) {
    /* 成员绑定目标：{k: dst.k} / [dst[0]]（M5.1e-d）。值已在栈顶。
     * 暂存值 -> 编译 obj(+key) -> 恢复值 -> StoreProp/StoreElem/StorePrivate。 */
    if (target.member.optional) {
      fail("optional chaining cannot be used as a destructuring target");
      return false;
    }
    if (target.children.empty()) {
      fail("invalid destructuring member target");
      return false;
    }
    const ASTNode *obj_expr = target.children[0];
    const bool computed = target.member.computed;
    const ASTNode *key_expr = computed ? target.children[1] : nullptr;
    uint16_t t_val = scope_->next_slot++;
    builder_.store_var(t_val);
    int priv_idx = -1;
    std::string prop_name;
    if (!computed) {
      const ASTNode *prop = target.children[1];
      if (prop->type == NodeType::PrivateIdentifier) {
        priv_idx = resolvePrivateName(prop->name);
        if (priv_idx < 0) return false;
      } else if (prop->type != NodeType::Identifier) {
        fail("invalid destructuring member property name");
        return false;
      } else {
        prop_name = prop->name;
      }
    }
    if (!compileExpr(*obj_expr)) return false;
    if (computed && !compileExpr(*key_expr)) return false;
    builder_.load_var(t_val);
    if (computed) {
      builder_.store_elem();
    } else if (priv_idx >= 0) {
      builder_.store_private(static_cast<uint16_t>(priv_idx));
    } else {
      builder_.store_prop(prop_name);
    }
    return true;
  }

  fail("invalid destructuring target");
  return false;
}

/* 解构模式编译：源在临时槽 src_slot，逐元素绑定。
 * 对象模式：LoadVar src; LoadProp key（值在栈顶）-> 绑定
 * 数组模式：LoadVar src; PushInt i; LoadElem（值在栈顶）-> 绑定；
 *           rest（SpreadElement 在数组模式末尾）用 RestToArray。 */
bool Compiler::compilePattern(const ASTNode &pattern, uint16_t src_slot,
                              bool is_declaration) {
  if (pattern.type == NodeType::ObjectPattern) {
    /* 已绑定键跟踪（对象 rest 用）：静态键名 + 计算键暂存槽。
     * 计算键绑定后键值暂存槽内，rest 时构建排除数组引用。 */
    std::vector<std::string> bound_keys;
    std::vector<uint16_t> bound_key_slots;
    for (const ASTNode *member : pattern.children) {
      if (member == nullptr) continue;
      if (member->type == NodeType::SpreadElement) {
        /* 对象 rest：{a, ...rest} = src（ES2018 §13.15.5）。
         * rest = 源中排除已绑定键后的剩余属性新对象。
         * 构建排除键数组（静态键 + 计算键值），RestObjectProps 复制。
         *   NewArray; StoreVar t_excl
         *   [静态键] PushConst k; AppendElem t_excl
         *   [计算键] LoadVar t_key; AppendElem t_excl
         *   LoadVar src; LoadVar t_excl; RestObjectProps {t_rest}
         * rest 目标绑定（值在栈顶）。 */
        if (member->children.empty()) {
          fail("invalid rest element");
          return false;
        }
        uint16_t t_excl = scope_->next_slot++;
        builder_.new_array();
        builder_.store_var(t_excl);
        for (const std::string &k : bound_keys) {
          builder_.push_const(k);
          builder_.append_elem(t_excl);
        }
        for (uint16_t ks : bound_key_slots) {
          builder_.load_var(ks);
          builder_.append_elem(t_excl);
        }
        uint16_t t_rest = scope_->next_slot++;
        builder_.new_object();
        builder_.store_var(t_rest);
        builder_.load_var(src_slot);
        builder_.load_var(t_excl);
        builder_.rest_object_props(t_rest);
        builder_.load_var(t_rest); /* rest 对象到栈顶供绑定 */
        if (!compileBindTarget(*member->children[0], is_declaration)) {
          return false;
        }
        builder_.pop(); /* 丢弃绑定值 */
        return true;    /* rest 之后无更多元素（Parser 已保证） */
      }
      if (member->type != NodeType::Property || member->children.empty()) {
        fail("invalid object pattern member");
        return false;
      }
      /* 键：计算 [expr]（M5.1e-d）或静态（Identifier/Literal）。
       * 简写默认值 { key = default }：单 child（AssignmentPattern，M5.1e-d
       * 修复双 free 后 key 由 AssignmentPattern 持有）。 */
      const bool computed = member->prop.is_computed;
      uint16_t t_key = 0;
      std::string key;
      if (computed) {
        /* 计算键 { [k]: v }：children[0] 为键表达式，children[1] 为值。
         * LoadVar src; [key]; StoreVar t_key; LoadVar t_key; LoadElem。
         * 键值暂存 t_key（rest 的排除集合与后续绑定共用）。 */
        if (member->children.size() < 2) {
          fail("invalid computed key pattern member");
          return false;
        }
        t_key = scope_->next_slot++;
        builder_.load_var(src_slot);
        if (!compileExpr(*member->children[0])) return false;
        builder_.store_var(t_key);
        builder_.load_var(t_key);
        builder_.load_elem(); /* 值在栈顶 */
        bound_key_slots.push_back(t_key);
      } else {
        /* 静态键：普通 { key: v } 取 children[0]；简写默认值 { key = d }
         * 单 child，键名取自 AssignmentPattern 的 target（children[0]）。 */
        const ASTNode *key_node = member->children[0];
        if (key_node->type == NodeType::AssignmentPattern) {
          if (key_node->children.empty()) {
            fail("invalid default value pattern");
            return false;
          }
          key_node = key_node->children[0];
        }
        if (!objectKeyToString(key_node, &key)) {
          fail("invalid property key in destructuring");
          return false;
        }
        builder_.load_var(src_slot);
        builder_.load_prop(key); /* 值在栈顶 */
        bound_keys.push_back(key);
      }
      /* 值：普通 { key: v } 取 children[1]；简写默认值 { key = d }
       * 单 child（AssignmentPattern，compileBindTarget 处理默认值）。 */
      const ASTNode *value =
          member->children.size() >= 2 ? member->children[1] : member->children[0];
      if (!compileBindTarget(*value, is_declaration)) {
        return false;
      }
      builder_.pop(); /* 丢弃绑定值 */
    }
    return true;
  }

  if (pattern.type == NodeType::ArrayPattern) {
    uint16_t index = 0;
    for (const ASTNode *elem : pattern.children) {
      if (elem == nullptr) {
        /* 空洞：跳过（无绑定），但索引仍递增。 */
        ++index;
        continue;
      }
      if (elem->type == NodeType::SpreadElement) {
        /* rest：...target（必须为数组模式最后一个元素，Parser 已保证）。
         * LoadVar src; PushInt index; RestToArray -> 剩余数组 */
        if (elem->children.empty()) {
          fail("invalid rest element");
          return false;
        }
        builder_.load_var(src_slot);
        builder_.push_int(index);
        builder_.rest_to_array();
        if (!compileBindTarget(*elem->children[0], is_declaration)) {
          return false;
        }
        builder_.pop();
        return true; /* rest 之后无更多元素 */
      }
      builder_.load_var(src_slot);
      builder_.push_int(index);
      builder_.load_elem(); /* 值在栈顶 */
      if (!compileBindTarget(*elem, is_declaration)) {
        return false;
      }
      builder_.pop(); /* 丢弃绑定值 */
      ++index;
    }
    return true;
  }

  fail("invalid pattern");
  return false;
}

/* for-in/for-of 循环 left 绑定（M5.1e-d，§14.7.4/§14.7.5）：栈顶为当前
 * 迭代值/键，按 left 形态绑定：
 *   - VariableDeclaration[Identifier]  -> declareLocal + StoreVar
 *   - VariableDeclaration[模式]        -> 预声明绑定名 + compilePattern(true)
 *   - Identifier（赋值既有变量）        -> StoreVar/StoreUpvalue
 *   - 模式（赋值解构 ({a} of ...)）     -> compilePattern(false)
 *   - MemberExpression（obj.x of ...）  -> 栈顶值暂存后 StoreProp/StoreElem
 *   - SuperProperty 非法（for-in/of left 不能是 super）。 */
bool Compiler::compileLoopLeft(const ASTNode *left) {
  if (left == nullptr) {
    fail("for-in/of without left target");
    return false;
  }
  if (left->type == NodeType::VariableDeclaration) {
    for (const ASTNode *declarator : left->children) {
      if (declarator == nullptr ||
          declarator->type != NodeType::VariableDeclarator ||
          declarator->children.empty()) {
        fail("invalid for-in/of loop variable declaration");
        return false;
      }
      const ASTNode *target = declarator->children[0];
      if (target->type == NodeType::ObjectPattern ||
          target->type == NodeType::ArrayPattern) {
        /* 声明解构：for (const {a} of arr)。预声明绑定名后解构。 */
        if (!declarePatternBindings(*target)) return false;
        uint16_t tmp = scope_->next_slot++;
        builder_.store_var(tmp); /* 栈顶迭代值 -> 临时槽 */
        if (!compilePattern(*target, tmp, /*is_declaration=*/true)) {
          return false;
        }
        /* compilePattern 内部已按元素 Pop 清栈，无需再 Pop。 */
        continue;
      }
      if (target->type != NodeType::Identifier) {
        fail("invalid for-in/of loop variable declaration target");
        return false;
      }
      uint16_t slot = declareLocal(target->name);
      builder_.store_var(slot);
    }
    return true;
  }
  if (left->type == NodeType::Identifier) {
    /* 赋值到既有变量（局部/捕获）或隐式全局（M5.1e-g，非严格）。 */
    uint16_t slot = 0;
    uint16_t capture = 0;
    bool is_upvalue = false;
    bool is_global = false;
    if (!resolveAssignTarget(left->name, &slot, &capture, &is_upvalue)) {
      is_global = true;
    }
    if (is_global) {
      builder_.store_global(left->name); /* 保留新值在栈顶 */
      builder_.pop();                    /* 循环 left：消费迭代值 */
    } else if (is_upvalue) {
      builder_.store_upvalue(capture);
    } else {
      builder_.store_var(slot);
    }
    return true;
  }
  if (left->type == NodeType::ObjectPattern ||
      left->type == NodeType::ArrayPattern) {
    /* 赋值解构：for ({a} of arr) / for ({a} in obj)。 */
    uint16_t tmp = scope_->next_slot++;
    builder_.store_var(tmp);
    if (!compilePattern(*left, tmp, /*is_declaration=*/false)) return false;
    /* compilePattern 内部已按元素 Pop 清栈，无需再 Pop。 */
    return true;
  }
  if (left->type == NodeType::MemberExpression) {
    /* 成员 left：for (obj.x of arr) / for (obj[x] in obj2)。
     * 栈顶迭代值暂存，编译 obj(+key) 后 Store。 */
    if (left->member.optional) {
      fail("optional chaining cannot be used as for-in/of left target");
      return false;
    }
    if (left->children.empty()) {
      fail("invalid for-in/of member left target");
      return false;
    }
    const ASTNode *obj_expr = left->children[0];
    const bool computed = left->member.computed;
    const ASTNode *key_expr = computed ? left->children[1] : nullptr;
    /* 栈顶迭代值暂存。 */
    uint16_t t_val = scope_->next_slot++;
    builder_.store_var(t_val);
    /* 私有字段 left：obj.#x（合法：for (o.#x of arr)）。 */
    int priv_idx = -1;
    std::string prop_name;
    if (!computed) {
      const ASTNode *prop = left->children[1];
      if (prop->type == NodeType::PrivateIdentifier) {
        priv_idx = resolvePrivateName(prop->name);
        if (priv_idx < 0) return false;
      } else if (prop->type != NodeType::Identifier) {
        fail("invalid for-in/of member left property name");
        return false;
      } else {
        prop_name = prop->name;
      }
    }
    if (!compileExpr(*obj_expr)) return false;
    if (computed && !compileExpr(*key_expr)) return false;
    builder_.load_var(t_val);
    if (computed) {
      builder_.store_elem();
    } else if (priv_idx >= 0) {
      builder_.store_private(static_cast<uint16_t>(priv_idx));
    } else {
      builder_.store_prop(prop_name);
    }
    return true;
  }
  fail("invalid for-in/of left target");
  return false;
}

/* 展开参数列表（M5.1a）：把 args 元素追加到槽 slot 的数组。
 * 非展开元素求值后 AppendElem（动态索引追加）；展开元素求值后
 * AppendSpread（迭代追加）。求值顺序 = 源码顺序（§13.2.3 等）。 */
bool Compiler::compileSpreadArgs(const std::vector<const ASTNode *> &args,
                                 uint16_t slot) {
  for (const ASTNode *arg : args) {
    if (arg == nullptr) continue;
    if (arg->type == NodeType::SpreadElement) {
      if (arg->children.empty() || !compileExpr(*arg->children[0])) return false;
      builder_.append_spread(slot);
    } else {
      if (!compileExpr(*arg)) return false;
      builder_.append_elem(slot);
    }
  }
  return true;
}

/* 变量声明：每个声明器分配局部槽 + 编译初始化表达式（声明值 = undefined）。 */
bool Compiler::compileVarDecl(const ASTNode &decl) {
  for (const ASTNode *declarator : decl.children) {
    if (declarator == nullptr || declarator->type != NodeType::VariableDeclarator) {
      fail("invalid variable declarator");
      return false;
    }
    const ASTNode *target = declarator->children[0];
    if (target->type == NodeType::ObjectPattern ||
        target->type == NodeType::ArrayPattern) {
      /* 解构声明（M5.1a）：先声明所有绑定名，再编译 init 并解构。
       * 无 init 的解构声明非法（Parser 已对 const 校验；var/let 缺省
       * 语义为解构 undefined，本阶段直接报错）。 */
      if (declarator->children.size() < 2) {
        fail("destructuring declaration requires initializer");
        return false;
      }
      /* 预声明绑定名（保证槽位分配稳定）。 */
      if (!declarePatternBindings(*target)) return false;
      if (!compileExpr(*declarator->children[1])) return false;
      uint16_t tmp = scope_->next_slot++;
      builder_.store_var(tmp);
      if (!compilePattern(*target, tmp, /*is_declaration=*/true)) return false;
      continue;
    }
    if (target->type != NodeType::Identifier) {
      fail("invalid variable declarator target");
      return false;
    }
    uint16_t slot = declareLocal(target->name);

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
    case NodeType::ForOfStatement:
      return compileForOf(node);
    case NodeType::ForInStatement:
      return compileForIn(node);
    case NodeType::LabeledStatement:
      return compileLabeled(node);
    case NodeType::BreakStatement:
      return compileBreak(node);
    case NodeType::ContinueStatement:
      return compileContinue(node);
    case NodeType::ReturnStatement:
      return compileReturn(node);
    case NodeType::ThrowStatement:
      return compileThrow(node); /* M5.1e-f */
    case NodeType::TryStatement:
      return compileTry(node);   /* M5.1e-f */
    case NodeType::FunctionDeclaration:
      /* 嵌套函数声明（M2.5 闭包）：编译子函数字节码，发射 MakeFunction
       * 实例化闭包并挂到当前函数局部变量。 */
      return compileNestedFunction(node);
    case NodeType::ClassDeclaration:
      /* class 声明（M5.1d-a）：编译构造器函数并挂到类名局部变量。 */
      return compileClass(node, /*is_declaration=*/true);
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

bool Compiler::compileWhile(const ASTNode &node, const std::string &label) {
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
  LoopContext ctx;
  ctx.label = label;
  ctx.break_label = end_label;
  ctx.continue_label = loop_label;
  ctx.is_loop = true;
  loops_.push_back(ctx);
  bool ok = compileStmt(*node.children[1]);
  loops_.pop_back();
  if (!ok) return false;
  builder_.jump(&loop_label);
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileDoWhile(const ASTNode &node, const std::string &label) {
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
  LoopContext ctx;
  ctx.label = label;
  ctx.break_label = end_label;
  ctx.continue_label = loop_label;
  ctx.is_loop = true;
  loops_.push_back(ctx);
  bool ok = compileStmt(*node.children[0]);
  loops_.pop_back();
  if (!ok) return false;
  if (!compileExpr(*node.children[1])) return false;
  builder_.jump_if_false(&end_label);
  builder_.jump(&loop_label);
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileFor(const ASTNode &node, const std::string &label) {
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
  /* body（continue 跳 update_label） */
  LoopContext ctx;
  ctx.label = label;
  ctx.break_label = end_label;
  ctx.continue_label = update_label;
  ctx.is_loop = true;
  loops_.push_back(ctx);
  bool body_ok =
      (node.children[3] == nullptr) || compileStmt(*node.children[3]);
  loops_.pop_back();
  if (!body_ok) return false;
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

bool Compiler::compileForOf(const ASTNode &node, const std::string &label) {
  /* for (left of right) body  （M4.4f，§14.7.5）
   *   [right]              ; 可迭代对象
   *   GetIterator          ; -> iterator
   *   StoreVar iter_tmp    ; 迭代器存临时槽
   * loop_label:
   *   LoadVar iter_tmp
   *   IteratorStep         ; -> [value, done]（done 在栈顶）
   *   JumpIfTrue end_label ; done true 弹栈跳 end；done false 继续（value 留栈顶）
   *   [left 赋值]          ; 栈顶 value -> 变量/成员
   *   [body]
   *   Jump loop_label
   * end_label:
   *   Pop                  ; 清完成路径残留 value（undefined）
   *
   * children = [left, right, body]；left 可为 VariableDeclaration 或表达式。 */
  if (node.children.size() < 3) {
    fail("for-of without clauses");
    return false;
  }
  const ASTNode *left = node.children[0];
  const ASTNode *right = node.children[1];
  const ASTNode *body = node.children[2];

  /* 可迭代对象求值。 */
  if (!compileExpr(*right)) return false;
  builder_.get_iterator();

  /* 迭代器存临时槽（跨循环迭代保活，栈根集保护）。
   * 用内部名 declareLocal：占住 locals 表，避免与循环体内
   * declareLocal(x) 复用同一槽号（无名 next_slot++ 会被覆盖）。
   * 名字带唯一计数：嵌套 for-of 的内层不复用外层迭代器槽。 */
  uint16_t iter_tmp =
      declareLocal("\x01iter" + std::to_string(iter_slot_seq_++));
  builder_.store_var(iter_tmp);

  BcLabel loop_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  BcLabel break_label = builder_.new_label();
  builder_.bind_label(loop_label);
  builder_.load_var(iter_tmp);
  builder_.iterator_step();
  builder_.jump_if_true(&end_label); /* done true -> 结束循环 */

  /* left 赋值：栈顶是当前迭代 value（M5.1e-d 支持解构/成员 left）。 */
  if (!compileLoopLeft(left)) return false;

  /* body（break -> break_label，continue -> loop）。break_label 独立于
   * end_label：结束路径栈顶残留 undefined 需 Pop，break 时栈已平衡。 */
  LoopContext ctx;
  ctx.label = label;
  ctx.break_label = break_label;
  ctx.continue_label = loop_label;
  ctx.is_loop = true;
  loops_.push_back(ctx);
  bool body_ok = (body == nullptr) || compileStmt(*body);
  loops_.pop_back();
  if (!body_ok) return false;
  builder_.jump(&loop_label);
  builder_.bind_label(end_label);
  builder_.pop(); /* 清完成路径残留 value */
  builder_.bind_label(break_label);
  return true;
}

bool Compiler::compileForIn(const ASTNode &node, const std::string &label) {
  /* for (left in right) body  （M5.1e-c，§14.7.4 + §7.3.24）
   *   [right]
   *   EnumerateKeys        ; -> 键数组（ToObject + 原型链可枚举字符串键）
   *   StoreVar keys_tmp    ; 键数组存临时槽
   * loop_label:
   *   LoadVar keys_tmp
   *   NextKey              ; -> 最后一个键（并移除）或 undefined（空）
   *   Dup
   *   JumpIfUndefined end  ; 键 undefined -> 结束（undefined 仍留栈顶）
   *   [left 赋值]          ; 栈顶键 -> 变量/成员（store 消费键）
   *   [body]
   *   Jump loop_label
   * end_label:
   *   Pop                  ; 清结束路径残留 undefined
   *
   * children = [left, right, body]。left 为 VariableDeclaration（Identifier）
   * 或 Identifier。v1 简化：键按 [[OwnPropertyKeys]] 序 + 原型链逐层，
   * 不做规范要求的新增/删除即时响应（枚举键在循环开始前快照）。 */
  if (node.children.size() < 3) {
    fail("for-in without clauses");
    return false;
  }
  const ASTNode *left = node.children[0];
  const ASTNode *right = node.children[1];
  const ASTNode *body = node.children[2];

  /* 枚举对象求值。 */
  if (!compileExpr(*right)) return false;
  builder_.enumerate_keys();

  /* 键数组存临时槽（跨循环迭代保活）。名字带唯一计数（嵌套防复用）。 */
  uint16_t keys_tmp =
      declareLocal("\x01forin" + std::to_string(forin_slot_seq_++));
  builder_.store_var(keys_tmp);

  BcLabel loop_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  BcLabel break_label = builder_.new_label();
  builder_.bind_label(loop_label);
  builder_.load_var(keys_tmp);
  builder_.next_key();
  builder_.dup();
  builder_.jump_if_undefined(&end_label); /* 键 undefined -> 结束 */

  /* left 赋值：栈顶是当前枚举键（M5.1e-d 支持解构/成员 left）。 */
  if (!compileLoopLeft(left)) return false;

  /* 循环体（label/continue 上下文：break -> break_label，continue -> loop）。
   * break_label 独立于 end_label：正常结束路径（键用尽）栈顶残留 undefined
   * 需 Pop 清栈；break 时栈已平衡，若跳 end_label 会在 Pop 处下溢。 */
  LoopContext ctx;
  ctx.label = label;
  ctx.break_label = break_label;
  ctx.continue_label = loop_label;
  ctx.is_loop = true;
  loops_.push_back(ctx);
  bool ok = (body == nullptr) || compileStmt(*body);
  loops_.pop_back();
  if (!ok) return false;

  builder_.jump(&loop_label);
  builder_.bind_label(end_label);
  builder_.pop(); /* 清结束路径残留 undefined */
  builder_.bind_label(break_label);
  return true;
}

bool Compiler::compileLabeled(const ASTNode &node) {
  /* label: stmt（M5.1e-c，§14.13）。
   * children[0] = 被标记语句；label 名存 node.name。
   *
   * body 是循环语句：标签名下传给循环编译（循环 ctx 记录 label，
   * is_loop=true），continue label 指向循环的 continue 目标。
   * body 非循环（块/if 等）：push 标签 ctx（is_loop=false，仅 break
   * 合法），break label 指向标签语句结尾。 */
  if (node.children.empty()) {
    fail("label without statement");
    return false;
  }
  const ASTNode *body = node.children[0];
  switch (body->type) {
    case NodeType::WhileStatement: return compileWhile(*body, node.name);
    case NodeType::DoWhileStatement: return compileDoWhile(*body, node.name);
    case NodeType::ForStatement: return compileFor(*body, node.name);
    case NodeType::ForInStatement: return compileForIn(*body, node.name);
    case NodeType::ForOfStatement: return compileForOf(*body, node.name);
    default: break;
  }
  BcLabel end_label = builder_.new_label();

  LoopContext ctx;
  ctx.label = node.name;
  ctx.break_label = end_label;
  ctx.is_loop = false; /* continue label 无效 */
  loops_.push_back(ctx);
  bool ok = compileStmt(*body);
  loops_.pop_back();
  if (!ok) return false;
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileBreak(const ASTNode &node) {
  /* break [label]（M5.1e-c，§14.8.1）。
   * 无标签：跳最近循环/标签的 break_label。
   * 有标签：沿 loops_ 找匹配 label，跳其 break_label。
   * 找不到 -> 编译错误（parser 已校验存在性，此处防御）。 */
  if (loops_.empty()) {
    fail("illegal break statement");
    return false;
  }
  if (node.name.empty()) {
    /* 无标签：跳最近上下文（循环或标签）的 break_label。 */
    builder_.jump(&loops_.back().break_label);
    return true;
  }
  for (auto it = loops_.rbegin(); it != loops_.rend(); ++it) {
    if (it->label == node.name) {
      builder_.jump(&it->break_label);
      return true;
    }
  }
  fail("undefined label '" + node.name + "'");
  return false;
}

bool Compiler::compileContinue(const ASTNode &node) {
  /* continue [label]（M5.1e-c，§14.8.2）。
   * 无标签：跳最近循环的 continue_label。
   * 有标签：沿 loops_ 找匹配的循环标签，跳其 continue_label。
   * 无标签且无循环 -> 非法；有标签但指向非循环 -> 非法（§14.8.2 步骤 2）。 */
  if (node.name.empty()) {
    for (auto it = loops_.rbegin(); it != loops_.rend(); ++it) {
      if (it->is_loop) {
        builder_.jump(&it->continue_label);
        return true;
      }
    }
    fail("illegal continue statement");
    return false;
  }
  for (auto it = loops_.rbegin(); it != loops_.rend(); ++it) {
    if (it->label == node.name) {
      if (!it->is_loop) {
        fail("continue to non-loop label '" + node.name + "'");
        return false;
      }
      builder_.jump(&it->continue_label);
      return true;
    }
  }
  fail("undefined label '" + node.name + "'");
  return false;
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

/* M5.1e-f：throw expr（§14.15）。
 *   [expr]
 *   Throw          -> 抛出（异常传播，经异常表/跨帧） */
bool Compiler::compileThrow(const ASTNode &node) {
  if (node.children.empty()) {
    fail("throw without argument");
    return false;
  }
  if (!compileExpr(*node.children[0])) return false;
  builder_.throw_value();
  return true;
}

/* M5.1e-f：try/catch/finally（§14.16）。
 * AST 布局：TryStatement children = [block, catch?, finally?]（至少一个 catch/finally）。
 * CatchClause children = [param?, block]（param 可缺省，ES2019 catch{}）。
 *
 * 编译布局（配合 VM 异常表 try_entries）：
 *   try_start:
 *     [try block]
 *     Jump finally_entry      （正常完成：进 finally）
 *     Jump try_end            （无 finally：跳过 handler）
 *   catch_start:              （VM 已把异常值绑定 catch_slot）
 *     [catch block]
 *     Jump finally_entry
 *   finally_entry:
 *     [finally block]
 *     RethrowPending          （有挂起异常则重新抛出）
 *   try_end:
 * 异常表：try [try_start, try_end) -> catch_start / finally_entry / finally_end=try_end。
 * catch_slot 由 VM 捕获时绑定（catch 参数）。
 *
 * 语义：
 *   - try 内异常：有 catch -> 绑定 catch_slot + 跳 catch_start（异常被消费）；
 *     无 catch 有 finally -> 挂起异常，跳 finally_entry，finally 后 RethrowPending 重抛。
 *   - catch 块内异常：继续传播（不在本 try 保护范围内，异常表按当前 pc 查）。
 *   - finally 正常执行（无 pending）：RethrowPending 直接继续到 try_end。 */
bool Compiler::compileTry(const ASTNode &node) {
  if (node.children.size() < 2) {
    fail("try without catch or finally");
    return false;
  }
  const ASTNode *try_block = node.children[0];
  const ASTNode *catch_clause = nullptr;
  const ASTNode *finally_block = nullptr;
  for (size_t i = 1; i < node.children.size(); ++i) {
    if (node.children[i]->type == NodeType::CatchClause) {
      catch_clause = node.children[i];
    } else {
      finally_block = node.children[i];
    }
  }

  const bool has_catch = catch_clause != nullptr;
  const bool has_finally = finally_block != nullptr;

  /* 异常表条目（区间在编译后回填位置）。 */
  TryEntry entry;
  uint16_t catch_slot = TryEntry::kNoCatchSlot;

  BcLabel finally_entry = builder_.new_label();
  BcLabel try_end = builder_.new_label();
  BcLabel catch_start = builder_.new_label();

  /* try 块起点。 */
  entry.try_start = static_cast<uint32_t>(builder_.position());

  /* try 块。 */
  if (!compileBlock(*try_block)) return false;

  /* try 块终点（异常检测区间结束）。 */
  entry.try_end = static_cast<uint32_t>(builder_.position());

  if (has_finally) {
    builder_.jump(&finally_entry); /* 正常完成 -> finally */
  } else {
    builder_.jump(&try_end);       /* 无 finally：跳过 handler */
  }

  /* catch handler（有 catch 时）。 */
  if (has_catch) {
    builder_.bind_label(catch_start);
    entry.catch_start = static_cast<uint32_t>(builder_.position());

    /* catch 参数绑定：VM 捕获时已把异常值写入 catch_slot。
     * 标识符参数：槽即绑定（VM 直接写栈底局部槽）。
     * 解构参数：分配临时槽存异常值，再编译模式绑定。
     * ES2019 catch{}（无参数）：children 只有块体，不绑定参数。 */
    if (catch_clause->children.size() >= 2) {
      const ASTNode *param = catch_clause->children[0];
      if (param->type == NodeType::Identifier) {
        catch_slot = declareLocal(param->name);
        builder_.set_local_name(catch_slot, param->name);
      } else {
        catch_slot = declareLocal("\x01" "catch" + std::to_string(catch_seq_++));
        if (!compilePattern(*param, catch_slot, /*is_declaration=*/true)) {
          return false;
        }
      }
    }
    entry.catch_slot = catch_slot;

    /* catch 块（children.back() 为块；有参数时 children=[param, block]）。 */
    const ASTNode *catch_body = catch_clause->children.back();
    if (!compileBlock(*catch_body)) return false;
    entry.catch_end = static_cast<uint32_t>(builder_.position());

    if (has_finally) {
      builder_.jump(&finally_entry);
    } else {
      builder_.jump(&try_end);
    }
  }

  /* finally handler。 */
  if (has_finally) {
    builder_.bind_label(finally_entry);
    entry.finally_start = static_cast<uint32_t>(builder_.position());

    if (!compileBlock(*finally_block)) return false;

    /* finally 末尾：若有挂起异常（try 抛且无 catch / catch 再抛）则重抛。 */
    builder_.rethrow_pending();
    entry.finally_end = static_cast<uint32_t>(builder_.position());
  }

  /* 汇聚点。 */
  builder_.bind_label(try_end);
  if (has_finally) builder_.bind_label(finally_entry);

  /* 注册异常表。 */
  builder_.add_try_entry(std::move(entry));
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
    case NodeType::TemplateLiteral: return compileTemplateLiteral(node);
    case NodeType::ImportMeta:
      /* M5.2c import.meta：LoadImportMeta（VM 沿 env 链查找模块 import.meta）。
       * 语法层已保证仅模块上下文合法（parser.cc），普通脚本不可达。 */
      builder_.load_import_meta();
      return true;
    case NodeType::ImportExpression: {
      /* M5.2c-2 动态 import(spec)（§16.2.1.8）：编译为对全局内置函数
       * `import` 的调用——先 LoadGlobal "import" 压入内置函数，再求值
       * specifier 压入参数，Call {1}（栈布局 [func, arg]，与普通调用
       * 一致——callee 先入栈，arg 后入栈）。运行时 dynamicImport 返回
       * Promise（模块加载成功 resolve 为 namespace 对象，失败 reject），
       * 微任务调度保证异步。 */
      if (node.children.empty()) {
        fail("SyntaxError: import() requires an argument");
        return false;
      }
      builder_.load_global("import");                    /* 内置函数 */
      if (!compileExpr(*node.children[0])) return false; /* specifier */
      builder_.call(1);
      return true;
    }
    case NodeType::Identifier: return compileIdentifier(node);
    case NodeType::ThisExpression: {
      /* M5.1c：this -> LoadThis（当前帧 this）。 */
      builder_.load_this();
      return true;
    }
    case NodeType::UnaryOp: return compileUnary(node);
    case NodeType::BinaryOp: return compileBinary(node);
    case NodeType::Conditional: return compileConditional(node);
    case NodeType::Assignment: return compileAssignment(node);
    case NodeType::CallExpression:
      /* 可选链调用（M5.1e-e）：callee 为可选链（?. 链）或 ?.() 独立短路点。
       * 普通调用 compileCall 处理；链调用 compileOptionalChain 整链短路。 */
      if (node.optional || (node.children.size() >= 1 &&
                            isOptionalChainNode(*node.children[0]))) {
        return compileOptionalChain(node);
      }
      return compileCall(node);
    case NodeType::SuperCall:
      /* M5.1d-c1 super()：派生类构造器内调用基类构造器（compileCall 特判）。 */
      return compileCall(node);
    case NodeType::Sequence: return compileSequence(node);
    case NodeType::MemberExpression:
      /* 可选链成员访问（M5.1e-e）：?.prop / ?.[expr] 短路。 */
      if (node.member.optional) return compileOptionalChain(node);
      return compileMemberLoad(node);
    case NodeType::SuperProperty: {
      /* M5.1d-b super 属性访问：super.x / super[expr]。
       * 从当前方法 HomeObject 的 [[Prototype]] 读取（VM 经 self）。 */
      if (node.children.empty()) {
        fail("super property without key");
        return false;
      }
      if (node.member.optional) {
        /* parser 已拦截 super?.x（'super' keyword unexpected here），防御。 */
        fail("SyntaxError: optional chaining on super");
        return false;
      }
      if (!node.member.computed) {
        const ASTNode *prop = node.children[0];
        if (prop->type != NodeType::Identifier) {
          fail("invalid super property name");
          return false;
        }
        builder_.super_load_prop(prop->name);
        return true;
      }
      if (!compileExpr(*node.children[0])) return false;
      builder_.super_load_elem();
      return true;
    }
    case NodeType::NewExpression: return compileNew(node);
    case NodeType::FunctionExpression:
    case NodeType::ArrowFunctionExpression:
      /* 函数表达式/箭头函数（M2.5 闭包）：编译子函数，MakeFunction 留栈顶。 */
      return compileNestedFunction(node, /*is_declaration=*/false);
    case NodeType::ClassExpression:
      /* class 表达式（M5.1d-a）：编译构造器函数，留栈顶。 */
      return compileClass(node, /*is_declaration=*/false);
    case NodeType::UpdateExpression: return compileUpdate(node);
    default:
      fail("not implemented: expression type " +
           std::to_string(static_cast<int>(node.type)));
      return false;
  }
}

bool Compiler::compileLiteral(const ASTNode &node) {
  const LiteralPayload &lit = node.literal;
  if (lit.is_bigint) {
    /* BigInt 字面量（M4.3）：PushBigInt 常量池条目（十进制数字串）。
     * 注意必须在 is_number 之前判断——parser 对 BigInt 同时置位
     * is_number（double 载荷）与 is_bigint（精确数字串）。
     * 字面量本身恒非负（负号由一元 Neg 处理，如 -1n）。 */
    builder_.push_bigint(lit.bigint_str, false);
    return true;
  }
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
      /* 对象展开（M5.1a，§12.2.6.9 CopyDataProperties）：
       * 求值源对象，own enumerable 字符串属性复制进 tmp。 */
      if (prop->children.empty() || !compileExpr(*prop->children[0])) {
        return false;
      }
      builder_.copy_object_props(tmp);
      continue;
    }
    if (prop->type != NodeType::Property) {
      fail("invalid object literal member");
      return false;
    }
    if (prop->children.empty()) {
      fail("invalid property");
      return false;
    }
    /* 键：计算键 [expr] 运行期求值；否则编译为常量串。 */
    std::string key;
    bool computed_key = prop->prop.is_computed;
    if (!computed_key && !objectKeyToString(prop->children[0], &key)) {
      fail("invalid property key");
      return false;
    }
    const PropertyKind kind = prop->prop.kind;
    builder_.load_var(tmp); /* obj */
    if (kind == PropertyKind::Method || kind == PropertyKind::Get ||
        kind == PropertyKind::Set) {
      /* M5.1c method/getter/setter：children[1] 为方法函数表达式。 */
      if (prop->children.size() < 2) {
        fail("invalid method property");
        return false;
      }
      if (computed_key) {
        if (!compileExpr(*prop->children[0])) return false;
      } else {
        builder_.push_const(key);
      }
      if (!compileExpr(*prop->children[1])) return false;
      if (kind == PropertyKind::Method) {
        /* method 是数据属性（函数值），用 DefineProp 定义。 */
        builder_.define_prop();
      } else {
        builder_.define_accessor(kind == PropertyKind::Set ? 1 : 0);
      }
      builder_.pop(); /* 丢弃定义结果 */
      continue;
    }
    if (computed_key) {
      /* 计算键 { [expr]: value }：obj, key, value -> DefineProp。 */
      if (!compileExpr(*prop->children[0])) return false;
      if (prop->children.size() < 2 || !compileExpr(*prop->children[1])) {
        return false;
      }
      builder_.define_prop();
      builder_.pop();
      continue;
    }
    if (kind == PropertyKind::Shorthand) {
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
  bool has_spread = false;
  for (const ASTNode *elem : node.children) {
    if (elem != nullptr && elem->type == NodeType::SpreadElement) {
      has_spread = true;
      break;
    }
  }
  if (has_spread) {
    /* 含展开：全部元素动态追加（展开前后顺序一致，索引动态）。
     * 空洞（nullptr）无值，不追加（保持紧凑存储语义）。 */
    for (const ASTNode *elem : node.children) {
      if (elem == nullptr) continue;
      if (elem->type == NodeType::SpreadElement) {
        if (elem->children.empty() || !compileExpr(*elem->children[0])) {
          return false;
        }
        builder_.append_spread(tmp);
      } else {
        if (!compileExpr(*elem)) return false;
        builder_.append_elem(tmp);
      }
    }
    builder_.load_var(tmp); /* 结果 = 数组 */
    return true;
  }
  for (const ASTNode *elem : node.children) {
    if (elem == nullptr) continue;
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

/* 模板串（M5.1e）：`a${x}b${y}c`——children 为 quasi 与插值交替
 * （[quasi, expr, quasi, expr, quasi]，首尾必为 quasi）。
 * 语义（§13.2.8.5 String.raw 的求值 / §12.3.7）：quasi 原样（已解码），
 * 插值 ToString 后拼接。
 * 发射：
 *   [quasi0]
 *   对每个 (expr, quasi)：[expr] ToString; Add 拼接;
 *   （首 quasi 为字符串字面量，Add 直接做字符串拼接）
 * 空 quasi 时压空串（Add 需两个操作数）。 */
bool Compiler::compileTemplateLiteral(const ASTNode &node) {
  if (node.children.empty()) {
    builder_.push_const("");
    return true;
  }
  size_t i = 0;
  const ASTNode *first = node.children[0];
  if (first->type == NodeType::Literal && first->literal.is_string) {
    builder_.push_const(first->literal.str);
    i = 1;
  } else {
    /* 无准值起始（理论上不发生）：压空串占位。 */
    builder_.push_const("");
  }
  /* 每对 (插值 expr, 后续 quasi)：expr ToString 后 Add 拼入累积串，
   * 再把 quasi 拼入（累积始终在栈顶）。 */
  for (; i < node.children.size(); i += 2) {
    if (!compileExpr(*node.children[i])) return false;
    builder_.to_string();
    builder_.push_op(Opcode::Add); /* 累积 + 插值串 */
    const ASTNode *q = (i + 1 < node.children.size()) ? node.children[i + 1] : nullptr;
    if (q != nullptr && q->type == NodeType::Literal && q->literal.is_string) {
      builder_.push_const(q->literal.str);
      builder_.push_op(Opcode::Add); /* 累积 + quasi */
    }
  }
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
    /* compileExpr 已前置调度 optional 成员到 compileOptionalChain，防御。 */
    fail("internal: optional chaining should be compiled by compileOptionalChain");
    return false;
  }
  if (!compileExpr(*node.children[0])) return false;
  if (!node.member.computed) {
    /* obj.prop：属性名为 Identifier 或 PrivateIdentifier。 */
    const ASTNode *prop = node.children[1];
    if (prop->type == NodeType::PrivateIdentifier) {
      /* M5.1d-c2：obj.#x -> receiver; LoadPrivate {name_index}（receiver 替换为值）。 */
      int idx = resolvePrivateName(prop->name);
      if (idx < 0) return false;
      builder_.load_private(static_cast<uint16_t>(idx));
      return true;
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

/* 可选链编译（M5.1e-e，§13.5.1.1/§13.5.1.2）。
 *
 * AST 形态（parser 已标 optional）：链是沿 children[0] 嵌套的 Member/Call。
 *   a?.b.c     -> Member(Member(a,b)opt, c)
 *   a?.b?.c    -> Member(Member(a,b)opt, c)opt
 *   a?.b()     -> Call(Member(a,b)opt, args) [call_short_circuit=false]
 *   a?.b?.()   -> Call(Member(a,b)opt, args) [call_short_circuit=true]
 *
 * 语义：
 *   1. 链头 = 最内层短路点之前的部分（a?.b.c 的 a；a.b?.c 的 a.b）。
 *   2. 短路点（?.prop / ?.[expr] / ?.(args)）判断「前一步结果」是否 nullish，
 *      是则短路：整链剩余全部跳过，结果为 undefined。
 *   3. 链延续（?. 之后的 .c / [k] / ()）不判断，短路时被跳过、
 *      非短路时正常访问（a?.b.c 中 a 非 nullish 但 a.b 为 undefined 时
 *      .c 访问 undefined 抛 TypeError——规范行为）。
 *
 * 发射（统一 chain_end 汇聚短路）：
 *   [链头求值 -> 栈顶]
 *   对链上每个短路点（从内到外）：
 *     Dup; JumpIfNullish short_i      // 判断前一步结果（弹副本）
 *     [正常访问本节点 -> 结果留栈顶]
 *     Jump chain_end
 *     short_i: Pop; PushUndefined     // 清前一步，压 undefined
 *     Jump chain_end
 *   对链延续节点：直接访问（不判断）
 *   chain_end:
 *
 * 嵌套链（a?.b?.c）：内层短路得 undefined 后，外层短路点再判断
 * undefined 恒 nullish -> 再次短路，结果仍 undefined，语义等价。 */
bool Compiler::compileOptionalChain(const ASTNode &node) {
  /* 1. 识别链头：沿 children[0] 下探到最内层链节点（短路点或链延续）。
   *    链头 = 最内层链节点的 children[0]（非链节点表达式）。 */
  const ASTNode *first_opt = &node;
  while (true) {
    const ASTNode *obj =
        first_opt->children.empty() ? nullptr : first_opt->children[0];
    if (obj == nullptr || !isOptionalChainNode(*obj)) break;
    first_opt = obj;
  }
  if (first_opt->children.empty()) {
    fail("invalid optional chain node");
    return false;
  }
  const ASTNode *head = first_opt->children[0];

  /* 2. 收集链节点：从 node 沿 children[0] 下探到 first_opt（从外到内），
   *    再反转得从内到外。 */
  std::vector<const ASTNode *> path;
  for (const ASTNode *cur = &node;; cur = cur->children[0]) {
    path.push_back(cur);
    if (cur == first_opt) break;
  }
  std::vector<const ASTNode *> chain(path.rbegin(), path.rend());

  /* 3. 编译链头（正常表达式求值，结果留栈顶）。 */
  if (!compileExpr(*head)) return false;

  /* 4. 逐个编译链节点（从内到外）。 */
  BcLabel chain_end = builder_.new_label();
  for (const ASTNode *cn : chain) {
    if (!compileOptionalChainNode(*cn, &chain_end)) return false;
  }
  builder_.bind_label(chain_end);
  return true;
}

/* 判断节点是否为可选链节点（Member/Call 且属于链：自身 optional）。
 * optional 由 parser 的 chain_optional 传播：?. 后的链延续（a?.b.c 的 .c）
 * 也标 optional，故可选链节点与短路点不同（短路点用 short_circuit 判）。 */
bool Compiler::isOptionalChainNode(const ASTNode &node) const {
  if (node.type == NodeType::MemberExpression) return node.member.optional;
  if (node.type == NodeType::CallExpression) return node.optional;
  return false;
}

/* 判断节点是否为短路点（?./?.[/?.(，需要判断前一步结果 nullish）。
 * 链延续（a?.b() 的 () / a?.b.c 的 .c）不是短路点。
 * Member：short_circuit 区分 ?.prop（短路）与 .prop（链延续）；
 * Call：call_short_circuit 区分 ?.()（短路）与 ()（链延续）。 */
bool Compiler::isOptionalShortCircuit(const ASTNode &node) const {
  if (node.type == NodeType::MemberExpression) {
    return node.member.short_circuit;
  }
  if (node.type == NodeType::CallExpression) {
    return node.call_short_circuit; /* ?.() 独立短路点 */
  }
  return false;
}

/* 编译单个链节点：短路点判断前一步结果，链延续直接访问。
 * 调用前：操作对象（前一步结果）已在栈顶。
 * 返回后：本节点结果留栈顶（非短路路径）。
 *
 * 短路点发射：
 *   Dup; JumpIfNullish short_i      // 前一步 nullish -> 短路
 *   [正常访问本节点]                 // 非短路：结果留栈顶
 *   Jump skip_i                     // 跳过短路块
 *   short_i: Pop; PushUndefined; Jump chain_end
 *   skip_i:
 * 注意：正常路径不得跳 chain_end（会跳过后续链节点），用 skip_i 仅跳过
 * 本短路块，后续链节点自然顺序继续。 */
bool Compiler::compileOptionalChainNode(const ASTNode &node, BcLabel *chain_end) {
  if (node.type == NodeType::MemberExpression) {
    if (node.children.size() < 2) {
      fail("member expression without property");
      return false;
    }
    const bool is_short = isOptionalShortCircuit(node);
    BcLabel short_circuit;
    BcLabel skip;
    if (is_short) {
      short_circuit = builder_.new_label();
      skip = builder_.new_label();
      builder_.dup();
      builder_.jump_if_nullish(&short_circuit);
    }
    /* 访问：obj 已在栈顶。 */
    if (!node.member.computed) {
      const ASTNode *prop = node.children[1];
      if (prop->type == NodeType::PrivateIdentifier) {
        int idx = resolvePrivateName(prop->name);
        if (idx < 0) return false;
        builder_.load_private(static_cast<uint16_t>(idx));
      } else if (prop->type != NodeType::Identifier) {
        fail("invalid member property name");
        return false;
      } else {
        builder_.load_prop(prop->name);
      }
    } else {
      if (!compileExpr(*node.children[1])) return false;
      builder_.load_elem();
    }
    if (is_short) {
      builder_.jump(&skip);
      builder_.bind_label(short_circuit);
      builder_.pop(); /* 清操作对象（null/undefined） */
      builder_.push_op(Opcode::PushUndefined);
      builder_.jump(chain_end);
      builder_.bind_label(skip);
    }
    return true;
  }

  if (node.type == NodeType::CallExpression) {
    const bool is_short = isOptionalShortCircuit(node);
    BcLabel short_circuit;
    BcLabel skip;
    if (is_short) {
      /* ?.() 独立短路点：判断前一步结果（callee 值）nullish。 */
      short_circuit = builder_.new_label();
      skip = builder_.new_label();
      builder_.dup();
      builder_.jump_if_nullish(&short_circuit);
    }
    /* 调用：栈顶是 callee（已求值）。非短路点（链延续 a?.b()）直接调用。 */
    size_t argc = node.children.size() - 1;
    bool has_spread = false;
    for (size_t i = 1; i < node.children.size(); ++i) {
      if (node.children[i] != nullptr &&
          node.children[i]->type == NodeType::SpreadElement) {
        has_spread = true;
        break;
      }
    }
    if (has_spread) {
      /* 展开调用：callee 已在栈顶，参数构建到数组，CallArray。 */
      uint16_t args_slot = scope_->next_slot++;
      builder_.new_array();
      builder_.store_var(args_slot);
      std::vector<const ASTNode *> args(node.children.begin() + 1,
                                        node.children.end());
      if (!compileSpreadArgs(args, args_slot)) return false;
      builder_.load_var(args_slot);
      builder_.call_array();
    } else {
      for (size_t i = 1; i < node.children.size(); ++i) {
        if (!compileExpr(*node.children[i])) return false;
      }
      builder_.call(static_cast<uint16_t>(argc));
    }
    if (is_short) {
      builder_.jump(&skip);
      builder_.bind_label(short_circuit);
      builder_.pop(); /* 清 callee（null/undefined） */
      builder_.push_op(Opcode::PushUndefined);
      builder_.jump(chain_end);
      builder_.bind_label(skip);
    }
    return true;
  }

  fail("invalid optional chain node type");
  return false;
}

/* delete 可选链（M5.1e-e，§13.5.1.2）：delete a?.b / delete a?.b.c。
 * 语义：链头 nullish -> 短路，delete 结果为 true（不删除任何东西）；
 * 链头非 nullish -> 求值整条引用链并删除最后属性。
 * 与普通可选链不同：短路路径结果压 true 而非 undefined。
 *
 * 发射：
 *   [链头]  （同 compileOptionalChain：先收集链，逐节点处理）
 *   短路点：Dup; JumpIfNullish short_i
 *           [访问本节点]（非最后）或 [删除本节点]（最后）-> 结果留栈顶
 *           Jump skip_i
 *           short_i: Pop; PushTrue; Jump chain_end
 *           skip_i:
 *   链延续：直接访问/删除（不判断）
 *   chain_end: */
bool Compiler::compileDeleteOptionalChain(const ASTNode &node) {
  /* 1. 识别链头（同 compileOptionalChain）。 */
  const ASTNode *first_opt = &node;
  while (true) {
    const ASTNode *obj =
        first_opt->children.empty() ? nullptr : first_opt->children[0];
    if (obj == nullptr || !isOptionalChainNode(*obj)) break;
    first_opt = obj;
  }
  if (first_opt->children.empty()) {
    fail("invalid optional chain node");
    return false;
  }
  const ASTNode *head = first_opt->children[0];

  /* 2. 收集链节点（从内到外）。 */
  std::vector<const ASTNode *> path;
  for (const ASTNode *cur = &node;; cur = cur->children[0]) {
    path.push_back(cur);
    if (cur == first_opt) break;
  }
  std::vector<const ASTNode *> chain(path.rbegin(), path.rend());

  /* 3. 编译链头。 */
  if (!compileExpr(*head)) return false;

  /* 4. 逐节点：非最后节点访问（引用求值），最后节点删除。 */
  BcLabel chain_end = builder_.new_label();
  for (size_t i = 0; i < chain.size(); ++i) {
    if (!compileDeleteChainNode(*chain[i], /*is_last=*/(i + 1 == chain.size()),
                                &chain_end))
      return false;
  }
  builder_.bind_label(chain_end);
  return true;
}

/* delete 可选链单节点（M5.1e-e）：非最后节点做访问（引用链求值，结果
 * 留栈顶供下节点）；最后节点做删除（obj -> bool）。短路点语义同
 * compileOptionalChainNode，但短路路径压 true（delete 结果）。
 * chain_end：整链短路汇聚标签（短路路径跳转）。 */
bool Compiler::compileDeleteChainNode(const ASTNode &node, bool is_last,
                                      BcLabel *chain_end) {
  if (node.type == NodeType::MemberExpression) {
    if (node.children.size() < 2) {
      fail("member expression without property");
      return false;
    }
    if (node.children[1]->type == NodeType::PrivateIdentifier) {
      fail("SyntaxError: Private fields can not be deleted");
      return false;
    }
    const bool is_short = isOptionalShortCircuit(node);
    BcLabel short_circuit;
    BcLabel skip;
    if (is_short) {
      short_circuit = builder_.new_label();
      skip = builder_.new_label();
      builder_.dup();
      builder_.jump_if_nullish(&short_circuit);
    }
    if (!node.member.computed) {
      if (node.children[1]->type != NodeType::Identifier) {
        fail("invalid member property name");
        return false;
      }
      if (is_last) {
        builder_.delete_prop(node.children[1]->name); /* obj -> bool */
      } else {
        builder_.load_prop(node.children[1]->name);
      }
    } else {
      if (!compileExpr(*node.children[1])) return false; /* key */
      if (is_last) {
        builder_.delete_elem(); /* obj, key -> bool */
      } else {
        builder_.load_elem();
      }
    }
    if (is_short) {
      builder_.jump(&skip);
      builder_.bind_label(short_circuit);
      builder_.pop(); /* 清操作对象（null/undefined） */
      builder_.push_op(Opcode::PushTrue);
      builder_.jump(chain_end);
      builder_.bind_label(skip);
    }
    return true;
  }
  fail("invalid optional chain delete node type");
  return false;
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
    /* parser 已拦截 a?.b = x（invalid assignment target），防御。 */
    fail("SyntaxError: assignment to optional chaining");
    return false;
  }
  const bool computed = lhs->member.computed;
  const ASTNode *obj_expr = lhs->children[0];
  const ASTNode *key_expr = computed ? lhs->children[1] : nullptr;

  /* 非计算成员：属性名（Identifier / PrivateIdentifier）。 */
  std::string prop_name;
  int priv_idx = -1;
  if (!computed) {
    const ASTNode *prop = lhs->children[1];
    if (prop->type == NodeType::PrivateIdentifier) {
      /* M5.1d-c2 私有字段赋值：#x 须在 class 词法作用域内声明。 */
      priv_idx = resolvePrivateName(prop->name);
      if (priv_idx < 0) return false;
    } else if (prop->type != NodeType::Identifier) {
      fail("invalid member property name");
      return false;
    } else {
      prop_name = prop->name;
    }
  }

  /* 逻辑复合赋值（&&= / ||= / ??=）：短路语义（M5.1b）。
   * 对象表达式求值一次（暂存），短路返回旧值（重新读 obj.p），
   * 非短路求 rhs 写回。 */
  if (node.assign_op == AssignmentOpKind::LogicalAndAssign ||
      node.assign_op == AssignmentOpKind::LogicalOrAssign ||
      node.assign_op == AssignmentOpKind::NullishAssign) {
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
    } else if (priv_idx >= 0) {
      builder_.load_private(static_cast<uint16_t>(priv_idx));
    } else {
      builder_.load_prop(prop_name);
    }
    /* 赋值条件判断：成立跳转赋值路径，否则落短路路径。 */
    BcLabel assign_path = builder_.new_label();
    BcLabel done = builder_.new_label();
    switch (node.assign_op) {
      case AssignmentOpKind::LogicalAndAssign:
        builder_.jump_if_true(&assign_path);
        break;
      case AssignmentOpKind::LogicalOrAssign:
        builder_.jump_if_false(&assign_path);
        break;
      case AssignmentOpKind::NullishAssign:
        builder_.jump_if_nullish(&assign_path);
        break;
      default:
        break;
    }
    /* 短路：返回旧值（重新读 obj.p）。 */
    builder_.load_var(t_obj);
    if (computed) {
      builder_.load_var(t_key);
      builder_.load_elem();
    } else if (priv_idx >= 0) {
      builder_.load_private(static_cast<uint16_t>(priv_idx));
    } else {
      builder_.load_prop(prop_name);
    }
    builder_.jump(&done);
    /* 赋值路径：求 rhs，暂存，压 obj(+key)+rhs 写回（StoreProp 留 rhs 栈顶）。 */
    builder_.bind_label(assign_path);
    if (!compileExpr(*rhs)) return false;
    uint16_t t_val = scope_->next_slot++;
    builder_.store_var(t_val);
    builder_.load_var(t_obj);
    if (computed) builder_.load_var(t_key);
    builder_.load_var(t_val);
    if (computed) {
      builder_.store_elem();
    } else if (priv_idx >= 0) {
      builder_.store_private(static_cast<uint16_t>(priv_idx));
    } else {
      builder_.store_prop(prop_name);
    }
    builder_.bind_label(done);
    return true;
  }

  const bool compound = (node.assign_op != AssignmentOpKind::Assign);
  if (!compound) {
    /* 简单赋值：对象表达式求值一次，留栈顶供 StoreProp/StoreElem 消费。
     * 私有字段：obj.#x = rhs -> obj; rhs; StorePrivate {idx}（receiver, val -> val）。 */
    if (!compileExpr(*obj_expr)) return false;
    if (computed && !compileExpr(*key_expr)) return false;
    if (!compileExpr(*rhs)) return false;
    if (computed) {
      builder_.store_elem();
    } else if (priv_idx >= 0) {
      builder_.store_private(static_cast<uint16_t>(priv_idx));
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
  } else if (priv_idx >= 0) {
    builder_.load_private(static_cast<uint16_t>(priv_idx));
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
  } else if (priv_idx >= 0) {
    builder_.store_private(static_cast<uint16_t>(priv_idx));
  } else {
    builder_.store_prop(prop_name);
  }
  return true;
}

/* 更新表达式（M5.1e-b）：++x / x++ / --x / x--（§13.4.4）。
 * 语义：读旧值，ToNumeric 后 ±1（Increment 指令），写回。
 *   前缀：表达式值 = 新值；
 *   后缀：表达式值 = 旧值（§13.4.4.2 步骤 6.b：oldValue 先保存）。
 * 目标：Identifier（局部/捕获/全局——全局暂不支持，见下）、
 *   MemberExpression（含私有字段 obj.#x++、计算键 obj[k]++）。 */
bool Compiler::compileUpdate(const ASTNode &node) {
  if (node.children.empty()) {
    fail("update expression without operand");
    return false;
  }
  const ASTNode *target = node.children[0];
  const bool prefix = node.prefix;
  const uint16_t dec = (node.update_op == UpdateOpKind::Dec) ? 1 : 0;

  if (target->type == NodeType::Identifier) {
    uint16_t slot = 0, capture = 0;
    bool is_upvalue = false;
    bool is_global = false;
    uint16_t import_idx = 0;
    bool is_import = false;
    if (!resolveAssignTarget(target->name, &slot, &capture, &is_upvalue)) {
      /* M5.2a：import 绑定 ++/-- 是写入（immutable binding）-> TypeError。
       * 读旧值 -> StoreImport（恒抛错）保持栈平衡。 */
      auto import_it = module_import_map_.find(target->name);
      if (import_it != module_import_map_.end()) {
        is_import = true;
        import_idx = import_it->second;
      } else {
        /* 全局标识符 ++x / x++：LoadGlobal 读旧值，Increment，StoreGlobal 写回
        * （§13.4.4，M5.1e-g 隐式全局，非严格）。 */
        is_global = true;
      }
    }
    /* 读旧值 -> 栈顶。 */
    if (is_import) {
      builder_.load_import(import_idx);
    } else if (is_global) {
      builder_.load_global(target->name);
    } else if (is_upvalue) {
      builder_.load_upvalue(capture);
    } else {
      builder_.load_var(slot);
    }
    uint16_t t_old = 0;
    if (!prefix) {
      /* 后缀：旧值暂存到临时槽（表达式结果）。 */
      t_old = scope_->next_slot++;
      builder_.store_var(t_old);
      builder_.load_var(t_old);
    }
    builder_.increment(dec); /* 新值 -> 栈顶 */
    /* 写回（Increment 后新值在栈顶，Store* 弹栈）。 */
    if (is_import) {
      /* StoreImport 恒抛 TypeError，结果路径不可达；弹栈后补 LoadImport
       * 保持栈平衡（前缀结果需栈顶有值）。 */
      builder_.store_import(import_idx);
      builder_.load_import(import_idx);
    } else if (is_global) {
      builder_.store_global(target->name); /* 保留新值在栈顶（赋值结果） */
      builder_.pop(); /* 清保留值：结果统一在下方重新装载 */
    } else if (is_upvalue) {
      builder_.store_upvalue(capture);
    } else {
      builder_.store_var(slot);
    }
    /* 结果：前缀 = 新值（重新读），后缀 = 旧值（已在临时槽）。 */
    if (prefix) {
      if (is_import) {
        builder_.load_import(import_idx);
      } else if (is_global) {
        builder_.load_global(target->name);
      } else if (is_upvalue) {
        builder_.load_upvalue(capture);
      } else {
        builder_.load_var(slot);
      }
    } else {
      builder_.load_var(t_old);
    }
    return true;
  }

  if (target->type == NodeType::MemberExpression) {
    if (target->member.optional) {
      /* parser 已拦截 a?.b++（invalid increment/decrement operand），防御。 */
      fail("SyntaxError: increment/decrement of optional chaining");
      return false;
    }
    const bool computed = target->member.computed;
    const ASTNode *obj_expr = target->children[0];
    const ASTNode *key_expr = computed ? target->children[1] : nullptr;
    std::string prop_name;
    int priv_idx = -1;
    if (!computed) {
      const ASTNode *prop = target->children[1];
      if (prop->type == NodeType::PrivateIdentifier) {
        priv_idx = resolvePrivateName(prop->name);
        if (priv_idx < 0) return false;
      } else if (prop->type != NodeType::Identifier) {
        fail("invalid member property name");
        return false;
      } else {
        prop_name = prop->name;
      }
    }

    /* 对象（和计算键）求值一次，暂存。 */
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
    } else if (priv_idx >= 0) {
      builder_.load_private(static_cast<uint16_t>(priv_idx));
    } else {
      builder_.load_prop(prop_name);
    }
    uint16_t t_old = 0;
    if (!prefix) {
      /* 后缀：旧值暂存。 */
      t_old = scope_->next_slot++;
      builder_.store_var(t_old);
      builder_.load_var(t_old);
    }
    builder_.increment(dec); /* 新值 -> 栈顶 */
    /* 写回：压 obj(+key) + 新值。新值先在栈顶，暂存后重排。 */
    uint16_t t_new = scope_->next_slot++;
    builder_.store_var(t_new);
    builder_.load_var(t_obj);
    if (computed) builder_.load_var(t_key);
    builder_.load_var(t_new);
    if (computed) {
      builder_.store_elem();
    } else if (priv_idx >= 0) {
      builder_.store_private(static_cast<uint16_t>(priv_idx));
    } else {
      builder_.store_prop(prop_name);
    }
    /* 结果：前缀 = 新值；后缀 = 旧值。 */
    if (prefix) {
      builder_.load_var(t_new);
    } else {
      builder_.load_var(t_old);
    }
    return true;
  }

  fail("invalid update expression operand");
  return false;
}

bool Compiler::compileUnary(const ASTNode &node) {
  if (node.children.empty()) {
    fail("unary op without operand");
    return false;
  }
  const ASTNode *operand = node.children[0];

  /* delete 特殊处理（§13.5.1）：只删除属性不读取值；非引用操作数
   * 不求值直接返回 true（§13.5.1.2 步骤 1）。 */
  if (node.unop == UnaryOpKind::Delete) {
    if (operand->type == NodeType::MemberExpression) {
      if (operand->member.optional) {
        /* delete a?.b / delete a?.b.c（M5.1e-e）：可选链删除，短路返回 true。 */
        return compileDeleteOptionalChain(*operand);
      }
      if (operand->children.size() < 2) {
        fail("delete member without object");
        return false;
      }
      if (!compileExpr(*operand->children[0])) return false; /* obj */
      if (!operand->member.computed) {
        const ASTNode *prop = operand->children[1];
        if (prop->type == NodeType::PrivateIdentifier) {
          /* delete obj.#x：SyntaxError（§13.5.1.1 步骤 1；parser 应已拦截）。 */
          fail("SyntaxError: Private fields can not be deleted");
          return false;
        }
        if (prop->type != NodeType::Identifier) {
          fail("invalid member property name");
          return false;
        }
        builder_.delete_prop(prop->name); /* obj -> bool */
        return true;
      }
      /* delete obj[expr]：obj; expr -> bool。 */
      if (!compileExpr(*operand->children[1])) return false;
      builder_.delete_elem();
      return true;
    }
    /* 非引用（标识符/字面量/调用等）：直接返回 true，不求值。 */
    builder_.push_op(Opcode::PushTrue);
    return true;
  }

  if (!compileExpr(*operand)) return false;
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
      /* 一元 +（§13.5.2）：ToNumber。 */
      builder_.to_number();
      return true;
    case UnaryOpKind::BitNot:
      /* 一元 ~（§13.5.5）：ToNumeric 后按位非。 */
      builder_.bit_not();
      return true;
    case UnaryOpKind::Delete:
      return true; /* 已在上方处理（不可达） */
    case UnaryOpKind::Await:
      /* await x（M5.4）：求值 x -> Await 指令（PromiseResolve 接合，
       * 未决则挂起当前 async 帧；VM 续延时把值压栈或抛异常）。
       * 仅 async 函数/模块顶层可达（parseUnary 已校验）。 */
      builder_.await();
      return true;
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

  /* #x in obj：私有字段存在性（M5.1d-c2，§8.3 PrivateFieldFind）。
   * lhs 为 PrivateIdentifier（引用 class 私有字段），rhs 为接收者对象。
   * 发射：receiver; HasPrivate {name_index}（receiver 替换为布尔）。 */
  if (node.binop == BinaryOpKind::In && lhs->type == NodeType::PrivateIdentifier) {
    if (node.children.size() < 2) {
      fail("private 'in' without right operand");
      return false;
    }
    int idx = resolvePrivateName(lhs->name);
    if (idx < 0) return false;
    if (!compileExpr(*rhs)) return false;
    builder_.has_private(static_cast<uint16_t>(idx));
    return true;
  }

  /* in / instanceof（M5.1e）：编译两操作数，VM 经 inOperator/instanceOf。 */
  if (node.binop == BinaryOpKind::In) {
    if (!compileExpr(*lhs)) return false;
    if (!compileExpr(*rhs)) return false;
    builder_.in();
    return true;
  }
  if (node.binop == BinaryOpKind::Instanceof) {
    if (!compileExpr(*lhs)) return false;
    if (!compileExpr(*rhs)) return false;
    builder_.instance_of();
    return true;
  }

  /* ??（Nullish coalescing，M5.1e）：lhs 非 null/undefined 则取 lhs，
   * 否则求 rhs（§13.12.3）。发射（同 &&/|| 短路模式）：
   *   [lhs] store tmp
   *   load tmp; JumpIfNullish rhs_path   (nullish -> 求 rhs)
   *   load tmp; Jump done                (非 nullish -> 结果 = lhs)
   *   rhs_path: [rhs]
   *   done: */
  if (node.binop == BinaryOpKind::Nullish) {
    if (!compileExpr(*lhs)) return false;
    uint16_t tmp = scope_->next_slot++; /* 临时槽 */
    builder_.store_var(tmp);
    BcLabel rhs_path = builder_.new_label();
    BcLabel done = builder_.new_label();
    builder_.load_var(tmp);
    builder_.jump_if_nullish(&rhs_path);
    builder_.load_var(tmp); /* 非 nullish：结果 = lhs */
    builder_.jump(&done);
    builder_.bind_label(rhs_path);
    if (!compileExpr(*rhs)) return false;
    builder_.bind_label(done);
    return true;
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
  if (lhs->type == NodeType::ObjectPattern ||
      lhs->type == NodeType::ArrayPattern) {
    /* 解构赋值（M5.1a）：[a] = arr / {a} = obj。
     * Parser 已保证是纯 = 赋值（复合赋值给解构目标非法）。 */
    if (!compileExpr(*rhs)) return false;
    uint16_t tmp = scope_->next_slot++;
    builder_.store_var(tmp);
    if (!compilePattern(*lhs, tmp, /*is_declaration=*/false)) return false;
    builder_.load_var(tmp); /* 解构赋值表达式值 = 源值（§13.15.2） */
    return true;
  }
  if (lhs->type != NodeType::Identifier) {
    fail("invalid assignment target");
    return false;
  }
  uint16_t slot = 0;
  uint16_t capture = 0;
  bool is_upvalue = false;
  bool is_global = false;
  uint16_t import_idx = 0;
  bool is_import = false;
  if (!resolveAssignTarget(lhs->name, &slot, &capture, &is_upvalue)) {
    /* M5.2a：import 绑定赋值 -> StoreImport（immutable binding，运行期
     * TypeError，§16.2.3）。import 绑定不入局部槽，resolve 不到。 */
    auto import_it = module_import_map_.find(lhs->name);
    if (import_it != module_import_map_.end()) {
      is_import = true;
      import_idx = import_it->second;
    } else {
      /* 未声明标识符赋值（M5.1e-g）：非严格隐式全局 -> 写入 globalThis
       * 属性（§8.1.1.2.2 PutValue -> SetMutableBinding 隐式全局创建）。 */
      is_global = true;
    }
  }

  /* 逻辑复合赋值（&&= / ||= / ??=）：短路语义（M5.1b）。
   *   a &&= b：old truthy 求 b 写回；falsy 短路返回 old
   *   a ||= b：old falsy 求 b 写回；truthy 短路返回 old
   *   a ??= b：old nullish 求 b 写回；非 nullish 短路返回 old
   * 发射（统一布局，赋值条件跳转到赋值路径，否则落短路路径）：
   *   LoadVar slot; JumpIfX assign_path; [old] Jump done;
   *   assign_path: [rhs]; StoreVar slot; LoadVar slot; done:
   * 短路路径重新读 slot（JumpIfX 弹栈，且短路时未写回）。 */
  if (node.assign_op == AssignmentOpKind::LogicalAndAssign ||
      node.assign_op == AssignmentOpKind::LogicalOrAssign ||
      node.assign_op == AssignmentOpKind::NullishAssign) {
    BcLabel assign_path = builder_.new_label();
    BcLabel done = builder_.new_label();
    /* 读旧值并按赋值条件跳转。 */
    if (is_import) {
      builder_.load_import(import_idx);
    } else if (is_global) {
      builder_.load_global(lhs->name);
    } else if (is_upvalue) {
      builder_.load_upvalue(capture);
    } else {
      builder_.load_var(slot);
    }
    switch (node.assign_op) {
      case AssignmentOpKind::LogicalAndAssign:
        builder_.jump_if_true(&assign_path); /* truthy 赋值 */
        break;
      case AssignmentOpKind::LogicalOrAssign:
        builder_.jump_if_false(&assign_path); /* falsy 赋值 */
        break;
      case AssignmentOpKind::NullishAssign:
        builder_.jump_if_nullish(&assign_path); /* nullish 赋值 */
        break;
      default:
        break;
    }
    /* 短路路径：返回旧值（重新读 slot，未写回）。 */
    if (is_import) {
      builder_.load_import(import_idx);
    } else if (is_global) {
      builder_.load_global(lhs->name);
    } else if (is_upvalue) {
      builder_.load_upvalue(capture);
    } else {
      builder_.load_var(slot);
    }
    builder_.jump(&done);
    /* 赋值路径：求 rhs 写回，值 = rhs。 */
    builder_.bind_label(assign_path);
    if (!compileExpr(*rhs)) return false;
    if (is_import) {
      /* StoreImport 弹栈（报 TypeError），赋值结果 = 新值须保留在栈顶。 */
      builder_.store_import(import_idx);
      /* 不可达（StoreImport 恒抛错），编译期补 load 保持栈平衡。 */
      builder_.load_import(import_idx);
    } else if (is_global) {
      builder_.store_global(lhs->name); /* 保留新值在栈顶（赋值结果） */
    } else if (is_upvalue) {
      builder_.store_upvalue(capture);
      builder_.load_upvalue(capture);
    } else {
      builder_.store_var(slot);
      builder_.load_var(slot);
    }
    builder_.bind_label(done);
    return true;
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
    if (is_import) {
      builder_.load_import(import_idx); /* 旧值 */
    } else if (is_global) {
      builder_.load_global(lhs->name); /* 旧值 */
    } else if (is_upvalue) {
      builder_.load_upvalue(capture); /* 旧值 */
    } else {
      builder_.load_var(slot);
    }
    if (!compileExpr(*rhs)) return false;
    builder_.push_op(op);
    if (is_import) {
      builder_.store_import(import_idx); /* 恒抛 TypeError */
      builder_.load_import(import_idx);
    } else if (is_global) {
      builder_.store_global(lhs->name); /* 保留新值在栈顶（赋值结果） */
    } else if (is_upvalue) {
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
  if (is_import) {
    builder_.store_import(import_idx); /* 恒抛 TypeError */
    builder_.load_import(import_idx);
  } else if (is_global) {
    builder_.store_global(lhs->name); /* 保留新值在栈顶（赋值结果） */
  } else if (is_upvalue) {
    builder_.store_upvalue(capture); /* 弹栈 */
    builder_.load_upvalue(capture);  /* 保留赋值结果在栈顶 */
  } else {
    builder_.store_var(slot);
    builder_.load_var(slot);
  }
  return true;
}

bool Compiler::compileCall(const ASTNode &node) {
  /* M5.1d-c1 super()：派生类构造器调用基类构造器于当前帧 this。
   * 与 CallExpression 不同，SuperCall 的 children 是纯参数列表
   * （非 [callee, args...]），故需单独处理：argc = children.size()，
   * 且无参 super() 时 children 为空属合法（不能走下方 empty 检查）。
   * 发射：args...; SuperCall {argc}（VM 从 HomeObject.[[Prototype]]
   * 取基类构造器，this = 当前帧 this）。 */
  if (node.type == NodeType::SuperCall) {
    size_t argc = node.children.size();
    bool has_spread = false;
    for (size_t i = 0; i < node.children.size(); ++i) {
      if (node.children[i] != nullptr &&
          node.children[i]->type == NodeType::SpreadElement) {
        has_spread = true;
        break;
      }
    }
    if (has_spread) {
      /* M5.1e-h super(...args)：参数构建到数组，SuperCallArray 展开调用
       * （语义同 SuperCall，基类构造器于当前帧 this，栈顶为 args 数组）。 */
      uint16_t args_slot = scope_->next_slot++;
      builder_.new_array();
      builder_.store_var(args_slot);
      std::vector<const ASTNode *> args(node.children.begin(),
                                        node.children.end());
      if (!compileSpreadArgs(args, args_slot)) return false;
      builder_.load_var(args_slot);
      builder_.super_call_array();
    } else {
      for (size_t i = 0; i < node.children.size(); ++i) {
        if (!compileExpr(*node.children[i])) return false;
      }
      builder_.super_call(static_cast<uint16_t>(argc));
    }
    /* M5.1d-c2/d 派生类构造器：super() 返回后实例字段初始化（§9.2.2.4
     * 步骤 8——字段初始化器在 super() 之后求值）。仅构造器且字段初始化
     * 未发射时发射一次。 */
    if (current_is_ctor_ && current_is_derived_ && !field_init_emitted_) {
      if (!emitInstanceFieldInits()) return false;
    }
    return true;
  }

  if (node.children.empty()) {
    fail("call without callee");
    return false;
  }
  const ASTNode *callee = node.children[0];
  size_t argc = node.children.size() - 1;

  /* 展开调用（含展开参数）：f(...a, b) / obj.m(...a)。
   * 成员调用保留 this（obj, func），普通调用只需 func。
   * 参数构建到临时数组，CallArray（普通）/ CallMethodArray（成员 this=obj）
   * 展开；无展开场景走 Call/CallMethod（M5.1e-h 成员展开 this 补全）。 */
  bool has_spread = false;
  for (size_t i = 1; i < node.children.size(); ++i) {
    if (node.children[i] != nullptr &&
        node.children[i]->type == NodeType::SpreadElement) {
      has_spread = true;
      break;
    }
  }

  if (callee->type == NodeType::SuperProperty) {
    /* M5.1d-b super.m(args)：this = 当前帧 this（§12.3.5.3）。
     * 发射：LoadThis; SuperLoadProp; args...; CallMethod。
     * 栈 [this, func, args] 与 CallMethod 布局一致。 */
    if (callee->member.optional || callee->children.empty()) {
      /* parser 已拦截 super?.m()（'super' keyword unexpected here），防御。 */
      fail("SyntaxError: optional chaining on super");
      return false;
    }
    builder_.load_this();
    if (!callee->member.computed) {
      const ASTNode *prop = callee->children[0];
      if (prop->type != NodeType::Identifier) {
        fail("invalid super property name");
        return false;
      }
      builder_.super_load_prop(prop->name);
    } else {
      if (!compileExpr(*callee->children[0])) return false;
      builder_.super_load_elem();
    }
    if (has_spread) {
      /* M5.1e-h super.m(...a)：this=当前帧 this，构建 args 数组后
       * CallMethodArray（栈 [this, func, args_array]，this 保留）。 */
      uint16_t args_slot = scope_->next_slot++;
      builder_.new_array();
      builder_.store_var(args_slot);
      std::vector<const ASTNode *> args(node.children.begin() + 1,
                                        node.children.end());
      if (!compileSpreadArgs(args, args_slot)) return false;
      builder_.load_var(args_slot);
      builder_.call_method_array();
      return true;
    }
    for (size_t i = 1; i < node.children.size(); ++i) {
      if (!compileExpr(*node.children[i])) return false;
    }
    builder_.call_method(static_cast<uint16_t>(argc));
    return true;
  }

  if (callee->type == NodeType::MemberExpression) {
    if (callee->member.optional || callee->children.size() < 2) {
      /* compileExpr 已前置调度可选链调用到 compileOptionalChain，防御。 */
      fail("internal: optional chaining call should be compiled by compileOptionalChain");
      return false;
    }
    const ASTNode *prop = callee->children[1];
    /* M5.1d-c2 私有方法调用 obj.#m(args)：LoadPrivate 把 obj 替换为值。 */
    int priv_idx = -1;
    if (prop->type == NodeType::PrivateIdentifier) {
      priv_idx = resolvePrivateName(prop->name);
      if (priv_idx < 0) return false;
    }
    if (has_spread) {
      /* M5.1e-h 成员展开调用 obj.m(...a) / obj.#m(...a)：求值 obj 得
       * this，dup 保留 obj（栈 [obj, obj]），读方法值（LoadProp/
       * LoadPrivate/LoadElem 替换栈顶为 func -> 栈 [obj, func]），
       * 构建 args 数组后 CallMethodArray（栈 [obj, func, args_array]，
       * this=obj 保留）。 */
      if (!compileExpr(*callee->children[0])) return false; /* obj */
      builder_.dup();                                        /* obj, obj */
      if (priv_idx >= 0) {
        builder_.load_private(static_cast<uint16_t>(priv_idx));
      } else if (!callee->member.computed) {
        if (prop->type != NodeType::Identifier) {
          fail("invalid member property name");
          return false;
        }
        builder_.load_prop(prop->name);
      } else {
        if (!compileExpr(*prop)) return false;
        builder_.load_elem();
      }
      uint16_t args_slot = scope_->next_slot++;
      builder_.new_array();
      builder_.store_var(args_slot);
      std::vector<const ASTNode *> args(node.children.begin() + 1,
                                        node.children.end());
      if (!compileSpreadArgs(args, args_slot)) return false;
      builder_.load_var(args_slot);
      builder_.call_method_array();
      return true;
    }
    if (!compileExpr(*callee->children[0])) return false; /* obj */
    builder_.dup();                                        /* obj, obj */
    if (!callee->member.computed) {
      /* M5.1d-c2：私有方法调用 prop 为 PrivateIdentifier（priv_idx 分支
       * 已 load_private），放行；普通成员要求 Identifier。 */
      if (priv_idx >= 0) {
        builder_.load_private(static_cast<uint16_t>(priv_idx)); /* obj, func */
      } else {
        if (prop->type != NodeType::Identifier) {
          fail("invalid member property name");
          return false;
        }
        builder_.load_prop(prop->name); /* obj, func */
      }
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
  if (has_spread) {
    /* f(...a, b)：参数构建到数组，CallArray 展开调用。 */
    uint16_t args_slot = scope_->next_slot++;
    builder_.new_array();
    builder_.store_var(args_slot);
    std::vector<const ASTNode *> args(node.children.begin() + 1,
                                      node.children.end());
    if (!compileSpreadArgs(args, args_slot)) return false;
    builder_.load_var(args_slot);
    builder_.call_array();
    return true;
  }
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
      /* parser 已拦截 `new a?.b()` 为语法错误，此处不可达。 */
      fail("SyntaxError: optional chaining in new callee");
      return false;
    }
    if (!compileExpr(*callee->children[0])) return false;
    const ASTNode *prop = callee->children[1];
    if (prop->type == NodeType::PrivateIdentifier) {
      /* M5.1e-h new a.#x(...)：LoadPrivate 把 a 替换为构造器值，
       * 布局变 [ctor, args...] 与普通 new 一致（new 不保留接收者）。 */
      int priv_idx = resolvePrivateName(prop->name);
      if (priv_idx < 0) return false;
      if (!compileExpr(*callee->children[0])) return false;
      builder_.load_private(static_cast<uint16_t>(priv_idx));
    } else if (!callee->member.computed) {
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
  /* 展开 new：new Foo(...a, b) —— 参数构建到数组，ConstructArray 展开。 */
  bool has_spread = false;
  for (size_t i = 1; i < node.children.size(); ++i) {
    if (node.children[i] != nullptr &&
        node.children[i]->type == NodeType::SpreadElement) {
      has_spread = true;
      break;
    }
  }
  if (has_spread) {
    uint16_t args_slot = scope_->next_slot++;
    builder_.new_array();
    builder_.store_var(args_slot);
    std::vector<const ASTNode *> args(node.children.begin() + 1,
                                      node.children.end());
    if (!compileSpreadArgs(args, args_slot)) return false;
    builder_.load_var(args_slot);
    builder_.construct_array();
    return true;
  }
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
 * 否则 LoadGlobal。返回 true 表示已发射 load 指令。
 * M5.2a 模块：模块求值函数内 import 绑定名命中 module_import_map_ 时发射
 * LoadImport（import 绑定不入局部槽，动态查源模块 env）。 */
bool Compiler::emitLoadIdentifier(const std::string &name) {
  uint16_t slot = 0;
  if (resolveLocal(name, &slot)) {
    builder_.load_var(slot);
    return true;
  }

  /* M5.2a：import 绑定读取（本函数作用域内最先命中，遮蔽外层局部/全局）。
   * M5.2d：namespace 导入（import * as ns）走 LoadImportNamespace 生成导出
   * 镜像对象；具名/默认导入走 LoadImport 读 live binding。 */
  auto import_it = module_import_map_.find(name);
  if (import_it != module_import_map_.end()) {
    if (module_imports_[import_it->second].is_namespace) {
      builder_.load_import_namespace(import_it->second);
    } else {
      builder_.load_import(import_it->second);
    }
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
