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
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <unordered_set>

namespace neo::core {

namespace {

// GC 标记访问器实现：标记 Value 并沿 data 子 value 递归；
// visitScope 沿作用域树（values + children）递归标记。
// 用单调递增的 epoch 代替布尔标记：本轮已标记过的 Value/data 其 markEpoch == epoch，
// 无需在标记前遍历 values_ 重置标记（STW 下 epoch 唯一）。
// data 也按 epoch 去重：多个 Value 共享同一 data（clone 引用语义 / copyForStorage）
// 时，子 value 只需枚举一次，否则「副本数 × 子数」会让标记退化为 O(n²)。
class Marker : public IGCVisitor {
public:
    explicit Marker(uint64_t epoch) : epoch_(epoch) {}

    void visit(Value* v) override {
        // dead：popScope 已提前析构（data 已释放），等待 sweep 回收内存。
        if (v == nullptr || v->dead()) return;
        if (v->markEpoch() == epoch_) return;
        v->setMarkEpoch(epoch_);
        IData* data = v->data();
        if (data->markEpoch() == epoch_) return;
        data->setMarkEpoch(epoch_);
        // 沿 data 引用链递归标记子 value
        data->enumerateChildren(this);
    }

    void visitScope(Scope* scope) override {
        if (scope == nullptr) return;
        // 同一作用域可能被多次请求标记（如闭包作用域被多个函数引用），去重
        if (!visitedScopes_.insert(scope).second) return;
        for (Value* v : scope->values()) visit(v);
        for (Scope* child : scope->children()) visitScope(child);
    }

private:
    uint64_t epoch_;
    std::unordered_set<Scope*> visitedScopes_;
};

#ifndef NDEBUG
// 统计 data 的子 Value 数：allocUnowned 只允许 leaf data（无子节点）。
class ChildCounter final : public IGCVisitor {
public:
    size_t count = 0;
    void visit(Value*) override { ++count; }
    void visitScope(Scope*) override {}
};
#endif

// 统计作用域树快照（P0）：values_ 中命名绑定的占比可近似「匿名临时值」比例。
void snapshotScopes(Scope* scope, GCStats& stats) {
    if (scope == nullptr) return;
    ++stats.lastScopeCount;
    stats.lastScopeValues += scope->valueCount();
    stats.lastNamedBindings += scope->namedCount();
    for (Scope* child : scope->children()) snapshotScopes(child, stats);
}

}  // namespace

MarkSweepGC::MarkSweepGC(infra::IAllocator* alloc) : alloc_(alloc) {}

MarkSweepGC::~MarkSweepGC() {
    // 0. 先清空根作用域树跟踪的 Value（不释放作用域内存），使所有 Value 不可达。
    //    不能先销毁作用域树：FunctionData 析构会沿 Environment 闭包的 parent 链
    //    解 pin，若作用域已释放会访问悬垂指针。因此保持作用域存活到 collect 收敛后。
    if (rootScope_ != nullptr) rootScope_->clearTrackedValuesRecursive();
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
    // 1. Value 全部回收后销毁根作用域树（ADR-044：所有权在本类）。
    if (rootScope_ != nullptr) {
        rootScope_->destroySelf();
        rootScope_ = nullptr;
    }
    // C++ 拥有的 Value 不在 values_ 中，GC 无法回收；析构时仍未移交即泄漏。
    if (cppOwnedCount_ != 0) {
        std::fprintf(stderr,
                     "MarkSweepGC leak: %zu C++-owned value(s) not adopted/freed\n",
                     cppOwnedCount_);
        std::abort();
    }
    // 收敛后仍有 Value 存活 = 内存泄漏（scope 未销毁，或 GC 标记/回收逻辑错误）。
    if (!values_.empty()) {
        // 不直接 abort()，先 fprintf 让错误信息可见，再 abort。
        std::fprintf(stderr,
                     "MarkSweepGC leak: %zu value(s) still alive after final collect\n",
                     values_.size());
        std::abort();
    }
    // 2. 归还复用的 Value 槽内存（此时不再有 Value 存活）。
    for (Value* v : freeValues_) alloc_->free(v);
    freeValues_.clear();
}

void* MarkSweepGC::rawAlloc(size_t bytes) {
    void* p = alloc_->alloc(bytes);
    if (p != nullptr) ++stats_.dataAllocations;
    return p;
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
    // 先持有一个临时引用，防止 maybeCollect 在本函数内回收 data。
    // 池化/共享后 data 可能仅被一个不可达 Value 持有（ref==1），此时 collect
    // 会析构该 Value 并把 data 一并释放，使下面的构造对已释放内存 ref++。
    if (data != nullptr) data->refInc();
    maybeCollect();

    void* p;
    if (!freeValues_.empty()) {
        // 复用上一轮回收的槽位：省一次 malloc。
        p = freeValues_.back();
        freeValues_.pop_back();
    } else {
        p = alloc_->alloc(sizeof(Value));
        if (p == nullptr) {  // OOM
            if (data != nullptr) data->refDec();
            return nullptr;
        }
    }
    Value* v = new (p) Value(type, data);  // placement new，Value 构造内 data->ref++
    if (data != nullptr) data->refDec();   // 归还临时引用
    v->markGcOwned();
    values_.push_back(v);
    ++stats_.valueAllocations;
    if (values_.size() > stats_.maxValueCount) {
        stats_.maxValueCount = values_.size();
    }
    return v;
}

Value* MarkSweepGC::allocUnownedRaw(IType* type, IData* data) {
    // C++ 拥有的 Value 不进入 values_，因此不参与 GC 标记/清除，也不触发
    // 基于 values_ 数量的 collect。内存压力仍由调用方/上层决定。
    void* p = alloc_->alloc(sizeof(Value));
    if (p == nullptr) return nullptr;
    Value* v = new (p) Value(type, data);
    ++cppOwnedCount_;
    ++stats_.cppOwnedAllocations;
    return v;
}

Value* MarkSweepGC::allocUnowned(IType* type, IData* data) {
#ifndef NDEBUG
    // 复杂 data 禁止由 native 从零构造：其子 Value 不会被 native 递归析构，
    // 会泄漏。复杂对象必须用 copyForNative 从 GC 侧复制（共享 data）。
    if (data != nullptr) {
        ChildCounter counter;
        data->enumerateChildren(&counter);
        assert(counter.count == 0 &&
               "allocUnowned: complex data must use copyForNative");
    }
#endif
    Value* v = allocUnownedRaw(type, data);
    if (v != nullptr) v->pinRefInc();  // 创建者持有 1 个 ref
    return v;
}

void MarkSweepGC::ref(Value* v) {
    if (v == nullptr || v->dead()) return;
    v->pinRefInc();
}

void MarkSweepGC::unref(Value* v) {
    if (v == nullptr || v->dead()) return;
    if (v->pinRef() <= 0) return;  // 不平衡的 unref：忽略（debug 由 verifier 报）
    v->pinRefDec();
    // native-owned 归 0：销毁；GC-owned 归 0：只移除根，等待 GC 回收。
    if (v->pinRef() == 0 && !v->gcOwned()) {
        freeUnowned(v);
    }
}

Value* MarkSweepGC::copyForNative(Value* source) {
    if (source == nullptr) return nullptr;
    assert(source->gcOwned() &&
           "copyForNative: source must be a GC-owned value");
    Value* copy = allocUnownedRaw(source->type(), source->data());
    if (copy == nullptr) return nullptr;
    copy->pinRefInc();  // 创建者持有 1 个 ref
    return copy;
}

void MarkSweepGC::freeUnowned(Value* v) {
    if (v == nullptr) return;
    assert(!v->gcOwned() && "freeUnowned called on GC-owned value");
    IData* d = v->data();
    v->~Value();
    alloc_->free(v);
    if (d->ref() == 0) {
        d->~IData();
        alloc_->free(d);
    }
    if (cppOwnedCount_ > 0) --cppOwnedCount_;
}

void MarkSweepGC::adopt(Value* v) {
    if (v == nullptr || v->dead() || v->gcOwned()) return;
    v->markGcOwned();
    values_.push_back(v);
    ++stats_.adoptedValues;
    if (values_.size() > stats_.maxValueCount) {
        stats_.maxValueCount = values_.size();
    }
    // 转移语义：消费创建者的那 1 个 ref。若调用方还想保留 native 引用，
    // 应在 adopt 前自行 ref；剩余 ref 继续作为额外 GC 根。
    if (v->pinRef() > 0) v->pinRefDec();
    if (cppOwnedCount_ > 0) --cppOwnedCount_;
}

void MarkSweepGC::collect() {
    if (values_.empty()) return;

    const auto collectStart = std::chrono::steady_clock::now();
    ++stats_.collections;

    // 作用域快照（标记前，作用域树仍完整）。
    stats_.lastScopeCount = 0;
    stats_.lastScopeValues = 0;
    stats_.lastNamedBindings = 0;
    snapshotScopes(rootScope_, stats_);
    if (stats_.lastScopeCount > stats_.maxScopeCount) {
        stats_.maxScopeCount = stats_.lastScopeCount;
    }
    if (stats_.lastScopeValues > stats_.maxScopeValues) {
        stats_.maxScopeValues = stats_.lastScopeValues;
    }

    // —— 标记阶段 ——
    // 每轮用新的单调递增 epoch，无需遍历 values_ 重置标记：
    // STW 下本轮 epoch 唯一，未活过本轮的 Value 下一轮已不在 values_ 中，
    // 不存在旧标记被误认为本轮标记的回环问题。
    const uint64_t epoch = ++markEpoch_;
    Marker marker(epoch);
    // 1. 从根作用域树（global）出发标记可达 Value
    marker.visitScope(rootScope_);
    // 2. pinRef > 0 的 Value 是额外根（调度器手工钉住，ADR-044）
    for (Value* v : values_) {
        if (v->pinRef() > 0) marker.visit(v);
    }
    // 3. 注册的根提供者（VM 操作数栈等）——这些引用不在作用域树上。
    for (IGCRootProvider* provider : rootProviders_) {
        if (provider != nullptr) provider->visitRoots(&marker);
    }

    // —— 清除阶段 ——
    // 未标记的 Value：析构（data->ref--）+ 检查 data ref 归 0 则 free(data) + free(value)
    // 用 partition 把存活 Value 移到前面，被回收的移到后面，便于一次性 erase
    size_t write = 0;
    for (size_t read = 0; read < values_.size(); ++read) {
        Value* v = values_[read];
        if (v->dead()) {
            // popScope 已析构并释放 data，这里只回收 Value 本体内存（留待复用）。
            freeValues_.push_back(v);
            ++collectedCount_;
            continue;
        }
        if (v->markEpoch() == epoch) {
            values_[write++] = v;  // 存活，保留
        } else {
            // 回收：先记 data 指针，析构 Value（data->ref--），再检查 data ref
            IData* d = v->data();
            v->~Value();  // 显式析构（placement new 的对象需显式析构）
            freeValues_.push_back(v);  // 槽位留待复用，不还给 allocator
            // data ref 归 0 则释放 data（多 Value 共享 data 时仅最后一次回收才 free）
            if (d->ref() == 0) {
                d->~IData();  // 显式析构（data 含 std::string 等需析构的成员，placement new 须显式析构）
                alloc_->free(d);
            }
            ++collectedCount_;
        }
    }
    values_.resize(write);
    // 回收后抬高阈值留出增长空间，避免「每次分配都触发全量 GC」的 O(n²) 抖动。
    // 必须无条件取 max：存活集可能恰好等于阈值（本轮只回收了个位数），
    // 若仅在 size >= threshold 时抬高，下一轮又立刻触发，退化为逐次全量 GC。
    const size_t target = values_.size() + values_.size() / 2 + 1024;
    if (target > countThreshold_) countThreshold_ = target;

    stats_.collectedValues = collectedCount_;
    const auto collectEnd = std::chrono::steady_clock::now();
    stats_.collectNanos += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(collectEnd -
                                                             collectStart)
            .count());
}

void MarkSweepGC::setRootScope(Scope* scope) {
    rootScope_ = scope;
}

void MarkSweepGC::addRootProvider(IGCRootProvider* provider) {
    if (provider == nullptr) return;
    rootProviders_.push_back(provider);
}

void MarkSweepGC::removeRootProvider(IGCRootProvider* provider) {
    for (auto it = rootProviders_.begin(); it != rootProviders_.end(); ++it) {
        if (*it == provider) {
            rootProviders_.erase(it);
            return;
        }
    }
}

void MarkSweepGC::releaseValue(Value* v) {
    if (v == nullptr || v->dead()) return;
    // 先标 dead（析构后不再触碰对象成员），再析构 Value。
    v->markDead();
    IData* d = v->data();
    v->~Value();
    if (d->ref() == 0) {
        d->~IData();
        alloc_->free(d);
    }
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
    for (Value* val : values_) {
        if (!val->dead()) v->visit(val);
    }
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
