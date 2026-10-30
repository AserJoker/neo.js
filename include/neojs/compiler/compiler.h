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
#include <unordered_set>
#include <vector>

#include "neojs/compiler/bytecode.h"
#include "neojs/frontend/ast.h"

namespace neojs {

/* 模块导出绑定（编译期登记，M5.2a）：导出名 -> 本地绑定名或 re-export 源。
 * 与运行时 JSModuleRecord::ExportBinding 同构；编译器不访问 JSContext，
 * 仅收集信息，由模块加载器回填 record。
 * from_specifier 非空 = re-export（export {a} from 'm' / export * from 'm'），
 * 本地导出时为空。 */
struct ModuleExportEntry {
  std::string export_name;      /* 导出名（"default" 为默认导出） */
  std::string local_name;       /* 本地绑定名（本模块 env 中的名字） */
  std::string from_specifier;   /* re-export 源模块 specifier（空 = 本地导出） */
  bool export_all = false;      /* export * from 'm'（不含 default） */
};

/* 编译结果：成功时 fn 为函数字节码；失败时 ok=false + error。
 * M5.2a 模块编译附加数据（compileModule 产物，供加载器回填模块记录）：
 *   module_exports —— export 声明编译期收集的导出绑定表；
 *   module_deps    —— import/export-from 声明的源模块 specifier（去重，源码序）。 */
struct CompileResult {
  FunctionBytecode fn;
  bool ok = false;
  std::string error;
  std::vector<ModuleExportEntry> module_exports;
  std::vector<std::string> module_deps;
};

/* 词法绑定（M5.5f 块作用域，§8.1.1 路径 A）：块内 let/const 声明记录。
 * 不占局部槽，运行时在块环境绑定表中。 */
struct LexicalBinding {
  std::string name;
  bool is_const = false;
};

/* 作用域帧（M2.5 闭包 + M5.5f 块作用域）：编译期作用域嵌套栈。
 * 两类帧：
 *   - 函数帧（is_function_scope=true）：每函数一个。持有 var/参数局部
 *     槽（locals -> 槽号，运行时栈底槽）。captures 记录本函数捕获列表。
 *   - 块帧（is_function_scope=false）：含 let/const 的块（及 for 循环头）
 *     压入。持有 block_bindings（let/const 词法绑定名 + const 标记）。
 *     仅「运行时创建块环境」的块压入块帧（无 let/const 的块不压，保证
 *     捕获 depth 与运行时环境节点数一一对应）。
 * parent = 外层作用域帧（函数帧或块帧）。 */
struct CompilerScope {
  CompilerScope *parent = nullptr;       /* 外层作用域帧 */
  bool is_function_scope = false;        /* 函数帧（var/参数槽 + 捕获） */
  std::unordered_map<std::string, uint16_t> locals; /* 函数帧：局部变量 -> 槽号 */
  uint16_t next_slot = 0;                /* 函数帧：下一个可用槽 */
  std::vector<UpvalueCapture> captures;  /* 函数帧：本函数捕获列表 */
  /* 块帧：词法绑定（let/const 声明顺序）。 */
  std::vector<LexicalBinding> block_bindings;
  /* M5.5f：块帧内声明的 var 名（§13.3.1.1 重名检测——块内 var 提升到
   * 函数作用域，但与块内 let/const 同名单向冲突，双向检测：
   *   var 声明时查块链 block_bindings（hasLexicalInChain）；
   *   let 声明时查块链 block_vars（块内 var 已声明）。 */
  std::vector<std::string> block_vars;
  /* M5.5f TDZ：compileBlock 预声明阶段登记的 let/const 绑定名（登记 + 发射
   * DeclareLexical 指令，块开头建立全部 TDZ 占位）。声明处 compileVarDecl
   * 据此区分「预声明登记」（跳过重复登记）与「真重名」（报 SyntaxError）。 */
  std::unordered_set<std::string> predeclared;
};

/* M5.1e-c 跳转上下文（break/continue 目标）：循环/标签语句编译期间压栈。
 * 循环 push is_loop=true（continue 合法）；标签语句 push is_loop=false
 * （continue 到非循环标签非法）。无标签 break 找最近循环；有标签
 * break/continue 沿栈找匹配 label。
 * M5.5f 块作用域：block_env_depth 记录进入循环/标签时当前块环境层数
 * （= 块帧链上运行时已创建的块环境数），break/continue 跳出时须 PopBlockEnv
 * 弹回该深度（循环体/标签体内的 let 块环境在跳出时清理）。 */
struct LoopContext {
  std::string label;        /* 关联标签名（可空） */
  BcLabel break_label;      /* break 目标 */
  BcLabel continue_label;   /* continue 目标（非循环时无效） */
  bool is_loop = false;     /* 是否循环（continue 合法） */
  uint16_t block_env_depth = 0; /* M5.5f：进入时块环境层数（跳出需弹回） */
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

