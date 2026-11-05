// context.cpp - Context 实现
//
// 构造：用 IAllocator 创建 GC（createGcEngine(alloc)，Context 持有所有权），
// placement new 创建 global scope（parent=nullptr）和 root scope（parent=global），
// global->addChild(root)，root 入 current 栈。
// 析构：先销毁 global scope（~Scope 递归销毁整棵树：栈中残留 scope → root → global），
// 再销毁 GC（unique_ptr 析构，GC 析构时 collect 回收残留 Value，泄漏则 panic），
// IAllocator/Console 不销毁（外部持有）。
//
// pushNewScope: 新建 parent 的子 scope 并设为 current。
// popScope: 销毁 current 并回到其 parent（parent 链即调用栈）。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、012（Scope 树）、030 阶段 4

#include "core/Context.h"
#include "core/IType.h"
#include "core/SymbolBuiltins.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Function.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/String.h"
#include "core/types/Symbol.h"
#include "core/types/Undefined.h"

#include <cstdlib>
#include <new>
#include <string>

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
    // 3. current 初始 = root
    current_ = root_;
    // 4. global 默认标注 strict（保持默认严格模式，后续由模块/函数体标注覆盖）
    global_->setFlag("strict");
    // 5. 预分配 OOM 哨兵（ADR-031）：最早分配，挂 global 使其存活于 Context 生命周期。
    //    连哨兵都分配不出说明系统不可用，直接 abort。
    auto* oomData = gc_->allocData<FatalData>("out of memory");
    oomValue_ = oomData != nullptr ? gc_->createValue(getFatalType(), oomData) : nullptr;
    if (oomValue_ == nullptr) {
        std::abort();
    }
    global_->add(oomValue_);
    global_->bind("__oom__", oomValue_);

    // 6. 预生成全部 well-known symbols（ES2024 §6.1.5.1），挂 global 使其存活于
    //    Context 生命周期。description 与属性名一致（如 "Symbol.iterator"）。
    //    OOM 时同样 abort（系统不可用）。
    for (size_t i = 0; i < kWellKnownSymbolCount; ++i) {
        auto which = static_cast<WellKnownSymbol>(i);
        std::string desc = std::string("Symbol.") + wellKnownSymbolName(which);
        auto* data = gc_->allocData<SymbolData>(desc);
        Value* v = data != nullptr ? gc_->createValue(getSymbolType(), data) : nullptr;
        if (v == nullptr) {
            std::abort();
        }
        global_->add(v);
        wellKnownSymbols_[i] = v;
    }

    // 7. 注册 Symbol 包装类（构造函数 + prototype.toString + well-known 属性）。
    //    依赖 well-known symbols 已就绪；失败说明系统不可用，abort。
    if (!registerSymbolBuiltins(this)) {
        std::abort();
    }
}

Context::~Context() {
    // 1. 销毁 global scope：~Scope 递归销毁整棵树（current → ... → root → global 逐层析构 + free）
    //    scopeRef 归 0 的 Value 在 GC 下次 collect 时被回收。
    //    注：current_/root_ 在 global_ 析构后悬垂，但不再被访问（成员只是裸指针）。
    global_->~Scope();
    alloc_->free(global_);
    // 2. 销毁 GC（unique_ptr 析构）：GC 析构调 collect 回收残留 Value，
    //    若有 Value 存活（scope 未正确销毁）则 panic 报告泄漏。
    //    gc_ 在 global_ 之后销毁——先释放 scope（scopeRef 归 0），再 collect 回收 Value。
    gc_.reset();
    // 3. alloc_/console_ 由外部销毁，Context 不持有所有权
}

Scope* Context::pushNewScope(Scope* parent) {
    void* p = alloc_->alloc(sizeof(Scope));
    if (p == nullptr) return nullptr;
    Scope* s = new (p) Scope(parent, alloc_);
    parent->addChild(s);
    current_ = s;
    return s;
}

void Context::popScope() {
    // root/global 不可 pop（模块根与全局是持久作用域）
    if (current_ == root_ || current_ == global_) return;
    // current 的 parent 即调用栈下一层
    Scope* parent = current_->parent();
    if (parent == nullptr) return;
    Scope* top = current_;
    current_ = parent;
    // 销毁弹出的 scope：~Scope 递归释放子树 + 从父摘除自己
    top->~Scope();
    alloc_->free(top);
}

// —— Value 创建便捷方法 ——
// 统一模式：allocData<T> → createValue(type, data) → current()->add(v) 跟踪。
// 可选 name：非空则 current()->bind(name, v) 命名绑定。
// OOM：allocData/createValue 失败时返回预分配 OOM 哨兵（ADR-031），不返回 nullptr。

namespace {

// 把 Value 加入 current scope：add 跟踪 + 可选 bind 命名绑定。
void trackValue(Scope* scope, Value* v, std::string_view name) {
    scope->add(v);
    if (!name.empty()) {
        scope->bind(name, v);
    }
}

}  // namespace

