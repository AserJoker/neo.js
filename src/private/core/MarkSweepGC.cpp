// mark_sweep_gc.cpp - MarkSweepGC 实现（标记-清除，扁平 Value 阶段）
//
// create_value：双重阈值检查 → alloc + placement new → 登记 values_。
// collect：标记阶段 scope_ref!=0 为根 + 沿 data 引用链遍历（本阶段 noop），
//          清除阶段未标记 Value 析构（data->ref--）+ 检查 data ref 归 0 则 free(data) + free(value)。
//
// 关联 ADR: 008（GC→Allocator）、010（接口-实现-工厂）、012（scope_ref 根）、
//          015（data 引用计数）、029（生命周期）

#include "core/impl_mark_sweep_gc.h"

#include "core/value.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace neo::core {

namespace {

// GC 标记访问器实现：标记 Value 并沿 data 子 value 递归（本阶段 data 扁平 noop）
class Marker : public GCVisitor {
public:
    void visit(Value* v) override {
        if (v == nullptr || v->marked()) return;
        v->set_marked(true);
        // 沿 data 引用链递归标记子 value（本阶段 data 扁平，enumerate_children 默认空）
        if (v->data() != nullptr) {
            v->data()->enumerate_children(this);
        }
    }
};

}  // namespace

MarkSweepGC::MarkSweepGC(infra::Allocator* alloc) : alloc_(alloc) {}

MarkSweepGC::~MarkSweepGC() {
    // 析构时调一次 collect 回收所有不可达 Value。
    // 调用方应在此之前销毁所有 scope（scope_ref 归 0），collect 后不应有存活 Value。
    // 若仍有 value 存活，说明存在泄漏（scope 未销毁、或 GC 逻辑 bug），assert 失败报告。
    collect();
    // collect 后 values_ 应为空：所有 Value 都应该 scope_ref=0 被回收。
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

void* MarkSweepGC::raw_alloc(size_t bytes) {
    return alloc_->alloc(bytes);
}

bool MarkSweepGC::maybe_collect() {
    // 双重阈值：内存阈值（limit != SIZE_MAX 时生效）+ Value 数量阈值（兜底）
    size_t lim = alloc_->limit();
    bool need = false;
    if (lim != SIZE_MAX) {
        // 避免除零/溢出：lim 至少为 1
        double ratio = static_cast<double>(alloc_->used()) / static_cast<double>(lim);
        if (ratio >= mem_threshold_) need = true;
    }
    if (values_.size() >= count_threshold_) need = true;
    if (need) {
        collect();
        return true;
    }
    return false;
}

Value* MarkSweepGC::create_value(Type* type, Data* data) {
    maybe_collect();

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
    for (Value* v : values_) v->set_marked(false);
    // scope_ref != 0 为根，沿 data 引用链标记
    Marker marker;
    for (Value* v : values_) {
        if (v->scope_ref() > 0) {
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
            Data* d = v->data();
            v->~Value();  // 显式析构（placement new 的对象需显式析构）
            alloc_->free(v);
            // data ref 归 0 则释放 data（多 Value 共享 data 时仅最后一次回收才 free）
            if (d != nullptr && d->ref() == 0) {
                alloc_->free(d);
            }
            ++collected_count_;
        }
    }
    values_.resize(write);
}

size_t MarkSweepGC::value_count() const {
    return values_.size();
}

size_t MarkSweepGC::collected_count() const {
    return collected_count_;
}

void MarkSweepGC::enumerate_values(GCVisitor* v) const {
    for (Value* val : values_) v->visit(val);
}

void MarkSweepGC::set_mem_threshold(double ratio) {
    mem_threshold_ = ratio;
}

void MarkSweepGC::set_count_threshold(size_t count) {
    count_threshold_ = count;
}

// 工厂函数
std::unique_ptr<GCEngine> create_gc_engine(infra::Allocator* alloc) {
    return std::make_unique<MarkSweepGC>(alloc);
}

}  // namespace neo::core
