// impl_context.h (private) - ContextImpl 实现声明
//
// ContextImpl 持有注入的 gc/alloc/console（无所有权），创建并持有 global/root scope，
// 管理 current 栈。析构时先销毁 global_scope（递归销毁整棵树），gc/alloc/console 不销毁。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、012（Scope 树）、030 阶段 4

#ifndef NEO_CORE_PRIVATE_IMPL_CONTEXT_H
#define NEO_CORE_PRIVATE_IMPL_CONTEXT_H

#include "core/context.h"
#include "core/scope.h"

#include <vector>

namespace neo::core {

class ContextImpl : public Context {
    infra::Allocator* alloc_;     // 注入，无所有权
    GCEngine* gc_;               // 注入，无所有权
    infra::Console* console_;     // 注入，无所有权

    Scope* global_ = nullptr;    // Context 创建，析构时销毁（递归销毁整棵树）
    Scope* root_ = nullptr;      // Context 创建，作为 global 的子，global 析构时递归销毁
    std::vector<Scope*> scope_stack_;  // current 栈（global 不入栈，root 是栈底）

public:
    ContextImpl(infra::Allocator* alloc, GCEngine* gc, infra::Console* console);
    ~ContextImpl() override;

    infra::Allocator* allocator() const override { return alloc_; }
    GCEngine* gc() const override { return gc_; }
    infra::Console* console() const override { return console_; }

    Scope* global() const override { return global_; }
    Scope* root() const override { return root_; }
    Scope* current() const override;

    void push_scope(Scope* s) override;
    void pop_scope() override;
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_IMPL_CONTEXT_H
