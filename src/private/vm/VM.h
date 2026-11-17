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
#include "vm/VmState.h"

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

    // 从挂起状态恢复执行。sent 是外部注入值：
    //   - 普通值：压栈作为 yield/await 表达式的结果；
    //   - Interrupt(Throw)：在挂起点抛出其 value；
    //   - Interrupt(Return)：在挂起点执行 return 展开。
    neo::core::Value* resume(const VmState& state, neo::core::Value* sent);

    // 保存当前执行状态（协程挂起时调用）。
    VmState snapshot() const;
    // 恢复执行状态（resume 内部使用）。
    void restore(const VmState& state);

    // 当前指令对应的源码位置（SourceMap 反查）；无映射返回空位置。
    neo::core::SourcePosition currentPosition() const;

    uint32_t pc() const { return pc_; }
    const std::vector<neo::core::Value*>& stack() const { return stack_; }

    // 供 Function::invoke 在启动 VM 前压入形参与 self（ADR-022）。
    void push(neo::core::Value* value) { stack_.push_back(value); }

private:
    // 指令处理函数：自行读取操作数并推进 pc。
    // 返回 nullptr 表示继续；非空表示执行信号，由 run 消费。
    using Handler = neo::core::Value* (VM::*)();

    // 分派表（OpCode → 成员函数指针）。
    static const Handler* handlers();

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

    // 消费 Break/Continue Interrupt：解析 label 栈并跳转。
    // 成功返回 nullptr（pc 已更新），失败返回错误值。
    neo::core::Value* handleLabelInterrupt(neo::core::Value* interrupt);

    // 回到 try 帧入口：截断操作数栈、回退 label 栈、回退作用域。
    void restoreFrame(const TryFrame& frame);
    // 把 pending 值挂到帧入口作用域，保证 restore/GC 期间存活。
    void trackRes(TryFrame& frame, neo::core::Value* res);
    // res 分派：Exception 找 catch；Interrupt 走 label；其他按返回值消费 finally。
    neo::core::Value* dispatchPending(neo::core::Value* res);
    // 返回值展开：消费 finally，耗尽后返回 value；跳转时返回 nullptr。
    neo::core::Value* unwindReturn(neo::core::Value* value);
    // 异常展开：从当前帧向外找 catch；无处理器时返回 exception。
    neo::core::Value* dispatchException(neo::core::Value* exception);
    // 三个出口信号。
    neo::core::Value* handleTryBodyEnd();
    neo::core::Value* handleCatchEnd();
    neo::core::Value* handleFinallyEnd();

    // yield* 单步：把 sent（正常值/Throw/Return）转发给被委托迭代器。
    // 返回 nullptr 表示委托完成（结果已压栈）；否则返回 Yield Interrupt 或错误。
    neo::core::Value* delegateStep(neo::core::Value* sent);

    // 取 Code::strings[index]；越界返回 nullptr。
    const std::string* stringOperand(uint32_t index) const;

    // 二元运算公共实现（算术/关系处理函数委托到此）。
    neo::core::Value* binary(neo::core::BinaryOperator op);

    // —— 指令处理函数 ——
    neo::core::Value* opPushUndefined();
    neo::core::Value* opPushUninitialized();
    neo::core::Value* opPushNull();
    neo::core::Value* opPushTrue();
    neo::core::Value* opPushFalse();
    neo::core::Value* opPushNumber();
    neo::core::Value* opPushString();
    neo::core::Value* opPushBigInt();
    neo::core::Value* opPushRegExp();
    neo::core::Value* opDeclareVar();
    neo::core::Value* opDirectEval();
    neo::core::Value* opDirectEvalSpread();
    neo::core::Value* opPop();
    neo::core::Value* opDup();
    neo::core::Value* opClone();
    neo::core::Value* opSwap();
    neo::core::Value* opDup2();
    neo::core::Value* opDeleteProp();
    neo::core::Value* opSetPropKeep();
    neo::core::Value* opSetSuperPropKeep();
    neo::core::Value* opDefine();
    neo::core::Value* opInitialize();
    neo::core::Value* opLoad();
    neo::core::Value* opStore();
    neo::core::Value* opGetProp();
    neo::core::Value* opSetProp();
    neo::core::Value* opNewObject();
    neo::core::Value* opNewArray();
    neo::core::Value* opDefineProp();
    neo::core::Value* opDefineAccessor();
    neo::core::Value* opSetProtoLiteral();
    neo::core::Value* opTemplateObject();
    neo::core::Value* opArrayAppend();
    neo::core::Value* opArrayGrow();
    neo::core::Value* opArraySpread();
    neo::core::Value* opObjectSpread();
    neo::core::Value* opObjectRest();
    neo::core::Value* opObjectRestDynamic();
    neo::core::Value* opForInKeys();
    neo::core::Value* opIsEnumerableOwn();
    neo::core::Value* opGetIterator();
    neo::core::Value* opIteratorNext();
    neo::core::Value* opIteratorClose();
    neo::core::Value* opGetAsyncIterator();
    neo::core::Value* opAsyncIteratorClose();
    neo::core::Value* opAdd();
    neo::core::Value* opSub();
    neo::core::Value* opMul();
    neo::core::Value* opDiv();
    neo::core::Value* opLt();
    neo::core::Value* opGt();
    neo::core::Value* opLe();
    neo::core::Value* opGe();
    neo::core::Value* opStrictEqual();
    neo::core::Value* opIn();
    neo::core::Value* opUnary();
    neo::core::Value* opToString();
    neo::core::Value* opJmp();
    neo::core::Value* opJTrue();
    neo::core::Value* opJFalse();
    neo::core::Value* opJNotNullish();
    neo::core::Value* opTryBegin();
    neo::core::Value* opTryBodyEnd();
    neo::core::Value* opCatchEnd();
    neo::core::Value* opFinallyEnd();
    neo::core::Value* opThrow();
    neo::core::Value* opPushLabel();
    neo::core::Value* opPopLabel();
    neo::core::Value* opPopLabelIfTop();
    neo::core::Value* opBreak();
    neo::core::Value* opContinue();
    neo::core::Value* opPushScope();
    neo::core::Value* opPopScope();
    neo::core::Value* opPushWithScope();
    neo::core::Value* opIterationEnv();
    neo::core::Value* opCommitIterationEnv();
    neo::core::Value* opPushFunction();
    neo::core::Value* opSetClosure();
    neo::core::Value* opSetFuncName();
    neo::core::Value* opBindThis();
    neo::core::Value* opLoadThis();
    neo::core::Value* opStoreThis();
    neo::core::Value* opCall();
    neo::core::Value* opCallMethod();
    neo::core::Value* opCallSpread();
    neo::core::Value* opCallMethodSpread();
    neo::core::Value* opNew();
    neo::core::Value* opNewSpread();
    neo::core::Value* opLoadNewTarget();
    neo::core::Value* opImportMeta();
    neo::core::Value* opDynamicImport();
    neo::core::Value* opYield();
    neo::core::Value* opYieldDelegate();
    neo::core::Value* opAwait();
    neo::core::Value* opRestArgs();
    neo::core::Value* opPushClass();
    neo::core::Value* opSetStrict();
    neo::core::Value* opInitInstanceElements();
    neo::core::Value* opConstructSuper();
    neo::core::Value* opConstructSuperSpread();
    neo::core::Value* opGetSuperProp();
    neo::core::Value* opSetSuperProp();
    neo::core::Value* opDefineClassMethod();
    neo::core::Value* opDefineClassField();
    neo::core::Value* opDefinePrivateField();
    neo::core::Value* opDefinePrivateMethod();
    neo::core::Value* opGetPrivateField();
    neo::core::Value* opGetPrivateMethod();
    neo::core::Value* opGetPrivateGetter();
    neo::core::Value* opSetPrivateField();
    neo::core::Value* opSetPrivateSetter();
    neo::core::Value* opSetPrivateFieldKeep();
    neo::core::Value* opSetPrivateSetterKeep();
    neo::core::Value* opSetOwnerClass();
    neo::core::Value* opPrivateIn();
    neo::core::Value* opRet();
    neo::core::Value* opHalt();

    // 执行循环主体（run/resume 共用）。
    neo::core::Value* execute();

    neo::core::IContext* ctx_;
    const Code* code_;
    uint32_t pc_ = 0;
    // 当前指令的起始字节偏移：处理函数推进 pc 后，异常位置仍能反查到本条指令。
    uint32_t instructionPc_ = 0;
    std::vector<neo::core::Value*> stack_;
    std::vector<LabelFrame> labelStack_;
    std::vector<TryFrame> tryStack_;
    // yield* 委托中的被委托迭代器（nullptr = 不在委托中）。
    neo::core::Value* delegateIterator_ = nullptr;

    // 本次执行的入口作用域与模块根作用域（借用，V0 仅记录）。
    neo::core::Scope* entryScope_ = nullptr;
    neo::core::Scope* rootScope_ = nullptr;
};

}  // namespace neo::vm

#endif  // NEO_VM_VM_H
