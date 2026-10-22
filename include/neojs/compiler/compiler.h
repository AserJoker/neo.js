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

/* M5.1e-c 跳转上下文（break/continue 目标）：循环/标签语句编译期间压栈。
 * 循环 push is_loop=true（continue 合法）；标签语句 push is_loop=false
 * （continue 到非循环标签非法）。无标签 break 找最近循环；有标签
 * break/continue 沿栈找匹配 label。 */
struct LoopContext {
  std::string label;        /* 关联标签名（可空） */
  BcLabel break_label;      /* break 目标 */
  BcLabel continue_label;   /* continue 目标（非循环时无效） */
  bool is_loop = false;     /* 是否循环（continue 合法） */
};

/* AST → 字节码编译器（M2.4 ① 骨架 + 表达式编译；M2.5 闭包作用域分析）。 */
class Compiler {
 public:
  Compiler() = default;
  /* scopes_ 为 owned 的 CompilerScope* 池：编译期逐函数 new，须在
   * 编译器销毁时释放（JS_Eval 每次新建 Compiler，否则泄漏整条作用域链）。 */
  ~Compiler() {
    for (CompilerScope *s : scopes_) delete s;
  }

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

  /* 语句编译（M2.4 ②）。循环函数 label 参数（M5.1e-c）：标签语句
   * 直接下传标签名给循环编译（label: for/while 时 continue label 合法），
   * 无标签时默认空。 */
  bool compileStmt(const ASTNode &node);
  bool compileBlock(const ASTNode &node);
  bool compileIf(const ASTNode &node);
  bool compileWhile(const ASTNode &node, const std::string &label = "");
  bool compileFor(const ASTNode &node, const std::string &label = "");
  bool compileForOf(const ASTNode &node, const std::string &label = ""); /* M4.4f for-of */
  bool compileForIn(const ASTNode &node, const std::string &label = ""); /* M5.1e-c for-in */
  bool compileLabeled(const ASTNode &node); /* M5.1e-c label: stmt（跳转上下文） */
  bool compileBreak(const ASTNode &node);   /* M5.1e-c break [label] */
  bool compileContinue(const ASTNode &node); /* M5.1e-c continue [label] */
  bool compileReturn(const ASTNode &node);
  bool compileDoWhile(const ASTNode &node, const std::string &label = "");

  /* 构造失败结果（error_ 已设置）。 */
  CompileResult failResult();

  /* 字面量 / 标识符 / 一元 / 二元 / 条件 / 赋值 / 调用 / 序列。 */
  bool compileLiteral(const ASTNode &node);
  bool compileIdentifier(const ASTNode &node);
  /* 对象 / 数组字面量（M3.1）：NewObject/NewArray + 逐属性/元素 StoreProp/StoreElem。
   * objectKeyToString 把对象属性键（Identifier/String/Number）转常量池字符串。 */
  bool compileObjectLiteral(const ASTNode &node);
  bool compileArrayLiteral(const ASTNode &node);
  /* 模板串（M5.1e）：quasi 与插值交替，ToString + Add 拼接。 */
  bool compileTemplateLiteral(const ASTNode &node);
  bool objectKeyToString(const ASTNode *key, std::string *out);

  /* 成员访问读取（M3.1）：obj.prop（LoadProp）/ obj[expr]（LoadElem）。 */
  bool compileMemberLoad(const ASTNode &node);
  /* 成员赋值（M3.2）：obj.prop = rhs / obj[expr] = rhs（含复合 op=）。
   * 返回 false 时已设置错误。 */
  bool compileMemberAssign(const ASTNode &node);
  bool compileUnary(const ASTNode &node);
  /* 更新表达式（M5.1e-b）：++x/x++/--x/x--（§13.4.4，标识符/成员/私有字段）。 */
  bool compileUpdate(const ASTNode &node);
  bool compileBinary(const ASTNode &node);
  bool compileConditional(const ASTNode &node);
  bool compileAssignment(const ASTNode &node);
  bool compileCall(const ASTNode &node);
  bool compileNew(const ASTNode &node); /* M4.2：new 构造 */
  bool compileSequence(const ASTNode &node);

  /* 赋值目标（Identifier -> StoreVar/StoreUpvalue；MemberExpression -> compileMemberAssign）。 */
  bool isSimpleAssignTarget(const ASTNode &node) const;

