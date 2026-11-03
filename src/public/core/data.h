// data.h - Value 的数据负载抽象（引用计数载体）
//
// Data 是 Value 的 data 部分（ADR-015）：扁平基础类型数据，本阶段不持子 value。
// ref_ 计数由 Value 管理（构造 ++ref_，析构 --ref_），ref 归 0 时由 GC sweep
// 调 allocator->free 释放（Data 不自销毁、不持 Allocator*）。
// enumerate_children 为复杂对象图遍历预留，本阶段默认空（扁平 data 无子 value）。
//
// 关联 ADR: 015（Value meta+data 与引用计数分层）、029（Scope-Value-GC 生命周期）

#ifndef NEO_CORE_DATA_H
#define NEO_CORE_DATA_H

namespace neo::core {

// 前向声明（enumerate_children 参数）
class GCVisitor;

// Value 的数据负载。本阶段为抽象基类，具体扁平类型（NumberData/StringData/...）继承。
// data 内存走 Allocator，由 GC 的 alloc_data<T> 分配、GC sweep 释放。
class Data {
    int ref_ = 0;  // 多少个 Value 指向此 data
    friend class Value;  // Value 构造/析构操作 ref_

public:
    virtual ~Data() = default;

    int ref() const { return ref_; }

    // 复杂对象阶段：枚举子 value 供 GC 标记。本阶段扁平 data 无子 value，默认空。
    // 复杂类型（ObjectData/ArrayData/...）覆写此方法向 visitor 报告持有的子 value。
    virtual void enumerate_children(GCVisitor* /*visitor*/) {}
};

}  // namespace neo::core

#endif  // NEO_CORE_DATA_H
