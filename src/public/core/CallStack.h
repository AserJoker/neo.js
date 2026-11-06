// CallStack.h - 调用栈帧（纯展示信息）
//
// 调用栈是**纯展示信息**：变量解析走 Scope 的 parent 链，不依赖栈帧。
// Context 维护一个 CallFrame 数组，供错误报告（Error.stack）、调试器、
// 宿主诊断读取。
//
// push/pop 必须由调用点显式维护：**只有 VM 知道当前源码位置**，Context 与
// Function 都不知道，所以不能在 call/invoke 内部自动插入。
//
// 帧语义是「**在某个函数的某处**」：
//   - name     —— 当前所在的函数（如 "foo"、"<anonymous>"）；
//   - position —— 该函数内当前执行到的位置（filename + line + column）。
//
// 而调用点的参数是「**在某处调用某函数**」，所以一次 push 跨两帧：
//   1. 把调用点位置写入**当前栈顶帧**（调用者的「某处」）；
//   2. 压入新帧，只写函数名（新帧的位置要等它自己再调用别人、或出错时才知道）。
// 出错时用 trace(callSite) 取快照：clone 一份栈，并用出错位置补全栈顶帧。
//
// 位置按 filename / line / column 三个字段分开存（不预拼接字符串），便于
// 未来集成调试器做断点匹配、源码跳转。非源码帧（native 内建、引擎内部逻辑）
// 的 filename 为 kInternalSource（"<internal>"），line/column 为 0。
//
// 整体 get/set 用于「自己持有调用栈」的场景（ADR-023/ADR-037）：
// 异常跨栈传播、async 恢复、生成器挂起时，各自保存一份快照，恢复时整体写回。
//
// 关联 ADR: 004（执行模型）、017（Interrupt）、022（函数调用）、037（调用栈）

#ifndef NEO_CORE_CALL_STACK_H
#define NEO_CORE_CALL_STACK_H

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace neo::core {

// 非源码帧的 filename 标记（native 内建 / 引擎内部逻辑）
inline constexpr std::string_view kInternalSource = "<internal>";

// 源码位置：文件名 + 行 + 列（1-based）。分开存，便于调试器断点匹配/源码跳转。
struct SourcePosition {
    std::string filename;   // 源码文件名；非源码帧为 "<internal>"
    uint32_t line = 0;      // 行号（1-based；0 = 未知）
    uint32_t column = 0;    // 列号（1-based；0 = 未知）

    SourcePosition() = default;
    SourcePosition(std::string file, uint32_t ln, uint32_t col)
        : filename(std::move(file)), line(ln), column(col) {}

    // 非源码位置（native 内建 / 引擎内部逻辑）
    static SourcePosition internal() {
        return SourcePosition(std::string(kInternalSource), 0, 0);
    }

    bool isInternal() const { return filename == kInternalSource; }
};

// 一个调用栈帧：在 name 函数的 position 位置（纯展示信息，不参与变量解析）
struct CallFrame {
    std::string name;       // 函数名（如 "foo"、"<anonymous>"）
    SourcePosition position;  // 函数内的位置；filename 为空 = 尚未确定

    CallFrame() = default;
    CallFrame(std::string fnName, SourcePosition pos)
        : name(std::move(fnName)), position(std::move(pos)) {}

    // 非源码帧（native 内建 / 引擎内部逻辑）
    static CallFrame internal(std::string fnName) {
        return CallFrame(std::move(fnName), SourcePosition::internal());
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_CALL_STACK_H
