// NativeCoroutine.cpp - native 协程恢复驱动实现（ADR-045 §3/§4/§5）
//
// 现场重建复用 invokeNativeFunction（FunctionInvoke.cpp）：
//   - 传入协程保存的 execScope → 重挂到 closureScope 下（首次为 nullptr → 新建）；
//   - 恢复挂起时的调用栈快照；
//   - 函数体返回 Interrupt(Yield) 时由 invokeNativeFunction 的挂起分支
//     保存断点/yield 值/作用域（不销毁 execScope）；
//   - 其他返回值 → 完成：execScope 已被销毁，状态转 Completed。
//
// 关联 ADR: 022（函数调用）、036（闭包作用域孤立化）、037（调用栈）、045（协程抽象）

#include "core/NativeCoroutine.h"

#include "core/FunctionInvoke.h"
#include "core/IContext.h"
#include "core/Value.h"
#include "core/types/Coroutine.h"
#include "core/types/Interrupt.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

// sentValue 已由 resumeCoroutine 写入 co->sentValue，函数体经协程 API 读取。
Value* resumeNativeCoroutine(IContext* ctx, Value* coroutine, Value* /*sentValue*/) {
    auto* co = asCoroutine(coroutine);
    if (co == nullptr || co->target == nullptr) {
        return detail::typeError(ctx, "coroutine has no target");
    }

    // 1. 恢复挂起时的调用栈快照（首次为空，等价于无操作）
    std::vector<CallFrame> savedStack = ctx->callStack();
    ctx->setCallStack(co->savedCallStack);

    // 2. 执行函数体：resumeScope 非空时重挂保存的孤立子树
    Value* res = invokeNativeFunction(ctx, co->target, co->receiver,
                                      static_cast<size_t>(co->initialArgs.size()),
                                      co->initialArgs.data(), nullptr, co->execScope);

    // 3. 判定挂起/完成。挂起分支已由 invokeNativeFunction 写入 resumePoint/yielded/
    //    execScope/savedCallStack；完成分支里 execScope 已被销毁，须清空指针。
    bool suspended = detail::isInterruptValue(res) &&
                     isSuspensionSignal(asInterrupt(res)->kind) &&
                     co->execScope != nullptr;
    if (suspended) {
        co->state = CoroutineState::Suspended;
    } else {
        co->state = CoroutineState::Completed;
        co->execScope = nullptr;
        co->yielded = res;
        co->savedCallStack.clear();
    }

    // 4. 恢复调用方调用栈
    ctx->setCallStack(std::move(savedStack));
    return res;
}

}  // namespace neo::core
