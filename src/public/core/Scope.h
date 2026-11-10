// Scope.h - 作用域（Value 跟踪容器 + 作用域树，堆对象）
//
// Scope 维护其跟踪的 Value 列表（ADR-012 §3）。**add 不再增减 Value 的引用计数**：
// GC 从 global 作用域树出发，沿「scope 的 values + children」和「data 子引用」标记
// 可达性（ADR-044）。scope 只是可达性图上的一个节点，不再是「根」。
//
// 作用域树（ADR-012 §1 + ADR-030 阶段 2）：Scope 持 parent_ 指针与 children_ 集合。
// **Scope 是堆对象**——内存经 IAllocator + placement new 分配，生命周期由父持有
// （父的 children_ 持子 Scope 指针）。析构函数负责递归销毁整个子树：
//   1. 从父作用域摘除自己（避免父持悬垂指针）
//   2. while(!children_.empty()) 逐个析构子 Scope（子析构会从本作用域摘除自己）
//      + allocator_->free(child) 释放子 Scope 对象内存
//   3. 清空自己的 values（不触碰 Value 的引用计数）
//
// Scope 借用 IAllocator*（由 Context 传入，Context 持有 IAllocator 所有权）。
// 析构子 Scope 时需 allocator_->free(child) 释放子对象内存——子析构只调 ~Scope()
// 不释放内存（placement new 对象需显式 free）。
//
// lookupChain 沿 parent 链向上查找命名 value（ADR-013 查找路径基础版）。
// namedValues_ 是名字→Value 映射（借用，不拥有）。
//
// 闭包作用域（ADR-036）是**孤立**的 Scope：parent_ = nullptr，不挂在 root 下，
// 生命周期随持有它的 FunctionData（data 引用计数归 0 时 destroySelf）。函数调用
// 期间经 reparent(rootScope) 临时挂载（查找链 closureScope → rootScope → global
// 成立），调用结束 reparent(nullptr) 摘回孤立。
//
// 关键（ADR-044）：孤立闭包作用域**不**是 GC 根。GC 标记到 FunctionData 时，
// 由 FunctionData::enumerateChildren 主动递归标记自己的 closureScope；因此
// 「函数 → closureScope → 捕获值 → 函数」的环，只要函数对象从 global 树不可达，
// 整条环都不会被标记，可被回收——根环问题由此根治。
//
// 关联 ADR: 012（Scope 树）、013（闭包三层作用域）、029（生命周期）、
//          030 阶段 2、036（闭包作用域孤立化）、044（可达性标记）

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

class IContext;

class Scope {
    // 间接绑定（导入绑定）：名字解析为 namespaceObj[exportKey]，读取时实时求值。
    // 不持有值槽位，因此导出方后续更新（含循环依赖中晚到的导出）都能被看到。
    // namespaceObj/exportKey 均为 GC 托管 Value*，登记到 values_ 保活。
    struct IndirectBinding {
        Value* namespaceObj = nullptr;
        Value* exportKey = nullptr;
    };

    Scope* parent_ = nullptr;                              // 父作用域（global 的 parent=nullptr）
    infra::IAllocator* allocator_ = nullptr;                // 借用，用于释放子 Scope 内存
    IContext* context_ = nullptr;                           // 借用，解析间接绑定用
    std::unordered_set<Scope*> children_;                  // 子作用域（无序、去重，析构时递归释放）
    std::vector<Value*> values_;                           // 跟踪的 value（含匿名临时值）
    std::unordered_map<std::string, Value*> namedValues_; // 命名绑定（借用）
    std::unordered_map<std::string, IndirectBinding> indirectBindings_;  // 导入绑定
    std::unordered_set<std::string> flags_;                // 作用域标注（如 "strict"）

public:
    // 绑定种类：store 需据此区分「直接槽位」与「只读导入绑定」。
    enum class BindingKind { None, Direct, Indirect };

    // 构造：parent 可为 nullptr（global scope），allocator 借用（用于析构子时 free 内存）。
    // global scope 的 parent_=nullptr，allocator 由 Context 传入。
    // 子 scope 的 parent 指向父，allocator 从父继承（或 Context 统一传入）。
    // context 借用，用于间接绑定解析（可为 nullptr，仅供不建导入绑定的测试作用域）。
    explicit Scope(Scope* parent, infra::IAllocator* alloc, IContext* context = nullptr)
        : parent_(parent), allocator_(alloc), context_(context) {}

    // 析构：递归销毁整个子树（见文件头注释）。
    // 调用方负责释放本 Scope 对象自身的内存（placement new 对象需显式
    // allocator->free(this)——但通常由父析构或 Context 负责）。
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

