// OpCode.h - VM 指令枚举与字节编码（ADR-060）
//
// 字节码是**完全扁平的字节数组**（std::vector<uint8_t>），pc 是字节偏移，
// 与 CPU 取指流程一致：读一个 opcode 字节，再按该指令的格式读操作数。
//
// 编码（小端）：
//   [opcode:u16][operand...]
//   push_number   u32 constIndex
//   push_string   u32 constIndex
//   define        u32 nameIndex, u32 flags
//   initialize    u32 nameIndex
//   load          u32 nameIndex
//   store         u32 nameIndex
//   jmp           u32 targetPc（字节偏移）
//   jtrue/jfalse  u32 targetPc（字节偏移）
//   push_function u32 entryPc, u32 paramCount, u32 flags
//   set_closure   u32 nameIndex
//   set_func_name u32 nameIndex
//   call          u32 argc
//   call_method   u32 argc
//   new_array     u32 length
//   try_begin     u32 catchStart, u32 finallyStart, u32 tryEnd
//   push_label    u32 labelIndex, u32 startPc, u32 endPc
//   pop_label     u32 labelIndex
//   break         u32 labelIndex
//   continue      u32 labelIndex
//   其余指令      无操作数
//
// 操作数的读取由各指令处理函数自己负责——只有它知道需要几个、什么类型。
// VM 主循环只负责取 opcode、分派、按处理函数报告的字节数推进 pc。
//
// 栈契约（每条指令前后操作数栈深度）：
//   push_*        d → d + 1        （push_uninitialized 压 TDZ 哨兵）
//   pop           d → d - 1
//   dup           d → d + 1
//   clone         d → d + 1        （复制栈顶 data，得到独立快照）
//   swap          d → d
//   define        d → d - 1        （弹初始值，建绑定）
//   initialize    d → d - 1        （弹值，解除 TDZ）
//   load          d → d + 1
//   store         d → d - 1        （弹值，写绑定）
//   get_prop      d → d - 1        （弹 key、obj，压属性值）
//   set_prop      d → d - 2        （弹 value、key、obj，压 RHS 值）
//   new_object    d → d + 1
//   new_array N   d → d + 1        （创建 length=N 的数组）
//   define_prop   d → d - 2        （弹 value、key、obj，压 obj）
//   define_accessor d → d - 3      （u32 flags；弹 setter、getter、key、obj，压 obj）
//   set_proto_literal d → d - 1    （弹 proto、obj，压 obj；非对象/null 为 no-op）
//   add/sub/mul/div  d → d - 1     （弹右、左，压结果）
//   lt/gt/le/ge   d → d - 1        （弹右、左，压 Boolean）
//   jmp           d → d            （pc = target）
//   jtrue/jfalse  d → d - 1        （弹条件，按 ToBoolean 决定是否跳转）
//   try_begin     d → d            （压 try 帧）
//   try_body_end  d → d            （产生 TryBodyEnd 信号）
//   catch_end     d → d            （产生 CatchEnd 信号）
//   finally_end   d → d            （产生 FinallyEnd 信号）
//   throw         d → d - 1        （弹值，包成 Exception）
//   push_label    d → d            （压 label 帧）
//   pop_label     d → d            （弹 label 帧）
//   break/continue d → d           （按 label 帧跳转并平衡 label 栈）
//   push_scope    d → d            （创建子作用域并设为 current）
//   push_with_scope d → d - 1      （弹 obj，创建 WithScope 并设为 current）
//   pop_scope     d → d            （销毁 current，回到 parent）
//   push_function d → d + 1
//   set_closure   d → d
//   set_func_name d → d
//   bind_this     d → d - 1        （弹 self，焊到 fn）
//   load_this     d → d + 1
//   store_this    d → d - 1
//   call N        d → d - N        （弹 fn + N args，压返回值）
//   call_method N d → d - N - 1    （弹 receiver + fn + N args，压返回值）
//   array_append  d → d - 1        （弹 value，追加到数组）
//   array_grow    d → d            （length += 1，保留空洞）
//   array_spread  d → d - 1        （弹 iterable，追加到数组）
//   object_spread d → d - 1        （弹 source，复制到 target）
//   object_rest_dynamic d → d - 2 - n（弹 n 个键、source、target，压 target）
//   call_spread   d → d - 1        （弹 argsArray 与 fn，压返回值）
//   call_method_spread d → d - 2   （弹 argsArray、fn、receiver，压返回值）
//   new_spread    d → d - 1        （弹 argsArray 与 ctor，压实例）
//   ret           d → 0            （弹返回值并结束）
//   hlt           d → d            （结束，返回 undefined）
//
// 关联 ADR: 024（字节码指令集）、060（VM 框架）

