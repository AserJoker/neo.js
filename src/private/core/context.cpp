// context.cpp - ContextImpl 实现
//
// 构造：用 Allocator placement new 创建 global scope（parent=nullptr）和 root scope
// （parent=global），global->add_child(root)，root 入 current 栈。
// 析构：销毁 global scope（~Scope 递归销毁整棵树：栈中残留 scope → root → global），
// 然后 gc/alloc/console 由外部销毁（Context 不持有所有权）。
//
// push_scope: s 压入 current 栈，current=s。
// pop_scope: 弹出栈顶（非 root），~Scope + alloc->free 销毁。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、012（Scope 树）、030 阶段 4

#include "core/impl_context.h"

#include "core/gc.h"

#include <new>

namespace neo::core {

ContextImpl::ContextImpl(infra::Allocator* alloc, GCEngine* gc, infra::Console* console)
    : alloc_(alloc), gc_(gc), console_(console) {
    // 1. 创建 global scope（根，parent=nullptr）
    void* gp = alloc_->alloc(sizeof(Scope));
    global_ = new (gp) Scope(nullptr, alloc_);
    // 2. 创建 root scope（parent=global），作为 global 的子
    void* rp = alloc_->alloc(sizeof(Scope));
    root_ = new (rp) Scope(global_, alloc_);
    global_->add_child(root_);
    // 3. root 入 current 栈（栈底）
    scope_stack_.push_back(root_);
}

ContextImpl::~ContextImpl() {
    // 销毁 global scope：~Scope 递归销毁整棵树
    //   （栈中残留的 current scope → root → global 逐层析构 + free）
    // 注：scope_stack_ 中的指针在 global_ 析构后全部悬垂，但栈本身只是 vector<Scope*>，
    //     析构 vector 不访问指针内容，安全。
    if (global_ != nullptr) {
        global_->~Scope();
        alloc_->free(global_);
    }
    // gc_/alloc_/console_ 由外部销毁，Context 不持有所有权
}

Scope* ContextImpl::current() const {
    if (scope_stack_.empty()) return nullptr;
    return scope_stack_.back();
}

void ContextImpl::push_scope(Scope* s) {
    scope_stack_.push_back(s);
}

void ContextImpl::pop_scope() {
    // 栈底 root 不可 pop（global 不在栈中，root 是最低保护层）
    if (scope_stack_.size() <= 1) return;  // 只剩 root（或空），不 pop
    Scope* top = scope_stack_.back();
    scope_stack_.pop_back();
    // 销毁弹出的 scope：~Scope 递归释放子树 + 从父摘除自己
    top->~Scope();
    alloc_->free(top);
}

}  // namespace neo::core

std::unique_ptr<neo::core::Context> neo::core::create_context(
    infra::Allocator* alloc, GCEngine* gc, infra::Console* console) {
    return std::make_unique<ContextImpl>(alloc, gc, console);
}
