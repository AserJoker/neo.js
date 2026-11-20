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
#include "core/types/TypeBehavior.h"

namespace neo::core {

namespace {

// 解析 with 对象是否绑定 key（ES2024 §9.1.1.2 HasBinding）：
// 先 HasProperty；再检查 @@unscopables 是否把该名字排除。
// 成功返回 nullptr，bound 表示是否绑定；getter 抛错时返回 Exception/Fatal。
Value* resolveWithBinding(IContext* ctx, Value* withObject, Value* key, bool& bound) {
    bound = false;
    Value* has = ctx->hasProperty(withObject, key);
    if (detail::isErrorValue(has)) return has;
    if (!detail::toBoolean(has)) return nullptr;

    Value* sym = ctx->getWellKnownSymbol(WellKnownSymbol::Unscopables);
    Value* unscopables = ctx->getProperty(withObject, sym);
    if (detail::isErrorValue(unscopables)) return unscopables;
    if (detail::isObjectKind(unscopables->type()->kind())) {
        Value* excluded = ctx->getProperty(unscopables, key);
        if (detail::isErrorValue(excluded)) return excluded;
        if (detail::toBoolean(excluded)) return nullptr;
    }
    bound = true;
    return nullptr;
}

}  // namespace

void Scope::destroySelf() {
    infra::IAllocator* alloc = allocator_;
    this->~Scope();
    alloc->free(this);
}

Value* Scope::lookup(std::string_view name) const {
    auto it = namedValues_.find(std::string(name));
    if (it != namedValues_.end()) return it->second.value;

    auto wit = withBindings_.find(std::string(name));
    if (wit != withBindings_.end()) {
        // with 属性绑定：读取时实时解析，属性被重新赋值后仍能看到新值。
        if (context_ == nullptr) return nullptr;
        return context_->getProperty(wit->second.withObject, wit->second.key);
    }

    auto iit = importBindings_.find(std::string(name));
    if (iit == importBindings_.end()) return nullptr;
    // 导入绑定：直接返回导出方共享槽位的当前值。
    return iit->second;
}

bool Scope::getImportBindingChain(std::string_view name, Value*& slot) const {
    const Scope* s = this;
    while (s != nullptr) {
        // 直接/with 绑定优先：一旦遇到，导入绑定被遮蔽。
        if (s->namedValues_.find(std::string(name)) != s->namedValues_.end()) {
            return false;
        }
        if (s->withBindings_.find(std::string(name)) != s->withBindings_.end()) {
            return false;
        }
        auto it = s->importBindings_.find(std::string(name));
        if (it != s->importBindings_.end()) {
            slot = it->second;
            return true;
        }
        s = s->parent_;
    }
    return false;
}

bool Scope::copyImportBindingTo(Scope* dst, std::string_view name) const {
    Value* slot = nullptr;
    if (!getImportBindingChain(name, slot)) return false;
    return dst->defineImport(name, slot);
}

bool Scope::getWithBindingChain(std::string_view name, Value*& withObject,
                                Value*& key) const {
    const Scope* s = this;
    while (s != nullptr) {
        // 直接绑定遮蔽 with 对象。
        if (s->namedValues_.find(std::string(name)) != s->namedValues_.end()) {
            return false;
        }
        // 已捕获的 with 属性绑定。
        auto it = s->withBindings_.find(std::string(name));
        if (it != s->withBindings_.end()) {
            withObject = it->second.withObject;
            key = it->second.key;
            return true;
        }
        // 导入绑定同样遮蔽 with 对象。
        if (s->importBindings_.find(std::string(name)) != s->importBindings_.end()) {
            return false;
        }
        // 动态 WithScope：对象真正绑定该名字（考虑 @@unscopables）时捕获。
        Value* obj = s->withObject();
        if (obj != nullptr && context_ != nullptr) {
            Value* k = context_->createString(name);
            if (k == context_->oomValue()) return false;
            bool bound = false;
            Value* err = resolveWithBinding(context_, obj, k, bound);
            if (err == nullptr && bound) {
                withObject = obj;
                key = k;
                return true;
            }
        }
        s = s->parent_;
    }
    return false;
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

Scope::StoreOutcome Scope::store(IContext* ctx, std::string_view name,
                                 Value* source, Value*& result) {
    // 闭包捕获的 with 属性：写入实时走 setProperty。
    auto it = withBindings_.find(std::string(name));
    if (it != withBindings_.end()) {
        result = ctx->setProperty(it->second.withObject, it->second.key, source);
        return detail::isErrorValue(result) ? StoreOutcome::Error : StoreOutcome::Handled;
    }
    // 普通作用域的直接/间接绑定由 Context::store 统一处理。
    return StoreOutcome::NotFound;
}

// —— GlobalScope ——

GlobalScope::GlobalScope(Scope* parent, infra::IAllocator* alloc,
                         IContext* context, Value* globalObject)
    : Scope(parent, alloc, context), globalObject_(globalObject) {
    if (globalObject_ != nullptr) add(globalObject_);
}

Value* GlobalScope::lookup(std::string_view name) const {
    Value* value = Scope::lookup(name);
    if (value != nullptr) return value;
    IContext* ctx = context();
    if (ctx == nullptr || globalObject_ == nullptr) return nullptr;
    Value* key = ctx->createString(name);
    if (key == ctx->oomValue()) return key;
    // 对象环境记录用 HasProperty（含原型链），不能用 getProperty 是否为
    // undefined 判断——`undefined` 属性本身的值就是 undefined。
    Value* has = ctx->hasProperty(globalObject_, key);
    if (detail::isErrorValue(has)) return has;
    if (!detail::toBoolean(has)) return nullptr;
    return ctx->getProperty(globalObject_, key);
}

Scope::StoreOutcome GlobalScope::store(IContext* ctx, std::string_view name,
                                       Value* source, Value*& result) {
    // 声明绑定（全局 let/const、内部值）由 Context::store 处理 TDZ/const。
    if (bindingKind(name) != BindingKind::None) return StoreOutcome::NotFound;
    // 严格模式下给不存在的全局属性赋值是 ReferenceError（§9.1.1.4.5
    // SetMutableBinding 把 S=true 传给对象记录），交给 Context::store 的
    // strict 兜底统一报错。
    Value* has = ctx->hasProperty(globalObject_, name);
    if (detail::isErrorValue(has)) {
        result = has;
        return StoreOutcome::Error;
    }
    if (!detail::toBoolean(has) && ctx->hasFlag("strict")) {
        return StoreOutcome::NotFound;
    }
    result = ctx->setProperty(globalObject_, name, source);
    return detail::isErrorValue(result) ? StoreOutcome::Error
                                        : StoreOutcome::Handled;
}

void GlobalScope::destroySelf() {
    infra::IAllocator* alloc = allocator();
    this->~GlobalScope();
    alloc->free(this);
}

// —— WithScope ——

WithScope::WithScope(Scope* parent, infra::IAllocator* alloc, IContext* context,
                     Value* withObject)
    : Scope(parent, alloc, context), withObject_(withObject) {
    // with 对象由本作用域保活（GC 从作用域树标记 values_）。
    add(withObject_);
}

Value* WithScope::lookup(std::string_view name) const {
    IContext* ctx = context();
    if (ctx != nullptr) {
        Value* key = ctx->createString(name);
        if (key == ctx->oomValue()) return key;
        bool bound = false;
        Value* err = resolveWithBinding(ctx, withObject_, key, bound);
        if (err != nullptr) return err;
        if (bound) return ctx->getProperty(withObject_, key);
    }
    return Scope::lookup(name);
}

Value* WithScope::withBindingObject(std::string_view name) const {
    IContext* ctx = context();
    if (ctx == nullptr) return nullptr;
    Value* key = ctx->createString(name);
    if (key == ctx->oomValue()) return key;
    bool bound = false;
    Value* err = resolveWithBinding(ctx, withObject_, key, bound);
    if (err != nullptr) return err;
    return bound ? withObject_ : nullptr;
}

Scope::StoreOutcome WithScope::store(IContext* ctx, std::string_view name,
                                     Value* source, Value*& result) {
    Value* key = ctx->createString(name);
    if (key == ctx->oomValue()) {
        result = key;
        return StoreOutcome::Error;
    }
    bool bound = false;
    Value* err = resolveWithBinding(ctx, withObject_, key, bound);
    if (err != nullptr) {
        result = err;
        return StoreOutcome::Error;
    }
    if (!bound) return StoreOutcome::NotFound;
    result = ctx->setProperty(withObject_, key, source);
    return detail::isErrorValue(result) ? StoreOutcome::Error : StoreOutcome::Handled;
}

void WithScope::destroySelf() {
    infra::IAllocator* alloc = allocator();
    this->~WithScope();
    alloc->free(this);
}

Scope::~Scope() {
    // 1. 先从父作用域摘除自己（避免父持悬垂指针）。
    //    linkedOnly 的 parent 仅用于查找，本作用域从未登记进 parent->children_。
    if (parent_ != nullptr && !linkedOnly_) {
        parent_->removeChild(this);
    }
    // 2. 递归析构子作用域：while 循环（子析构会从本作用域 removeChild，children_ 动态收缩）
    //    子 Scope 是 placement new 对象，需显式调析构 + allocator_->free 释放内存。
    //    仍被 Environment 闭包 pin 住的子作用域不能销毁：摘离本树并标记孤儿，
    //    待闭包析构时按 captureRef 归 0 回收（ADR-036 扩展）。
    while (!children_.empty()) {
        Scope* child = *children_.begin();
        if (child->captureRef() > 0) {
            children_.erase(child);
            child->parent_ = nullptr;
            child->markAutoDispose();
            continue;
        }
        child->destroySelf();         // 虚析构 + 释放子 Scope 对象内存
    }
    // 3. 清空自己的 values（Value 的回收由 GC 的可达性标记决定）
    values_.clear();
    namedValues_.clear();
    importBindings_.clear();
    withBindings_.clear();
}

}  // namespace neo::core
