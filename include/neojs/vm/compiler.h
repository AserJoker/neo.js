/*
 * vm 层：表达式字节码生成器（M4.2）。
 *
 * 架构（docs/architecture.md §4.3 + scope-redesign.md §10.8）：
 *   - 编译器：AST → 字节码。M4 仅表达式（无函数/语句）。
 *   - 作用域解析在编译期：M4 表达式仅支持全局/当前作用域变量（LoadVar）。
 *   - 逻辑短路（&&/||/??）用跳转实现，值语义与 engine 逻辑运算一致。
 *
 * 约定：
 *   - compile_expression 递归编译表达式到字节码。
 *   - 表达式求值结果压在操作数栈顶。
 *   - 未支持的节点返回 false（不产出字节码）。
 */

#ifndef NEOJS_VM_COMPILER_H
#define NEOJS_VM_COMPILER_H

#include "neojs/frontend/ast.h"
#include "neojs/vm/bytecode.h"

namespace neojs {
namespace vm {

/* 编译表达式子树到 bc。返回 true 成功，false 未支持节点。 */
bool compile_expression(ASTNode *node, Bytecode &bc);

/* 编译单条语句到 bc（M5-A：声明/控制流/return 等）。 */
bool compile_statement(ASTNode *node, Bytecode &bc);

/* 编译 Program（顶层语句列表）到 bc。返回 true 成功。 */
bool compile_program(ASTNode *node, Bytecode &bc);

}  // namespace vm
}  // namespace neojs

#endif  // NEOJS_VM_COMPILER_H