#ifndef NEO_VM_OPCODE_H
#define NEO_VM_OPCODE_H

#include <cstddef>
#include <cstdint>

namespace neo::vm {

enum class OpCode : uint16_t {
    PushUndefined,
    PushUninitialized,  // TDZ 哨兵（let/const 提升建槽）
    PushNull,
    PushTrue,
    PushFalse,
    PushNumber,   // u32 Code::numbers 下标
    PushString,   // u32 Code::strings 下标
    PushBigInt,   // u32 Code::strings 下标（digits），u32 radix

    Pop,
    Dup,
    Clone,  // [v] → [v, copy]：复制 data，快照旧值
    Swap,

    Define,       // u32 Code::strings 下标（名字），u32 flags
    Initialize,   // u32 Code::strings 下标（名字）
    Load,         // u32 Code::strings 下标（名字）
    // 同 Load，但名字完全未解析时压 undefined（typeof 未声明标识符）。
    // TDZ/Import 未初始化仍报 ReferenceError。
    LoadSoft,     // u32 Code::strings 下标（名字）
    // 当前作用域无该绑定时才定义（完成值保存槽的惰性定义）。
    DefineIfMissing,  // u32 Code::strings 下标（名字）
    Store,        // u32 Code::strings 下标（名字）
    // u32 名字：[value] → []，写入变量环境绑定（跳过词法作用域）。
    // Annex B.3.3 块级函数同步到外层 var 绑定用。
    StoreVar,

    // 属性访问：[obj, key] → [value] / [obj, key, value] → [value]
    GetProp,
    SetProp,
    // 对象/数组字面量
    NewObject,     // [] → [obj]
    NewArray,      // u32 length：[] → [arr]（预置 length，支持 elision）
    DefineProp,    // [obj, key, value] → [obj]：CreateDataProperty
    // u32 flags（bit0=hasGetter, bit1=hasSetter）：[obj, key, getter, setter] → [obj]。
    // 缺失的一侧不修改已有描述符（对象字面量 get/set 合并）。
    DefineAccessor,
    // [obj, proto] → [obj]：对象字面量 __proto__ 设原型；非对象/null 的 proto 为 no-op。
    SetProtoLiteral,
    // 标签模板对象（§13.2.8.2）：u32 count + count × (u32 cookedIndex, u32 rawIndex)。
    // [] → [templateObject]：cooked 数组 + 冻结的 raw 数组，按 site 缓存。
    TemplateObject,
    // 展开：[arr, value] → [arr] / [arr, iterable] → [arr] / [target, source] → [target]
    ArrayAppend,
    ArrayGrow,  // [arr] → [arr]：length += 1（elision 空洞）
    ArraySpread,
    ObjectSpread,
    // 对象解构 rest：u32 count + count × u32 nameIndex。
    // [target, source] → [target]：复制 source 自有 enumerable 属性，排除列出的键。
    ObjectRest,
    // 运行期键版本：u32 count；[target, source, key1..keyN] → [target]。
    ObjectRestDynamic,
    ForInKeys,     // [obj] → [entries]：for-in 候选 [owner,key,...]（含原型链、去重）
    IsEnumerableOwn,  // [obj, key] → [bool]：自有属性存在且 enumerable
    GetIterator,   // [iterable] → [iterator]：GetIterator(iterable, sync)
    IteratorNext,  // [iterator] → [iterResult]：调 iterator.next()
    IteratorClose, // [iterator] → []：调 iterator.return()（忽略缺失）

    Add,
    Sub,
    Mul,
    Div,

    // 关系运算（if/while 条件所需）
    Lt,
    Gt,
    Le,
    Ge,
    StrictEqual,  // ===（switch 匹配与用户代码共用）
    In,           // [left, right] → [bool]：left in right（HasProperty）

