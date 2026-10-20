/*
 * 内部 C++ 实现：Compiler（AST → 字节码）。
 *
 * 设计（架构 §5.1 指令集 + §4.3 编译模型）：
 *   - 输入 AST（Parser 产物），输出 FunctionBytecode（VM 消费）。
 *   - 复用 BytecodeBuilder 发射式 API；label 回填跳转目标。
 *   - 局部变量经槽位分配（name -> slot 映射）；未解析标识符走
 *     LoadGlobal（全局对象属性读取）。
 *   - 编译错误经 CompileResult{ok, error} 传递（#3 不抛 C++ 异常）；
 *     与运行时 ConversionResult 同风格。
 *
 * M2.4 分层：
 *   ① 编译器骨架 + 表达式编译（本模块）
 *   ② 语句编译（if/while/for/return/块）
 *   ③ 函数编译 + JS_Eval 端到端接入
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_COMPILER_COMPILER_H
#define NEOJS_COMPILER_COMPILER_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "neojs/compiler/bytecode.h"
#include "neojs/frontend/ast.h"

namespace neojs {

/* 编译结果：成功时 fn 为函数字节码；失败时 ok=false + error。 */
struct CompileResult {
  FunctionBytecode fn;
  bool ok = false;
  std::string error;
};

/* AST → 字节码编译器（M2.4 ① 骨架 + 表达式编译）。 */
class Compiler {
 public:
  Compiler() = default;

  /* 编译顶层程序（Program/Module 根）：产出函数字节码。
   * 失败时返回 ok=false + error。 */
  CompileResult compile(const AST &ast);

  /* 编译单个函数（FunctionDeclaration/FunctionExpression）：
   * 产出该函数的 FunctionBytecode（参数入局部槽 0..n-1，body 编译）。
   * 失败时返回 ok=false + error。 */
  CompileResult compileFunction(const ASTNode &fn);

  /* 收集顶层函数声明：返回 Program 中 FunctionDeclaration 节点列表。
   * 供 JS_Eval 在程序执行前实例化函数对象并挂到 global。 */
  std::vector<const ASTNode *> collectTopLevelFunctions(const AST &ast);

 private:
  /* 编译表达式：求值后栈顶为结果。 */
  bool compileExpr(const ASTNode &node);

  /* 变量声明语句：为声明器分配局部槽 + 编译初始化。 */
  bool compileVarDecl(const ASTNode &decl);

  /* 语句编译（M2.4 ②）。 */
  bool compileStmt(const ASTNode &node);
  bool compileBlock(const ASTNode &node);
  bool compileIf(const ASTNode &node);
  bool compileWhile(const ASTNode &node);
  bool compileFor(const ASTNode &node);
  bool compileReturn(const ASTNode &node);
  bool compileDoWhile(const ASTNode &node);

  /* 构造失败结果（error_ 已设置）。 */
  CompileResult failResult();

  /* 字面量 / 标识符 / 一元 / 二元 / 条件 / 赋值 / 调用 / 序列。 */
  bool compileLiteral(const ASTNode &node);
  bool compileIdentifier(const ASTNode &node);
  bool compileUnary(const ASTNode &node);
  bool compileBinary(const ASTNode &node);
  bool compileConditional(const ASTNode &node);
  bool compileAssignment(const ASTNode &node);
  bool compileCall(const ASTNode &node);
  bool compileSequence(const ASTNode &node);

  /* 赋值目标（Identifier -> StoreVar；MemberExpression 暂不支持）。 */
  bool isSimpleAssignTarget(const ASTNode &node) const;

  /* 局部变量槽分配：声明/参数登记；引用解析（未找到 -> LoadGlobal）。 */
  uint16_t declareLocal(const std::string &name);
  bool resolveLocal(const std::string &name, uint16_t *slot);

  void fail(const std::string &msg) { error_ = msg; }

  BytecodeBuilder builder_;              /* 字节码发射器 */
  std::unordered_map<std::string, uint16_t> locals_; /* 局部变量名 -> 槽号 */
  uint16_t next_slot_ = 0;               /* 下一个可用局部槽 */
  bool failed_ = false;                  /* 编译失败标记 */
  std::string error_;                    /* 失败信息 */
};

}  // namespace neojs

#endif  // NEOJS_COMPILER_COMPILER_H
