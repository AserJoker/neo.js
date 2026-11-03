// scope.h - 作用域（Value 跟踪容器）
//
// Scope 维护其跟踪的 Value 列表（ADR-012 §3）：add 时 scope_ref++，on_exit 逐个
// scope_ref--。GC 以 scope_ref != 0 为存活根，scope 退出后 scope_ref 归 0 的 Value
// 在下次 collect 时被回收。
// named_values_ 是名字→Value 映射（借用，不拥有，scope_ref 由 add 负责）。
// 本阶段 Scope 是具体类（无 parent 链，作用域查找/Context 整合留后续 ADR）。
//
// 关联 ADR: 012（Scope 树 + scope_ref）、029（生命周期）

#ifndef NEO_CORE_SCOPE_H
#define NEO_CORE_SCOPE_H

#include "core/value.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace neo::core {

class Scope {
public:
    Scope() = default;
    ~Scope() = default;

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = default;
    Scope& operator=(Scope&&) = default;

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

    // 作用域退出：遍历 values_ 逐个 scope_ref_dec（ADR-012 §5），清空容器。
    // 调用后 scope_ref 归 0 的 Value 在下次 GC collect 时被回收。
    void on_exit() {
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

private:
    std::vector<Value*> values_;                          // 跟踪的 value（含匿名临时值）
    std::unordered_map<std::string, Value*> named_values_;  // 命名绑定（借用）
};

}  // namespace neo::core

#endif  // NEO_CORE_SCOPE_H
