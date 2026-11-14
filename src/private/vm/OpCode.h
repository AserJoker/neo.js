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
//   其余指令      无操作数
//
// 操作数的读取由各指令处理函数自己负责——只有它知道需要几个、什么类型。
// VM 主循环只负责取 opcode、分派、按处理函数报告的字节数推进 pc。
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

#include <cstddef>
#include <cstdint>

namespace neo::vm {

enum class OpCode : uint16_t {
    PushUndefined,
    PushNull,
    PushTrue,
    PushFalse,
    PushNumber,   // u32 Code::numbers 下标
    PushString,   // u32 Code::strings 下标

    Pop,
    Dup,
    Swap,

    Define,       // u32 Code::strings 下标（名字），u32 flags
    Initialize,   // u32 Code::strings 下标（名字）
    Load,         // u32 Code::strings 下标（名字）
    Store,        // u32 Code::strings 下标（名字）

    Add,
    Sub,
    Mul,
    Div,

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

// 指令名（诊断/测试用）。
const char* opCodeName(OpCode op);

}  // namespace neo::vm

#endif  // NEO_VM_OPCODE_H
