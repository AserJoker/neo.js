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
public:
    // 命名绑定（环境记录条目）。
    // mutability 属于绑定而不是 Value：同一个 Value* 可以在导出方是 mutable、
    // 在导入方是只读；闭包捕获时复制绑定属性。
    struct Binding {
        Value* value = nullptr;
        bool mutableBinding = true;
        bool initialized = true;  // false 表示 TDZ
    };

private:
    // 间接绑定（导入绑定）：名字解析为 namespaceObj[exportKey]，读取时实时求值。
    // 不持有值槽位，因此导出方后续更新（含循环依赖中晚到的导出）都能被看到。
    // namespaceObj/exportKey 均为 GC 托管 Value*，登记到 values_ 保活。
    struct IndirectBinding {
        Value* namespaceObj = nullptr;
        Value* exportKey = nullptr;
    };

    // with 对象属性绑定（闭包捕获用）：name -> withObject[key]。
    // 读取走 getProperty、写入走 setProperty，保持对象环境记录的动态语义。
    struct WithBinding {
        Value* withObject = nullptr;
        Value* key = nullptr;
    };

    Scope* parent_ = nullptr;                              // 父作用域（global 的 parent=nullptr）
    infra::IAllocator* allocator_ = nullptr;                // 借用，用于释放子 Scope 内存
    IContext* context_ = nullptr;                           // 借用，解析间接绑定用
    std::unordered_set<Scope*> children_;                  // 子作用域（无序、去重，析构时递归释放）
    std::vector<Value*> values_;                           // 跟踪的 value（含匿名临时值）
    std::unordered_map<std::string, Binding> namedValues_; // 命名绑定（借用 Value）
    std::unordered_map<std::string, IndirectBinding> indirectBindings_;  // 导入绑定
    std::unordered_map<std::string, WithBinding> withBindings_;          // with 属性捕获
    std::unordered_set<std::string> flags_;                // 作用域标注（如 "strict"）

    // —— 动态作用域（运行时闭包链）——
    // captureRef_：Environment 模式闭包对定义点环境链的 pin 计数。> 0 表示本作用域
    //   不能随 pop/父析构销毁，必须等闭包释放后回收。
    // autoDispose_：已 pop 但仍被 pin 住的作用域（孤儿），计数归 0 时由闭包析构回收。
    // dynamic_：本作用域内执行过直接 eval，之后在此（及子作用域）定义的函数需捕获
    //   运行时环境链，而非静态 slot。
    // linkedOnly_：parent_ 仅用于查找，不登记进 parent->children_（Environment 模式
    //   闭包作用域的所有权仍归 FunctionData，避免与作用域树双重销毁）。
    size_t captureRef_ = 0;
    bool autoDispose_ = false;
    bool dynamic_ = false;
    bool linkedOnly_ = false;

    // —— 函数环境记录（ADR-047）——
    // this / new.target 是函数环境专用槽，不是普通命名绑定：不参与 lookup，
    // 不能被 with 拦截，也不能被 var/let this 声明或赋值。
    // hasOwnThis_ 区分「本函数有自己的 this」与「箭头函数继承外层 this」。
    // 值登记进 values_ 保活（GC 从作用域树标记）。
    Value* thisValue_ = nullptr;
    bool hasOwnThis_ = false;
    Value* newTarget_ = nullptr;
    bool hasOwnNewTarget_ = false;

protected:
    // 派生作用域（WithScope）解析属性时借用 Context。
    IContext* context() const { return context_; }

