// IContext.h - Context 门面（引擎入口，整合 GC/IAllocator/Console + Scope 树）
//
// IContext 是引擎对外门面（ADR-011 §2/§3）：持有 GC 所有权（内部创建并销毁），
// 注入 IAllocator 和 Console（无所有权，由外部销毁），持有 global/root/current
// 三层 scope（ADR-012 §1）。
//
// GC 由 Context 持有所有权：Context 用注入的 IAllocator 创建 GC（createGcEngine(alloc)），
// 析构时由 Context 销毁 GC。IAllocator 和 Console 是注入资源，Context 不持有所有权。
//
// 三层 scope（ADR-012 + ADR-030 阶段 4）：
//   global —— 根作用域（parent=nullptr，永久，Context 存活期间一直在）
//   root   —— 模块根作用域（parent=global，Context 存活期间一直在）
//   current —— 动态栈顶（pushScope 压入，popScope 弹出并销毁），初始=root
//
// 析构顺序：先销毁 global_scope（~Scope 递归销毁整棵 scope 树：current 栈残留 → root → global），
// 再销毁 GC（GC 析构时 collect 回收残留 Value，若有泄漏则 panic），
// IAllocator/Console 由外部销毁（Context 不持有所有权）。
//
// 接口-实现-工厂（ADR-010）：IContext 抽象接口在 public/core/，Context 在 private/core/，
// 工厂 createContext(alloc, console) 返回接口指针。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面 + 内存归属）、012（Scope 树）、030 阶段 4

#ifndef NEO_CORE_ICONTEXT_H
#define NEO_CORE_ICONTEXT_H

#include "infra/IAllocator.h"
#include "infra/Console.h"

#include <memory>
#include <string_view>

namespace neo::core {

// 前向声明
class IGCEngine;
class Scope;
class Value;

// 引擎门面：持有 GC 所有权 + 注入 IAllocator/Console + Scope 树
class IContext {
public:
    virtual ~IContext() = default;

    // —— 资源访问 ——
    // allocator/console: 注入资源（无所有权，由外部销毁）
    // gc: Context 持有所有权（内部创建并销毁）
    virtual infra::IAllocator* allocator() const = 0;
    virtual IGCEngine* gc() const = 0;
    virtual infra::Console* console() const = 0;

    // —— 三层 scope 访问 ——
    virtual Scope* global() const = 0;
    virtual Scope* root() const = 0;
    virtual Scope* current() const = 0;

    // —— current 栈管理 ——
    // pushScope: 将 s 压入 current 栈，current=s。
    // s 的 parent 应与当前 current 一致（由调用方保证）。
    // s 的内存由 IAllocator 分配（placement new），popScope 时销毁。
    virtual void pushScope(Scope* s) = 0;

    // popScope: 弹出 current 栈顶 scope 并销毁（~Scope 递归释放子树 + allocator->free）。
    // global/root 不可 pop（栈底保护）。栈中只剩 root 时 pop 无操作（或断言失败）。
    virtual void popScope() = 0;

    // —— Value 创建便捷接口（ADR-030 阶段 4）——
    // 内部封装 gc->allocData<T> + gc->createValue(type, data)，把 Value 加入 current scope
    // 跟踪（scopeRef++）。可选 name：指定则同时 bind 到 current scope（命名绑定）。
    // 返回 Value*（正常结果）。OOM 处理留待阶段 5（OOM Fatal 封装）。
    virtual Value* createUndefined(std::string_view name = {}) = 0;
    virtual Value* createNull(std::string_view name = {}) = 0;
    virtual Value* createBoolean(bool b, std::string_view name = {}) = 0;
    virtual Value* createNumber(double n, std::string_view name = {}) = 0;
    virtual Value* createString(std::string_view s, std::string_view name = {}) = 0;
};

// 工厂：创建 Context，注入 IAllocator/Console（无所有权，外部销毁）。
// Context 内部用 IAllocator 创建 GC（持有所有权）+ global/root scope。
std::unique_ptr<IContext> createContext(infra::IAllocator* alloc,
                                         infra::Console* console);

}  // namespace neo::core

#endif  // NEO_CORE_ICONTEXT_H