  /* 编译模块（M5.2a）：Program 根为 Module 时调用。
   * 与 compile 差异：
   *   - import 声明注册进 module_imports_（LoadImport/StoreImport 索引），
   *     绑定名不入局部槽（读走 LoadImport，写走 StoreImport 报 TypeError）。
   *   - export 声明编译期登记 module_exports_（运行期无 SetExport 指令，
   *     导出绑定 = 模块 env 绑定，live binding）。
   *   - 顶层 FunctionDeclaration/ClassDeclaration 提升：体开头发射
   *     MakeFunction+StoreVar 序列（源码位置跳过，不挂 global）。
   *   - 顶层 var/let/const 声明为模块求值函数局部槽，经 StoreVar 的 env
   *     同步写入模块 env；闭包沿 env 链捕获模块顶层变量。
   * 模块求值不走 compile 的 completion value 语义：函数末尾统一 undefined。
   * 失败时返回 ok=false + error。 */
  CompileResult compileModule(const AST &ast);

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
  bool compileAsyncForOf(const ASTNode &node, const ASTNode *left,
                         const ASTNode *right, const ASTNode *body,
                         const std::string &label); /* M5.5c-2 for await...of */
  bool compileForIn(const ASTNode &node, const std::string &label = ""); /* M5.1e-c for-in */
  bool compileLabeled(const ASTNode &node); /* M5.1e-c label: stmt（跳转上下文） */
  bool compileBreak(const ASTNode &node);   /* M5.1e-c break [label] */
  bool compileContinue(const ASTNode &node); /* M5.1e-c continue [label] */
  bool compileReturn(const ASTNode &node);
  bool compileDoWhile(const ASTNode &node, const std::string &label = "");
  /* M5.1e-f 异常处理语句（§14.15/§14.16）。
   * compileThrow：throw expr -> [expr] Throw。
   * compileTry：编译 try/catch/finally，生成 try 异常保护表条目。 */
  bool compileThrow(const ASTNode &node);
  bool compileTry(const ASTNode &node);

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
  /* 可选链编译（M5.1e-e）：?. / ?.[ / ?.() 短路（§13.5.1.1/§13.5.1.2）。
   * node 为链顶层节点（Member/Call，含 optional）。
   * 语义：链头（第一个 ?. 前表达式）nullish → 整链 undefined；
   * 每个 ?. 短路点判断「前一步结果」nullish，短路跳过链剩余；
   * 链延续（a?.b().c 的 .c / a?.b() 的 ()）不判断、短路时被跳过。 */
  bool compileOptionalChain(const ASTNode &node);
  /* 编译单个可选链节点（链上从内到外）。chain_end 为整链短路汇聚标签。 */
  bool compileOptionalChainNode(const ASTNode &node, BcLabel *chain_end);
  /* 判断节点是否为可选链节点（Member/Call 且属于链：自身 optional）。 */
  bool isOptionalChainNode(const ASTNode &node) const;
  /* 判断节点是否为可选链短路点（?./?.[/?.(）。 */
  bool isOptionalShortCircuit(const ASTNode &node) const;
  /* delete 可选链（M5.1e-e）：delete a?.b / delete a?.b.c（§13.5.1.2）。
   * 链头 nullish 短路时 delete 返回 true；否则求值引用并删除。
   * 短路路径压 true（delete 结果）而非 undefined（普通可选链）。 */
  bool compileDeleteOptionalChain(const ASTNode &node);
  /* delete 可选链单节点：非最后节点做访问（引用链求值），最后节点做删除。
   * chain_end：整链短路汇聚标签（短路路径跳转）。 */
  bool compileDeleteChainNode(const ASTNode &node, bool is_last,
                              BcLabel *chain_end);
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
  /* for-in/for-of 循环 left 绑定（M5.1e-d）：栈顶为当前迭代值/键。
   * 支持声明标识符 / 声明解构 / 赋值标识符 / 赋值解构 / 成员 left。 */
  bool compileLoopLeft(const ASTNode *left);
  /* 预声明解构模式中的全部绑定名（保证槽位分配稳定，声明语义）。 */
  bool declarePatternBindings(const ASTNode &pattern);
  /* M5.5f：预声明解构模式中的全部绑定名为词法绑定（let/const 解构声明，
   * TDZ 占位 + DeclareLexical 发射）。 */
  bool declarePatternLexical(const ASTNode &pattern, bool is_const);
  /* M5.5f TDZ：解构模式中所有绑定名是否都已在当前块帧预声明（compileBlock
   * 预声明阶段对解构语句整体调用 declarePatternLexical，声明处据此跳过
   * 重复登记，避免误报重名）。 */
  bool patternPredeclared(const ASTNode &pattern) const;

