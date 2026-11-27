// MarkSweepGC.h (private) - MarkSweepGC 实现声明
//
// 本头仅在 core 模块内部可见（src/private/），其他模块无法引用 MarkSweepGC，
// 只能通过 public 的 IGCEngine 接口 + createGcEngine() 工厂使用。
// 文件名与实现类名一致（MarkSweepGC.h），与 public 接口头 IGCEngine.h 不冲突。
//
// MarkSweepGC：标记-清除算法。collect 从 rootScope（global 作用域树）出发，
// 沿「scope.values + scope.children」与「data 子引用」标记可达 Value；pinRef>0
// 的 Value 作为额外根。未标记 Value 析构 + allocator->free，data ref 归 0 则 free。
//
// 关联 ADR: 010（接口-实现-工厂）、029（生命周期）、044（可达性标记）

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

    const GCStats& stats() const override { return stats_; }

    Value* createValue(IType* type, IData* data) override;
    Value* allocUnowned(IType* type, IData* data) override;
    void   freeUnowned(Value* v) override;
    void   adopt(Value* v) override;
    size_t cppOwnedCount() const override { return cppOwnedCount_; }
    void   ref(Value* v) override;
    void   unref(Value* v) override;
    Value* copyForNative(Value* source) override;
    void   collect() override;
    void   setRootScope(Scope* scope) override;
    void   addRootProvider(IGCRootProvider* provider) override;
    void   removeRootProvider(IGCRootProvider* provider) override;
    void   releaseData(IData* d) override;
    void   releaseValue(Value* v) override;

    size_t valueCount() const override;
    size_t collectedCount() const override;
    void   enumerateValues(IGCVisitor* v) const override;

    void setMemThreshold(double ratio) override;
    void setCountThreshold(size_t count) override;

protected:
    void* rawAlloc(size_t bytes) override;

private:
    // 分配 native-owned Value，不做 leaf 检查（allocUnowned/copyForNative 共用）。
    Value* allocUnownedRaw(IType* type, IData* data);
    // 检查是否需触发 GC（双重阈值），返回 true 表示已触发一次 collect
    bool maybeCollect();

    infra::IAllocator* alloc_;  // 注入，不持有所有权
    Scope* rootScope_ = nullptr;  // 根作用域树（Context 的 global，借用，不拥有）
    std::vector<Value*> values_;  // GC 持有所有 Value 的跟踪表
    // 空闲 Value 槽复用链表（避免每轮回收/分配都 malloc/free 抖动）。
    // 回收的 Value 内存不立即还给 allocator，而是留在本表供下次 createValue
    // 复用；~MarkSweepGC 时统一归还。内存占用上界 ≈ 峰值同时在用的 Value 数。
    std::vector<Value*> freeValues_;
    // GC 图之外的活跃根（VM 操作数栈等），非拥有。
    std::vector<IGCRootProvider*> rootProviders_;
    size_t collectedCount_ = 0;  // 累计回收数
    size_t cppOwnedCount_ = 0;   // 仍由 C++ 拥有、未 adopt/free 的 Value 数
    uint64_t markEpoch_ = 0;     // 当前标记代数（每轮 collect 递增，Value 记录最近标记代数）
    GCStats stats_;              // 诊断统计（P0）

    double memThreshold_   = 0.8;     // 内存阈值（used/limit，limit==SIZE_MAX 时不生效）
    // Value 数量阈值（无限额时兜底自动 GC）。必须显著高于 bootstrap 后的
    // 常驻 Value 数（当前约 10k）：否则内建注册完成后每次 createValue 都会
    // 触发 collect，且未挂到作用域的临时 Value 会被提前回收。
    size_t countThreshold_ = 100000;
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_MARK_SWEEP_GC_H
