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
    // 4. global 默认标注 strict（保持默认严格模式，后续由模块/函数体标注覆盖）
    global_->setFlag("strict");
}

Context::~Context() {
    // 1. 销毁 global scope：~Scope 递归销毁整棵树
    //    （栈中残留的 current scope → root → global 逐层析构 + free）
    //    scopeRef 归 0 的 Value 在 GC 下次 collect 时被回收。
    // 注：scopeStack_ 中的指针在 global_ 析构后全部悬垂，但栈本身只是 vector<Scope*>，
    //     析构 vector 不访问指针内容，安全。成员析构顺序：scopeStack_ → gc_ → ...
    // 1. 销毁 global scope：~Scope 递归销毁整棵树
    //    （栈中残留的 current scope → root → global 逐层析构 + free）
    //    scopeRef 归 0 的 Value 在 GC 下次 collect 时被回收。
    // 注：scopeStack_ 中的指针在 global_ 析构后全部悬垂，但栈本身只是 vector<Scope*>，
    //     析构 vector 不访问指针内容，安全。成员析构顺序：scopeStack_ → gc_ → ...
    global_->~Scope();
    alloc_->free(global_);
    // 2. 销毁 GC（unique_ptr 析构）：GC 析构调 collect 回收残留 Value，
    //    若有 Value 存活（scope 未正确销毁）则 panic 报告泄漏。
    //    gc_ 在 global_ 之后销毁——先释放 scope（scopeRef 归 0），再 collect 回收 Value。
    gc_.reset();
    // 3. alloc_/console_ 由外部销毁，Context 不持有所有权
}

Scope* Context::current() const {
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

// —— 变量定义与赋值（ADR-030 阶段 5）——

namespace {
inline bool isErrorValue(Value* v) {
    return v->type()->kind() == TypeKind::Exception ||
           v->type()->kind() == TypeKind::Fatal;
}
}  // namespace

Value* Context::define(std::string_view name, Value* source) {
    // Exception/Fatal 永远不出现在左右值，提前透传
    if (isErrorValue(source)) return source;
    // 同一作用域重复定义无条件报错：var/let/const 的差异在 Sema 阶段已抹平，
    // 到达 Engine 时定义语义统一（同 scope 不可重复声明，与 strict 无关）。
    if (current()->lookup(name) != nullptr) {
        // 临时用 Exception 携带 string 代替错误报告（待 Error 对象体系就绪后替换）
        std::string msg = "Identifier '" + std::string(name) + "' has already been declared";
        return createException(createString(msg));
    }
    // clone source 的 data 进新槽位
    IData* cloned = source->data()->clone(alloc_);
    Value* slot = gc_->createValue(source->type(), cloned);
    // Scope::define 做唯一 add(跟踪) + bind
    current()->define(name, slot);
    return slot;
}

Value* Context::store(std::string_view name, Value* source) {
    // Exception/Fatal 永远不出现在左右值，提前透传
    if (isErrorValue(source)) return source;
    Value* target = current()->lookupChain(name);
    if (target != nullptr) {
        // 不可赋值（const 声明）：拦截并报错
        if (!target->isMutable()) {
            std::string msg = "Assignment to constant variable '" + std::string(name) + "'";
            return createException(createString(msg));
        }
        // 找到：clone source 的 data，替换 target 的 type+data
        IData* cloned = source->data()->clone(alloc_);
        IData* old = target->replaceMeta(source->type(), cloned);
        // 旧 data 已从 Value 摘除，collect 遍历不到，需显式回收（ref 归 0 才释放）
        gc_->releaseData(old);
        return target;
    }
    // 未找到：按当前作用域链的 strict 标注分派
    if (hasFlag("strict")) {
        std::string msg = "Assignment to undeclared identifier '" + std::string(name) + "'";
        return createException(createString(msg));
    }
    // 非严格模式：在 global 定义全局变量
    IData* cloned = source->data()->clone(alloc_);
    Value* slot = gc_->createValue(source->type(), cloned);
    global()->define(name, slot);
    return slot;
}

// —— 可赋值性（ADR-030 阶段 5）——

void Context::setMutable(Value* v, bool m) {
    v->setMutable(m);
}

bool Context::isMutable(Value* v) const {
    return v->isMutable();
}

// —— 作用域标注（ADR-030 阶段 5）——

void Context::setFlag(std::string_view name) {
    current()->setFlag(name);
}

bool Context::hasFlag(std::string_view name) const {
    return current()->lookupFlagChain(name);
}

}  // namespace neo::core

std::unique_ptr<neo::core::IContext> neo::core::createContext(
    infra::IAllocator* alloc, infra::Console* console) {
    return std::make_unique<Context>(alloc, console);
}
