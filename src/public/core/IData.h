// IData.h - Value 的数据负载抽象接口（引用计数载体）
//
// IData 是 Value 的 data 部分（ADR-015）：扁平基础类型数据，本阶段不持子 value。
// ref_ 计数由 Value 管理（构造 ++ref_，析构 --ref_），ref 归 0 时由 GC sweep
// 调 allocator->free 释放（IData 不自销毁、不持 IAllocator*）。
// enumerateChildren 为复杂对象图遍历预留，本阶段默认空（扁平 data 无子 value）。
// clone 为赋值语义（ADR-030 阶段 5）：每种 data 知道如何克隆自己，走 IAllocator 分配。
//
// 关联 ADR: 015（Value meta+data 与引用计数分层）、029（Scope-Value-GC 生命周期）

#ifndef NEO_CORE_IDATA_H
#define NEO_CORE_IDATA_H

namespace neo::infra {
class IAllocator;  // 前向声明：clone 参数
}

namespace neo::core {

// 前向声明（enumerateChildren 参数）
class IGCVisitor;

// Value 的数据负载。本阶段为抽象基类，具体扁平类型（NumberData/StringData/...）继承。
// data 内存走 IAllocator，由 GC 的 allocData 分配、GC sweep 释放。
class IData {
    int ref_ = 0;  // 多少个 Value 指向此 data
    friend class Value;  // Value 构造/析构操作 ref_

public:
    virtual ~IData() = default;

    int ref() const { return ref_; }

    // 复杂对象阶段：枚举子 value 供 GC 标记。本阶段扁平 data 无子 value，默认空。
    // 复杂类型（ObjectData/ArrayData/...）覆写此方法向 visitor 报告持有的子 value。
    virtual void enumerateChildren(IGCVisitor* /*visitor*/) {}

    // 克隆 data（赋值语义，ADR-030 阶段 5）：用 allocator 分配新 data 并拷贝负载。
    // 每种 data 子类覆写：简单标量直接拷贝值，复杂类型深拷贝/共享子 value。
    // 返回新分配的 IData*（调用方通过 Value::replaceMeta 接管）。
    virtual IData* clone(neo::infra::IAllocator* alloc) const = 0;
};

}  // namespace neo::core

#endif  // NEO_CORE_IDATA_H
