// Scope.h - 作用域（Value 跟踪容器 + 作用域树，堆对象）
//
// Scope 维护其跟踪的 Value 列表（ADR-012 §3）：add 时 scopeRef++，析构时逐个
// scopeRef--。GC 以 scopeRef != 0 为存活根，scope 析构后 scopeRef 归 0 的 Value
// 在下次 collect 时被回收。
//
// 作用域树（ADR-012 §1 + ADR-030 阶段 2）：Scope 持 parent_ 指针与 children_ 集合。
// **Scope 是堆对象**——内存经 IAllocator + placement new 分配，生命周期由父持有
// （父的 children_ 持子 Scope 指针）。析构函数负责递归销毁整个子树：
//   1. 从父作用域摘除自己（避免父持悬垂指针）
//   2. while(!children_.empty()) 逐个析构子 Scope（子析构会从本作用域摘除自己）
//      + allocator_->free(child) 释放子 Scope 对象内存
//   3. 释放自己的 values（scopeRefDec）
//
// Scope 借用 IAllocator*（由 Context 传入，Context 持有 IAllocator 所有权）。
// 析构子 Scope 时需 allocator_->free(child) 释放子对象内存——子析构只调 ~Scope()
// 不释放内存（placement new 对象需显式 free）。
//
// lookupChain 沿 parent 链向上查找命名 value（ADR-013 查找路径基础版）。
// namedValues_ 是名字→Value 映射（借用，不拥有，scopeRef 由 add 负责）。
//
// 关联 ADR: 012（Scope 树 + scopeRef）、013（闭包三层作用域）、029（生命周期）、030 阶段 2

#ifndef NEO_CORE_SCOPE_H
#define NEO_CORE_SCOPE_H

#include "core/Value.h"
#include "infra/IAllocator.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace neo::core {

class Scope {
    Scope* parent_ = nullptr;                              // 父作用域（global 的 parent=nullptr）
    infra::IAllocator* allocator_ = nullptr;                // 借用，用于释放子 Scope 内存
    std::unordered_set<Scope*> children_;                  // 子作用域（无序、去重，析构时递归释放）
    std::vector<Value*> values_;                           // 跟踪的 value（含匿名临时值）
    std::unordered_map<std::string, Value*> namedValues_; // 命名绑定（借用）
    std::unordered_set<std::string> flags_;                // 作用域标注（如 "strict"）

public:
    // 构造：parent 可为 nullptr（global scope），allocator 借用（用于析构子时 free 内存）。
    // global scope 的 parent_=nullptr，allocator 由 Context 传入。
    // 子 scope 的 parent 指向父，allocator 从父继承（或 Context 统一传入）。
    explicit Scope(Scope* parent, infra::IAllocator* alloc)
        : parent_(parent), allocator_(alloc) {}

    // 析构：递归销毁整个子树（见文件头注释）。
    // 调用方负责释放本 Scope 对象自身的内存（placement new 对象需显式
    // allocator->free(this)——但通常由父析构或 Context 负责）。
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

    // —— 作用域树 ——
    Scope* parent() const { return parent_; }
    infra::IAllocator* allocator() const { return allocator_; }

    // 添加子作用域（本作用域成为 child 的 parent）。
    // child 的 parent 应与本作用域一致（由调用方保证，addChild 不改 child->parent_）。
    // children_ 是 unordered_set，重复 add 同一 child 无副作用（去重）。
    void addChild(Scope* child) { children_.insert(child); }

    // 从本作用域的 children_ 中移除 child（子作用域析构时调此方法摘除自己）。
    // 找不到则无操作（幂等）。unordered_set 的 erase O(1)。
    void removeChild(Scope* child) { children_.erase(child); }

    const std::unordered_set<Scope*>& children() const { return children_; }
    size_t childCount() const { return children_.size(); }

    // —— Value 跟踪 ——
    // 跟踪一个 Value（scopeRef++）。同一 Value 多次 add 会多次 scopeRefInc。
    void add(Value* v) {
        values_.push_back(v);
        v->scopeRefInc();
    }

    // 取消跟踪一个 Value（scopeRef--）。用于动态根（如事件循环任务实体）：
    // 出队时解除根引用，使其可被 GC 回收。
    // 只移除 values_ 中的一次出现（同一 Value 被 add 多次需对应 remove 多次）。
    // 找不到返回 false。
    bool remove(Value* v) {
        for (auto it = values_.begin(); it != values_.end(); ++it) {
            if (*it == v) {
                values_.erase(it);
                v->scopeRefDec();
                return true;
            }
        }
        return false;
    }

    // 绑定名字→Value（借用，不影响 scopeRef；Value 需先 add 跟踪）。
    // 重名绑定覆盖旧条目（不递减旧 Value 的 scopeRef，scopeRef 由 add/析构管理）。
    void bind(std::string_view name, Value* v) {
        namedValues_[std::string(name)] = v;
    }

    // 定义变量（ADR-030 阶段 5）：仅在当前 scope 绑定 name→v + add 跟踪。
    // 重复定义返回 false（调用方报错），成功返回 true。
    // 重复检查无条件生效——var/let/const 的差异在 Sema 阶段已抹平，与 strict 无关。
    // 与 bind 的区别：bind 覆盖重名且不 add 跟踪；define 重复检查 + add 跟踪。
    bool define(std::string_view name, Value* v) {
        if (lookup(name) != nullptr) return false;
        add(v);
        namedValues_[std::string(name)] = v;
        return true;
    }

    // 按名字查找 Value（找不到返回 nullptr）
    Value* lookup(std::string_view name) const {
        auto it = namedValues_.find(std::string(name));
        return it == namedValues_.end() ? nullptr : it->second;
    }

    // 沿 parent 链向上查找命名 value（ADR-013 查找路径基础版）。
    // 本作用域找不到则查 parent，递归到 global（parent=nullptr 时返回 nullptr）。
    Value* lookupChain(std::string_view name) const {
        const Scope* s = this;
        while (s != nullptr) {
            Value* v = s->lookup(name);
            if (v != nullptr) return v;
            s = s->parent_;
        }
        return nullptr;
    }

    // —— 作用域标注（ADR-030 阶段 5）——
    // flags 是作用域级标注（如 "strict"），语法上只能标注在模块作用域/函数体上。
    // 查找沿 parent 链向上继承（内层 scope 自动继承外层标注）。
    void setFlag(std::string_view name) { flags_.insert(std::string(name)); }
    void clearFlag(std::string_view name) { flags_.erase(std::string(name)); }

    // 本作用域是否标注了 name（不查 parent）
    bool hasFlag(std::string_view name) const {
        return flags_.find(std::string(name)) != flags_.end();
    }

    // 沿 parent 链向上查找标注（本作用域找不到则查 parent，递归到 global）
    bool lookupFlagChain(std::string_view name) const {
        const Scope* s = this;
        while (s != nullptr) {
            if (s->hasFlag(name)) return true;
            s = s->parent_;
        }
        return false;
    }

    // —— 装饰性查询 ——
    size_t valueCount() const { return values_.size(); }
    size_t namedCount() const { return namedValues_.size(); }
    size_t flagCount() const { return flags_.size(); }

    // 遍历跟踪的 Value（调试/测试用）
    template <typename Fn>
    void forEachValue(Fn&& fn) const {
        for (Value* v : values_) fn(v);
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_SCOPE_H
