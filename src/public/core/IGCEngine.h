// IGCEngine.h - GC 引擎接口（标记-清除，扁平 Value 阶段）
//
// IGCEngine 是 GC 抽象接口（ADR-008/029）：createValue 创建 Value（走 IAllocator +
// placement new），allocData 分配 Data 内存，collect 标记-清除回收不可达 Value。
// 自动 GC：createValue 时双重阈值（内存 used/limit + Value 数量）超限先 collect。
// 返回 nullptr 表示 OOM（IAllocator 配额耗尽且 GC 后仍不够），Context 封装为 OOM Exception。
//
// 具体实现 MarkSweepGC 在 private，通过工厂 createGcEngine(IAllocator*) 获取。
//
// 关联 ADR: 008（IAllocator/GC 分层）、010（接口-实现-工厂）、012（scopeRef 根）、
//          015（data 引用计数分层）、029（生命周期）

#ifndef NEO_CORE_IGCENGINE_H
#define NEO_CORE_IGCENGINE_H

#include "infra/IAllocator.h"

#include <cstddef>
#include <memory>
#include <new>

namespace neo::core {

// 前向声明
class IData;
class IType;
class Value;

// GC 标记访问器：collect 标记阶段由 IGCEngine 实现，对每个存活 Value 调 visit。
// 复杂对象阶段 data->enumerateChildren(visitor) 沿引用链递归标记子 value；
// 本阶段 data 扁平，enumerateChildren 默认空，仅 scopeRef!=0 的根被标记。
class IGCVisitor {
public:
    virtual ~IGCVisitor() = default;
    // 标记 v 存活，并递归标记其 data 持有的子 value（本阶段递归 noop）
    virtual void visit(Value* v) = 0;
};

// GC 引擎抽象接口
class IGCEngine {
public:
    virtual ~IGCEngine() = default;

    // 创建 Value：alloc(sizeof(Value)) + placement new + 登记到 values_ 表。
    // 自动 GC：双重阈值超限先 collect 再分配（见 §6）。
    // 返回 nullptr 表示 OOM（Context 封装为 OOM Exception）。
    // 注：data 由调用方通过 allocData<T> 预先分配，createValue 只接管指针。
    virtual Value* createValue(IType* type, IData* data) = 0;

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
