// VM.h - 一次逻辑执行的 VM（ADR-060）
//
// 一个 VM 只负责一次逻辑执行（函数调用 / 模块顶层 / eval）。V0 只保留
// pc + 操作数栈：try/label 栈按纵向切片推迟到 V3/V4。
//
// 操作数栈不 pin Value：栈上元素都是 Engine 已创建、由当前作用域跟踪的值，
// VM 只借用指针，不增加 GC 根，也不负责生命周期。
//
// 关联 ADR: 004（执行模型）、006（异常即值）、022（函数调用）、
//          024（字节码指令集）、037（调用栈）、060（VM 框架）

#ifndef NEO_VM_VM_H
#define NEO_VM_VM_H

#include "core/CallStack.h"
#include "core/Value.h"
#include "vm/Code.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace neo::core {
class IContext;
class Scope;
}  // namespace neo::core

namespace neo::vm {

enum class VmStatus {
    Returned,   // ret：value 为返回值
    Halted,     // hlt：value 为 undefined
    Suspended,  // Interrupt：V0 不产生
    Failed,     // Exception/Fatal：value 为错误值
};

struct VmResult {
    VmStatus status = VmStatus::Halted;
    neo::core::Value* value = nullptr;
};

class VM {
public:
    // ctx 借用；code 借用（生命周期由 Module / script Function 持有）。
    // 入口作用域取 ctx->current()，root 取 ctx->root()。
    VM(neo::core::IContext* ctx, const Code* code);

    // 从 pc=0 执行到 ret/hlt/失败。
    VmResult run();

    // 当前指令对应的源码位置（SourceMap 反查）；无映射返回空位置。
    neo::core::SourcePosition currentPosition() const;

    uint32_t pc() const { return pc_; }
    const std::vector<neo::core::Value*>& stack() const { return stack_; }

private:
    void push(neo::core::Value* value) { stack_.push_back(value); }
    neo::core::Value* pop() {
        neo::core::Value* value = stack_.back();
        stack_.pop_back();
        return value;
    }
    neo::core::Value* top() const { return stack_.back(); }

    // 指令失败：补全异常栈后返回 Failed。
    VmResult fail(neo::core::Value* error);
    // 内部字节码错误（栈下溢/常量下标越界）。
    VmResult badBytecode(const char* message);

    // 取 Code::strings[a]；越界返回 nullptr。
    const std::string* stringOperand(uint32_t index) const;

    neo::core::IContext* ctx_;
    const Code* code_;
    uint32_t pc_ = 0;
    std::vector<neo::core::Value*> stack_;

    // 本次执行的入口作用域与模块根作用域（借用，V0 仅记录）。
    neo::core::Scope* entryScope_ = nullptr;
    neo::core::Scope* rootScope_ = nullptr;
};

}  // namespace neo::vm

#endif  // NEO_VM_VM_H
