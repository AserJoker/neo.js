// Value.h - 语言的最小值单元（meta + data + scope_ref）
//
// Value 是 GC 管理的单元（ADR-012/015）：meta=IType* 描述类型，data=IData* 持数据负载，
// scope_ref 记录被几个 scope 跟踪（GC 根判定）。
// Value 构造 data->ref++，析构 data->ref--（不销毁 data；data 由 GC sweep 在 ref 归 0 时释放）。
// Value 内存走 IAllocator + placement new（GC 负责分配/释放，ADR-029 §7）。
//
// 关联 ADR: 012（Scope 树 + scope_ref）、015（Value meta+data）、016（Type 体系）、029（生命周期）

#ifndef NEO_CORE_VALUE_H
#define NEO_CORE_VALUE_H

#include "core/IData.h"

namespace neo::core {

// 前向声明：Type 是 meta（ADR-016，抽象接口，具体实现后续补充）。
// 本阶段 Value 只持 IType* 指针，不需要 Type 完整定义。
class IType;

// 语言的值单元。GC 创建并持有所有权，调用方通过 Scope 跟踪（scope_ref++）。
class Value {
    IType* type_;          // meta：类型描述（单例/共享，GC 不管理 Type 生命周期）
    IData* data_;          // data：数据负载（引用计数管理，ADR-015）
    int   scope_ref_ = 0; // 被几个 scope 跟踪（GC 根判定，ADR-012）

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
    int   scope_ref() const { return scope_ref_; }

    // Scope 跟踪时调用（ADR-012 §4）
    void scope_ref_inc() { ++scope_ref_; }
    void scope_ref_dec() { --scope_ref_; }

    // GC 标记内部用（MarkSweepGC 标记阶段置位）
    bool marked() const { return marked_; }
    void set_marked(bool m) { marked_ = m; }

private:
    bool marked_ = false;  // GC 标记位（collect 时重置，标记阶段置 true）
};

}  // namespace neo::core

#endif  // NEO_CORE_VALUE_H
