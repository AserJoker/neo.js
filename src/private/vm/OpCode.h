// OpCode.h - VM 指令枚举（ADR-060）
//
// V0 只覆盖字面量、变量绑定、算术与返回协议。跳转/函数调用/异常/try/label
// 按纵向切片逐步加入（V1+）。
//
// 栈契约（每条指令前后操作数栈深度）：
//   push_*        d → d + 1
//   pop           d → d - 1
//   dup           d → d + 1
//   swap          d → d
//   define        d → d - 1        （弹初始值，建绑定）
//   initialize    d → d - 1        （弹值，解除 TDZ）
//   load          d → d + 1
//   store         d → d - 1        （弹值，写绑定）
//   add/sub/mul/div  d → d - 1     （弹右、左，压结果）
//   ret           d → 0            （弹返回值并结束）
//   hlt           d → d            （结束，返回 undefined）
//
// 关联 ADR: 024（字节码指令集）、060（VM 框架）

#ifndef NEO_VM_OPCODE_H
#define NEO_VM_OPCODE_H

#include <cstdint>

namespace neo::vm {

enum class OpCode : uint16_t {
    PushUndefined,
    PushNull,
    PushTrue,
    PushFalse,
    PushNumber,   // a = Code::numbers 下标
    PushString,   // a = Code::strings 下标

    Pop,
    Dup,
    Swap,

    Define,       // a = Code::strings 下标（名字），b = flags
    Initialize,   // a = Code::strings 下标（名字）
    Load,         // a = Code::strings 下标（名字）
    Store,        // a = Code::strings 下标（名字）

    Add,
    Sub,
    Mul,
    Div,

    Ret,
    Halt,
};

// Define 的 b 操作数位标志。未置位即 false：
//   mutable    —— 绑定可被 store 改写（const 为 false）
//   initialized —— 绑定已初始化（let/const 提升建槽时为 false，进入 TDZ）
inline constexpr uint32_t kDefineMutable = 1u << 0;
inline constexpr uint32_t kDefineInitialized = 1u << 1;

// 指令名（诊断/测试用）。
const char* opCodeName(OpCode op);

}  // namespace neo::vm

#endif  // NEO_VM_OPCODE_H
