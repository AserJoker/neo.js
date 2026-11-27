// Value.h - 语言的最小值单元（meta + data + pinRef）
//
// Value 是 GC 管理的单元（ADR-012/015）：meta=IType* 描述类型，data=IData* 持数据负载。
// 可达性由 GC 从 global 作用域树出发标记（ADR-044），**不**由 scope 跟踪计数决定。
// pinRef 是统一的 native 引用计数（默认 0），语义按所有权区分：
//   - GC-owned：>0 时该 Value 作为额外根被标记（调度器、借用期 pin 等）；
//   - native-owned：即生命周期计数，归 0 时由 GC 引擎销毁。
// C++ 多位置共享/传递 Value 直接走 IGCEngine::ref/unref，无需外层 shared_ptr。
// Value 构造 data->ref++，析构 data->ref--（不销毁 data；data 由 GC sweep 在 ref 归 0 时释放）。
// Value 内存走 IAllocator + placement new（GC 负责分配/释放，ADR-029 §7）。
//
// 关联 ADR: 012（Scope 树）、015（Value meta+data）、016（Type 体系）、029（生命周期）、
//          044（GC 从作用域树可达性标记）

#ifndef NEO_CORE_VALUE_H
#define NEO_CORE_VALUE_H

#include "core/IData.h"

#include <cstdint>

namespace neo::core {

// 前向声明：Type 是 meta（ADR-016，抽象接口，具体实现后续补充）。
// 本阶段 Value 只持 IType* 指针，不需要 Type 完整定义。
class IType;

// 语言的值单元。GC 创建并持有所有权；可达性由 GC 从 global 作用域树标记。
class Value {
    IType* type_;          // meta：类型描述（单例/共享，GC 不管理 Type 生命周期）
    IData* data_;          // data：数据负载（引用计数管理，ADR-015）
    int   pinRef_ = 0;     // 手工钉住计数（>0 即额外 GC 根，ADR-044）
    // 被 Scope::values_ 登记的出现次数（ADR-065 两域所有权）。
    // 仅作 popScope 提前释放的提示，不参与 GC 根判定。
    uint32_t scopeRef_ = 0;

public:
    // 构造时 data->ref++（建立"Value 指向 data"引用关系）
    Value(IType* type, IData* data);
    // 析构时 data->ref--（不销毁 data；data 由 GC sweep 在 ref 归 0 时释放）
    ~Value();

    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
    Value(Value&&) = delete;
    Value& operator=(Value&&) = delete;

    IType* type() const { return type_; }
    IData* data() const { return data_; }
    int   pinRef() const { return pinRef_; }

    // 手工钉住/解除（ADR-044）：>0 时该 Value 是 GC 根，与作用域树可达性无关。
    // 供调度器（Task::entity）等 GC 图之外的强引用使用。
    void pinRefInc() { ++pinRef_; }
    void pinRefDec() { --pinRef_; }

    // 作用域登记计数（ADR-065）：Scope::add 递增、remove 递减。
    // 最高位借作 dead 标记（popScope 已析构、等待 GC 释放内存），
    // 次高位借作 gcOwned 标记（所有权已转移给 GC；未标记 = C++ 拥有），
    // 使 Value 保持 32 字节布局。
    static constexpr uint32_t kDeadBit = 0x80000000u;
    static constexpr uint32_t kGcOwnedBit = 0x40000000u;
    static constexpr uint32_t kFlagMask = kDeadBit | kGcOwnedBit;
    bool dead() const { return (scopeRef_ & kDeadBit) != 0; }
    void markDead() { scopeRef_ |= kDeadBit; }
    // 所有权：true = 已登记进 GC（GC 负责回收）；false = C++ 拥有。
    bool gcOwned() const { return (scopeRef_ & kGcOwnedBit) != 0; }
    void markGcOwned() { scopeRef_ |= kGcOwnedBit; }
    uint32_t scopeRef() const { return scopeRef_ & ~kFlagMask; }
    void scopeRefInc() {
        if (!dead()) {
            scopeRef_ = (scopeRef_ & kFlagMask) | (scopeRef() + 1);
        }
    }
    void scopeRefDec() {
        uint32_t n = scopeRef();
        if (n > 0) scopeRef_ = (scopeRef_ & kFlagMask) | (n - 1);
    }

    // 替换 meta+data（赋值语义，ADR-030 阶段 5）：
    // 旧 data ref--，新 data ref++，type_ 和 data_ 同时替换。
    // Value 本体不变（指针地址不变），仅内部 meta+data 被替换。
    // 返回旧 data（调用方交给 gc->releaseData 回收；ref 归 0 才真正释放）。
    IData* replaceMeta(IType* type, IData* data);

    // GC 标记内部用：markEpoch 是「最近一次标记到本 Value 的 GC 代数」。
    // 每轮 collect 用新的单调递增代数，故无需在标记前重置所有 Value
    // （STW 下代数唯一；未活过本轮的 Value 下一轮已不存在，不会出现旧标记
    // 被误认为本轮标记的回环问题）。
    uint64_t markEpoch() const { return markEpoch_; }
    void setMarkEpoch(uint64_t epoch) { markEpoch_ = epoch; }

private:
    uint64_t markEpoch_ = 0;  // 最近一次标记的 GC 代数（0 = 从未标记）
};

// 借用值的临时 GC 根（ADR-065 §5）：C++ 局部变量不是 GC 根，从 VM 栈 / 作用域
// 弹出后跨「可能触发分配」的调用（getProperty / call / binaryOp…）持有的值，
// 须用 PinGuard 钉住，否则中途 GC 可能把它当作不可达回收。
// RAII：构造 pinRefInc、析构 pinRefDec；reset 切换目标（先解除旧值）。
class PinGuard {
public:
    PinGuard() = default;
    explicit PinGuard(Value* v) : value_(v) {
        if (value_ != nullptr) value_->pinRefInc();
    }
    ~PinGuard() { release(); }

    PinGuard(const PinGuard&) = delete;
    PinGuard& operator=(const PinGuard&) = delete;

    void reset(Value* v) {
        if (v == value_) return;
        release();
        value_ = v;
        if (value_ != nullptr) value_->pinRefInc();
    }

    Value* get() const { return value_; }

private:
    void release() {
        if (value_ != nullptr) {
            value_->pinRefDec();
            value_ = nullptr;
        }
    }

    Value* value_ = nullptr;
};

}  // namespace neo::core

#endif  // NEO_CORE_VALUE_H
