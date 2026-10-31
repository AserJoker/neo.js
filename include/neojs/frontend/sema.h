/*
 * 前端：sema 独立阶段（AST 后、compile 前，scope-redesign.md §10.8）。
 *
 * 定位：
 *   - 独立阶段：AST 生成后、compile 前。
 *   - 保守：只模拟作用域和变量关系，不校验真实来源。
 *   - 这是"收集变量提升"的基础。
 *
 * 核心机制：shadow_scope + shadow_value
 *   - 遍历语法树，模拟真实的作用域创建销毁、变量定义、标识符查找。
 *   - 对每个标识符访问，查找它的词法来源。
 *   - 产出的挂载：
 *     1. shadow_scope 挂到"产生作用域的 AST 节点"上（node->scope_info）。
 *     2. 定义点引用挂到 identifier 访问节点上（node->resolved）。
 *     3. 定义点和访问点穿越函数 scope 时 → 名字加进函数 AST 的捕获列表
 *        （函数节点 node->captures）。
 *
 * 变量提升（sema 侧消减）：
 *   - 每个 shadow_scope 记录该 scope 的所有变量：
 *     - var：向上查找到最近的函数/模块级 scope（提升）。
 *     - let/const：找当前 scope。
 *   - compile 不需要在定义点做提升；作用域进入时按 shadow_scope 变量清单
 *     创建绑定（var=undefined、let/const=UNINITIALIZED 哨兵），源码声明处
 *     退化为一条赋值语句。
 *
 * 字节码是运行/编译分水岭：shadow 纯编译期，不流到运行期。
 */

#ifndef NEOJS_FRONTEND_SEMA_H
#define NEOJS_FRONTEND_SEMA_H

#include <string>
#include <unordered_map>
#include <vector>

#include "neojs/core/allocator.h"
#include "neojs/frontend/ast.h"

namespace neojs {
namespace sema {

using neojs::ASTNode;
using neojs::VarKind;

/* 编译期变量（shadow_value）：记录归属 scope + 定义节点。 */
struct ShadowValue : core::AllocatedObject {
  std::string name;
  VarKind kind = VarKind::Var;
  ASTNode *decl = nullptr; /* 定义节点（VariableDeclarator / 参数 Identifier） */
};

/* 编译期作用域（shadow_scope）：模拟运行时作用域树。
 * is_function：函数/模块级（var 提升目标）。 */
struct ShadowScope : core::AllocatedObject {
  ShadowScope *parent = nullptr;
  std::vector<ShadowScope *> children;
  std::vector<ShadowValue *> values; /* 本 scope 定义的变量 */
  std::unordered_map<std::string, ShadowValue *> named; /* name → value */
  bool is_function = false; /* var 提升目标（函数/模块级） */
  ASTNode *node = nullptr;  /* 产生该 scope 的 AST 节点（sema 遍历时挂） */
};

/* sema 遍历结果（编译期，随 AST 存活）。 */
struct Result : core::AllocatedObject {
  ShadowScope *root = nullptr; /* Program/Module 顶层 shadow scope */
};

/* 遍历 AST，执行 sema：填充 shadow 树 + identifier 解析 + 捕获列表。
 * 返回 false 表示不支持的结构（M5-B 范围内应始终成功）。 */
bool analyze(ASTNode *root, Result &out);

}  // namespace sema
}  // namespace neojs

#endif  // NEOJS_FRONTEND_SEMA_H
