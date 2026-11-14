// VM.h - 一次逻辑执行的 VM（ADR-060）
//
// 一个 VM 只负责一次逻辑执行（函数调用 / 模块顶层 / eval）。V0 只保留
// pc + 操作数栈：try/label 栈按纵向切片推迟到 V3/V4。
//
// 字节码是扁平字节数组，pc 是**字节偏移**。主循环只负责取 opcode、查分派表
// 调用对应成员函数、消费返回值；**pc 的推进由各处理函数自己完成**——只有它
// 知道本条指令有几个、什么类型的操作数。
//
// 处理函数返回 Value*：
//   - nullptr：正常执行完，继续下一条；
//   - 非空：执行信号（ret 的值 / Exception / Fatal / Interrupt），主循环立即返回。
//
// 操作数栈不 pin Value：栈上元素都是 Engine 已创建、由当前作用域跟踪的值，
// VM 只借用指针，不增加 GC 根，也不负责生命周期。
//
// 关联 ADR: 004（执行模型）、006（异常即值）、022（函数调用）、
//          024（字节码指令集）、037（调用栈）、060（VM 框架）

#ifndef NEO_VM_VM_H
#define NEO_VM_VM_H

#include "core/CallStack.h"
#include "core/OperatorKind.h"
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

class VM {
public:
    // ctx 借用；code 借用（生命周期由 Module / script Function 持有）。
    // 入口作用域取 ctx->current()，root 取 ctx->root()。
    VM(neo::core::IContext* ctx, const Code* code);

    // 从 entryPc 执行到 ret/hlt/失败，返回执行结果值。
    // 函数调用即「借用同一份 Code + 从函数地址（entryPc）开始执行」，
    // 故入口 pc 由调用方（Function::invoke）传入；模块顶层/eval 传 0。
    //
    // 返回值自描述，调用方按类型分派，不需要额外 status：
    //   - Interrupt：协程/异步挂起信号；
    //   - Exception：未捕获异常；
    //   - Fatal：不可恢复异常；
    //   - 其他：正常返回（ret 的值 / hlt 与自然结束的 undefined）。
    neo::core::Value* run(uint32_t entryPc = 0);

    // 当前指令对应的源码位置（SourceMap 反查）；无映射返回空位置。
    neo::core::SourcePosition currentPosition() const;

    uint32_t pc() const { return pc_; }
    const std::vector<neo::core::Value*>& stack() const { return stack_; }

private:
    // 指令处理函数：自行读取操作数并推进 pc。
    // 返回 nullptr 表示继续；非空表示执行信号，由 run 消费。
    using Handler = neo::core::Value* (VM::*)();

    // 分派表（OpCode → 成员函数指针）。
    static const Handler* handlers();

    void push(neo::core::Value* value) { stack_.push_back(value); }
    neo::core::Value* pop() {
        neo::core::Value* value = stack_.back();
        stack_.pop_back();
        return value;
    }
    neo::core::Value* top() const { return stack_.back(); }

    // 指令失败：补全异常栈后返回错误值（Exception/Fatal）。
    neo::core::Value* fail(neo::core::Value* error);
    // 内部字节码错误（栈下溢/常量下标越界/操作数截断）。
    neo::core::Value* badBytecode(const char* message);

    // 取 Code::strings[index]；越界返回 nullptr。
    const std::string* stringOperand(uint32_t index) const;

    // 二元运算公共实现（算术/关系处理函数委托到此）。
    neo::core::Value* binary(neo::core::BinaryOperator op);

    // —— 指令处理函数 ——
    neo::core::Value* opPushUndefined();
    neo::core::Value* opPushNull();
    neo::core::Value* opPushTrue();
    neo::core::Value* opPushFalse();
    neo::core::Value* opPushNumber();
    neo::core::Value* opPushString();
    neo::core::Value* opPop();
    neo::core::Value* opDup();
    neo::core::Value* opSwap();
    neo::core::Value* opDefine();
    neo::core::Value* opInitialize();
    neo::core::Value* opLoad();
    neo::core::Value* opStore();
    neo::core::Value* opAdd();
    neo::core::Value* opSub();
    neo::core::Value* opMul();
    neo::core::Value* opDiv();
    neo::core::Value* opLt();
    neo::core::Value* opGt();
    neo::core::Value* opLe();
    neo::core::Value* opGe();
    neo::core::Value* opJmp();
    neo::core::Value* opJTrue();
    neo::core::Value* opJFalse();
    neo::core::Value* opPushScope();
    neo::core::Value* opPopScope();
    neo::core::Value* opRet();
    neo::core::Value* opHalt();

    neo::core::IContext* ctx_;
    const Code* code_;
    uint32_t pc_ = 0;
    // 当前指令的起始字节偏移：处理函数推进 pc 后，异常位置仍能反查到本条指令。
    uint32_t instructionPc_ = 0;
    std::vector<neo::core::Value*> stack_;

    // 本次执行的入口作用域与模块根作用域（借用，V0 仅记录）。
    neo::core::Scope* entryScope_ = nullptr;
    neo::core::Scope* rootScope_ = nullptr;
};

}  // namespace neo::vm

#endif  // NEO_VM_VM_H