// 创建 Value 并跟踪：data 已由调用方 allocData 分配。
// 失败时回收游离 data 并返回 OOM 哨兵（ADR-031）。
Value* Context::createTracked(IType* type, IData* data, std::string_view name) {
    if (data == nullptr) return oomValue_;
    Value* v = gc_->createValue(type, data);
    if (v == nullptr) {
        // data 已分配但 Value 创建失败，回收游离 data
        gc_->releaseData(data);
        return oomValue_;
    }
    trackValue(current(), v, name);
    return v;
}

Value* Context::createUndefined(std::string_view name) {
    return createTracked(getUndefinedType(), gc_->allocData<UndefinedData>(), name);
}

Value* Context::createNull(std::string_view name) {
    return createTracked(getNullType(), gc_->allocData<NullData>(), name);
}

Value* Context::createBoolean(bool b, std::string_view name) {
    return createTracked(getBooleanType(), gc_->allocData<BooleanData>(b), name);
}

Value* Context::createNumber(double n, std::string_view name) {
    return createTracked(getNumberType(), gc_->allocData<NumberData>(n), name);
}

Value* Context::createString(std::string_view s, std::string_view name) {
    return createTracked(getStringType(), gc_->allocData<StringData>(s), name);
}

Value* Context::createSymbol(std::string_view description, std::string_view name) {
    // 每次创建分配独立的 SymbolData：唯一性由 data 身份保证
    return createTracked(getSymbolType(), gc_->allocData<SymbolData>(description), name);
}

// —— Symbol 注册表（ES2024 §20.4.2.1 / §20.4.2.6）——
//
// Symbol.for(key)：查全局注册表，命中返回已有 symbol，否则创建并登记。
// 注册表中的 symbol 必须存活于 Context 生命周期（否则 key 反查会悬垂），
// 故登记时挂到 global scope（scopeRef++）。
//
// Symbol.keyFor(sym)：按 symbol data 指针反查 key；非 symbol 或未登记 → undefined。

Value* Context::symbolFor(std::string_view key) {
    std::string k(key);
    auto it = symbolRegistry_.find(k);
    if (it != symbolRegistry_.end()) return it->second;

    // 未登记：创建新 symbol（description = key）并挂 global 保活
    Value* sym = createSymbol(k);
    if (sym == oomValue_) return sym;
    global_->add(sym);
    symbolRegistry_.emplace(k, sym);
    symbolRegistryReverse_.emplace(sym->data(), k);
    return sym;
}