    // 析构 + 释放自身内存（placement new 对象的完整销毁）。
    // 供 data 层（如 FunctionData）在引用计数归 0 时销毁自己持有的孤立 Scope。
    void destroySelf() {
        infra::IAllocator* alloc = allocator_;
        this->~Scope();
        alloc->free(this);
    }

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

    // 重新挂载父作用域（parent_ 与两侧 children_ 同步维护）。
    // 用于闭包作用域的临时挂载/摘除：平时孤立（parent=nullptr，生命周期随
    // FunctionData），函数调用期间挂到模块根下使查找链成立，调用结束摘回孤立。
    // newParent 可为 nullptr。与当前父相同时无操作。
    void reparent(Scope* newParent) {
        if (parent_ == newParent) return;
        if (parent_ != nullptr) parent_->removeChild(this);
        parent_ = newParent;
        if (parent_ != nullptr) parent_->addChild(this);
    }

    const std::unordered_set<Scope*>& children() const { return children_; }
    size_t childCount() const { return children_.size(); }

    // —— Value 跟踪 ——
    // 跟踪一个 Value（仅登记，不改变其引用计数；可达性由 GC 从 global 树标记）。
    // 同一 Value 多次 add 会多次登记（GC 标记幂等，无副作用）。
    void add(Value* v) { values_.push_back(v); }

    // 取消跟踪一个 Value（仅移除登记）。只移除一次出现，找不到返回 false。
    bool remove(Value* v) {
        for (auto it = values_.begin(); it != values_.end(); ++it) {
            if (*it == v) {
                values_.erase(it);
                return true;
            }
        }
        return false;
    }

    // 绑定名字→Value（借用，不影响可达性；Value 需先 add 跟踪）。
    // 重名绑定覆盖旧条目（同时清除同名间接绑定）。
    void bind(std::string_view name, Value* v) {
        indirectBindings_.erase(std::string(name));
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

    // 定义导入绑定：name → namespaceObj[exportKey]，读取时实时解析，写入恒拒绝。
    // 与直接绑定共享同一套查找路径，故 eval/with 等动态路径也受只读约束。
    // 重复定义返回 false。namespaceObj/exportKey 登记到 values_ 保活。
    bool defineIndirect(std::string_view name, Value* namespaceObj, Value* exportKey) {
        if (lookup(name) != nullptr) return false;
        add(namespaceObj);
        add(exportKey);
        indirectBindings_[std::string(name)] = IndirectBinding{namespaceObj, exportKey};
        return true;
    }

    // 按名字查找 Value（找不到返回 nullptr）。
    // 间接绑定经 context 解析为命名空间属性值（缺失时返回 undefined）。
    Value* lookup(std::string_view name) const;

    // 沿 parent 链向上查找命名 value（ADR-013 查找路径基础版）。
    // 本作用域找不到则查 parent，递归到 global（parent=nullptr 时返回 nullptr）。
    Value* lookupChain(std::string_view name) const;

    // 本作用域内绑定种类（不解析、不查 parent）。
    BindingKind bindingKind(std::string_view name) const {
        if (namedValues_.find(std::string(name)) != namedValues_.end()) {
            return BindingKind::Direct;
        }
        if (indirectBindings_.find(std::string(name)) != indirectBindings_.end()) {
            return BindingKind::Indirect;
        }
        return BindingKind::None;
    }

    // 沿 parent 链查找绑定种类（store 用于区分只读导入绑定）。
    BindingKind bindingKindChain(std::string_view name) const {
        const Scope* s = this;
        while (s != nullptr) {
            BindingKind k = s->bindingKind(name);
            if (k != BindingKind::None) return k;
            s = s->parent_;
        }
        return BindingKind::None;
    }

    // 沿 parent 链取间接绑定的 namespace/key。
    // 找到返回 true；若最近的是直接绑定（遮蔽）或不存在返回 false。
    bool getIndirectBindingChain(std::string_view name, Value*& namespaceObj,
                                 Value*& exportKey) const;

    // 把本作用域链上的间接绑定复制到 dst（闭包捕获用）。
    // 找到并复制成功返回 true；无间接绑定或 dst 已有同名绑定返回 false。
    bool copyIndirectBindingTo(Scope* dst, std::string_view name) const;

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

    // 遍历本作用域直接跟踪的 Value（供 GC 标记访问器使用）
    const std::vector<Value*>& values() const { return values_; }
};

}  // namespace neo::core

#endif  // NEO_CORE_SCOPE_H
