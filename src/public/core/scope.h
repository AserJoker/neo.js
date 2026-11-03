// scope.h - 作用域（Value 跟踪容器 + 作用域树）
//
// Scope 维护其跟踪的 Value 列表（ADR-012 §3）：add 时 scope_ref++，on_exit 逐个
// scope_ref--。GC 以 scope_ref != 0 为存活根，scope 退出后 scope_ref 归 0 的 Value
// 在下次 collect 时被回收。
//
// 作用域树（ADR-012 §1 + ADR-030 阶段 2）：Scope 持 parent_ 指针与 children_ 列表。
// on_exit 递归释放所有 children（先递归子作用域 on_exit，再释放自己的 values），
// 对应 ADR-013「作用域析构时递归释放子作用域」。
// lookup_chain 沿 parent 链向上查找命名 value（ADR-013 查找路径基础版）。
//
// named_values_ 是名字→Value 映射（借用，不拥有，scope_ref 由 add 负责）。
//
// 关联 ADR: 012（Scope 树 + scope_ref）、013（闭包三层作用域）、029（生命周期）

#ifndef NEO_CORE_SCOPE_H
#define NEO_CORE_SCOPE_H

#include "core/value.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace neo::core {

class Scope {
    Scope* parent_ = nullptr;                              // 父作用域（global 的 parent=nullptr）
    std::unordered_set<Scope*> children_;                  // 子作用域（无序、去重，on_exit 时递归释放）
    std::vector<Value*> values_;                           // 跟踪的 value（含匿名临时值）
    std::unordered_map<std::string, Value*> named_values_; // 命名绑定（借用）

public:
    Scope() = default;
    explicit Scope(Scope* parent) : parent_(parent) {}
    ~Scope() = default;

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = default;
    Scope& operator=(Scope&&) = default;

    // —— 作用域树 ——
    Scope* parent() const { return parent_; }

    // 添加子作用域（本作用域成为 child 的 parent）。
    // child 的 parent 应与本作用域一致（由调用方保证，add_child 不改 child->parent_）。
    // children_ 是 unordered_set，重复 add 同一 child 无副作用（去重）。
    void add_child(Scope* child) { children_.insert(child); }

    // 从本作用域的 children_ 中移除 child（子作用域 on_exit 时调此方法摘除自己）。
    // 找不到则无操作（幂等）。unordered_set 的 erase O(1)。
    void remove_child(Scope* child) { children_.erase(child); }

    const std::unordered_set<Scope*>& children() const { return children_; }
    size_t child_count() const { return children_.size(); }

    // —— Value 跟踪 ——
    // 跟踪一个 Value（scope_ref++）。同一 Value 多次 add 会多次 scope_ref_inc。
    void add(Value* v) {
        values_.push_back(v);
        v->scope_ref_inc();
    }

    // 绑定名字→Value（借用，不影响 scope_ref；Value 需先 add 跟踪）。
    // 重名绑定覆盖旧条目（不递减旧 Value 的 scope_ref，scope_ref 由 add/on_exit 管理）。
    void bind(std::string_view name, Value* v) {
        named_values_[std::string(name)] = v;
    }

    // 按名字查找 Value（找不到返回 nullptr）
    Value* lookup(std::string_view name) const {
        auto it = named_values_.find(std::string(name));
        return it == named_values_.end() ? nullptr : it->second;
    }

    // 沿 parent 链向上查找命名 value（ADR-013 查找路径基础版）。
    // 本作用域找不到则查 parent，递归到 global（parent=nullptr 时返回 nullptr）。
    Value* lookup_chain(std::string_view name) const {
        const Scope* s = this;
        while (s != nullptr) {
            Value* v = s->lookup(name);
            if (v != nullptr) return v;
            s = s->parent_;
        }
        return nullptr;
    }

    // 作用域退出：先从父作用域摘除自己，再递归释放所有子作用域，最后释放自己的 values。
    //
    // 摘除自己（ADR-030 阶段 2）：on_exit 开头先调 parent_->remove_child(this)，
    // 否则父作用域的 children_ 会持悬垂指针。
    //
    // 递归释放 children（ADR-013）：不能用 for 循环遍历 children_——子作用域 on_exit
    // 会调本作用域 remove_child 修改 children_，迭代器失效。用 while(!children_.empty())
    // 每次取 *begin() 调其 on_exit（子会从本作用域摘除自己），循环到 children_ 清空。
    // children_ 是 unordered_set，*begin() 取任意一个元素（无序，但都能被递归释放）。
    //
    // 调用后所有子作用域及本作用域的 Value scope_ref 归 0，下次 GC collect 时被回收。
    void on_exit() {
        // 1. 先从父作用域摘除自己（避免父持悬垂指针）
        if (parent_ != nullptr) {
            parent_->remove_child(this);
        }
        // 2. 递归释放子作用域：while 循环（子 on_exit 会从本作用域摘除自己，children_ 动态收缩）
        while (!children_.empty()) {
            (*children_.begin())->on_exit();  // 子 on_exit 内会调本作用域 remove_child
        }
        // 3. 释放自己的 values
        for (Value* v : values_) v->scope_ref_dec();
        values_.clear();
        named_values_.clear();
    }

    // —— 装饰性查询 ——
    size_t value_count() const { return values_.size(); }
    size_t named_count() const { return named_values_.size(); }

    // 遍历跟踪的 Value（调试/测试用）
    template <typename Fn>
    void for_each_value(Fn&& fn) const {
        for (Value* v : values_) fn(v);
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_SCOPE_H