public:
    // 绑定种类：store 需据此区分「直接槽位」「只读导入绑定」「with 属性」。
    enum class BindingKind { None, Direct, Indirect, With };

    // 赋值处理结果：NotFound 表示本作用域不处理，继续沿 parent 链查找。
    enum class StoreOutcome { NotFound, Handled, Error };

    // 构造：parent 可为 nullptr（global scope），allocator 借用（用于析构子时 free 内存）。
    // global scope 的 parent_=nullptr，allocator 由 Context 传入。
    // 子 scope 的 parent 指向父，allocator 从父继承（或 Context 统一传入）。
    // context 借用，用于间接绑定解析（可为 nullptr，仅供不建导入绑定的测试作用域）。
    explicit Scope(Scope* parent, infra::IAllocator* alloc, IContext* context = nullptr)
        : parent_(parent), allocator_(alloc), context_(context) {}

    // 析构：递归销毁整个子树（见文件头注释）。
    // 虚析构：WithScope 等派生作用域经 Scope* 销毁时需正确析构。
    virtual ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

    // 析构 + 释放自身内存（placement new 对象的完整销毁）。
    // 供 data 层（如 FunctionData）在引用计数归 0 时销毁自己持有的孤立 Scope。
    // 虚函数：派生作用域需释放自己的成员后再归还内存。
    virtual void destroySelf();

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
        if (parent_ == newParent) {
            // 从 lookup-only 转为真实子节点（仍挂在同一父下时补登记）。
            if (newParent != nullptr && linkedOnly_) {
                linkedOnly_ = false;
                newParent->addChild(this);
            }
            return;
        }
        if (parent_ != nullptr && !linkedOnly_) parent_->removeChild(this);
        linkedOnly_ = false;
        parent_ = newParent;
        if (parent_ != nullptr) parent_->addChild(this);
    }

    // 只建立查找链，不登记进 parent->children_（Environment 模式闭包作用域用）。
    // 生命周期仍归持有者（FunctionData），父作用域析构时不会连带销毁本作用域。
    void linkLookupParent(Scope* newParent) {
        if (parent_ != nullptr && !linkedOnly_) parent_->removeChild(this);
        parent_ = newParent;
        linkedOnly_ = true;
    }

    // 从作用域树摘除，但保留 parent_ 查找链（autoDispose 孤儿用）。
    // 摘除后不再从 global 树可达：只有逃逸闭包（FunctionData）能标记它，
    // 否则其 values 中的函数对象会反过来把作用域变成永久根。
    void detachFromParentTree() {
        if (parent_ != nullptr && !linkedOnly_) {
            parent_->removeChild(this);
            linkedOnly_ = true;
        }
    }

    // —— 动态作用域 / pin 计数 ——
    void incCaptureRef() { ++captureRef_; }

    // 减少 pin 计数；若本次归 0 返回 true（调用方据此回收 autoDispose 孤儿）。
    bool decCaptureRef() {
        if (captureRef_ == 0) return false;
        --captureRef_;
        return captureRef_ == 0;
    }

    size_t captureRef() const { return captureRef_; }
    void markAutoDispose() { autoDispose_ = true; }
    bool autoDispose() const { return autoDispose_; }
    void markDynamic() { dynamic_ = true; }
    bool dynamic() const { return dynamic_; }

    // 本作用域或任一祖先是否标记为 dynamic（直接 eval 沿链继承）。
    bool dynamicChain() const {
        const Scope* s = this;
        while (s != nullptr) {
            if (s->dynamic_) return true;
            s = s->parent_;
        }
        return false;
    }

    bool linkedOnly() const { return linkedOnly_; }

    // —— 函数环境：this / new.target（ADR-047）——
    // 专用槽位，不进入 namedValues_；set 时登记到 values_ 保活。
    bool hasOwnThis() const { return hasOwnThis_; }
    Value* thisValue() const { return thisValue_; }
    void setThis(Value* v) {
        thisValue_ = v;
        hasOwnThis_ = true;
        if (v != nullptr) add(v);
    }

    bool hasOwnNewTarget() const { return hasOwnNewTarget_; }
    Value* newTarget() const { return newTarget_; }
    void setNewTarget(Value* v) {
        newTarget_ = v;
        hasOwnNewTarget_ = true;
        if (v != nullptr) add(v);
    }

    // 沿 parent 链找最近的函数环境槽（箭头函数没有自己的槽，继承外层）。
    Value* lookupThis() const {
        for (const Scope* s = this; s != nullptr; s = s->parent_) {
            if (s->hasOwnThis_) return s->thisValue_;
        }
        return nullptr;
    }

    Value* lookupNewTarget() const {
        for (const Scope* s = this; s != nullptr; s = s->parent_) {
            if (s->hasOwnNewTarget_) return s->newTarget_;
        }
        return nullptr;
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
    void bind(std::string_view name, Value* v, bool mutableBinding = true) {
        indirectBindings_.erase(std::string(name));
        namedValues_[std::string(name)] =
            Binding{v, mutableBinding, true};
    }

    // 定义变量（ADR-030 阶段 5）：仅在当前 scope 绑定 name→v + add 跟踪。
    // 重复定义返回 false（调用方报错），成功返回 true。
    // 重复检查无条件生效——var/let/const 的差异在 Sema 阶段已抹平，与 strict 无关。
    // 与 bind 的区别：bind 覆盖重名且不 add 跟踪；define 重复检查 + add 跟踪。
    bool define(std::string_view name, Value* v, bool mutableBinding = true,
                bool initialized = true) {
        // 只检查本作用域的直接/间接绑定；with 对象属性不是声明。
        if (bindingKind(name) != BindingKind::None) return false;
        add(v);
        namedValues_[std::string(name)] =
            Binding{v, mutableBinding, initialized};
        return true;
    }

    // 本作用域的直接绑定条目（不解析 parent，不触发 TDZ）。
    Binding* lookupBinding(std::string_view name) {
        auto it = namedValues_.find(std::string(name));
        return it == namedValues_.end() ? nullptr : &it->second;
    }

    const Binding* lookupBinding(std::string_view name) const {
        auto it = namedValues_.find(std::string(name));
        return it == namedValues_.end() ? nullptr : &it->second;
    }

    // 沿 parent 链查找直接绑定条目；with/间接绑定不返回。
    Binding* lookupBindingChain(std::string_view name) {
        for (Scope* s = this; s != nullptr; s = s->parent_) {
            if (Binding* b = s->lookupBinding(name)) return b;
        }
        return nullptr;
    }

    const Binding* lookupBindingChain(std::string_view name) const {
        for (const Scope* s = this; s != nullptr; s = s->parent_) {
            if (const Binding* b = s->lookupBinding(name)) return b;
        }
        return nullptr;
    }

    // 定义导入绑定：name → namespaceObj[exportKey]，读取时实时解析，写入恒拒绝。
    // 与直接绑定共享同一套查找路径，故 eval/with 等动态路径也受只读约束。
    // 重复定义返回 false。namespaceObj/exportKey 登记到 values_ 保活。
    bool defineIndirect(std::string_view name, Value* namespaceObj, Value* exportKey) {
        if (bindingKind(name) != BindingKind::None) return false;
        add(namespaceObj);
        add(exportKey);
        indirectBindings_[std::string(name)] = IndirectBinding{namespaceObj, exportKey};
        return true;
    }

    // 定义 with 属性绑定（闭包捕获）：name -> withObject[key]。
    // 读取实时 getProperty，写入实时 setProperty，与对象环境记录一致。
    bool defineWith(std::string_view name, Value* withObject, Value* key) {
        if (bindingKind(name) != BindingKind::None) return false;
        add(withObject);
        add(key);
        withBindings_[std::string(name)] = WithBinding{withObject, key};
        return true;
    }

    // 按名字查找 Value（找不到返回 nullptr）。
    // 间接绑定经 context 解析为命名空间属性值（缺失时返回 undefined）。
    // 虚函数：WithScope 先查 with 对象属性，未命中再回退普通绑定。
    virtual Value* lookup(std::string_view name) const;

    // 沿 parent 链向上查找命名 value（ADR-013 查找路径基础版）。
    // 本作用域找不到则查 parent，递归到 global（parent=nullptr 时返回 nullptr）。
    Value* lookupChain(std::string_view name) const;

    // 本作用域处理赋值（with 对象属性写入）。返回 NotFound 时由调用方继续沿链查找；
    // Handled/Error 时 result 为赋值结果（Error 时为 Exception/Fatal）。
    // 基类不处理（直接/间接绑定由 Context::store 统一处理）。
    virtual StoreOutcome store(IContext* ctx, std::string_view name, Value* source,
                               Value*& result);

    // with 对象（非 WithScope 返回 nullptr）。Context::store 据此分派对象属性写入。
    virtual Value* withObject() const { return nullptr; }

    // 本作用域内绑定种类（不解析、不查 parent）。
    BindingKind bindingKind(std::string_view name) const {
        if (namedValues_.find(std::string(name)) != namedValues_.end()) {
            return BindingKind::Direct;
        }
        if (withBindings_.find(std::string(name)) != withBindings_.end()) {
            return BindingKind::With;
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

    // 沿 parent 链取 with 属性绑定（动态 WithScope 或静态 withBindings_）。
    // 找到返回 true；直接/间接绑定遮蔽或不存在返回 false。
    bool getWithBindingChain(std::string_view name, Value*& withObject,
                             Value*& key) const;

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

// with 作用域（ES2024 §9.1.1.2 Object Environment Records）：
// 在普通作用域链上插入一个对象环境——名字先查 with 对象（含原型链），
// 命中则读写对象属性；未命中回退到外层作用域。
//
// 与普通 Scope 的区别：
//   - 多一个 withObject_（已 ToObject 的对象），登记到 values_ 保活；
//   - lookup 先查对象属性（getProperty，含 getter/原型链）；
//   - store 命中对象属性时走 setProperty，而非原地替换 Value 槽位。
//
// 生命周期：由 Context::pushWithScope 创建、popScope 销毁；是 current 的普通子作用域。
class WithScope : public Scope {
    Value* withObject_ = nullptr;

public:
    WithScope(Scope* parent, infra::IAllocator* alloc, IContext* context,
              Value* withObject);

    Value* withObject() const override { return withObject_; }

    // 读取：with 对象有该属性（含原型链）则返回属性值，否则回退普通绑定。
    Value* lookup(std::string_view name) const override;

    // 写入：with 对象有该属性则 setProperty；否则 NotFound 交给外层。
    StoreOutcome store(IContext* ctx, std::string_view name, Value* source,
                       Value*& result) override;

    void destroySelf() override;
};

}  // namespace neo::core

#endif  // NEO_CORE_SCOPE_H