  /* 局部变量槽分配：声明/参数登记；引用解析（未找到 -> LoadGlobal）。 */
  uint16_t declareLocal(const std::string &name);
  bool resolveLocal(const std::string &name, uint16_t *slot);

  /* M5.5f 块作用域：
   *   declareLexical(name, is_const) —— 当前块帧声明词法绑定（let/const）。
   *   resolveLexical(name) —— 沿块帧链（不含函数帧）查词法绑定（找到返回 true）。
   *   resolveBinding(name, slot, capture, is_upvalue, is_lexical) —— 统一解析：
   *     本函数帧局部 var/参数 -> slot；词法绑定（本块或外层块）-> is_lexical=true
   *     （发射 LoadLexical/StoreLexical）；外层函数绑定 -> capture upvalue。
   *   emitLoadBinding(name) —— 统一标识符读取发射（var/词法/import/捕获/全局）。
   *   enterBlockScope()/exitBlockScope() —— 压入/弹出块帧（块含 let/const 时）。
   *   functionScope() —— 当前函数帧（沿 parent 找 is_function_scope）。 */
  bool declareLexical(const std::string &name, bool is_const);
  bool resolveLexical(const std::string &name);
  /* M5.5f TDZ：预声明 let/const 绑定（compileBlock 块开头）——登记到块帧
   * 并发射 DeclareLexical 指令（块内任何位置访问抛 TDZ），名记入
   * scope_->predeclared。已存在（含预声明）返回 false（真重名）。 */
  bool predeclareLexical(const std::string &name, bool is_const);
  /* M5.5f TDZ：预声明解构模式全部绑定名（递归，等价 declarePatternLexical
   * 的登记 + 发射，但经 predeclareLexical 记入 predeclared）。 */
  bool predeclarePattern(const ASTNode &pattern, bool is_const);
  /* M5.5f：当前块帧链（含函数帧词法绑定）是否已有同名 let/const
   * （§13.3.1.1 块内 var 与 let/const 重名检测，var 声明时调用）。 */
  bool hasLexicalInChain(const std::string &name) const;
  bool resolveBinding(const std::string &name, uint16_t *slot, uint16_t *capture,
                      bool *is_upvalue, bool *is_lexical);
  bool emitLoadBinding(const std::string &name);
  CompilerScope *enterBlockScope();
  void exitBlockScope();
  CompilerScope *functionScope();
  /* M5.5f：当前块环境层数（运行时 frame.env 链上块节点数，= 块帧链
   * 块帧数）。PopBlockEnv 目标深度 / LoopContext.block_env_depth 用。 */
  uint16_t blockScopeDepth();

