// IGCEngine.h - GC 引擎接口（标记-清除，扁平 Value 阶段）
//
// IGCEngine 是 GC 抽象接口（ADR-008/029）：createValue 创建 Value（走 IAllocator +
// placement new），allocData 分配 Data 内存，collect 标记-清除回收不可达 Value。
// 自动 GC：createValue 时双重阈值（内存 used/limit + Value 数量）超限先 collect。
// 返回 nullptr 表示 OOM（IAllocator 配额耗尽且 GC 后仍不够），Context 封装为 OOM Exception。
//
// 具体实现 MarkSweepGC 在 private，通过工厂 createGcEngine(IAllocator*) 获取。
//
// 关联 ADR: 008（IAllocator/GC 分层）、010（接口-实现-工厂）、012（Scope 树）、
//          015（data 引用计数分层）、029（生命周期）、044（从作用域树可达性标记）

#ifndef NEO_CORE_IGCENGINE_H
#define NEO_CORE_IGCENGINE_H

#include "core/IType.h"
#include "infra/IAllocator.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace neo::core {

// 前向声明
class IData;
class IType;
class Scope;
class Value;

// GC 标记访问器：collect 标记阶段由 IGCEngine 实现，对每个存活 Value 调 visit。
// data->enumerateChildren(visitor) 沿 data 引用链递归标记子 value。
// visitScope 供 FunctionData 主动标记自己的孤立闭包作用域（ADR-044）：
// 闭包作用域不在 global 作用域树上，只能由持有它的函数对象在标记时递归进来。
class IGCVisitor {
public:
    virtual ~IGCVisitor() = default;
    // 标记 v 存活，并递归标记其 data 持有的子 value
    virtual void visit(Value* v) = 0;
    // 标记 scope 中的 values 及其 children 子树可达（供函数对象标记闭包作用域）
    virtual void visitScope(Scope* scope) = 0;
};

// GC 根提供者：GC 图之外的活跃根（如 VM 操作数栈）在 collect 标记阶段
// 主动把自己的引用交给 visitor。注册方须保证生命周期覆盖注册期。
class IGCRootProvider {
public:
    virtual ~IGCRootProvider() = default;
    virtual void visitRoots(IGCVisitor* visitor) = 0;
};

// TypeKind 的枚举项数（不含哨兵；新增类型时需同步 WeakRef 之后的末尾项）。
inline constexpr std::size_t kTypeKindCount =
    static_cast<std::size_t>(TypeKind::WeakRef) + 1;

// 诊断统计（P0 GC 重构评估用，只读）。计数器常开，单次分配仅一次自增。
struct GCStats {
    uint64_t valueAllocations = 0;   // createValue 成功次数
    uint64_t dataAllocations = 0;    // allocData 成功次数
    uint64_t collections = 0;        // collect 次数
    uint64_t collectedValues = 0;    // 累计回收 Value 数
    uint64_t collectNanos = 0;       // collect 累计耗时
    uint64_t maxValueCount = 0;      // values_ 峰值（含两次 collect 之间）
    uint64_t cppOwnedAllocations = 0;  // allocUnowned 次数
    uint64_t adoptedValues = 0;        // C++ -> GC 所有权转移次数

    // 最近一次 collect 时作用域树快照（values_ 中命名绑定占比的近似）。
    uint64_t lastScopeCount = 0;
    uint64_t lastScopeValues = 0;
    uint64_t lastNamedBindings = 0;
    // 历次 collect 的峰值。
    uint64_t maxScopeCount = 0;
    uint64_t maxScopeValues = 0;
};

// GC 引擎抽象接口
class IGCEngine {
public:
    virtual ~IGCEngine() = default;

    // 诊断统计（只读）。生命周期与引擎一致。
    virtual const GCStats& stats() const = 0;

    // 创建 Value：alloc(sizeof(Value)) + placement new + 登记到 values_ 表。
    // 自动 GC：双重阈值超限先 collect 再分配（见 §6）。
    // 返回 nullptr 表示 OOM（Context 封装为 OOM Exception）。
    // 注：data 由调用方通过 allocData<T> 预先分配，createValue 只接管指针。
    virtual Value* createValue(IType* type, IData* data) = 0;