    // 一元运算：u32 core::UnaryOperator
    Unary,
    // ToString（模板替换用）：[v] → [string]，走完整 §7.1.14（ToPrimitive hint string）
    ToString,

    // 控制流
    Jmp,          // u32 targetPc
    JTrue,        // u32 targetPc：弹条件，ToBoolean 为真则跳转
    JFalse,       // u32 targetPc：弹条件，ToBoolean 为假则跳转
    JNotNullish,  // u32 targetPc：弹条件，非 null/undefined 则跳转（?? 短路）

    // try：catch/finally 为 out-of-line 块，出口产生 Interrupt 交给 VM 裁决
    TryBegin,     // u32 catchStart, u32 finallyStart, u32 tryEnd（0 = 无）
    // 同 TryBegin，但内部实现用（如 for-of 的 IteratorClose），不做
    // eval 完成值的 UpdateEmpty(abrupt, undefined) 归一化。
    TryBeginInternal,
    TryBodyEnd,   // 产生 Interrupt(TryBodyEnd)
    CatchEnd,     // 产生 Interrupt(CatchEnd)
    FinallyEnd,   // 产生 Interrupt(FinallyEnd)
    Throw,        // 弹值，包成 Exception 并交给 VM 展开

    // label 栈：循环/switch/带标签语句
    PushLabel,    // u32 labelIndex, u32 startPc（continue）, u32 endPc（break）
    PopLabel,     // u32 labelIndex
    PopLabelIfTop,  // u32 labelIndex：栈顶匹配才弹（逐迭代 label 清理用）
    Break,        // u32 labelIndex（"" = 最近可 break 目标）
    Continue,     // u32 labelIndex（"" = 最近循环）
    // eval 完成值：break/continue 携带当前完成值（EMPTY 用 Uninitialized 哨兵）。
    BreakValue,     // u32 labelIndex：[value] → []，创建带完成值的 Break 信号
    ContinueValue,  // u32 labelIndex：[value] → []，创建带完成值的 Continue 信号

    // —— eval 完成值（Completion Record 近似，ADR-033）——
    // 完成值寄存器为隐藏绑定 *eval-completion*，EMPTY 用 Uninitialized 哨兵。
    EnableCompletion,       // 开启完成值模式（break/continue 携带值、try 帧归一化）
    // UpdateEmpty(inner, outer)：inner（寄存器）为 EMPTY 时写入 outer 槽。
    MergeCompletion,        // u32 completionName, u32 slotName
    // UpdateEmpty(inner, undefined)：寄存器为 EMPTY 时写入 undefined。
    CompletionToUndefined,  // u32 completionName
    // 循环 V 累积：寄存器非 EMPTY 时写入 loopSlot。
    UpdateLoopValue,        // u32 completionName, u32 loopSlot
    // 循环/switch 结束：有 pending break 时取 break 值（EMPTY 则取 V），否则取 V。
    LoopEnd,                // u32 completionName, u32 loopSlot
    // 标签语句结束：有 pending break 时寄存器 = break 值（EMPTY 保持 EMPTY）。
    LabelledEnd,            // u32 completionName
    // 最终结果：寄存器为 EMPTY 时压 undefined，否则压寄存器值。
    LoadCompletionResult,   // u32 completionName

    // 作用域
    PushScope,
    // 函数体变量环境作用域：与 PushScope 相同，但标注 "var-scope"，
    // 供 store_var 定位 Annex B.3.3 的变量绑定。
    PushVarScope,
    PopScope,
    // [obj] → []：以 obj 为 with 对象创建 WithScope 并设为 current。
    PushWithScope,
    // 逐迭代环境（for let/const）：u32 count, count × u32 nameIndex。
    // IterationEnv：在当前作用域下新建子作用域，把同名绑定复制进去并设为 current。
    // CommitIterationEnv：把同名绑定写回父作用域，然后 pop 当前子作用域。
    IterationEnv,
    CommitIterationEnv,