  /* 标识符解析（M2.5）：本函数局部 / 外层局部（捕获 upvalue）/ 全局。
   * 返回 true 表示已发射 load 指令。 */
  bool emitLoadIdentifier(const std::string &name);
  /* 赋值目标解析（M2.5）：局部 / 捕获 upvalue / 全局。
   * 返回 true 表示目标是局部或捕获（slot/capture 已填），false 表示全局
   * （M5.1e-g：调用方发射 StoreGlobal，非严格隐式全局赋值）。 */
  bool resolveAssignTarget(const std::string &name, uint16_t *slot,
                           uint16_t *capture, bool *is_upvalue,
                           bool *is_lexical = nullptr);

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

  /* M5.2a 模块编译状态（compileModule 期间有效）：
   *   module_import_map_ —— import 绑定本地名 -> imports 表下标
   *     （emitLoadIdentifier/compileAssignment 检查：命中 = 导入绑定）。
   *   module_imports_    —— 导入绑定收集（builder_.add_import 同步去重）。
   *   module_exports_    —— 导出绑定收集（本地导出 + re-export，源码序）。
   *   module_deps_       —— 依赖 specifier 收集（去重，源码序）。
   * 编译完成由 compileModule 写入 CompileResult（加载器回填模块记录）。 */
  std::unordered_map<std::string, uint16_t> module_import_map_;
  std::vector<ModuleImportEntry> module_imports_;
  std::vector<ModuleExportEntry> module_exports_;
  std::vector<std::string> module_deps_;

  /* M5.2a 模块编译内部：处理单条 import 声明（ImportDeclaration）。
   * 解析 specifier/各 specifier，注册 module_imports_ + module_deps_。 */
  bool compileModuleImport(const ASTNode &node);
  /* M5.2a 模块编译内部：处理单条 export 声明（ExportNamedDeclaration /
   * ExportDefaultDeclaration / ExportAllDeclaration），登记 module_exports_。
   * is_decl = true 时返回被包装的声明节点（调用方继续编译其体）。
   * register_exports = true 时才登记 module_exports_（两遍编译：第 1 遍
   * 收集元数据时登记，第 3 遍编译体时只取 decl、不重复登记，M5.2d）。 */
  bool compileModuleExport(const ASTNode &node, const ASTNode **decl,
                           bool register_exports = true);

  void fail(const std::string &msg) { error_ = msg; }

  BytecodeBuilder builder_;              /* 字节码发射器 */
  CompilerScope *scope_ = nullptr;       /* 当前函数作用域帧（M2.5） */
  std::vector<CompilerScope *> scopes_;  /* 作用域帧池（栈，owned） */
  uint32_t iter_slot_seq_ = 0;           /* for-of 迭代器槽唯一名计数（M4.4f 嵌套防复用） */
  uint32_t forin_slot_seq_ = 0;          /* for-in 键数组槽唯一名计数（M5.1e-c 嵌套防复用） */
  uint32_t catch_seq_ = 0;               /* catch 解构参数临时槽唯一名计数（M5.1e-f 嵌套防复用） */
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
  /* M5.5c-1 async generator：当前编译函数是否 async function*（is_async &&
   * is_generator）。YieldExpression 编译据此在 yield 值后插 Await 指令——
   * AsyncGeneratorYield 语义（§27.6.3.4：yield 值先 await 再挂起返回
   * {value,done:false}）。compileChildFunction/compileFunction 设置。 */
  bool current_is_async_generator_ = false;
  bool current_is_ctor_ = false;     /* 当前编译函数是 class 构造器 */
  bool current_is_derived_ = false;  /* 当前 class 有 extends（字段在 super() 后初始化） */
  bool field_init_emitted_ = false;  /* 字段初始化已发射（派生类 super 后一次） */
};

}  // namespace neojs

#endif  // NEOJS_COMPILER_COMPILER_H
