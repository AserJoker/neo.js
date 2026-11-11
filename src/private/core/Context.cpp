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
#include "core/ArrayBuiltins.h"
#include "core/AsyncGeneratorBuiltins.h"
#include "core/ErrorBuiltins.h"
#include "core/GeneratorBuiltins.h"
#include "core/IType.h"
#include "core/IteratorBuiltins.h"
#include "core/NativeCoroutine.h"
#include "core/ObjectBuiltins.h"
#include "core/PromiseBuiltins.h"
#include "core/PrimitiveWrapperBuiltins.h"
#include "core/SymbolBuiltins.h"
#include "core/TimerBuiltins.h"
#include "core/types/Array.h"
#include "core/types/AsyncGenerator.h"
#include "core/types/Boolean.h"
#include "core/types/Coroutine.h"
#include "core/types/Error.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Function.h"
#include "core/types/Interrupt.h"
#include "core/types/Iterator.h"
#include "core/types/ModuleNamespace.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/Promise.h"
#include "core/types/String.h"
#include "core/types/Symbol.h"
#include "core/types/TypeBehavior.h"
#include "core/types/Uninitialized.h"
#include "core/types/Undefined.h"

#include <algorithm>
#include <cstdlib>
#include <new>
#include <string>

namespace neo::core {

Context::Context(infra::IAllocator* alloc, infra::Console* console,
                 infra::IClock* clock)
    : alloc_(alloc), gc_(createGcEngine(alloc)), console_(console),
      clock_(clock != nullptr ? clock : infra::defaultClock()) {
    // 1. 创建 global scope（根，parent=nullptr）。
    //    所有权移交 GC（ADR-044）：GC 从它出发标记可达性，并在自身析构时销毁整棵树。
    void* gp = alloc_->alloc(sizeof(Scope));
    global_ = new (gp) Scope(nullptr, alloc_, this);
    gc_->setRootScope(global_);
    // 2. 创建 root scope（parent=global），作为 global 的子
    void* rp = alloc_->alloc(sizeof(Scope));
    root_ = new (rp) Scope(global_, alloc_, this);
    global_->addChild(root_);
    // 3. current 初始 = root；顶层函数环境也指向 root（this/new.target 由 VM 入口绑定）
    current_ = root_;
    currentFunctionEnv_ = root_;
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

    // 8. 建立内建原型链并注册 Object / Function 构造函数（ADR-035）。
    //    必须在其它内建之后：此前创建的对象/函数不挂链，之后自动挂。
    if (!registerObjectFunctionBuiltins(this)) {
        std::abort();
    }

    // 9. 注册 Array 构造函数与 Array.prototype（ADR-018 §6）。
    //    依赖 Object.prototype / Function.prototype 已就绪。
    if (!registerArrayBuiltins(this)) {
        std::abort();
    }

    // 10. 注册 Boolean/Number/String 包装类与四个包装原型（ES2024 §10.4）。
    //     依赖 Object.prototype / Function.prototype / Symbol.prototype 已就绪。
    if (!registerPrimitiveWrapperBuiltins(this)) {
        std::abort();
    }

    // 11. 注册 Iterator 抽象类与数组迭代器内建（ES2024 §23.1.5 / ES2025 §27.1）。
    //     依赖 Object.prototype / Function.prototype / Array.prototype 已就绪。
    if (!registerIteratorBuiltins(this)) {
        std::abort();
    }

    // 11b. 注册 %GeneratorPrototype%（ADR-045 §8）。
    //      依赖 %Iterator.prototype% 已就绪。
    if (!registerGeneratorBuiltins(this)) {
        std::abort();
    }

    // 12. 注册定时器（setTimeout/setInterval/clear*），事件循环的宿主侧入口。
    //     依赖 global scope 与事件循环（本类成员）已就绪。
    if (!registerTimerBuiltins(this)) {
        std::abort();
    }

    // 13. 注册 Error 基类（%Error.prototype% + 构造函数）。
    //     依赖 Object.prototype / Function.prototype 已就绪；stack 渲染依赖调用栈。
    if (!registerErrorBuiltins(this)) {
        std::abort();
    }

    // 14. 注册 Promise（%Promise.prototype% + 构造函数，ADR-043）。
    //     依赖 Object.prototype / Function.prototype 与事件循环（微任务队列）已就绪。
    if (!registerPromiseBuiltins(this)) {
        std::abort();
    }

    // 15. 注册异步生成器协议（%AsyncIteratorPrototype% / %AsyncGeneratorPrototype%）。
    //     依赖 Promise 内建（capability）与 Iterator 内建已就绪。
    if (!registerAsyncGeneratorBuiltins(this)) {
        std::abort();
    }
}

Context::~Context() {
    // 0a. 兜底上报仍待处理的未处理拒绝（ADR-043 M4），并归还它们持有的动态根。
    //     宿主可能没有排空事件循环就销毁 Context，此时未处理拒绝仍需可见。
    reportUnhandledRejections();
    // 0. 归还所有未执行任务持有的动态根（ADR-042）。任务实体不挂任何 scope，
    //    其 pinRef 由 Context 直接管理；队列里剩下的任务不会再出队，故在此统一
    //    pinRefDec，否则这些 Value 会被 GC 判为泄漏。
    releaseAllTaskRoots();
    // 0b. 清空模块表（Module 只借用作用域/导出对象，不拥有它们）。
    //     先于 GC 销毁，避免留下悬垂指针。
    modules_.clear();
    // 1. 销毁 GC（unique_ptr 析构）。global 作用域树的所有权已移交 GC（ADR-044）：
    //    ~MarkSweepGC 先销毁根作用域树，再 collect 回收全部残留 Value
    //    （此时根树已空，所有 Value 不可达），最后若有残留则 panic 报告泄漏。
    //    注：global_/root_/current_ 在此之后悬垂，但不再被访问。
    gc_.reset();
    // 2. alloc_/console_ 由外部销毁，Context 不持有所有权
}

Scope* Context::pushNewScope(Scope* parent) {
    void* p = alloc_->alloc(sizeof(Scope));
    if (p == nullptr) return nullptr;
    Scope* s = new (p) Scope(parent, alloc_, this);
    parent->addChild(s);
    current_ = s;
    return s;
}

Value* Context::pushWithScope(Value* obj, Scope** outScope) {
    // with 在严格模式是语法错误（ES2024 §14.11.2）。
    if (hasFlag("strict")) {
        return detail::errorOf(this, ErrorKind::SyntaxError,
                               "Strict mode code may not include a with statement");
    }
    // ToObject：null/undefined 抛 TypeError，原始值转包装对象，对象原样使用。
    TypeKind k = obj->type()->kind();
    if (k == TypeKind::Null || k == TypeKind::Undefined) {
        return detail::typeError(this, "Cannot convert undefined or null to object");
    }
    Value* target = obj;
    if (!detail::isObjectKind(k)) {
        target = createPrimitiveWrapper(obj);
        if (target == oomValue_) return target;
    }
    void* p = alloc_->alloc(sizeof(WithScope));
    if (p == nullptr) return oomValue_;
    auto* s = new (p) WithScope(current_, alloc_, this, target);
    current_->addChild(s);
    current_ = s;
    *outScope = s;
    return nullptr;
}

void Context::popScope() {
    // root/global 不可 pop（模块根与全局是持久作用域）
    if (current_ == root_ || current_ == global_) return;
    // current 的 parent 即调用栈下一层
    Scope* parent = current_->parent();
    if (parent == nullptr) return;
    Scope* top = current_;
    current_ = parent;
    // 仍被 Environment 闭包 pin 住：延迟销毁（孤儿），保留 parent_ 查找链。
    // 必须从作用域树摘除：否则其 values 中的函数对象会经 global 树永久可达，
    // 闭包永远不回收。逃逸闭包由外部持有，GC 经 FunctionData 标记环境链。
    if (top->captureRef() > 0) {
        top->markAutoDispose();
        top->detachFromParentTree();
        return;
    }
    // 销毁弹出的 scope：虚析构 + 递归释放子树 + 从父摘除自己
    top->destroySelf();
}

// —— Value 创建便捷方法 ——
// 统一模式：allocData<T> → createValue(type, data) → current()->add(v) 跟踪。
// 可选 name：非空则 current()->bind(name, v) 命名绑定。
// OOM：allocData/createValue 失败时返回预分配 OOM 哨兵（ADR-031），不返回 nullptr。

namespace {

// 把函数的 closureScope 挂到定义点环境链（Environment 模式）：
// lookup-only 挂载（不登记 children，所有权仍归 FunctionData），并沿 env → root
// 逐层 pin（captureRef++）。单调性：子 scope 被 pin 则所有祖先都被 pin。
void linkEnvironmentCapture(FunctionData* d, Scope* env) {
    if (d->capturesEnvironment || d->closureScope == nullptr || env == nullptr) return;
    d->closureScope->linkLookupParent(env);
    for (Scope* s = env; s != nullptr; s = s->parent()) s->incCaptureRef();
    d->capturesEnvironment = true;
}

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

Value* Context::createUninitialized() {
    return createTracked(getUninitializedType(), gc_->allocData<UninitializedData>(), {});
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
// 故登记时挂到 global scope（从 global 树可达）。
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
    return construct(fn, fn, argc, argv);
}

Value* Context::construct(Value* fn, Value* newTarget, size_t argc, Value* argv[]) {
    // new.target 是构造期的 Context 级动态值：保存/恢复，嵌套构造不串。
    Value* saved = newTarget_;
    newTarget_ = newTarget != nullptr ? newTarget : fn;
    Value* r = fn->type()->construct(this, fn, argc, argv);
    // 构造返回的对象归属调用方作用域：class 派生构造的 this 由 super() 在
    // 内层 execScope 创建，若只返回裸指针，外层 execScope pop 后会被 GC 回收。
    if (r != nullptr && detail::isObjectKind(r->type()->kind()) && current_ != nullptr) {
        current_->add(r);
    }
    newTarget_ = saved;
    return r;
}

Value* Context::constructSuper(Value* superCtor, Value* newTarget, size_t argc,
                               Value* argv[]) {
    if (superCtor == nullptr || superCtor->type()->kind() == TypeKind::Null ||
        superCtor->type()->kind() == TypeKind::Undefined) {
        return detail::typeError(this, "Super constructor null is not a constructor");
    }
    Scope* env = currentFunctionEnv_;
    if (env != nullptr) {
        Value* current = env->thisValue();
        if (current != nullptr && current->type()->kind() != TypeKind::Uninitialized) {
            return detail::referenceError(this, "Super constructor may only be called once");
        }
    }
    Value* obj = construct(superCtor, newTarget, argc, argv);
    if (detail::isErrorValue(obj)) return obj;
    if (!detail::isObjectKind(obj->type()->kind())) {
        return detail::typeError(this, "Super constructor must return an object");
    }
    if (env != nullptr) env->setThis(obj);
    return obj;
}

Value* Context::getThis() {
    Scope* env = currentFunctionEnv_;
    Value* v = env != nullptr ? env->lookupThis() : nullptr;
    if (v == nullptr || v->type()->kind() == TypeKind::Uninitialized) {
        return detail::referenceError(
            this, "Must call super constructor before accessing 'this'");
    }
    return v;
}

void Context::initializeThis(Value* value) {
    if (currentFunctionEnv_ != nullptr) currentFunctionEnv_->setThis(value);
}

bool Context::isThisInitialized() {
    Scope* env = currentFunctionEnv_;
    Value* v = env != nullptr ? env->lookupThis() : nullptr;
    return v != nullptr && v->type()->kind() != TypeKind::Uninitialized;
}

Value* Context::getSuperProperty(Value* homeObject, Value* key, Value* receiver) {
    Value* base = getPrototype(homeObject);
    if (base == nullptr || base->type()->kind() == TypeKind::Null ||
        base->type()->kind() == TypeKind::Undefined) {
        return detail::typeError(this, "Cannot read properties of null (reading 'super')");
    }
    return getPropertyWithReceiver(this, base, key, receiver);
}

Value* Context::setSuperProperty(Value* homeObject, Value* key, Value* value,
                                 Value* receiver) {
    Value* base = getPrototype(homeObject);
    if (base == nullptr || base->type()->kind() == TypeKind::Null ||
        base->type()->kind() == TypeKind::Undefined) {
        return detail::typeError(this, "Cannot set properties of null (setting 'super')");
    }
    return setPropertyWithReceiver(this, base, key, value, receiver);
}

namespace {

// class 构造器 → ClassData；非 class 返回 nullptr。
ClassData* classDataOf(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::Function) return nullptr;
    auto* d = static_cast<FunctionData*>(v->data());
    return d->isClassConstructor ? static_cast<ClassData*>(d) : nullptr;
}

// 对象的私有槽表；非对象返回 nullptr。所有对象负载都继承 ObjectData，
// 因此普通对象、函数、内建 exotic 都可承载私有槽（ADR-047 §5）。
PrivateSlots* privateSlotsOf(Value* v) {
    if (v == nullptr || !detail::isObjectKind(v->type()->kind())) return nullptr;
    return &static_cast<ObjectData*>(v->data())->slots;
}

PrivateSlot* findPrivateSlot(PrivateSlots* slots, PrivateName* name) {
    if (slots == nullptr || name == nullptr) return nullptr;
    for (PrivateSlot& s : *slots) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

Value* privateMemberError(IContext* ctx, PrivateName* name, const char* what) {
    std::string msg = "Cannot ";
    msg += what;
    msg += " private member #";
    msg += name != nullptr ? name->description : std::string();
    return detail::typeError(ctx, msg);
}

}  // namespace

void Context::addInstanceField(Value* classCtor, Value* key, Value* initializer) {
    ClassData* cd = classDataOf(classCtor);
    if (cd == nullptr) return;
    InstanceElement e;
    e.kind = InstanceElement::Kind::PublicField;
    e.key = key;
    e.initializer = initializer;
    cd->instanceElements.push_back(e);
}

void Context::addClassInitializer(Value* classCtor, Value* initializer) {
    ClassData* cd = classDataOf(classCtor);
    if (cd == nullptr) return;
    cd->classInitializers.push_back(initializer);
}

Value* Context::initializeInstanceElements(Value* instance, Value* classCtor) {
    if (instance == nullptr || !detail::isObjectKind(instance->type()->kind())) {
        return detail::typeError(this, "instance elements initialization on non-object");
    }
    ClassData* cd = classDataOf(classCtor);
    if (cd == nullptr) {
        return detail::typeError(this, "instance elements initialization on non-class");
    }
    PrivateSlots* slots = privateSlotsOf(instance);
    if (slots == nullptr) {
        return detail::typeError(this, "instance elements initialization on non-object");
    }

    // 1. 先给私有方法/访问器加 brand（ES2024 §10.2.1.2），与字段声明顺序无关。
    for (const PrivateElement& e : cd->privateElements) {
        if (findPrivateSlot(slots, e.name) != nullptr) continue;
        ::neo::core::retainPrivateName(e.name);
        slots->push_back({e.name, nullptr});
    }

    // 2. public/private 字段按源码声明顺序交错初始化。
    for (const InstanceElement& elem : cd->instanceElements) {
        Value* value = createUndefined();
        if (elem.initializer != nullptr) {
            value = call(elem.initializer, instance, 0, nullptr);
            if (detail::isErrorValue(value)) return value;
        }
        if (elem.kind == InstanceElement::Kind::PublicField) {
            Value* r = defineProperty(instance, elem.key, value, true, true, true);
            if (detail::isErrorValue(r)) return r;
            for (Value* extra : elem.extraInitializers) {
                Value* er = call(extra, instance, 0, nullptr);
                if (detail::isErrorValue(er)) return er;
            }
        } else {
            if (PrivateSlot* existing = findPrivateSlot(slots, elem.name)) {
                existing->value = value;
            } else {
                ::neo::core::retainPrivateName(elem.name);
                slots->push_back({elem.name, value});
            }
        }
    }

    // 3. 装饰器 addInitializer 回调在字段之后执行。
    for (Value* init : cd->classInitializers) {
        Value* r = call(init, instance, 0, nullptr);
        if (detail::isErrorValue(r)) return r;
    }
    return createUndefined();
}

// —— 私有字段/方法/访问器（ADR-047 §5）——

PrivateName* Context::createPrivateName(std::string_view description) {
    return ::neo::core::createPrivateName(description);
}

void Context::retainPrivateName(PrivateName* name) {
    ::neo::core::retainPrivateName(name);
}

void Context::releasePrivateName(PrivateName* name) {
    ::neo::core::releasePrivateName(name);
}

void Context::addPrivateField(Value* classCtor, PrivateName* name, Value* initializer) {
    ClassData* cd = classDataOf(classCtor);
    if (cd == nullptr || name == nullptr) return;
    ::neo::core::retainPrivateName(name);
    InstanceElement e;
    e.kind = InstanceElement::Kind::PrivateField;
    e.name = name;
    e.initializer = initializer;
    cd->instanceElements.push_back(e);
}

void Context::addStaticPrivateField(Value* classCtor, PrivateName* name,
                                    Value* initializer) {
    ClassData* cd = classDataOf(classCtor);
    if (cd == nullptr || name == nullptr) return;
    ::neo::core::retainPrivateName(name);
    cd->staticPrivateFields.push_back({name, initializer});
}

void Context::addPrivateMethod(Value* classCtor, PrivateName* name, Value* method) {
    ClassData* cd = classDataOf(classCtor);
    if (cd == nullptr || name == nullptr) return;
    ::neo::core::retainPrivateName(name);
    PrivateElement e;
    e.name = name;
    e.kind = PrivateElement::Kind::Method;
    e.method = method;
    cd->privateElements.push_back(e);
}

void Context::addPrivateAccessor(Value* classCtor, PrivateName* name, Value* getter,
                                 Value* setter) {
    ClassData* cd = classDataOf(classCtor);
    if (cd == nullptr || name == nullptr) return;
    ::neo::core::retainPrivateName(name);
    PrivateElement e;
    e.name = name;
    e.kind = PrivateElement::Kind::Accessor;
    e.getter = getter;
    e.setter = setter;
    cd->privateElements.push_back(e);
}

void Context::addStaticPrivateMethod(Value* classCtor, PrivateName* name, Value* method) {
    ClassData* cd = classDataOf(classCtor);
    if (cd == nullptr || name == nullptr) return;
    ::neo::core::retainPrivateName(name);
    PrivateElement e;
    e.name = name;
    e.kind = PrivateElement::Kind::Method;
    e.method = method;
    cd->staticPrivateElements.push_back(e);
    // 静态私有方法在声明位置即挂 brand，后续静态字段初始化器可访问；
    // 声明之前访问则 brand 尚不存在，符合求值顺序。
    if (findPrivateSlot(&cd->slots, name) == nullptr) {
        ::neo::core::retainPrivateName(name);
        cd->slots.push_back({name, nullptr});
    }
}

void Context::addStaticPrivateAccessor(Value* classCtor, PrivateName* name, Value* getter,
                                       Value* setter) {
    ClassData* cd = classDataOf(classCtor);
    if (cd == nullptr || name == nullptr) return;
    ::neo::core::retainPrivateName(name);
    PrivateElement e;
    e.name = name;
    e.kind = PrivateElement::Kind::Accessor;
    e.getter = getter;
    e.setter = setter;
    cd->staticPrivateElements.push_back(e);
    if (findPrivateSlot(&cd->slots, name) == nullptr) {
        ::neo::core::retainPrivateName(name);
        cd->slots.push_back({name, nullptr});
    }
}

Value* Context::initializeStaticPrivateField(Value* classCtor, PrivateName* name) {
    ClassData* cd = classDataOf(classCtor);
    if (cd == nullptr) {
        return detail::typeError(this, "static private field initialization on non-class");
    }
    const PrivateFieldDef* def = nullptr;
    for (const PrivateFieldDef& f : cd->staticPrivateFields) {
        if (f.name == name) {
            def = &f;
            break;
        }
    }
    if (def == nullptr) {
        return privateMemberError(this, name, "initialize");
    }
    Value* value = createUndefined();
    if (def->initializer != nullptr) {
        value = call(def->initializer, classCtor, 0, nullptr);
        if (detail::isErrorValue(value)) return value;
    }
    if (PrivateSlot* existing = findPrivateSlot(&cd->slots, name)) {
        existing->value = value;
    } else {
        ::neo::core::retainPrivateName(name);
        cd->slots.push_back({name, value});
    }
    return createUndefined();
}

Value* Context::initializeStaticPrivateElements(Value* classCtor) {
    ClassData* cd = classDataOf(classCtor);
    if (cd == nullptr) {
        return detail::typeError(this, "static private elements initialization on non-class");
    }
    // 静态方法/访问器的 brand 已在 add* 时挂上；这里兜底处理手工构造的 ClassData。
    for (const PrivateElement& e : cd->staticPrivateElements) {
        if (findPrivateSlot(&cd->slots, e.name) != nullptr) continue;
        ::neo::core::retainPrivateName(e.name);
        cd->slots.push_back({e.name, nullptr});
    }
    for (const PrivateFieldDef& f : cd->staticPrivateFields) {
        PrivateSlot* slot = findPrivateSlot(&cd->slots, f.name);
        if (slot != nullptr && slot->value != nullptr) continue;  // 已按位置初始化
        Value* r = initializeStaticPrivateField(classCtor, f.name);
        if (detail::isErrorValue(r)) return r;
    }
    return createUndefined();
}

Value* Context::getPrivate(Value* obj, PrivateName* name) {
    PrivateSlot* slot = findPrivateSlot(privateSlotsOf(obj), name);
    if (slot == nullptr) return privateMemberError(this, name, "read");
    if (slot->value == nullptr) {
        return privateMemberError(this, name, "read non-field");
    }
    return slot->value;
}

Value* Context::setPrivate(Value* obj, PrivateName* name, Value* value) {
    PrivateSlot* slot = findPrivateSlot(privateSlotsOf(obj), name);
    if (slot == nullptr) return privateMemberError(this, name, "write");
    if (slot->value == nullptr) {
        return privateMemberError(this, name, "write non-field");
    }
    slot->value = value;
    return createUndefined();
}

Value* Context::privateIn(Value* obj, PrivateName* name) {
    if (obj == nullptr || !detail::isObjectKind(obj->type()->kind())) {
        return detail::typeError(
            this, "Cannot use 'in' operator to search for a private name in a non-object");
    }
    return createBoolean(hasPrivate(obj, name));
}

bool Context::hasPrivate(Value* obj, PrivateName* name) {
    return findPrivateSlot(privateSlotsOf(obj), name) != nullptr;
}

namespace {

const PrivateElement* findPrivateElement(const std::vector<PrivateElement>& elements,
                                         PrivateName* name) {
    for (const PrivateElement& e : elements) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

Value* privateElementAccess(IContext* ctx, Value* obj, PrivateName* name,
                            const PrivateElement* e, int which) {
    std::string desc = name != nullptr ? name->description : std::string();
    if (e == nullptr) {
        return detail::typeError(ctx, "Private member #" + desc + " is not defined");
    }
    if (!ctx->hasPrivate(obj, name)) {
        return privateMemberError(ctx, name, "read");
    }
    if (which == 0) {
        if (e->kind != PrivateElement::Kind::Method) {
            return detail::typeError(ctx,
                                     "Private member #" + desc + " is not a method");
        }
        return e->method != nullptr ? e->method : ctx->createUndefined();
    }
    if (e->kind != PrivateElement::Kind::Accessor) {
        return detail::typeError(ctx,
                                 "Private member #" + desc + " is not an accessor");
    }
    Value* v = which == 1 ? e->getter : e->setter;
    if (v == nullptr) {
        return detail::typeError(
            ctx, "Private accessor #" + desc +
                     (which == 1 ? " has no getter" : " has no setter"));
    }
    return v;
}

}  // namespace

namespace {

// obj == classCtor 表示静态私有访问（this 是类构造器本身）。
const PrivateElement* lookupPrivateElement(ClassData* cd, Value* classCtor, Value* obj,
                                           PrivateName* name) {
    if (cd == nullptr) return nullptr;
    const auto& elements =
        obj == classCtor ? cd->staticPrivateElements : cd->privateElements;
    return findPrivateElement(elements, name);
}

}  // namespace

Value* Context::getPrivateMethod(Value* classCtor, Value* obj, PrivateName* name) {
    ClassData* cd = classDataOf(classCtor);
    return privateElementAccess(this, obj, name,
                                lookupPrivateElement(cd, classCtor, obj, name), 0);
}

Value* Context::getPrivateGetter(Value* classCtor, Value* obj, PrivateName* name) {
    ClassData* cd = classDataOf(classCtor);
    return privateElementAccess(this, obj, name,
                                lookupPrivateElement(cd, classCtor, obj, name), 1);
}

Value* Context::getPrivateSetter(Value* classCtor, Value* obj, PrivateName* name) {
    ClassData* cd = classDataOf(classCtor);
    return privateElementAccess(this, obj, name,
                                lookupPrivateElement(cd, classCtor, obj, name), 2);
}

void Context::setFunctionClassConstructor(Value* fn, bool isDerived, Value* superCtor) {
    if (fn == nullptr || fn->type()->kind() != TypeKind::Function) return;
    auto* d = static_cast<FunctionData*>(fn->data());
    // 只有创建时用 FunctionCreateOptions.isClassConstructor 分配了 ClassData 才可更新。
    if (!d->isClassConstructor) return;
    auto* cd = static_cast<ClassData*>(d);
    cd->isDerived = isDerived;
    cd->superConstructor = superCtor;
}

Value* Context::instanceOf(Value* value, Value* target) {
    return detail::instanceOf(this, value, target);
}

Value* Context::createNativeFunction(NativeFn fn, size_t minArgc, std::string_view bindName,
                                     const FunctionCreateOptions& opts) {
    // closureScope 是**孤立**作用域（parent=nullptr），Sema 收集的穿透闭包变量绑定于此。
    // 不挂到 root 下：否则它常驻为 GC 根，函数对象不可达后其捕获值也不释放（ADR-036）。
    // 生命周期随 FunctionData（data ref 归 0 时析构销毁）；调用期间临时挂到 rootScope。
    void* p = alloc_->alloc(sizeof(Scope));
    if (p == nullptr) return oomValue_;
    Scope* closure = new (p) Scope(nullptr, alloc_, this);
    // rootScope 借用当前 root（不拥有）。class 构造器用 ClassData 承载额外内部槽。
    FunctionData* d = nullptr;
    if (opts.isClassConstructor) {
        d = gc_->allocData<ClassData>(closure, root_, fn, minArgc);
    } else {
        d = gc_->allocData<FunctionData>(closure, root_, fn, minArgc);
    }
    if (d == nullptr) {
        // data 分配失败：闭包作用域已无所有者，显式销毁（否则泄漏）
        closure->destroySelf();
        return oomValue_;
    }
    d->constructible = opts.constructible;
    d->isClassConstructor = opts.isClassConstructor;
    d->homeObject = opts.homeObject;
    d->source = opts.source;
    retainSourceText(d->source.source);
    if (opts.isClassConstructor) {
        auto* cd = static_cast<ClassData*>(d);
        cd->isDerived = opts.isDerived;
        cd->superConstructor = opts.superConstructor;
    }
    if (opts.strict) closure->setFlag("strict");
    // 定义点处于 dynamic 作用域（直接 eval 沿链继承）时，函数必须捕获运行时
    // 环境链而非静态 slot：closureScope 挂到 current 并 pin 整条链。
    if (current_ != nullptr && current_->dynamicChain()) {
        linkEnvironmentCapture(d, current_);
    }

    // bindName 仅用于变量绑定（与函数名无关）
    Value* fnValue = createTracked(getFunctionType(), d, bindName);
    if (fnValue == oomValue_) return fnValue;

    // 函数对象的原型是 Function.prototype（ADR-035）。bootstrap 期间尚未建立，跳过。
    if (functionPrototype_ != nullptr) {
        static_cast<ObjectData*>(fnValue->data())->prototype = functionPrototype_;
    }

    // —— 函数名作为 "name" 属性（ES2024 §20.2.4.2）——
    // writable=false / enumerable=false / configurable=true；缺省 "<anonymous>"，
    // 由 setFunctionName 更新（对应 VM 的 setname 字节码）。
    Value* anonName = createString("<anonymous>");
    if (anonName == oomValue_) return anonName;
    if (defineProperty(fnValue, "name", anonName, false, false, true)->type()->kind() ==
        TypeKind::Exception) {
        return oomValue_;
    }

    // —— 函数 "length" 属性（ES2024 §20.2.4.1）：形参个数 ——
    // writable=false / enumerable=false / configurable=true。
    // native 函数用 minArgc 作为形参个数。
    Value* lengthValue = createNumber(static_cast<double>(minArgc));
    if (lengthValue == oomValue_) return lengthValue;
    if (defineProperty(fnValue, "length", lengthValue, false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return oomValue_;
    }

    // —— 自动创建 prototype（ADR-034）——
    // 每个可构造函数创建时生成一个普通对象作为 fn.prototype，并在其上定义 constructor
    // 指回函数，构成 JS 的「构造函数 ↔ 原型」双向引用。两者均不可枚举（与 ES 一致）。
    // class 方法/箭头函数等 hasPrototypeProperty=false：不创建 prototype 属性。
    // 存活：fn 的属性 value 被 ObjectData::enumerateChildren 遍历 → proto 保活；
    //       proto 的 constructor 属性同理保活 fn。
    if (opts.hasPrototypeProperty) {
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
    }
    return fnValue;
}

void Context::setFunctionConstructor(Value* fn, NativeFn ctor) {
    // 仅对 Function 类型有效（内建注册用），其他类型忽略
    if (fn->type()->kind() != TypeKind::Function) return;
    static_cast<FunctionData*>(fn->data())->constructFn = ctor;
}

void Context::setFunctionConstructible(Value* fn, bool constructible) {
    // 仅对 Function 类型有效，其他类型忽略
    if (fn->type()->kind() != TypeKind::Function) return;
    static_cast<FunctionData*>(fn->data())->constructible = constructible;
}

void Context::setFunctionGenerator(Value* fn, bool generator) {
    // 仅对 Function 类型有效，其他类型忽略
    if (fn->type()->kind() != TypeKind::Function) return;
    auto* d = static_cast<FunctionData*>(fn->data());
    d->generator = generator;
    if (!generator) return;
    // 生成器函数不可构造（ES2024 §27.5.1）
    d->constructible = false;
    // fn.prototype 的 [[Prototype]] 改为 %GeneratorPrototype%
    // （ES2024 §27.5.1.2）。bootstrap 期间尚未建立时跳过。
    if (generatorPrototype_ != nullptr) {
        Value* proto = getProperty(fn, "prototype");
        if (detail::isObjectKind(proto->type()->kind())) {
            (void)setPrototype(proto, generatorPrototype_);
        }
    }
}

void Context::setFunctionAsyncGenerator(Value* fn, bool asyncGenerator) {
    // 仅对 Function 类型有效，其他类型忽略
    if (fn->type()->kind() != TypeKind::Function) return;
    auto* d = static_cast<FunctionData*>(fn->data());
    d->asyncGenerator = asyncGenerator;
    if (!asyncGenerator) return;
    // 异步生成器同时是生成器函数，且不可构造（ES2024 §27.6）
    d->generator = true;
    d->constructible = false;
    // fn.prototype 的 [[Prototype]] 改为 %AsyncGeneratorPrototype%
    if (asyncGeneratorPrototype_ != nullptr) {
        Value* proto = getProperty(fn, "prototype");
        if (detail::isObjectKind(proto->type()->kind())) {
            (void)setPrototype(proto, asyncGeneratorPrototype_);
        }
    }
}

void Context::setFunctionName(Value* fn, std::string_view name) {
    // 仅对 Function 类型有效（setname 字节码），其他类型忽略
    if (fn->type()->kind() != TypeKind::Function) return;
    // 函数名是 "name" 属性（writable=false），用 defineProperty 直接覆盖描述符值
    Value* nameValue = createString(name);
    if (nameValue == oomValue_) return;
    defineProperty(fn, "name", nameValue, false, false, true);
}

void Context::setFunctionStrict(Value* fn) {
    // 仅对 Function 类型有效，其他类型忽略。
    if (fn->type()->kind() != TypeKind::Function) return;
    // closureScope 在调用期间挂入查找链，标注后函数体内 hasFlag("strict") 为 true，
    // 且平时孤立，不会泄漏给调用方。
    auto* d = static_cast<FunctionData*>(fn->data());
    if (d->closureScope != nullptr) d->closureScope->setFlag("strict");
}

void Context::setFunctionCapturesEnvironment(Value* fn) {
    if (fn->type()->kind() != TypeKind::Function) return;
    auto* d = static_cast<FunctionData*>(fn->data());
    linkEnvironmentCapture(d, current_);
}

void Context::setFunctionHomeObject(Value* fn, Value* homeObject) {
    if (fn->type()->kind() != TypeKind::Function) return;
    static_cast<FunctionData*>(fn->data())->homeObject = homeObject;
}

void Context::setFunctionSource(Value* fn, const SourceSlice& slice) {
    if (fn->type()->kind() != TypeKind::Function) return;
    auto* d = static_cast<FunctionData*>(fn->data());
    if (d->source.source == slice.source) {
        // 同一 SourceText：只更新切片，不改变引用计数。
        d->source = slice;
        return;
    }
    releaseSourceText(d->source.source);
    d->source = slice;
    retainSourceText(d->source.source);
}

Value* Context::createBoundFunction(Value* target, Value* boundThis, size_t argc,
                                    Value* argv[]) {
    // 绑定函数（ES2024 §10.4.1.3）：新建 FunctionData，记录原函数/this/前置实参。
    // native 用占位（调用时由 FunctionType::call 的 boundTarget 分支接管）。
    // 闭包作用域同样孤立（ADR-036），生命周期随 FunctionData。
    void* p = alloc_->alloc(sizeof(Scope));
    if (p == nullptr) return oomValue_;
    Scope* closure = new (p) Scope(nullptr, alloc_, this);
    auto* d = gc_->allocData<FunctionData>(closure, root_, nullptr, size_t{0});
    if (d == nullptr) {
        closure->destroySelf();
        return oomValue_;
    }
    d->boundTarget = target;
    d->boundThis = boundThis != nullptr ? boundThis : createUndefined();
    if (argc > 0) d->boundArgs.assign(argv, argv + argc);
    // §10.4.1.3 step 2：绑定函数继承 target 的 [[Construct]] 能力
    if (target->type()->kind() == TypeKind::Function) {
        d->constructible = static_cast<FunctionData*>(target->data())->constructible;
    } else {
        d->constructible = false;
    }

    Value* fnValue = createTracked(getFunctionType(), d, {});
    if (fnValue == oomValue_) return fnValue;
    if (functionPrototype_ != nullptr) {
        static_cast<ObjectData*>(fnValue->data())->prototype = functionPrototype_;
    }
    // 绑定函数的 name = "bound " + target.name（ES2024 §10.4.1.3 step 6）
    Value* targetName = getProperty(target, "name");
    std::string name = "bound ";
    if (targetName->type()->kind() == TypeKind::String) {
        name += static_cast<StringData*>(targetName->data())->value;
    }
    Value* nameValue = createString(name);
    if (nameValue == oomValue_) return nameValue;
    defineProperty(fnValue, "name", nameValue, false, false, true);
    // 绑定函数的 length = max(0, target.length - boundArgs.size())（ES2024 §10.4.1.3）
    Value* targetLength = getProperty(target, "length");
    double len = 0.0;
    if (targetLength->type()->kind() == TypeKind::Number) {
        len = static_cast<NumberData*>(targetLength->data())->value -
              static_cast<double>(argc);
        if (len < 0.0) len = 0.0;
    }
    Value* lengthValue = createNumber(len);
    if (lengthValue == oomValue_) return lengthValue;
    defineProperty(fnValue, "length", lengthValue, false, false, true);
    // 绑定函数无 prototype 属性（不可 new）
    return fnValue;
}

// —— 对象与属性（ADR-018）——

Value* Context::createObject(std::string_view bindName) {
    Value* obj = createTracked(getObjectType(), gc_->allocData<ObjectData>(), bindName);
    // 自动挂 Object.prototype（ADR-035 完整原型链）。
    // bootstrap 期间 objectPrototype_ 尚未建立，此时为 nullptr，跳过。
    if (obj != oomValue_ && objectPrototype_ != nullptr) {
        static_cast<ObjectData*>(obj->data())->prototype = objectPrototype_;
    }
    return obj;
}

Value* Context::createModuleNamespace(std::string_view bindName) {
    // 模块命名空间：对象布局 + 原型为 null（ObjectData::prototype 默认 nullptr）。
    Value* ns = createTracked(getModuleNamespaceType(), gc_->allocData<ModuleNamespaceData>(),
                              bindName);
    if (ns == oomValue_) return ns;
    // @@toStringTag = "Module"（不可写/不可枚举/可配置）
    Value* tag = createString("Module");
    if (tag == oomValue_) return tag;
    Value* r = defineProperty(ns, getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                              tag, false, false, true);
    return r->type()->kind() == TypeKind::Exception ? r : ns;
}

Value* Context::createArray(std::string_view bindName) {
    Value* arr = createTracked(getArrayType(), gc_->allocData<ArrayData>(), bindName);
    // 自动挂 Array.prototype（ADR-018 §6）。bootstrap 期间尚未建立，跳过。
    if (arr != oomValue_ && arrayPrototype_ != nullptr) {
        static_cast<ObjectData*>(arr->data())->prototype = arrayPrototype_;
    }
    return arr;
}

Value* Context::createPrimitiveWrapper(Value* primitive, std::string_view bindName) {
    // [[PrimitiveValue]] 必须是原始值之一（Boolean/Number/String/Symbol）。
    // 调用方契约保证，非法类型返回 Exception（对外 API 边界校验）。
    Value* proto = nullptr;
    switch (primitive->type()->kind()) {
        case TypeKind::Boolean: proto = booleanPrototype_; break;
        case TypeKind::Number:  proto = numberPrototype_; break;
        case TypeKind::String:  proto = stringPrototype_; break;
        case TypeKind::Symbol:  proto = symbolPrototype_; break;
        default:
            return detail::typeError(this, "Primitive wrapper requires a primitive value");
    }
    auto* d = gc_->allocData<PrimitiveWrapperData>();
    if (d == nullptr) return oomValue_;
    d->primitiveValue = primitive;
    Value* wrapper = createTracked(getPrimitiveWrapperType(), d, bindName);
    // 自动挂对应原型（bootstrap 期间尚未建立，跳过）
    if (wrapper != oomValue_ && proto != nullptr) {
        static_cast<ObjectData*>(wrapper->data())->prototype = proto;
    }
    return wrapper;
}

Value* Context::createArrayIterator(Value* iteratedObject, ArrayIterationKind kind,
                                    std::string_view bindName) {
    auto* d = gc_->allocData<ArrayIteratorData>();
    if (d == nullptr) return oomValue_;
    d->iteratedObject = iteratedObject;
    d->nextIndex = 0;
    d->kind = kind;
    Value* iter = createTracked(getArrayIteratorType(), d, bindName);
    // 自动挂 %ArrayIteratorPrototype%（bootstrap 期间尚未建立，跳过）
    if (iter != oomValue_ && arrayIteratorPrototype_ != nullptr) {
        static_cast<ObjectData*>(iter->data())->prototype = arrayIteratorPrototype_;
    }
    return iter;
}

Value* Context::createIteratorWrapper(Value* iterated, Value* nextMethod,
                                      std::string_view bindName) {
    auto* d = gc_->allocData<IteratorWrapperData>();
    if (d == nullptr) return oomValue_;
    d->iterated = iterated;
    d->nextMethod = nextMethod;
    Value* wrapper = createTracked(getIteratorWrapperType(), d, bindName);
    // 自动挂 %WrapForValidIteratorPrototype%（bootstrap 期间尚未建立，跳过）
    if (wrapper != oomValue_ && iteratorWrapperPrototype_ != nullptr) {
        static_cast<ObjectData*>(wrapper->data())->prototype = iteratorWrapperPrototype_;
    }
    return wrapper;
}

// —— Error 对象（ADR-038）——

namespace {

// 把 SourcePosition 渲染成 "filename:line:column"；未知位置返回空串。
std::string renderPosition(const SourcePosition& pos) {
    if (pos.filename.empty()) return {};
    if (pos.line == 0) return pos.filename;
    if (pos.column == 0) return pos.filename + ":" + std::to_string(pos.line);
    return pos.filename + ":" + std::to_string(pos.line) + ":" +
           std::to_string(pos.column);
}

// 渲染 stack：首行 "Name: message"（message 空则只有 Name），
// 随后每帧一行 "    at name (position)"（position 未知则只有 name）。
// 帧按最近调用在前（自顶向下）输出，与 V8 一致。
std::string renderStack(const std::vector<CallFrame>& frames, std::string_view name,
                        std::string_view message) {
    std::string out(name);
    if (!message.empty()) {
        out += ": ";
        out += message;
    }
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
        out += "\n    at ";
        out += it->name.empty() ? "<anonymous>" : it->name;
        std::string pos = renderPosition(it->position);
        if (!pos.empty()) {
            out += " (";
            out += pos;
            out += ")";
        }
    }
    return out;
}

}  // namespace

Value* Context::createError(std::string_view message, std::string_view name,
                            const SourcePosition& position, std::string_view bindName) {
    auto* d = gc_->allocData<ErrorData>();
    if (d == nullptr) return oomValue_;
    Value* err = createTracked(getErrorType(), d, bindName);
    if (err == oomValue_) return err;

    // 原型：%Error.prototype%（bootstrap 期间尚未建立，跳过）
    if (errorPrototype_ != nullptr) {
        static_cast<ObjectData*>(err->data())->prototype = errorPrototype_;
    }

    // message：非空才定义（ES2024 §20.5.1.1 step 3：undefined 时不建属性）
    if (!message.empty()) {
        Value* msg = createString(message);
        if (msg == oomValue_) return msg;
        if (defineProperty(err, "message", msg, true, false, true)->type()->kind() ==
            TypeKind::Exception) {
            return oomValue_;
        }
    }

    // stack：渲染调用栈快照（writable / 不可枚举 / 可配置）。
    // 用 trace(position) 而非直接读 callStack_：栈顶帧的位置（出错点）只有调用方
    // 知道，trace 会 clone 并用 position 补全栈顶帧，且不改动 Context 的栈。
    Value* stack = createString(renderStack(trace(position), name, message));
    if (stack == oomValue_) return stack;
    if (defineProperty(err, "stack", stack, true, false, true)->type()->kind() ==
        TypeKind::Exception) {
        return oomValue_;
    }
    return err;
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

Value* Context::defineAccessor(Value* obj, Value* key, Value* getter, Value* setter,
                               bool enumerable, bool configurable) {
    return obj->type()->defineAccessor(this, obj, key, getter, setter, enumerable,
                                       configurable);
}

Value* Context::getPrototype(Value* obj) {
    return obj->type()->getPrototype(this, obj);
}

Value* Context::setPrototype(Value* obj, Value* proto) {
    return obj->type()->setPrototype(this, obj, proto);
}

Value* Context::ownPropertyKeys(Value* obj, bool enumerableOnly, bool includeSymbols) {
    std::vector<Value*> keys =
        neo::core::ownPropertyKeys(this, obj, enumerableOnly, includeSymbols);
    Value* arr = createArray();
    if (arr == oomValue_) return arr;
    auto* d = static_cast<ArrayData*>(arr->data());
    for (size_t i = 0; i < keys.size(); ++i) {
        d->setElement(static_cast<uint32_t>(i), keys[i]);
    }
    return arr;
}

Value* Context::ownEnumerableStringKeys(Value* obj) {
    return ownPropertyKeys(obj, true, false);
}

Value* Context::setClosure(Value* fn, std::string_view name) {
    // 仅对 Function 类型有效
    if (fn->type()->kind() != TypeKind::Function) {
        std::string msg = std::string(fn->type()->name()) + " is not a function";
        return detail::typeError(this, msg);
    }
    auto* d = static_cast<FunctionData*>(fn->data());
    // Environment 模式：名字运行时沿定义点环境链查找，不做静态 slot 捕获。
    // 否则静态捕获会遮蔽 eval 之后注入的绑定（under-capture）。
    if (d->capturesEnvironment) return fn;
    // with 属性：复制对象环境记录（withObject + key），而非当前值。
    // 属性后续被 setProperty 重新赋值时，闭包仍能读到新值。
    Value* withObject = nullptr;
    Value* withKey = nullptr;
    if (current()->getWithBindingChain(name, withObject, withKey)) {
        (void)d->closureScope->defineWith(name, withObject, withKey);
        return fn;
    }
    // 导入绑定：复制间接绑定而非当前解析值，闭包在导出晚到时仍能实时解析。
    if (current()->bindingKindChain(name) == Scope::BindingKind::Indirect) {
        // 已捕获同名绑定视为幂等（与直接绑定的 define 语义一致）。
        (void)current()->copyIndirectBindingTo(d->closureScope, name);
        return fn;
    }
    // 沿当前作用域链查找闭包变量
    Value* v = current()->lookupChain(name);
    if (v == nullptr) {
        std::string msg = "Closure variable '" + std::string(name) + "' is not defined";
        return detail::referenceError(this, msg);
    }
    if (detail::isErrorValue(v)) return v;
    // 绑定 = 共享同一 Value（ADR-013 §2）：closureScope 登记同一 Value
    d->closureScope->define(name, v);
    return fn;
}

Value* Context::captureValue(Value* fn, std::string_view name, Value* value) {
    if (fn->type()->kind() != TypeKind::Function) {
        return detail::typeError(this, "captureValue target is not a function");
    }
    auto* d = static_cast<FunctionData*>(fn->data());
    // 直接注入闭包作用域（不查作用域链）。重复注入同名视为内部错误。
    if (!d->closureScope->define(name, value)) {
        return detail::typeError(this, "captureValue: duplicate name '" +
                                           std::string(name) + "'");
    }
    return fn;
}

// —— Promise（ADR-043）——

Value* Context::createPromise(std::string_view bindName) {
    auto* d = gc_->allocData<PromiseData>();
    if (d == nullptr) return oomValue_;
    // [[PromiseResult]] 在 Pending 期间为 undefined
    d->result = createUndefined();
    if (d->result == oomValue_) return d->result;
    Value* p = createTracked(getPromiseType(), d, bindName);
    if (p == oomValue_) return p;
    if (promisePrototype_ != nullptr) {
        static_cast<ObjectData*>(p->data())->prototype = promisePrototype_;
    }
    return p;
}

// —— 控制流信号（ADR-046）——

Value* Context::createInterrupt(InterruptKind kind, uint64_t offset, Value* value) {
    // 无载荷时用 undefined（value 契约上永不为 nullptr）
    Value* payload = value != nullptr ? value : createUndefined();
    if (payload == oomValue_) return payload;
    auto* d = gc_->allocData<InterruptData>(kind, offset, payload);
    if (d == nullptr) return oomValue_;
    return createTracked(getInterruptType(), d, {});
}

// —— 协程（ADR-045）——

Value* Context::createCoroutine(Value* target, Value* receiver, size_t argc, Value* argv[],
                                std::string_view bindName) {
    if (target == nullptr || target->type()->kind() != TypeKind::Function) {
        return detail::typeError(this, "coroutine target is not a function");
    }
    auto* fd = static_cast<FunctionData*>(target->data());
    // 绑定函数没有自己的 native 函数体，不能作为协程 target
    if (fd->boundTarget != nullptr || fd->native == nullptr) {
        return detail::typeError(this, "coroutine target is not resumable");
    }

    auto* d = gc_->allocData<CoroutineData>();
    if (d == nullptr) return oomValue_;
    d->state = CoroutineState::Suspended;
    d->resumePoint = 0;
    d->target = target;
    d->receiver = receiver != nullptr ? receiver : createUndefined();
    if (d->receiver == oomValue_) return d->receiver;
    d->yielded = createUndefined();
    if (d->yielded == oomValue_) return d->yielded;
    d->sentValue = createUndefined();
    if (d->sentValue == oomValue_) return d->sentValue;
    if (argc > 0) d->initialArgs.assign(argv, argv + argc);
    // 恢复驱动按 target 种类选择（未来 VM 函数 → resumeVmCoroutine）
    d->resume = resumeNativeCoroutine;

    Value* co = createTracked(getCoroutineType(), d, bindName);
    if (co == oomValue_) return co;
    // 实例原型取 target.prototype（OrdinaryCreateFromConstructor 语义）：
    // 生成器函数的 fn.prototype 已挂到 %GeneratorPrototype% 下；
    // 取不到对象原型时回退 Object.prototype。
    Value* proto = getProperty(target, "prototype");
    if (detail::isObjectKind(proto->type()->kind())) {
        static_cast<ObjectData*>(co->data())->prototype = proto;
    } else if (objectPrototype_ != nullptr) {
        static_cast<ObjectData*>(co->data())->prototype = objectPrototype_;
    }
    return co;
}

Value* Context::createAsyncGenerator(Value* target, Value* receiver, size_t argc,
                                     Value* argv[], std::string_view bindName) {
    if (target == nullptr || target->type()->kind() != TypeKind::Function) {
        return detail::typeError(this, "async generator target is not a function");
    }
    auto* fd = static_cast<FunctionData*>(target->data());
    if (fd->boundTarget != nullptr || fd->native == nullptr) {
        return detail::typeError(this, "async generator target is not resumable");
    }

    auto* d = gc_->allocData<AsyncGeneratorData>();
    if (d == nullptr) return oomValue_;
    d->state = CoroutineState::Suspended;
    d->asyncState = AsyncGeneratorState::SuspendedStart;
    d->resumePoint = 0;
    d->target = target;
    d->receiver = receiver != nullptr ? receiver : createUndefined();
    if (d->receiver == oomValue_) return d->receiver;
    d->yielded = createUndefined();
    if (d->yielded == oomValue_) return d->yielded;
    d->sentValue = createUndefined();
    if (d->sentValue == oomValue_) return d->sentValue;
    if (argc > 0) d->initialArgs.assign(argv, argv + argc);
    d->resume = resumeNativeCoroutine;

    Value* gen = createTracked(getAsyncGeneratorType(), d, bindName);
    if (gen == oomValue_) return gen;
    Value* proto = getProperty(target, "prototype");
    if (detail::isObjectKind(proto->type()->kind())) {
        static_cast<ObjectData*>(gen->data())->prototype = proto;
    } else if (objectPrototype_ != nullptr) {
        static_cast<ObjectData*>(gen->data())->prototype = objectPrototype_;
    }
    return gen;
}

Value* Context::resumeCoroutine(Value* coroutine, Value* sentValue) {
    auto* co = asCoroutine(coroutine);
    if (co == nullptr) {
        return detail::typeError(this, "resume target is not a coroutine");
    }
    if (co->state == CoroutineState::Running) {
        return detail::typeError(this, "coroutine is already running");
    }
    if (co->state == CoroutineState::Completed) {
        return createUndefined();
    }
    if (co->resume == nullptr) {
        return detail::typeError(this, "coroutine has no resume driver");
    }

    Value* sent = sentValue != nullptr ? sentValue : createUndefined();
    if (sent == oomValue_) return sent;
    co->sentValue = sent;

    // 协程对象在恢复期间必须存活：它持有孤立的 execScope，而 execScope 只能
    // 经协程对象在标记时递归进来（ADR-044）。栈本身不是 GC 根，故显式钉住。
    coroutine->pinRefInc();
    coroutineStack_.push_back(coroutine);
    CoroutineState prev = co->state;
    co->state = CoroutineState::Running;

    Value* res = co->resume(this, coroutine, sent);

    // runner 正常会写回 Suspended/Completed；若中途异常返回，恢复原状态以便重试。
    if (co->state == CoroutineState::Running) co->state = prev;
    coroutineStack_.pop_back();
    coroutine->pinRefDec();
    return res;
}

// —— 错误值创建（ADR-006 异常即值 + ADR-017 §4 Fatal）——

Value* Context::createException(Value* child) {
    return createTracked(getExceptionType(), gc_->allocData<ExceptionData>(child), {});
}

Value* Context::createFatal(const char* msg) {
    return createTracked(getFatalType(), gc_->allocData<FatalData>(msg), {});
}

// 规范错误异常（ADR-039）：创建对应 Error 对象并包进 Exception 通道。
// 子类实例的原型改挂到 global 上对应构造函数（bootstrap 期间可能尚未就绪，
// 此时保留 createError 挂的 %Error.prototype%，不影响错误传播）。
Value* Context::createErrorException(ErrorKind kind, std::string_view message,
                                     const SourcePosition& position) {
    const char* name = errorKindName(kind);
    Value* err = createError(message, name, position);
    if (err == oomValue_) return err;

    if (kind != ErrorKind::Error) {
        Value* ctor = global()->lookup(name);
        if (ctor != nullptr) {
            Value* proto = getProperty(ctor, "prototype");
            if (proto->type()->kind() != TypeKind::Exception) {
                Value* r = setPrototype(err, proto);
                if (r->type()->kind() == TypeKind::Exception) return r;
            }
        }
    }
    return createException(err);
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
    // 用 bindingKind 而非 lookup：with 对象属性不是本作用域的声明，不应阻止定义。
    if (current()->bindingKind(name) != Scope::BindingKind::None) {
        std::string msg = "Identifier '" + std::string(name) + "' has already been declared";
        return detail::errorOf(this, ErrorKind::SyntaxError, msg);
    }
    return defineCloned(current(), name, source);
}

Value* Context::store(std::string_view name, Value* source) {
    // Exception/Fatal 永远不出现在左右值，提前透传
    if (isErrorValue(source)) return source;

    // 沿作用域链查找：with 对象优先（对象环境记录），其次直接/间接绑定。
    for (Scope* s = current(); s != nullptr; s = s->parent()) {
        // with 对象属性写入：必须走 setProperty（访问器/原型/只读语义），
        // 不能像普通槽位那样原地 replaceMeta。
        Value* result = nullptr;
        Scope::StoreOutcome outcome = s->store(this, name, source, result);
        if (outcome == Scope::StoreOutcome::Handled) return result;
        if (outcome == Scope::StoreOutcome::Error) return result;

        switch (s->bindingKind(name)) {
            case Scope::BindingKind::Indirect:
                // 导入绑定只读：必须在解析前拦截，否则会写穿到导出方槽位。
                // 放在运行时而非 Sema，是因为 eval/with 等动态路径绕过静态分析。
                return detail::typeError(
                    this, "Assignment to import binding '" + std::string(name) + "'");
            case Scope::BindingKind::Direct: {
                Value* target = s->lookup(name);
                if (target == nullptr) break;  // 内部契约：Direct 必有槽位
                // 不可赋值（const 声明）：拦截并报错
                if (!target->isMutable()) {
                    return detail::typeError(
                        this, "Assignment to constant variable '" + std::string(name) + "'");
                }
                // 找到：clone source 的 data，替换 target 的 type+data
                IData* cloned = cloneForNewValue(source);
                if (cloned == nullptr) return oomValue_;  // clone 失败，target 不变
                IData* old = target->replaceMeta(source->type(), cloned);
                // 旧 data 已从 Value 摘除，collect 遍历不到，需显式回收（ref 归 0 才释放）
                gc_->releaseData(old);
                return target;
            }
            case Scope::BindingKind::With:
                // 已在 s->store 中处理（闭包捕获的 with 属性）。
                break;
            case Scope::BindingKind::None:
                break;
        }
    }
    // 未找到：按当前作用域链的 strict 标注分派
    if (hasFlag("strict")) {
        std::string msg = "Assignment to undeclared identifier '" + std::string(name) + "'";
        return detail::referenceError(this, msg);
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

// —— 调用栈（纯展示信息，ADR-037）——

void Context::pushFrame(const SourcePosition& callSite, std::string_view fnName) {
    // 一次 push 跨两帧：调用点写进调用者帧，再压入新帧。
    // 栈空时是最外层调用，没有「调用者」，只压入新帧。
    if (!callStack_.empty()) {
        callStack_.back().position = callSite;
    }
    callStack_.push_back(CallFrame(std::string(fnName), SourcePosition{}));
}

std::vector<CallFrame> Context::trace(const SourcePosition& callSite) const {
    // clone 一份，用出错位置补全栈顶帧（栈顶帧的位置平时是未知的）
    std::vector<CallFrame> snapshot = callStack_;
    if (!snapshot.empty()) {
        snapshot.back().position = callSite;
    }
    return snapshot;
}

// —— 事件循环（非阻塞调度）——

// 任务实体作为动态 GC 根（ADR-042/044）：**不挂任何 scope**，由 Context 直接
// 增减 pinRef。这样队列里的任务不会因为挂到 global 而在运行期长期保活，
// Context 析构时也能精确地一次性归还全部根（见 ~Context）。
void Context::retainTaskRoot(Value* entity) {
    size_t& refs = taskRootRefs_[entity];
    if (refs == 0) entity->pinRefInc();  // 首个任务：成为 GC 根
    ++refs;
}

void Context::releaseTaskRoot(Value* entity) {
    auto it = taskRootRefs_.find(entity);
    if (it == taskRootRefs_.end()) return;
    if (--it->second == 0) {
        taskRootRefs_.erase(it);
        entity->pinRefDec();  // 最后一个任务已出队/取消：解除 GC 根
    }
}

void Context::releaseAllTaskRoots() {
    // Context 析构专用：队列里未执行的任务不再有机会出队，必须在这里统一归还
    // 它们持有的 pinRef，否则 GC 最终 collect 会把这些 Value 判为泄漏。
    for (auto& [entity, refs] : taskRootRefs_) {
        for (size_t i = 0; i < refs; ++i) entity->pinRefDec();
    }
    taskRootRefs_.clear();
    microtasks_.clear();
    macrotasks_.clear();
}

void Context::insertOrdered(const Task& task) {
    QueuedTask qt{task, nextSeq_++};
    // 按 (due, seq) 升序：due 相同则保持入队顺序（稳定）
    auto pos = std::lower_bound(
        macrotasks_.begin(), macrotasks_.end(), qt,
        [](const QueuedTask& a, const QueuedTask& b) {
            if (a.task.due() != b.task.due()) return a.task.due() < b.task.due();
            return a.seq < b.seq;
        });
    macrotasks_.insert(pos, qt);
}

uint64_t Context::enqueue(Value* entity, uint64_t timeout, bool loop, TaskRunner run) {
    if (entity == nullptr) return 0;
    Task task;
    task.id = nextTaskId_++;
    task.start = clock_->nowMs();
    task.timeout = timeout;
    task.loop = loop;
    task.entity = entity;
    task.run = run != nullptr ? run : defaultTaskRunner;
    retainTaskRoot(entity);
    insertOrdered(task);
    return task.id;
}

uint64_t Context::postMicrotask(Value* entity, TaskRunner run) {
    if (entity == nullptr) return 0;
    Task task;
    task.id = nextTaskId_++;
    task.start = clock_->nowMs();
    task.timeout = 0;  // 微任务无延迟
    task.loop = false; // 微任务不循环
    task.entity = entity;
    task.run = run != nullptr ? run : defaultTaskRunner;
    retainTaskRoot(entity);
    microtasks_.push_back(QueuedTask{task, nextSeq_++});  // FIFO
    return task.id;
}

uint64_t Context::postTask(Value* entity, uint64_t timeout, TaskRunner run) {
    return enqueue(entity, timeout, false, run);
}

uint64_t Context::postLoopTask(Value* entity, uint64_t timeout, TaskRunner run) {
    return enqueue(entity, timeout, true, run);
}

bool Context::eraseTask(uint64_t id) {
    for (auto* queue : {&microtasks_, &macrotasks_}) {
        for (auto it = queue->begin(); it != queue->end(); ++it) {
            if (it->task.id != id) continue;
            Value* entity = it->task.entity;
            queue->erase(it);
            releaseTaskRoot(entity);
            return true;
        }
    }
    return false;
}

bool Context::cancelTask(uint64_t id) {
    return eraseTask(id);
}

bool Context::hasRunnableTask() const {
    // 微任务永远优先：只要微任务队列非空即可运行
    if (!microtasks_.empty()) return true;
    return !macrotasks_.empty() && macrotasks_.front().task.due() <= clock_->nowMs();
}

Value* Context::executeTask(const Task& task) {
    Value* result = task.run(this, task.entity);
    bool fatal = result != nullptr && result->type()->kind() == TypeKind::Fatal;

    // 循环任务：以当前时刻为起点重新入队（Fatal 时终止循环，不再入队）
    if (task.loop && !fatal) {
        Task next = task;
        next.start = clock_->nowMs();
        retainTaskRoot(next.entity);
        insertOrdered(next);
    }

    // 释放本次任务持有的根引用（循环任务已重新持有，计数不减到 0）
    releaseTaskRoot(task.entity);

    // Fatal 透传（ADR-017 §4）
    if (fatal) return result;
    return result != nullptr ? result : createUndefined();
}

Value* Context::runOneTask() {
    // 微任务优先：每次只取一个，排空微任务后才轮到宏任务。
    // 注意：这里**不**做未处理拒绝检查点。检查点是宿主的 microtask checkpoint
    // 职责（§27.2.1.9），时机应是「宏任务结束 / 脚本结束」，而不是「每排空
    // 一批微任务」——后者会过早上报那些计划在下一个宏任务里 catch 的拒绝。
    // 宿主在事件循环空闲时调用 reportUnhandledRejections()；~Context 兜底一次。
    if (!microtasks_.empty()) {
        Task task = microtasks_.front().task;
        microtasks_.erase(microtasks_.begin());
        return executeTask(task);
    }
    if (!macrotasks_.empty() && macrotasks_.front().task.due() <= clock_->nowMs()) {
        Task task = macrotasks_.front().task;
        macrotasks_.erase(macrotasks_.begin());
        return executeTask(task);
    }
    return createUndefined();
}

// —— 未处理拒绝上报（ES2024 §27.2.1.9，ADR-043 M4）——

void Context::hostPromiseRejectionTracker(Value* promise, bool handled) {
    if (promise == nullptr) return;
    auto it = pendingRejections_.find(promise);
    if (!handled) {
        // 拒绝且尚未被接住 → 登记为动态根，等待上报
        if (it == pendingRejections_.end()) {
            pendingRejections_.emplace(promise, true);
            promise->pinRefInc();
        }
        return;
    }
    // 已被接住 → 移出待上报集合并归还根引用
    if (it != pendingRejections_.end()) {
        pendingRejections_.erase(it);
        promise->pinRefDec();
    }
}

void Context::reportUnhandledRejections() {
    if (pendingRejections_.empty()) return;
    for (auto& [promise, pending] : pendingRejections_) {
        (void)pending;
        // 上报前再确认一次：登记之后可能已有 then/catch 接住（tracker 会移出集合，
        // 但同一轮内若先登记后接住又被重新登记，这里仍以 isHandled 为准）。
        PromiseData* d = asPromise(promise);
        if (d != nullptr && d->isHandled) {
            promise->pinRefDec();
            continue;
        }
        std::string reason = d != nullptr ? detail::describeValue(this, d->result)
                                          : "undefined";
        console_->printLine("Unhandled promise rejection: {}", reason);
        promise->pinRefDec();
    }
    pendingRejections_.clear();
}

}  // namespace neo::core

std::unique_ptr<neo::core::IContext> neo::core::createContext(
    infra::IAllocator* alloc, infra::Console* console, infra::IClock* clock) {
    return std::make_unique<Context>(alloc, console, clock);
}
