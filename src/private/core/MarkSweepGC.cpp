// mark_sweep_gc.cpp - MarkSweepGC 实现（标记-清除）
//
// createValue：双重阈值检查 → alloc + placement new → 登记 values_。
// collect：
//   1. 重置所有 Value 的 marked；
//   2. 从 rootScope_（Context 的 global）出发，沿「scope.values + scope.children」
//      递归标记；孤立闭包作用域由 FunctionData 在标记时主动 visitScope 递归进来；
//   3. pinRef > 0 的 Value 作为额外根（调度器手工钉住，ADR-044）；
//   4. 未标记的 Value 析构（data->ref--），data ref 归 0 则 free(data) + free(value)。
//
// 关联 ADR: 008（GC→IAllocator）、010（接口-实现-工厂）、012（Scope 树）、
//          015（data 引用计数）、029（生命周期）、044（可达性标记）

#include "core/MarkSweepGC.h"

#include "core/Scope.h"
#include "core/Value.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <unordered_set>

namespace neo::core {

namespace {

// GC 标记访问器实现：标记 Value 并沿 data 子 value 递归；
// visitScope 沿作用域树（values + children）递归标记。
class Marker : public IGCVisitor {
public:
    void visit(Value* v) override {
        if (v->marked()) return;
        v->setMarked(true);
        // 沿 data 引用链递归标记子 value
        v->data()->enumerateChildren(this);
    }

    void visitScope(Scope* scope) override {
        if (scope == nullptr) return;
        // 同一作用域可能被多次请求标记（如闭包作用域被多个函数引用），去重
        if (!visitedScopes_.insert(scope).second) return;
        for (Value* v : scope->values()) visit(v);
        for (Scope* child : scope->children()) visitScope(child);
    }

private:
    std::unordered_set<Scope*> visitedScopes_;
};

}  // namespace

MarkSweepGC::MarkSweepGC(infra::IAllocator* alloc) : alloc_(alloc) {}

MarkSweepGC::~MarkSweepGC() {
    // 0. 销毁根作用域树（ADR-044：所有权在本类）。销毁后 rootScope_ 为空，
    //    所有 Value 都不可达，下面的 collect 才能把它们全部回收。
    if (rootScope_ != nullptr) {
        rootScope_->destroySelf();
        rootScope_ = nullptr;
    }
    // 析构时反复 collect 直到收敛。
    //
    // 关键点（ADR-036）：sweep 阶段销毁 data 可能连带销毁其持有的作用域
    // （如 FunctionData 析构销毁孤立闭包作用域），进而使别的 Value 失去可达性。
    // 这些 Value 在本轮标记阶段仍被标记存活，要到**下一轮**才被回收。
    // 因此「collect 后 values_ 非空」不再是泄漏判据，判据是**本轮净销毁量为 0**：
    // 只要还有进展就继续下一轮，直到某轮一个都没回收——此时残留的才是真泄漏。
    while (!values_.empty()) {
        size_t before = collectedCount_;
        collect();
        if (collectedCount_ == before) break;  // 本轮零进展：收敛，残留即泄漏
    }
    // 收敛后仍有 Value 存活 = 内存泄漏（scope 未销毁，或 GC 标记/回收逻辑错误）。
    if (!values_.empty()) {
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
    // 1. 重置所有 marked
    for (Value* v : values_) v->setMarked(false);
    Marker marker;
    // 2. 从根作用域树（global）出发标记可达 Value
    marker.visitScope(rootScope_);
    // 3. pinRef > 0 的 Value 是额外根（调度器手工钉住，ADR-044）
    for (Value* v : values_) {
        if (v->pinRef() > 0) marker.visit(v);
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

void MarkSweepGC::setRootScope(Scope* scope) {
    rootScope_ = scope;
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
