// mark_sweep_gc.cpp - MarkSweepGC 实现（标记-清除，扁平 Value 阶段）
//
// createValue：双重阈值检查 → alloc + placement new → 登记 values_。
// collect：标记阶段 scopeRef!=0 为根 + 沿 data 引用链遍历（本阶段 noop），
//          清除阶段未标记 Value 析构（data->ref--）+ 检查 data ref 归 0 则 free(data) + free(value)。
//
// 关联 ADR: 008（GC→IAllocator）、010（接口-实现-工厂）、012（scopeRef 根）、
//          015（data 引用计数）、029（生命周期）

#include "core/MarkSweepGC.h"

#include "core/Value.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace neo::core {

namespace {

// GC 标记访问器实现：标记 Value 并沿 data 子 value 递归（本阶段 data 扁平 noop）
class Marker : public IGCVisitor {
public:
    void visit(Value* v) override {
        if (v->marked()) return;
        v->setMarked(true);
        // 沿 data 引用链递归标记子 value（本阶段 data 扁平，enumerateChildren 默认空）
        v->data()->enumerateChildren(this);
    }
};

}  // namespace

MarkSweepGC::MarkSweepGC(infra::IAllocator* alloc) : alloc_(alloc) {}

MarkSweepGC::~MarkSweepGC() {
    // 析构时调一次 collect 回收所有不可达 Value。
    // 调用方应在此之前销毁所有 scope（scopeRef 归 0），collect 后不应有存活 Value。
    // 若仍有 value 存活，说明存在泄漏（scope 未销毁、或 GC 逻辑 bug），assert 失败报告。
    collect();
    // collect 后 values_ 应为空：所有 Value 都应该 scopeRef=0 被回收。
    // 残留 Value 即内存泄漏（调用方未正确销毁 scope，或 GC 标记/回收逻辑错误）。
    if (!values_.empty()) {
        // 内存泄漏：报告残留 Value 数，便于定位。
        // 不直接 abort()，先 fprintf 让错误信息可见，再 abort。
        std::fprintf(stderr,
                     "MarkSweepGC leak: %zu value(s) still alive after final collect\n",
                     values_.size());
        std::abort();
    }
}

void* MarkSweepGC::rawAlloc(size_t bytes) {
    return alloc_->alloc(bytes);
}

bool MarkSweepGC::maybeCollect() {
    // 双重阈值：内存阈值（limit != SIZE_MAX 时生效）+ Value 数量阈值（兜底）
    size_t lim = alloc_->limit();
    bool need = false;
    if (lim != SIZE_MAX) {
        // 避免除零/溢出：lim 至少为 1
        double ratio = static_cast<double>(alloc_->used()) / static_cast<double>(lim);
        if (ratio >= memThreshold_) need = true;
    }
    if (values_.size() >= countThreshold_) need = true;
    if (need) {
        collect();
        return true;
    }
    return false;
}

Value* MarkSweepGC::createValue(IType* type, IData* data) {
    maybeCollect();

    void* p = alloc_->alloc(sizeof(Value));
    if (p == nullptr) return nullptr;  // OOM
    Value* v = new (p) Value(type, data);  // placement new，Value 构造内 data->ref++
    values_.push_back(v);
    return v;
}

void MarkSweepGC::collect() {
    if (values_.empty()) return;

    // —— 标记阶段 ——
    // 重置所有 marked
    for (Value* v : values_) v->setMarked(false);
    // scopeRef != 0 为根，沿 data 引用链标记
    Marker marker;
    for (Value* v : values_) {
        if (v->scopeRef() > 0) {
            marker.visit(v);
        }
    }

    // —— 清除阶段 ——
    // 未标记的 Value：析构（data->ref--）+ 检查 data ref 归 0 则 free(data) + free(value)
    // 用 partition 把存活 Value 移到前面，被回收的移到后面，便于一次性 erase
    size_t write = 0;
    for (size_t read = 0; read < values_.size(); ++read) {
        Value* v = values_[read];
        if (v->marked()) {
            values_[write++] = v;  // 存活，保留
        } else {
            // 回收：先记 data 指针，析构 Value（data->ref--），再检查 data ref
            IData* d = v->data();
            v->~Value();  // 显式析构（placement new 的对象需显式析构）
            alloc_->free(v);
            // data ref 归 0 则释放 data（多 Value 共享 data 时仅最后一次回收才 free）
            if (d->ref() == 0) {
                d->~IData();  // 显式析构（data 含 std::string 等需析构的成员，placement new 须显式析构）
                alloc_->free(d);
            }
            ++collectedCount_;
        }
    }
    values_.resize(write);
}

void MarkSweepGC::releaseData(IData* d) {
    // 赋值 replaceMeta 后旧 data 被摘除，collect 遍历不到它。
    // ref 归 0 则析构 + free；仍被其他 Value 共享则无操作。
    if (d->ref() == 0) {
        d->~IData();
        alloc_->free(d);
    }
}

size_t MarkSweepGC::valueCount() const {
    return values_.size();
}

size_t MarkSweepGC::collectedCount() const {
    return collectedCount_;
}

void MarkSweepGC::enumerateValues(IGCVisitor* v) const {
    for (Value* val : values_) v->visit(val);
}

void MarkSweepGC::setMemThreshold(double ratio) {
    memThreshold_ = ratio;
}

void MarkSweepGC::setCountThreshold(size_t count) {
    countThreshold_ = count;
}

// 工厂函数
std::unique_ptr<IGCEngine> createGcEngine(infra::IAllocator* alloc) {
    return std::make_unique<MarkSweepGC>(alloc);
}

}  // namespace neo::core
