// VmState.h - VM 挂起状态快照（ADR-060 §8 / ADR-045）
//
// 一次逻辑执行的 VM 挂起时保存 pc、操作数栈、label/try 栈与入口/根作用域。
// 恢复时重建 VM 并恢复这些字段，再从 pc 继续执行。
//
// 生命周期：由 CoroutineData 独占持有；挂起期间 execScope 是孤立子树，
// 栈上的 Value 与 try 帧的 res 由协程对象在 GC 标记时遍历（visitVmState）。
//
// 关联 ADR: 023（协程挂起/恢复）、045（协程抽象）、046（Interrupt）、
//          060（VM 框架）

#ifndef NEO_VM_VM_STATE_H
#define NEO_VM_VM_STATE_H

#include "core/IGCEngine.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace neo::core {
class Scope;
class Value;
}  // namespace neo::core

namespace neo::vm {

// label 帧：label 为空表示无标签；startPc=0 表示不是 continue 目标。
struct LabelFrame {
    std::string label;
    uint32_t startPc = 0;
    uint32_t endPc = 0;
    neo::core::Scope* scope = nullptr;
    size_t stackDepth = 0;
    size_t tryDepth = 0;
};

// try 帧：catch/finally 为 out-of-line 块，出口由 VM 裁决。
// res 自描述 completion：nullptr=正常；Exception=待传播；Interrupt=break/continue；
// 其他=返回值。
struct TryFrame {
    uint32_t catchStart = 0;
    uint32_t finallyStart = 0;
    uint32_t tryEnd = 0;
    neo::core::Value* res = nullptr;
    neo::core::Scope* scope = nullptr;
    size_t stackDepth = 0;
    size_t labelDepth = 0;
};

struct VmState {
    uint32_t pc = 0;
    std::vector<neo::core::Value*> stack;
    std::vector<LabelFrame> labelStack;
    std::vector<TryFrame> tryStack;
    neo::core::Scope* entryScope = nullptr;
    neo::core::Scope* rootScope = nullptr;
    // 挂起时最内层作用域（可能是块作用域）；恢复时据此还原 current。
    neo::core::Scope* currentScope = nullptr;
    // yield* 委托中的被委托迭代器（非空表示当前挂起点属于委托）。
    neo::core::Value* delegateIterator = nullptr;
};

// GC 遍历：栈上的 Value 与 try 帧的 res。作用域由协程的 execScope 遍历覆盖。
inline void visitVmState(VmState* state, neo::core::IGCVisitor* visitor) {
    if (state == nullptr || visitor == nullptr) return;
    for (neo::core::Value* value : state->stack) {
        if (value != nullptr) visitor->visit(value);
    }
    for (const TryFrame& frame : state->tryStack) {
        if (frame.res != nullptr) visitor->visit(frame.res);
    }
    if (state->delegateIterator != nullptr) {
        visitor->visit(state->delegateIterator);
    }
}

}  // namespace neo::vm

#endif  // NEO_VM_VM_STATE_H
