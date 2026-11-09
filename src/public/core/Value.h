// Value.h - 语言的最小值单元（meta + data + pinRef）
//
// Value 是 GC 管理的单元（ADR-012/015）：meta=IType* 描述类型，data=IData* 持数据负载。
// 可达性由 GC 从 global 作用域树出发标记（ADR-044），**不**由 scope 跟踪计数决定。
// pinRef 是手工钉住计数（默认 0）：>0 时该 Value 作为额外根被标记，用于调度器
// 临时持有（Task::entity、待上报的未兑现 Promise）等 GC 图之外的强引用。
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
    bool  mutable_ = true;  // 是否可赋值（const 声明由 Sema 置 false，store 时拦截）

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

    // 可赋值性（ADR-030 阶段 5）：const 声明的变量槽位 mutable=false，
    // store 时拦截并报错。默认 true（var/let 可赋值）。
    bool isMutable() const { return mutable_; }
    void setMutable(bool m) { mutable_ = m; }

    // 手工钉住/解除（ADR-044）：>0 时该 Value 是 GC 根，与作用域树可达性无关。
    // 供调度器（Task::entity）等 GC 图之外的强引用使用。
    void pinRefInc() { ++pinRef_; }
    void pinRefDec() { --pinRef_; }

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

}  // namespace neo::core

#endif  // NEO_CORE_VALUE_H