    // 函数定义
    PushFunction,   // u32 entryPc, u32 paramCount, u32 flags
    // set_param_names u32 count, count × u32 nameIndex：[fn] → [fn]。
    // 记录形参名，供 mapped arguments 建立索引→绑定映射。
    SetParamNames,
    SetClosure,     // u32 nameIndex：把作用域链上的变量绑进函数闭包
    SetFuncName,    // u32 nameIndex：设置函数 name
    BindThis,       // [fn, self] → [fn]：把 self 焊死到函数（箭头）
    LoadThis,       // [] → [this]
    StoreThis,      // [self] → []：写入当前函数环境 this

    // 调用
    Call,           // u32 argc：[fn, argv...] → [ret]，thisArg=undefined
    CallMethod,     // u32 argc：[receiver, fn, argv...] → [ret]，thisArg=receiver
    CallSpread,     // [fn, argsArray] → [ret]，thisArg=undefined
    CallMethodSpread,  // [receiver, fn, argsArray] → [ret]，thisArg=receiver
    New,            // u32 argc：[ctor, argv...] → [instance]，new.target=ctor
    NewSpread,      // [ctor, argsArray] → [instance]，new.target=ctor
    LoadNewTarget,  // [] → [new.target]（普通调用为 undefined）

    // 生成器/async 挂起：[value] → [sent]（恢复时压入外部注入值）
    Yield,
    Await,

    Ret,
    Halt,
    // rest 参数：[] → [array]。把函数序言栈上剩余实参（argv[paramCount..]）
    // 按顺序收集成数组；调用方已按 hasRest 压入全部实参。
    RestArgs,

    // —— class ——
    // push_class u32 entryPc, u32 paramCount, u32 flags：[superCtor] → [ctor]。
    // flags 含 kFunctionHasRest / kClassDerived。
    PushClass,
    // set_strict：[fn] → [fn]，把函数闭包作用域标记为严格模式。
    SetStrict,
    // mark_catch_scope：[] → []，把当前作用域标记为简单 catch 参数作用域
    // （Annex B.3.4：eval 的 var 声明可与其同名）。
    MarkCatchScope,
    // init_instance_elements：[] → []，以当前 this 初始化当前 class 的实例元素。
    InitInstanceElements,
    // construct_super u32 argc：[argv...] → [this]，派生构造器的 super(...)。
    ConstructSuper,
    // construct_super_spread：[argsArray] → [this]。
    ConstructSuperSpread,
    // get_super_prop：[key] → [value]；set_super_prop：[key,value] → [value]。
    GetSuperProp,
    SetSuperProp,
    // define_class_method u32 flags：[ctor,key,fn] → [ctor]。
    // flags: bit0=isStatic, bit1..2=kind(0 data/1 getter/2 setter)。
    DefineClassMethod,
    // define_class_field u32 flags：[ctor,key,init] → [ctor]；bit0=isStatic。
    DefineClassField,
    // yield* 委托：[iterable] → [completion]。内部维护被委托迭代器，
    // 恢复时把 next/throw/return 转发给它。
    YieldDelegate,
    // [a,b] → [a,b,a,b]：复合成员赋值/更新只求值一次 obj 与 key。
    Dup2,
    // delete obj[key]：[obj,key] → [bool]。
    DeleteProp,
    // u32 nameIndex：[] → [boolean]：sloppy `delete Identifier`。
    DeleteIdentifier,
    // [obj,key,value,keep] → [keep]：set_prop 后保留 keep（后缀更新）。
    SetPropKeep,
    // [key,value,keep] → [keep]：set_super_prop 后保留 keep。
    SetSuperPropKeep,

