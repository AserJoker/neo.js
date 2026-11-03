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
#include "core/IType.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/Undefined.h"

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
    if (global_ != nullptr) {  // NEO_NO_COVERAGE (else 不可达：构造必成功否则抛异常)
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
    if (scopeStack_.empty()) return nullptr;  // NEO_NO_COVERAGE (root 始终在栈底)
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

// —— Value 创建便捷方法 ——
// 统一模式：allocData<T> → createValue(type, data) → current()->add(v) 跟踪。
// 可选 name：非空则 current()->bind(name, v) 命名绑定。
// OOM 处理留待阶段 5（目前 allocData/createValue 返回 nullptr 时直接返回 nullptr）。

namespace {

// 把 Value 加入 current scope：add 跟踪 + 可选 bind 命名绑定。
void trackValue(Scope* scope, Value* v, std::string_view name) {
    if (v == nullptr) return;  // NEO_NO_COVERAGE (阶段5 前 createValue 不返 nullptr)
    scope->add(v);
    if (!name.empty()) {
        scope->bind(name, v);
    }
}

}  // namespace

Value* Context::createUndefined(std::string_view name) {
    auto* d = gc_->allocData<UndefinedData>();
    Value* v = gc_->createValue(getUndefinedType(), d);
    trackValue(current(), v, name);
    return v;
}

Value* Context::createNull(std::string_view name) {
    auto* d = gc_->allocData<NullData>();
    Value* v = gc_->createValue(getNullType(), d);
    trackValue(current(), v, name);
    return v;
}

Value* Context::createBoolean(bool b, std::string_view name) {
    auto* d = gc_->allocData<BooleanData>(b);
    Value* v = gc_->createValue(getBooleanType(), d);
    trackValue(current(), v, name);
    return v;
}

Value* Context::createNumber(double n, std::string_view name) {
    auto* d = gc_->allocData<NumberData>(n);
    Value* v = gc_->createValue(getNumberType(), d);
    trackValue(current(), v, name);
    return v;
}

Value* Context::createString(std::string_view s, std::string_view name) {
    auto* d = gc_->allocData<StringData>(s);
    Value* v = gc_->createValue(getStringType(), d);
    trackValue(current(), v, name);
    return v;
}

// —— 运算代理（纯代理，ADR-016 §4）——
// ctx->add(a,b) → a->type()->add(this, a, b)，Context 不持协商规则表。

Value* Context::add(Value* a, Value* b) {
    return a->type()->add(this, a, b);
}

Value* Context::sub(Value* a, Value* b) {
    return a->type()->sub(this, a, b);
}

Value* Context::mul(Value* a, Value* b) {
    return a->type()->mul(this, a, b);
}

Value* Context::div(Value* a, Value* b) {
    return a->type()->div(this, a, b);
}

Value* Context::toString(Value* self) {
    return self->type()->toString(this, self);
}

// —— 错误值创建（ADR-006 异常即值 + ADR-017 §4 Fatal）——

Value* Context::createException(Value* child) {
    auto* d = gc_->allocData<ExceptionData>(child);
    Value* v = gc_->createValue(getExceptionType(), d);
    trackValue(current(), v, {});
    return v;
}

Value* Context::createFatal(const char* msg) {
    auto* d = gc_->allocData<FatalData>(msg);
    Value* v = gc_->createValue(getFatalType(), d);
    trackValue(current(), v, {});
    return v;
}

}  // namespace neo::core

std::unique_ptr<neo::core::IContext> neo::core::createContext(
    infra::IAllocator* alloc, infra::Console* console) {
    return std::make_unique<Context>(alloc, console);
}
