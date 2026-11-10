// scope.cpp - Scope 析构函数实现（递归销毁子树）
//
// 析构逻辑（原 on_exit 并入析构）：
//   1. 从父作用域摘除自己（避免父持悬垂指针）
//   2. while(!children_.empty()) 逐个析构子 Scope + allocator_->free(child)
//      （子析构会从本作用域 removeChild 自己，children_ 动态收缩）
//   3. 清空自己的 values（不触碰 Value 引用计数；可达性由 GC 标记，ADR-044）
//
// 不能用 for 循环遍历 children_——子析构调本作用域 removeChild 修改 children_，
// 迭代器失效。while 取 *begin() 循环到清空。
//
// 关联 ADR: 012（Scope 树）、013（递归释放子作用域）、030 阶段 2、044（可达性标记）

#include "core/Scope.h"

#include "core/IContext.h"

namespace neo::core {

Value* Scope::lookup(std::string_view name) const {
    auto it = namedValues_.find(std::string(name));
    if (it != namedValues_.end()) return it->second;

    auto iit = indirectBindings_.find(std::string(name));
    if (iit == indirectBindings_.end()) return nullptr;
    // 间接绑定：读取时经命名空间解析，导出方后续更新/晚到导出都能看到。
    if (context_ == nullptr) return nullptr;
    return context_->getProperty(iit->second.namespaceObj, iit->second.exportKey);
}

bool Scope::getIndirectBindingChain(std::string_view name, Value*& namespaceObj,
                                    Value*& exportKey) const {
    const Scope* s = this;
    while (s != nullptr) {
        // 直接绑定优先：一旦遇到直接绑定，间接绑定被遮蔽。
        if (s->namedValues_.find(std::string(name)) != s->namedValues_.end()) {
            return false;
        }
        auto it = s->indirectBindings_.find(std::string(name));
        if (it != s->indirectBindings_.end()) {
            namespaceObj = it->second.namespaceObj;
            exportKey = it->second.exportKey;
            return true;
        }
        s = s->parent_;
    }
    return false;
}

bool Scope::copyIndirectBindingTo(Scope* dst, std::string_view name) const {
    Value* namespaceObj = nullptr;
    Value* exportKey = nullptr;
    if (!getIndirectBindingChain(name, namespaceObj, exportKey)) return false;
    return dst->defineIndirect(name, namespaceObj, exportKey);
}

Value* Scope::lookupChain(std::string_view name) const {
    const Scope* s = this;
    while (s != nullptr) {
        Value* v = s->lookup(name);
        if (v != nullptr) return v;
        s = s->parent_;
    }
    return nullptr;
}

Scope::~Scope() {
    // 1. 先从父作用域摘除自己（避免父持悬垂指针）
    if (parent_ != nullptr) {
        parent_->removeChild(this);
    }
    // 2. 递归析构子作用域：while 循环（子析构会从本作用域 removeChild，children_ 动态收缩）
    //    子 Scope 是 placement new 对象，需显式调析构 + allocator_->free 释放内存。
    while (!children_.empty()) {
        Scope* child = *children_.begin();
        child->~Scope();              // 子析构内会调本作用域 removeChild(child)
        allocator_->free(child);      // 释放子 Scope 对象内存
    }
    // 3. 清空自己的 values（Value 的回收由 GC 的可达性标记决定）
    values_.clear();
    namedValues_.clear();
    indirectBindings_.clear();
}

}  // namespace neo::core