    // —— class 私有元素（名字操作数为 u32 下标，不含 '#'）——
    // [ctor, init] → [ctor]：注册私有字段；静态字段同时执行初始化器。
    DefinePrivateField,
    // [ctor, fn] → [ctor]：注册私有方法/访问器；flags: bit0=static, bit1..2=kind。
    DefinePrivateMethod,
    // [obj] → [value]：读私有字段 / 取私有方法 / 调私有 getter。
    GetPrivateField,
    GetPrivateMethod,
    GetPrivateGetter,
    // [obj, value] → [value]：写私有字段 / 调私有 setter。
    SetPrivateField,
    SetPrivateSetter,
    // [obj, value, keep] → [keep]：写私有字段/调 setter 后保留 keep（后缀更新）。
    SetPrivateFieldKeep,
    SetPrivateSetterKeep,
    // [fn, ctor] → [fn]：设置函数 ownerClass（static block 私有访问用）。
    SetOwnerClass,
    // u32 nameIndex：[obj] → [bool]：`#x in obj`（PrivateIn）。
    PrivateIn,
    // [] → [import.meta]：当前模块的 import.meta 对象（按模块缓存）。
    ImportMeta,
    // [specifier] → [promise]：动态 import()，返回解析为模块命名空间的 Promise。
    DynamicImport,
    // [iterable] → [iterator]：GetIterator(iterable, async)，优先 @@asyncIterator。
    GetAsyncIterator,
    // [iterator] → [returnResult]：AsyncIteratorClose 前半（取 return 并调用）；
    // 无 return 时压 undefined。调用方随后 await 并丢弃结果。
    AsyncIteratorClose,
    // u32 patternIndex, u32 flagsIndex：[] → [regexp]（正则字面量）。
    PushRegExp,
    // u32 nameIndex：[] → []：eval 变量声明（缺失才建槽，已有绑定不变）。
    DeclareVar,
    // u32 nameIndex：[] → []：全局 var 声明（CreateGlobalVarBinding）。
    DeclareGlobalVar,
    // u32 nameIndex：[] → []：全局函数声明（CreateGlobalFunctionBinding）。
    DeclareGlobalFunction,
    // u32 nameIndex：[] → []：全局 let/const 与不可配置全局属性冲突检查。
    CheckGlobalLexical,
    // u32 nameIndex, u32 flags：[value] → []：在全局声明记录定义 let/const/class。
    DefineGlobalLexical,
    // u32 argc：[callee, args...] → [result]：直接 eval 调用点。
    DirectEval,
    // [callee, argsArray] → [result]：带展开的直接 eval 调用点。
    DirectEvalSpread,
    // [] → []：把当前作用域标注为严格模式（严格 eval 的 eval 词法作用域）。
    SetScopeStrict,
    // [left, right] → [bool]：left instanceof right（§13.10.2）。
    InstanceOf,
    // u32 core::BinaryOperator：[left, right] → [result]。
    // 未单独设指令的二元运算符（==、%、**、位运算、移位）走统一入口。
    Binary,
    // u32 nameIndex：[fn] → [fn]：仅当 fn 无自有 "name" 时设置名字
    // （NamedEvaluation，class 表达式用）。
    SetFuncNameIfAbsent,
    // [fn, key] → [fn, key]：按运行时属性键设置函数名（computed 键）。
    SetFuncNameFromKey,
};

// 指令总数（分派表大小）。
inline constexpr size_t kOpCodeCount =
    static_cast<size_t>(OpCode::SetFuncNameFromKey) + 1;

// Define 的 flags 位。未置位即 false：
//   mutable    —— 绑定可被 store 改写（const 为 false）
//   initialized —— 绑定已初始化（let/const 提升建槽时为 false，进入 TDZ）
//   var        —— 变量环境绑定（var/函数/形参），eval 的 var 可同名复用
inline constexpr uint32_t kDefineMutable = 1u << 0;
inline constexpr uint32_t kDefineInitialized = 1u << 1;
inline constexpr uint32_t kDefineVar = 1u << 2;

// push_function 的 flags 位。
inline constexpr uint32_t kFunctionHasRest = 1u << 0;
inline constexpr uint32_t kFunctionConstructible = 1u << 1;
inline constexpr uint32_t kFunctionHasPrototype = 1u << 2;
inline constexpr uint32_t kFunctionGenerator = 1u << 3;
inline constexpr uint32_t kFunctionAsync = 1u << 4;
// async function*：同时是生成器与异步生成器。
inline constexpr uint32_t kFunctionAsyncGenerator = 1u << 6;
// 非箭头 script 函数：调用时创建 arguments 对象。
inline constexpr uint32_t kFunctionHasArguments = 1u << 7;
// arguments 与形参绑定映射（非严格 + 简单形参列表）。
inline constexpr uint32_t kFunctionArgumentsMapped = 1u << 8;
// push_class：派生类（extends 非 null）。
inline constexpr uint32_t kClassDerived = 1u << 5;

// 指令名（诊断/测试用）。
const char* opCodeName(OpCode op);

}  // namespace neo::vm

#endif  // NEO_VM_OPCODE_H
