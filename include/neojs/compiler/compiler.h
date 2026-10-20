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

/* 作用域帧（M2.5 闭包）：编译期函数嵌套栈。
 * 每个函数编译期间一个帧，记录该函数声明的局部变量（用于判断
 * 标识符是局部还是捕获外层）；父帧 = 外层函数。 */
struct CompilerScope {
  CompilerScope *parent = nullptr;       /* 外层函数作用域帧 */
  std::unordered_map<std::string, uint16_t> locals; /* 本函数局部变量 -> 槽号 */
  uint16_t next_slot = 0;                /* 本函数下一个可用槽 */
  std::vector<UpvalueCapture> captures;  /* 本函数捕获列表 */
};

/* AST → 字节码编译器（M2.4 ① 骨架 + 表达式编译；M2.5 闭包作用域分析）。 */
class Compiler {
 public:
  Compiler() = default;

  /* 编译顶层程序（Program/Module 根）：产出函数字节码。
   * 失败时返回 ok=false + error。 */
  CompileResult compile(const AST &ast);

  /* 编译单个函数（FunctionDeclaration/FunctionExpression/ArrowFunctionExpression）：
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
  /* 对象 / 数组字面量（M3.1）：NewObject/NewArray + 逐属性/元素 StoreProp/StoreElem。
   * objectKeyToString 把对象属性键（Identifier/String/Number）转常量池字符串。 */
  bool compileObjectLiteral(const ASTNode &node);
  bool compileArrayLiteral(const ASTNode &node);
  bool objectKeyToString(const ASTNode *key, std::string *out);

  /* 成员访问读取（M3.1）：obj.prop（LoadProp）/ obj[expr]（LoadElem）。 */
  bool compileMemberLoad(const ASTNode &node);
  /* 成员赋值（M3.2）：obj.prop = rhs / obj[expr] = rhs（含复合 op=）。
   * 返回 false 时已设置错误。 */
  bool compileMemberAssign(const ASTNode &node);
  bool compileUnary(const ASTNode &node);
  bool compileBinary(const ASTNode &node);
  bool compileConditional(const ASTNode &node);
  bool compileAssignment(const ASTNode &node);
  bool compileCall(const ASTNode &node);
  bool compileSequence(const ASTNode &node);

  /* 赋值目标（Identifier -> StoreVar/StoreUpvalue；MemberExpression -> compileMemberAssign）。 */
  bool isSimpleAssignTarget(const ASTNode &node) const;

  /* 局部变量槽分配：声明/参数登记；引用解析（未找到 -> LoadGlobal）。 */
  uint16_t declareLocal(const std::string &name);
  bool resolveLocal(const std::string &name, uint16_t *slot);

  /* 标识符解析（M2.5）：本函数局部 / 外层局部（捕获 upvalue）/ 全局。
   * 返回 true 表示已发射 load 指令。 */
  bool emitLoadIdentifier(const std::string &name);
  /* 赋值目标解析（M2.5）：局部 / 捕获 upvalue / 全局。
   * 返回 true 表示目标是局部或捕获（slot/capture 已填），false 表示全局。 */
  bool resolveAssignTarget(const std::string &name, uint16_t *slot,
                           uint16_t *capture, bool *is_upvalue);

  /* 函数编译内部：解析参数 + 编译 body（共享于 compile/compileFunction）。 */
  bool compileFunctionBody(const ASTNode &fn);

  /* 嵌套函数编译（M2.5）：编译子函数字节码加入当前 builder 的 nested 表，
   * 发射 MakeFunction 指令（声明时挂局部变量，表达式时留栈顶）。
   * is_declaration = true 时按函数名声明局部槽。 */
  bool compileNestedFunction(const ASTNode &fn, bool is_declaration = true);

  /* 编译子函数到独立 builder（共享参数/body 解析）。调用方负责
   * 保存/恢复外层 builder_ 与作用域帧。返回子函数字节码。 */
  bool compileChildFunction(const ASTNode &fn, FunctionBytecode *out);

  void fail(const std::string &msg) { error_ = msg; }

  BytecodeBuilder builder_;              /* 字节码发射器 */
  CompilerScope *scope_ = nullptr;       /* 当前函数作用域帧（M2.5） */
  std::vector<CompilerScope *> scopes_;  /* 作用域帧池（栈，owned） */
  bool failed_ = false;                  /* 编译失败标记 */
  std::string error_;                    /* 失败信息 */
};

}  // namespace neojs

#endif  // NEOJS_COMPILER_COMPILER_H