Value* Context::symbolKeyFor(Value* sym) {
    if (sym->type()->kind() != TypeKind::Symbol) return createUndefined();
    auto it = symbolRegistryReverse_.find(sym->data());
    if (it == symbolRegistryReverse_.end()) return createUndefined();
    return createString(it->second);
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

// —— 函数调用（ADR-022）——

Value* Context::call(Value* fn, Value* thisArg, size_t argc, Value* argv[]) {
    return fn->type()->call(this, fn, thisArg, argc, argv);
}

Value* Context::construct(Value* fn, size_t argc, Value* argv[]) {
    return fn->type()->construct(this, fn, argc, argv);
}

Value* Context::createNativeFunction(NativeFn fn, size_t minArgc, std::string_view bindName) {
    // closureScope 是当前 root scope 的子作用域（Sema 收集的穿透闭包变量绑定于此）
    void* p = alloc_->alloc(sizeof(Scope));
    if (p == nullptr) return oomValue_;
    Scope* closure = new (p) Scope(root_, alloc_);
    root_->addChild(closure);
    // rootScope 借用当前 root（不拥有）
    auto* d = gc_->allocData<FunctionData>(closure, root_, fn, minArgc);
    // bindName 仅用于变量绑定（与函数名无关）
    Value* fnValue = createTracked(getFunctionType(), d, bindName);
    if (fnValue == oomValue_) return fnValue;

    // —— 函数名作为 "name" 属性（ES2024 §20.2.4.2）——
    // writable=false / enumerable=false / configurable=true；缺省 "<anonymous>"，
    // 由 setFunctionName 更新（对应 VM 的 setname 字节码）。
    Value* anonName = createString("<anonymous>");
    if (anonName == oomValue_) return anonName;
    if (defineProperty(fnValue, "name", anonName, false, false, true)->type()->kind() ==
        TypeKind::Exception) {
        return oomValue_;
    }

    // —— 自动创建 prototype（ADR-034）——
    // 每个函数创建时生成一个普通对象作为 fn.prototype，并在其上定义 constructor 指回函数，
    // 构成 JS 的「构造函数 ↔ 原型」双向引用。两者均不可枚举（与 ES 一致）。
    // 存活：fn 的属性 value 被 ObjectData::enumerateChildren 遍历 → proto 保活；
    //       proto 的 constructor 属性同理保活 fn。
    Value* proto = createObject();
    if (proto == oomValue_) return proto;
    // fn.prototype：writable，不可枚举，不可配置
    if (defineProperty(fnValue, "prototype", proto, true, false, false)->type()->kind() ==
        TypeKind::Exception) {
        return oomValue_;
    }
    // proto.constructor：writable，不可枚举，可配置
    if (defineProperty(proto, "constructor", fnValue, true, false, true)->type()->kind() ==
        TypeKind::Exception) {
        return oomValue_;
    }
    return fnValue;
}

void Context::setFunctionName(Value* fn, std::string_view name) {
    // 仅对 Function 类型有效（setname 字节码），其他类型忽略
    if (fn->type()->kind() != TypeKind::Function) return;
    // 函数名是 "name" 属性（writable=false），用 defineProperty 直接覆盖描述符值
    Value* nameValue = createString(name);
    if (nameValue == oomValue_) return;
    defineProperty(fn, "name", nameValue, false, false, true);
}

// —— 对象与属性（ADR-018）——

Value* Context::createObject(std::string_view bindName) {
    return createTracked(getObjectType(), gc_->allocData<ObjectData>(), bindName);
}

Value* Context::getProperty(Value* obj, Value* key) {
    return obj->type()->getProperty(this, obj, key);
}

Value* Context::setProperty(Value* obj, Value* key, Value* value) {
    return obj->type()->setProperty(this, obj, key, value);
}

bool Context::hasProperty(Value* obj, Value* key) {
    return obj->type()->hasProperty(this, obj, key);
}

Value* Context::deleteProperty(Value* obj, Value* key) {
    return obj->type()->deleteProperty(this, obj, key);
}

Value* Context::defineProperty(Value* obj, Value* key, Value* value,
                               bool writable, bool enumerable, bool configurable) {
    return obj->type()->defineProperty(this, obj, key, value, writable, enumerable, configurable);
}

Value* Context::getPrototype(Value* obj) {
    return obj->type()->getPrototype(this, obj);
}

Value* Context::setPrototype(Value* obj, Value* proto) {
    return obj->type()->setPrototype(this, obj, proto);
}

Value* Context::setClosure(Value* fn, std::string_view name) {
    // 仅对 Function 类型有效
    if (fn->type()->kind() != TypeKind::Function) {
        std::string msg = std::string(fn->type()->name()) + " is not a function";
        return createException(createString(msg));
    }
    // 沿当前作用域链查找闭包变量
    Value* v = current()->lookupChain(name);
    if (v == nullptr) {
        std::string msg = "Closure variable '" + std::string(name) + "' is not defined";
        return createException(createString(msg));
    }
    // 绑定 = 共享同一 Value（ADR-013 §2）：closureScope 加入时 scopeRef++
    auto* d = static_cast<FunctionData*>(fn->data());
    d->closureScope->define(name, v);
    return fn;
}

// —— 错误值创建（ADR-006 异常即值 + ADR-017 §4 Fatal）——

Value* Context::createException(Value* child) {
    return createTracked(getExceptionType(), gc_->allocData<ExceptionData>(child), {});
}

Value* Context::createFatal(const char* msg) {
    return createTracked(getFatalType(), gc_->allocData<FatalData>(msg), {});
}

// —— 变量定义与赋值（ADR-030 阶段 5）——

namespace {
inline bool isErrorValue(Value* v) {
    return v->type()->kind() == TypeKind::Exception ||
           v->type()->kind() == TypeKind::Fatal;
}
}  // namespace

// 为新 Value 接管而克隆 data（调用方随后 createValue/replaceMeta，会再计一次引用）。
// 简单类型 clone 返回新 data（ref=0），直接交给新 Value 计数即可。
// 复杂对象 clone 按 ADR-019 §2 共享 data 并已 ref++，与新 Value 的计数重复，
// 故此处抵消一次，保证 ref 数恒等于指向该 data 的 Value 数（否则 data 永不释放）。
IData* Context::cloneForNewValue(Value* source) {
    IData* cloned = source->data()->clone(alloc_);
    if (cloned == nullptr) return nullptr;
    if (cloned == source->data()) {
        cloned->refDec();
    }
    return cloned;
}

// clone source 的 data 到 scope 定义新槽位。失败返回 OOM 哨兵（ADR-031）。
Value* Context::defineCloned(Scope* scope, std::string_view name, Value* source) {
    IData* cloned = cloneForNewValue(source);
    if (cloned == nullptr) return oomValue_;
    Value* slot = gc_->createValue(source->type(), cloned);
    if (slot == nullptr) {
        gc_->releaseData(cloned);
        return oomValue_;
    }
    scope->define(name, slot);
    return slot;
}

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
    return defineCloned(current(), name, source);
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
        IData* cloned = cloneForNewValue(source);
        if (cloned == nullptr) return oomValue_;  // clone 失败，target 不变
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
    return defineCloned(global(), name, source);
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
