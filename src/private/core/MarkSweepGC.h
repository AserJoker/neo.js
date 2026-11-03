// MarkSweepGC.h (private) - MarkSweepGC 实现声明
//
// 本头仅在 core 模块内部可见（src/private/），其他模块无法引用 MarkSweepGC，
// 只能通过 public 的 IGCEngine 接口 + createGcEngine() 工厂使用。
// 文件名与实现类名一致（MarkSweepGC.h），与 public 接口头 IGCEngine.h 不冲突。
//
// MarkSweepGC：标记-清除算法。collect 时 scopeRef!=0 为根 + 沿 data 引用链遍历
// 标记（本阶段 data 扁平 noop），未标记 Value 析构 + allocator->free，并检查 data
// ref 归 0 则 allocator->free(data)。
//
// 关联 ADR: 010（接口-实现-工厂）、029（生命周期）

#ifndef NEO_CORE_PRIVATE_MARK_SWEEP_GC_H
#define NEO_CORE_PRIVATE_MARK_SWEEP_GC_H

#include "core/IGCEngine.h"  // public 接口

#include <cstddef>
#include <vector>

namespace neo::core {

// 标记-清除 GC 实现
class MarkSweepGC : public IGCEngine {
public:
    explicit MarkSweepGC(infra::IAllocator* alloc);
    ~MarkSweepGC() override;  // 析构时强制回收所有残留 Value，释放内存给 IAllocator

    Value* createValue(IType* type, IData* data) override;
    void   collect() override;

    size_t valueCount() const override;
    size_t collectedCount() const override;
    void   enumerateValues(IGCVisitor* v) const override;

    void setMemThreshold(double ratio) override;
    void setCountThreshold(size_t count) override;

protected:
    void* rawAlloc(size_t bytes) override;

private:
    // 检查是否需触发 GC（双重阈值），返回 true 表示已触发一次 collect
    bool maybeCollect();

    infra::IAllocator* alloc_;  // 注入，不持有所有权
    std::vector<Value*> values_;  // GC 持有所有 Value 的跟踪表
    size_t collectedCount_ = 0;  // 累计回收数

    double memThreshold_   = 0.8;     // 内存阈值（used/limit，limit==SIZE_MAX 时不生效）
    size_t countThreshold_ = 10000;   // Value 数量阈值（无限额时兜底自动 GC）
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_MARK_SWEEP_GC_H
