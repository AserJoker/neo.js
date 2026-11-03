// scope.cpp - Scope 析构函数实现（递归销毁子树）
//
// 析构逻辑（原 on_exit 并入析构）：
//   1. 从父作用域摘除自己（避免父持悬垂指针）
//   2. while(!children_.empty()) 逐个析构子 Scope + allocator_->free(child)
//      （子析构会从本作用域 remove_child 自己，children_ 动态收缩）
//   3. 释放自己的 values（scope_ref_dec）
//
// 不能用 for 循环遍历 children_——子析构调本作用域 remove_child 修改 children_，
// 迭代器失效。while 取 *begin() 循环到清空。
//
// 关联 ADR: 012（Scope 树）、013（递归释放子作用域）、030 阶段 2

#include "core/Scope.h"

namespace neo::core {

Scope::~Scope() {
    // 1. 先从父作用域摘除自己（避免父持悬垂指针）
    if (parent_ != nullptr) {
        parent_->remove_child(this);
    }
    // 2. 递归析构子作用域：while 循环（子析构会从本作用域 remove_child，children_ 动态收缩）
    //    子 Scope 是 placement new 对象，需显式调析构 + allocator_->free 释放内存。
    while (!children_.empty()) {
        Scope* child = *children_.begin();
        child->~Scope();              // 子析构内会调本作用域 remove_child(child)
        allocator_->free(child);      // 释放子 Scope 对象内存
    }
    // 3. 释放自己的 values
    for (Value* v : values_) v->scope_ref_dec();
    values_.clear();
    named_values_.clear();
}

}  // namespace neo::core
