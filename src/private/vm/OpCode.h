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
//   pop_scope     d → d            （销毁 current，回到 parent）
//   push_function d → d + 1
//   set_closure   d → d
//   set_func_name d → d
//   bind_this     d → d - 1        （弹 self，焊到 fn）
//   load_this     d → d + 1
//   store_this    d → d - 1
//   call N        d → d - N        （弹 fn + N args，压返回值）
//   call_method N d → d - N - 1    （弹 receiver + fn + N args，压返回值）
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

    Pop,
    Dup,
    Clone,  // [v] → [v, copy]：复制 data，快照旧值
    Swap,

    Define,       // u32 Code::strings 下标（名字），u32 flags
    Initialize,   // u32 Code::strings 下标（名字）
    Load,         // u32 Code::strings 下标（名字）
    Store,        // u32 Code::strings 下标（名字）

    // 属性访问：[obj, key] → [value] / [obj, key, value] → [value]
    GetProp,
    SetProp,
    // 对象/数组字面量
    NewObject,     // [] → [obj]
    NewArray,      // u32 length：[] → [arr]（预置 length，支持 elision）
    DefineProp,    // [obj, key, value] → [obj]：CreateDataProperty

    Add,
    Sub,
    Mul,
    Div,

    // 关系运算（if/while 条件所需）
    Lt,
    Gt,
    Le,
    Ge,

    // 一元运算：u32 core::UnaryOperator
    Unary,

    // 控制流
    Jmp,          // u32 targetPc
    JTrue,        // u32 targetPc：弹条件，ToBoolean 为真则跳转
    JFalse,       // u32 targetPc：弹条件，ToBoolean 为假则跳转
    JNotNullish,  // u32 targetPc：弹条件，非 null/undefined 则跳转（?? 短路）

    // try：catch/finally 为 out-of-line 块，出口产生 Interrupt 交给 VM 裁决
    TryBegin,     // u32 catchStart, u32 finallyStart, u32 tryEnd（0 = 无）
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

    // 作用域
    PushScope,
    PopScope,
    // 逐迭代环境（for let/const）：u32 count, count × u32 nameIndex。
    // IterationEnv：在当前作用域下新建子作用域，把同名绑定复制进去并设为 current。
    // CommitIterationEnv：把同名绑定写回父作用域，然后 pop 当前子作用域。
    IterationEnv,
    CommitIterationEnv,

    // 函数定义
    PushFunction,   // u32 entryPc, u32 paramCount, u32 flags
    SetClosure,     // u32 nameIndex：把作用域链上的变量绑进函数闭包
    SetFuncName,    // u32 nameIndex：设置函数 name
    BindThis,       // [fn, self] → [fn]：把 self 焊死到函数（箭头）
    LoadThis,       // [] → [this]
    StoreThis,      // [self] → []：写入当前函数环境 this

    // 调用
    Call,           // u32 argc：[fn, argv...] → [ret]，thisArg=undefined
    CallMethod,     // u32 argc：[receiver, fn, argv...] → [ret]，thisArg=receiver

    // 生成器/async 挂起：[value] → [sent]（恢复时压入外部注入值）
    Yield,
    Await,

    Ret,
    Halt,
};

// 指令总数（分派表大小）。
inline constexpr size_t kOpCodeCount = static_cast<size_t>(OpCode::Halt) + 1;

// Define 的 flags 位。未置位即 false：
//   mutable    —— 绑定可被 store 改写（const 为 false）
//   initialized —— 绑定已初始化（let/const 提升建槽时为 false，进入 TDZ）
inline constexpr uint32_t kDefineMutable = 1u << 0;
inline constexpr uint32_t kDefineInitialized = 1u << 1;

// push_function 的 flags 位。
inline constexpr uint32_t kFunctionHasRest = 1u << 0;
inline constexpr uint32_t kFunctionConstructible = 1u << 1;
inline constexpr uint32_t kFunctionHasPrototype = 1u << 2;
inline constexpr uint32_t kFunctionGenerator = 1u << 3;
inline constexpr uint32_t kFunctionAsync = 1u << 4;

// 指令名（诊断/测试用）。
const char* opCodeName(OpCode op);

}  // namespace neo::vm

#endif  // NEO_VM_OPCODE_H
