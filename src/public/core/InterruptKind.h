// InterruptKind.h - 控制流信号种类（ADR-046）
//
// Interrupt 是统一的控制信号值（kind + offset + value），与 Exception/Fatal 正交。
// 种类决定消费方如何处理：Yield 挂起并产出值、Return 立即返回、Await 等待结果、
// Throw 解包为异常。
//
// 关联 ADR: 017（Interrupt 规划）、045（协程抽象）、046（Interrupt 控制信号）

#ifndef NEO_CORE_INTERRUPT_KIND_H
#define NEO_CORE_INTERRUPT_KIND_H

#include <cstdint>

namespace neo::core {

// 控制信号种类（ADR-046 §1）
enum class InterruptKind : uint8_t {
    Yield,   // 挂起并产出 value（函数返回方向）
    Return,  // 立即以 value 返回（外部注入方向；承载 finally 语义）
    Await,   // 未来：等待 Promise 结果（VM 方向）
    Throw,   // 未来：VM 内部 throw 信号，解包为 Exception（ADR-017 §6）
};

}  // namespace neo::core

#endif  // NEO_CORE_INTERRUPT_KIND_H
