// context.cpp - Context 实现
//
// 构造：用 IAllocator 创建 GC（createGcEngine(alloc)，Context 持有所有权），
// placement new 创建 global scope（parent=nullptr）和 root scope（parent=global），
// global->addChild(root)，root 入 current 栈。
// 析构：先销毁 global scope（~Scope 递归销毁整棵树：栈中残留 scope → root → global），
// 再销毁 GC（unique_ptr 析构，GC 析构时 collect 回收残留 Value，泄漏则 panic），
// IAllocator/Console 不销毁（外部持有）。
//
// pushScope: s 压入 current 栈，current=s。
// popScope: 弹出栈顶（非 root），~Scope + alloc->free 销毁。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、012（Scope 树）、030 阶段 4

#include "core/Context.h"

#include <new>

namespace neo::core {

Context::Context(infra::IAllocator* alloc, infra::Console* console)
    : alloc_(alloc), gc_(createGcEngine(alloc)), console_(console) {
    // 1. 创建 global scope（根，parent=nullptr）
    void* gp = alloc_->alloc(sizeof(Scope));
    global_ = new (gp) Scope(nullptr, alloc_);
    // 2. 创建 root scope（parent=global），作为 global 的子
    void* rp = alloc_->alloc(sizeof(Scope));
    root_ = new (rp) Scope(global_, alloc_);
    global_->addChild(root_);
    // 3. root 入 current 栈（栈底）
    scopeStack_.push_back(root_);
}

Context::~Context() {
    // 1. 销毁 global scope：~Scope 递归销毁整棵树
    //    （栈中残留的 current scope → root → global 逐层析构 + free）
    //    scopeRef 归 0 的 Value 在 GC 下次 collect 时被回收。
    // 注：scopeStack_ 中的指针在 global_ 析构后全部悬垂，但栈本身只是 vector<Scope*>，
    //     析构 vector 不访问指针内容，安全。成员析构顺序：scopeStack_ → gc_ → ...
    if (global_ != nullptr) {
        global_->~Scope();
        alloc_->free(global_);
    }
    // 2. 销毁 GC（unique_ptr 析构）：GC 析构调 collect 回收残留 Value，
    //    若有 Value 存活（scope 未正确销毁）则 panic 报告泄漏。
    //    gc_ 在 global_ 之后销毁——先释放 scope（scopeRef 归 0），再 collect 回收 Value。
    gc_.reset();
    // 3. alloc_/console_ 由外部销毁，Context 不持有所有权
}

Scope* Context::current() const {
    if (scopeStack_.empty()) return nullptr;
    return scopeStack_.back();
}

void Context::pushScope(Scope* s) {
    scopeStack_.push_back(s);
}

void Context::popScope() {
    // 栈底 root 不可 pop（global 不在栈中，root 是最低保护层）
    if (scopeStack_.size() <= 1) return;  // 只剩 root（或空），不 pop
    Scope* top = scopeStack_.back();
    scopeStack_.pop_back();
    // 销毁弹出的 scope：~Scope 递归释放子树 + 从父摘除自己
    top->~Scope();
    alloc_->free(top);
}

}  // namespace neo::core

std::unique_ptr<neo::core::IContext> neo::core::createContext(
    infra::IAllocator* alloc, infra::Console* console) {
    return std::make_unique<Context>(alloc, console);
}
