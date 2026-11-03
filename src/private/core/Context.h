// impl_context.h (private) - ContextImpl 实现声明
//
// ContextImpl 持有 GC 所有权（unique_ptr<GCEngine>），注入 Allocator/Console（无所有权），
// 创建并持有 global/root scope，管理 current 栈。
// 析构顺序：先销毁 global_scope（递归销毁整棵树），再销毁 GC（unique_ptr 析构），
// Allocator/Console 不销毁（外部持有）。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、012（Scope 树）、030 阶段 4

#ifndef NEO_CORE_PRIVATE_IMPL_CONTEXT_H
#define NEO_CORE_PRIVATE_IMPL_CONTEXT_H

#include "core/context.h"
#include "core/gc.h"
#include "core/scope.h"

#include <memory>
#include <vector>

namespace neo::core {

class ContextImpl : public Context {
    infra::Allocator* alloc_;              // 注入，无所有权
    std::unique_ptr<GCEngine> gc_;         // Context 持有所有权
    infra::Console* console_;              // 注入，无所有权

    Scope* global_ = nullptr;              // Context 创建，析构时销毁（递归销毁整棵树）
    Scope* root_ = nullptr;                // Context 创建，作为 global 的子，global 析构时递归销毁
    std::vector<Scope*> scope_stack_;      // current 栈（global 不入栈，root 是栈底）

public:
    ContextImpl(infra::Allocator* alloc, infra::Console* console);
    ~ContextImpl() override;

    infra::Allocator* allocator() const override { return alloc_; }
    GCEngine* gc() const override { return gc_.get(); }
    infra::Console* console() const override { return console_; }

    Scope* global() const override { return global_; }
    Scope* root() const override { return root_; }
    Scope* current() const override;

    void push_scope(Scope* s) override;
    void pop_scope() override;
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_IMPL_CONTEXT_H