    // —— 显式所有权（P1）——
    // allocUnowned：分配 Value 但不登记进 values_，GC 不可见；所有权归 C++，
    //   调用方必须用 freeUnowned 或 adopt 移交。失败返回 nullptr。
    // freeUnowned：析构 C++ 拥有的 Value（data ref--，归 0 则释放 data）并归还内存。
    //   对已 adopt 的 Value 调用是错误（断言）。
    // adopt：把 C++ 拥有的 Value 转移给 GC（登记进 values_，标记 gcOwned）。
    //   幂等：已 gcOwned 时无操作。
    virtual Value* allocUnowned(IType* type, IData* data) = 0;
    virtual void freeUnowned(Value* v) = 0;
    virtual void adopt(Value* v) = 0;
    // 当前仍由 C++ 拥有、尚未 adopt/free 的 Value 数（诊断/泄漏检测）。
    virtual size_t cppOwnedCount() const = 0;

    // —— 统一 native 引用计数（P2）——
    // ref/unref 对两种所有权都有效：
    //   - GC-owned：pinRef > 0 即额外 GC 根；unref 归 0 只移除根，GC 后续回收。
    //   - native-owned：pinRef 即生命周期计数；unref 归 0 由引擎销毁。
    // 供 C++ 多位置共享/传递 Value，无需外层 shared_ptr。
    virtual void ref(Value* v) = 0;
    virtual void unref(Value* v) = 0;
    // 从 GC 侧复制一个 native-owned 副本：共享 source 的 data（data ref++），
    // 返回时 ref=1。按 B 契约，调用方还需 ref(source) 锚住源 Value，
    // 否则源不可达后 GC 不再标记该 data 的子图。
    virtual Value* copyForNative(Value* source) = 0;

    // 分配 Data 内存：走 IAllocator + placement new。调用方用
    //   auto* d = gc->allocData<NumberData>(42);
    // 拿到 IData*，再 createValue(type, d)。data 的释放由 GC sweep 负责
    // （ref 归 0 时 allocator->free），调用方不释放。
    // 返回 nullptr 表示 OOM。模板非虚，内部调 rawAlloc 虚函数拿裸内存。
    template <typename T, typename... Args>
    T* allocData(Args&&... args) {
        void* p = rawAlloc(sizeof(T));
        if (p == nullptr) return nullptr;
        return new (p) T(std::forward<Args>(args)...);
    }

    // 显式触发 GC（测试/手动控制用）
    virtual void collect() = 0;

    // 设置 GC 的根作用域树（ADR-044）：collect 从该 scope 出发，沿
    // 「scope.values + scope.children」与「data 子引用」标记可达 Value。
    // Context 在 bootstrap 后设置 global scope，并保持其生命周期长于 GC。
    virtual void setRootScope(Scope* scope) = 0;

    // 注册/注销额外的根提供者（VM 操作数栈等）。非拥有，调用方负责注销。
    virtual void addRootProvider(IGCRootProvider* provider) = 0;
    virtual void removeRootProvider(IGCRootProvider* provider) = 0;

    // 回收已从 Value 摘除的 data（赋值 replaceMeta 后调用）：
    // ref 归 0 则析构 + free，否则无操作（仍被其他 Value 共享）。
    // GC 的 collect 只遍历存活 Value 当前持有的 data，摘除后的游离 data 需显式回收。
    virtual void releaseData(IData* d) = 0;

    // 提前释放一个确定死亡的 Value（ADR-065 §3，仅 popScope 调用）：
    // 析构 Value（data ref--，归 0 则析构+free data）并标 dead；
    // Value 本体内存由下一次 collect 的 sweep 释放（deferred free）。
    // 调用方须保证该 Value 不再被任何作用域/data/VM 栈引用。
    virtual void releaseValue(Value* v) = 0;

    // —— 装饰性查询接口 ——
    virtual size_t valueCount() const = 0;       // 当前存活的 Value 总数
    virtual size_t collectedCount() const = 0;   // 累计已回收的 Value 数（统计用）
    virtual void   enumerateValues(IGCVisitor* v) const = 0;  // 遍历所有存活 Value

    // —— GC 触发阈值配置 ——
    virtual void setMemThreshold(double ratio) = 0;    // 内存阈值（默认 0.8）
    virtual void setCountThreshold(size_t count) = 0;  // Value 数量阈值（默认 10000）

protected:
    // 分配 sizeof(T) 字节裸内存（实现类提供，走 IAllocator）
    virtual void* rawAlloc(size_t bytes) = 0;
};

// 工厂：创建 MarkSweepGC，注入 IAllocator（ADR-008 GC→IAllocator 依赖方向）
std::unique_ptr<IGCEngine> createGcEngine(infra::IAllocator* alloc);

}  // namespace neo::core

#endif  // NEO_CORE_IGCENGINE_H