  /* 解构编译（M5.1a）：源值已在临时槽 src_slot，编译模式绑定。
   * is_declaration=true（声明）：目标标识符 declareLocal + store；
   * false（赋值）：目标标识符 resolveAssignTarget + store。 */
  bool compilePattern(const ASTNode &pattern, uint16_t src_slot, bool is_declaration);
  /* 单绑定目标（Identifier / 嵌套模式 / AssignmentPattern 默认值）。 */
  bool compileBindTarget(const ASTNode &target, bool is_declaration);
  /* 预声明解构模式中的全部绑定名（保证槽位分配稳定，声明语义）。 */
  bool declarePatternBindings(const ASTNode &pattern);

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
   * 保存/恢复外层 builder_ 与作用域帧。返回子函数字节码。
   * is_ctor = true 时该子函数是 class 构造器（M5.1d-c2 私有字段初始化
   * 在 body 前发射）；inherit_private 控制是否继承外层 class 的私有
   * 字段访问权（class 成员 true；嵌套函数仅箭头函数 true）。 */
  bool compileChildFunction(const ASTNode &fn, FunctionBytecode *out,
                            bool is_ctor = false, bool inherit_private = true);

  /* class 编译（M5.1d-a）：编译构造器函数 + prototype 构建 + 方法挂载 +
   * extends 原型链。is_declaration=true 时按类名声明局部槽。 */
  bool compileClass(const ASTNode &node, bool is_declaration);
  /* 编译单个类方法/构造器函数：compileChildFunction + 标志设置 +
   * MakeFunction，函数对象留栈顶。 */
  bool compileClassMemberFunction(const ASTNode &fn, const std::string &fn_name,
                                  bool is_ctor);
  /* 编译单个类元素（方法/访问器）挂到目标对象。 */
  bool compileClassElementDef(const ASTNode &prop, uint16_t proto_slot,
                              bool is_static);

  /* 展开参数列表（M5.1a）：把 args 的元素（含 SpreadElement）追加到槽
   * slot 的数组。非展开元素 AppendElem，展开 AppendSpread。
   * 返回 false 时已设置错误。 */
  bool compileSpreadArgs(const std::vector<const ASTNode *> &args,
                         uint16_t slot);

  /* M5.1d-c2 私有字段：#x（独立私有槽表，§6.1.7.5）。
   * resolvePrivateName：字段名（不含 '#'）-> current_private_names_ 下标；
   * 未声明返回 -1（已设置编译错误）。
   * emitInstanceFieldInits：发射构造器实例字段初始化指令序列
   * （LoadThis; [init|undefined]; InitPrivate/StoreProp 逐字段，源码序）。 */
  int resolvePrivateName(const std::string &name);
  bool emitInstanceFieldInits();
  /* M5.1d-d：编译并执行 static 初始化（static 字段 + static 块，源码序）。
   * 合成 static_init 函数（this = 构造器）调用，类定义完成后执行。
   * 调用前外层 builder 栈顶须为 this_obj（构造器），返回后已消费；
   * 调用前 current_static_elems_ 须指向本 class 的 static 元素表。 */
  bool emitStaticInit();

  void fail(const std::string &msg) { error_ = msg; }

  BytecodeBuilder builder_;              /* 字节码发射器 */
  CompilerScope *scope_ = nullptr;       /* 当前函数作用域帧（M2.5） */
  std::vector<CompilerScope *> scopes_;  /* 作用域帧池（栈，owned） */
  uint32_t iter_slot_seq_ = 0;           /* for-of 迭代器槽唯一名计数（M4.4f 嵌套防复用） */
  uint32_t forin_slot_seq_ = 0;          /* for-in 键数组槽唯一名计数（M5.1e-c 嵌套防复用） */
  std::vector<LoopContext> loops_;       /* 跳转上下文栈（M5.1e-c） */
  bool failed_ = false;                  /* 编译失败标记 */
  std::string error_;                    /* 失败信息 */

  /* M5.1d-c2/d 类字段编译状态（compileClass 设置，class 体编译期间有效，
   * 含嵌套函数：class 词法范围内的私有字段名对整个 class 体可见）。 */
  const std::vector<std::string> *current_private_names_ = nullptr; /* 私有字段全名表（含 '#'） */
  const std::vector<const ASTNode *> *current_field_inits_ = nullptr; /* 私有字段初始化表达式（与表一一对应） */
  /* M5.1d-d 实例字段统一表（源码序，private + public 混合）：
   * 元素 = InstanceFieldEntry（见 compiler.cc）——构造器内按序初始化，
   * 基类 body 前 / 派生类 super() 后（§9.2.2.4）。 */
  const void *current_instance_fields_ = nullptr;
  /* M5.1d-d static 元素表（StaticElement，见 compiler.cc）：static 字段 + static
   * 块，源码序，类定义完成后由 static_init 函数执行（this = 构造器）。
   * compileClass 在调用 emitStaticInit 前指向本 class 的表。 */
  const void *current_static_elems_ = nullptr;
  bool current_is_ctor_ = false;     /* 当前编译函数是 class 构造器 */
  bool current_is_derived_ = false;  /* 当前 class 有 extends（字段在 super() 后初始化） */
  bool field_init_emitted_ = false;  /* 字段初始化已发射（派生类 super 后一次） */
};

}  // namespace neojs

#endif  // NEOJS_COMPILER_COMPILER_H
