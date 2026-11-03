// context.h - Context 门面（引擎入口，整合 GC/Allocator/Console + Scope 树）
//
// Context 是引擎对外门面（ADR-011 §2/§3）：持有 GC 所有权（内部创建并销毁），
// 注入 Allocator 和 Console（无所有权，由外部销毁），持有 global/root/current
// 三层 scope（ADR-012 §1）。
//
// GC 由 Context 持有所有权：Context 用注入的 Allocator 创建 GC（create_gc_engine(alloc)），
// 析构时由 Context 销毁 GC。Allocator 和 Console 是注入资源，Context 不持有所有权。
//
// 三层 scope（ADR-012 + ADR-030 阶段 4）：
//   global —— 根作用域（parent=nullptr，永久，Context 存活期间一直在）
//   root   —— 模块根作用域（parent=global，Context 存活期间一直在）
//   current —— 动态栈顶（push_scope 压入，pop_scope 弹出并销毁），初始=root
//
// 析构顺序：先销毁 global_scope（~Scope 递归销毁整棵 scope 树：current 栈残留 → root → global），
// 再销毁 GC（GC 析构时 collect 回收残留 Value，若有泄漏则 panic），
// Allocator/Console 由外部销毁（Context 不持有所有权）。
//
// 接口-实现-工厂（ADR-010）：Context 抽象接口在 public/core/，ContextImpl 在 private/core/，
// 工厂 create_context(alloc, console) 返回接口指针。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面 + 内存归属）、012（Scope 树）、030 阶段 4

#ifndef NEO_CORE_CONTEXT_H
#define NEO_CORE_CONTEXT_H

#include "infra/allocator.h"
#include "infra/console.h"

#include <memory>

namespace neo::core {

// 前向声明
class GCEngine;
class Scope;
class Value;

// 引擎门面：持有 GC 所有权 + 注入 Allocator/Console + Scope 树
class Context {
public:
    virtual ~Context() = default;

    // —— 资源访问 ——
    // allocator/console: 注入资源（无所有权，由外部销毁）
    // gc: Context 持有所有权（内部创建并销毁）
    virtual infra::Allocator* allocator() const = 0;
    virtual GCEngine* gc() const = 0;
    virtual infra::Console* console() const = 0;

    // —— 三层 scope 访问 ——
    virtual Scope* global() const = 0;
    virtual Scope* root() const = 0;
    virtual Scope* current() const = 0;

    // —— current 栈管理 ——
    // push_scope: 将 s 压入 current 栈，current=s。
    // s 的 parent 应与当前 current 一致（由调用方保证）。
    // s 的内存由 Allocator 分配（placement new），pop_scope 时销毁。
    virtual void push_scope(Scope* s) = 0;

    // pop_scope: 弹出 current 栈顶 scope 并销毁（~Scope 递归释放子树 + allocator->free）。
    // global/root 不可 pop（栈底保护）。栈中只剩 root 时 pop 无操作（或断言失败）。
    virtual void pop_scope() = 0;
};

// 工厂：创建 Context，注入 Allocator/Console（无所有权，外部销毁）。
// Context 内部用 Allocator 创建 GC（持有所有权）+ global/root scope。
std::unique_ptr<Context> create_context(infra::Allocator* alloc,
                                        infra::Console* console);

}  // namespace neo::core

#endif  // NEO_CORE_CONTEXT_H
