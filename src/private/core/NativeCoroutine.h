// NativeCoroutine.h - native 协程恢复驱动（ADR-045 §4）
//
// resumeNativeCoroutine 是 CoroutineData::resume 的 native 实现：
//   - 按协程保存的 execScope / savedCallStack 重建现场；
//   - 调 target 的 native 函数体（状态机按 resumePoint 分派）；
//   - 返回 Interrupt(Yield) 时保存现场（execScope 孤立化）并挂起；
//   - 返回其他值时销毁执行作用域并完成。
//
// 与 invokeNative（Function.cpp）共享「闭包作用域临时挂载 + 作用域栈平衡」逻辑，
// 区别仅在于挂起时不能销毁 execScope。
//
// 关联 ADR: 022（函数调用）、036（闭包作用域孤立化）、037（调用栈）、045（协程抽象）

#ifndef NEO_CORE_PRIVATE_NATIVE_COROUTINE_H
#define NEO_CORE_PRIVATE_NATIVE_COROUTINE_H

namespace neo::core {

class IContext;
class Value;

// 恢复 native 协程一次。返回函数体的原始结果（挂起时为 Interrupt(Yield)）。
Value* resumeNativeCoroutine(IContext* ctx, Value* coroutine, Value* sentValue);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_NATIVE_COROUTINE_H
