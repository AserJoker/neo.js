// Coroutine.h - 协程状态与恢复驱动（ADR-045）
//
// Coroutine 是**可恢复的计算句柄**（TypeKind::Coroutine），与「挂起信号」
// Interrupt(Yield)（ADR-046）区分：
//   - Coroutine 对象持上下文（孤立执行作用域子树 + 调用栈快照）、断点、状态；
//   - 挂起信号是函数返回的 Interrupt 值，runner 消费后把 yielded 交给调用方。
//
// 恢复驱动多态（参考 Task::run）：创建协程时按 target 种类选定 runner。
//   - native 函数 → resumeNativeCoroutine（resumePoint = 阶段号）
//   - VM 函数（未来）→ resumeVmCoroutine（resumePoint = PC）
//
// 关联 ADR: 004（执行模型）、017（Interrupt）、023（协程挂起/恢复）、
//          037（调用栈）、045（协程抽象）、046（Interrupt 控制信号）

#ifndef NEO_CORE_COROUTINE_H
#define NEO_CORE_COROUTINE_H

#include <cstdint>

namespace neo::core {

class IContext;
class Value;

// 协程状态机（ADR-045 §2）
enum class CoroutineState : uint8_t {
    Suspended,  // 已创建/已挂起，等待 resume
    Running,    // 正在恢复执行
    Completed,  // 已正常返回/抛出/被 return 注入终止
};

// 恢复驱动：ctx=语言上下文，coroutine=协程对象，sentValue=本次恢复传入的值。
// 返回函数体的原始结果：挂起时为 Interrupt(Yield)，完成为最终值/异常。
using CoroutineRunner = Value* (*)(IContext* ctx, Value* coroutine, Value* sentValue);

}  // namespace neo::core

#endif  // NEO_CORE_COROUTINE_H
