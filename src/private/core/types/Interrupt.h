// types/Interrupt.h - InterruptType + InterruptData
//
// Interrupt 是**控制流信号值**（ADR-046），与 Exception/Fatal 正交：
// 它不是错误，不参与 isErrorValue / try-catch，只由协程函数体或 VM 执行循环
// 产生、由协程 runner 消费。正常业务路径不会创建它，故不需要透传纪律。
//
// 三部分：
//   - kind   控制信号种类（Yield / Return / Await / Throw）
//   - offset 断点：native = 阶段号（stage）；VM = PC。抽象层视为不透明整数。
//   - value  载荷（yield 值 / return 值 / await 结果 / 错误值）
//
// offset 双向语义：
//   - 函数返回方向（Yield）：恢复后应从哪个位置继续（nextStage / nextPC）
//   - 外部注入方向（Return/Throw）：当前所在位置（currentStage / currentPC）
//
// 非对象类型（objectLayout=false），行为方法全部透传 self（与 Exception/Fatal 同类）。
//
// 关联 ADR: 006（异常即值）、016（类型体系）、017（Interrupt 规划）、
//          045（协程抽象）、046（Interrupt 控制信号）

#ifndef NEO_CORE_PRIVATE_TYPES_INTERRUPT_H
#define NEO_CORE_PRIVATE_TYPES_INTERRUPT_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/InterruptKind.h"
#include "infra/IAllocator.h"

#include <cstdint>
#include <new>
#include <string_view>

namespace neo::core {

class Value;

class InterruptType : public IType {
public:
    InterruptType() : IType(TypeKind::Interrupt) {}
    const char* name() const override { return "interrupt"; }

    // 行为方法：全部透传 self（控制信号不参与运算/属性/调用）
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;
    Value* getProperty(IContext* ctx, Value* self, Value* key) override;
    Value* setProperty(IContext* ctx, Value* self, Value* key, Value* value) override;
    Value* hasProperty(IContext* ctx, Value* self, Value* key) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
    Value* defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                          Value* setter, bool enumerable, bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

// 控制信号负载：kind + offset + value（value 由 GC 遍历保活）
class InterruptData : public IData {
public:
    InterruptKind kind;
    uint64_t offset;
    Value* value;  // 载荷（永远不为 nullptr，无载荷时用 undefined）

    InterruptData(InterruptKind k, uint64_t off, Value* v)
        : kind(k), offset(off), value(v) {}

    // GC 遍历：value 是 GC 管的子 value
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

// 内部槽访问：非 Interrupt 返回 nullptr。
InterruptData* asInterrupt(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_INTERRUPT_H
