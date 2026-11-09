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
#include "core/ErrorBuiltins.h"
#include "core/IType.h"
#include "core/IteratorBuiltins.h"
#include "core/ObjectBuiltins.h"
#include "core/PromiseBuiltins.h"
#include "core/PrimitiveWrapperBuiltins.h"
#include "core/SymbolBuiltins.h"
#include "core/TimerBuiltins.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Error.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Function.h"
#include "core/types/Iterator.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/Promise.h"
#include "core/types/String.h"
#include "core/types/Symbol.h"
#include "core/types/TypeBehavior.h"
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
}

Context::~Context() {
    // 0. 归还所有未执行任务持有的动态根（ADR-042）。任务实体不挂任何 scope，
    //    其 scopeRef 由 Context 直接管理；队列里剩下的任务不会再出队，故在此统一
    //    scopeRefDec，否则这些 Value 会被 GC 判为泄漏。
    releaseAllTaskRoots();
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

Value* Context::instanceOf(Value* value, Value* target) {
    return detail::instanceOf(this, value, target);
}

Value* Context::createNativeFunction(NativeFn fn, size_t minArgc, std::string_view bindName) {
    // closureScope 是**孤立**作用域（parent=nullptr），Sema 收集的穿透闭包变量绑定于此。
    // 不挂到 root 下：否则它常驻为 GC 根，函数对象不可达后其捕获值也不释放（ADR-036）。
    // 生命周期随 FunctionData（data ref 归 0 时析构销毁）；调用期间临时挂到 rootScope。
    void* p = alloc_->alloc(sizeof(Scope));
    if (p == nullptr) return oomValue_;
    Scope* closure = new (p) Scope(nullptr, alloc_);
    // rootScope 借用当前 root（不拥有）
    auto* d = gc_->allocData<FunctionData>(closure, root_, fn, minArgc);
    if (d == nullptr) {
        // data 分配失败：闭包作用域已无所有者，显式销毁（否则泄漏）
        closure->destroySelf();
        return oomValue_;
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

void Context::setFunctionConstructor(Value* fn, NativeFn ctor) {
    // 仅对 Function 类型有效（内建注册用），其他类型忽略
    if (fn->type()->kind() != TypeKind::Function) return;
    static_cast<FunctionData*>(fn->data())->constructFn = ctor;
}

void Context::setFunctionName(Value* fn, std::string_view name) {
    // 仅对 Function 类型有效（setname 字节码），其他类型忽略
    if (fn->type()->kind() != TypeKind::Function) return;
    // 函数名是 "name" 属性（writable=false），用 defineProperty 直接覆盖描述符值
    Value* nameValue = createString(name);
    if (nameValue == oomValue_) return;
    defineProperty(fn, "name", nameValue, false, false, true);
}

Value* Context::createBoundFunction(Value* target, Value* boundThis, size_t argc,
                                    Value* argv[]) {
    // 绑定函数（ES2024 §10.4.1.3）：新建 FunctionData，记录原函数/this/前置实参。
    // native 用占位（调用时由 FunctionType::call 的 boundTarget 分支接管）。
    // 闭包作用域同样孤立（ADR-036），生命周期随 FunctionData。
    void* p = alloc_->alloc(sizeof(Scope));
    if (p == nullptr) return oomValue_;
    Scope* closure = new (p) Scope(nullptr, alloc_);
    auto* d = gc_->allocData<FunctionData>(closure, root_, nullptr, size_t{0});
    if (d == nullptr) {
        closure->destroySelf();
        return oomValue_;
    }
    d->boundTarget = target;
    d->boundThis = boundThis != nullptr ? boundThis : createUndefined();
    if (argc > 0) d->boundArgs.assign(argv, argv + argc);

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
    // 沿当前作用域链查找闭包变量
    Value* v = current()->lookupChain(name);
    if (v == nullptr) {
        std::string msg = "Closure variable '" + std::string(name) + "' is not defined";
        return detail::referenceError(this, msg);
    }
    // 绑定 = 共享同一 Value（ADR-013 §2）：closureScope 加入时 scopeRef++
    auto* d = static_cast<FunctionData*>(fn->data());
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
    if (current()->lookup(name) != nullptr) {
        std::string msg = "Identifier '" + std::string(name) + "' has already been declared";
        return detail::errorOf(this, ErrorKind::SyntaxError, msg);
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
            return detail::typeError(this, msg);
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

// 任务实体作为动态 GC 根（ADR-042）：**不挂任何 scope**，由 Context 直接
// 增减 scopeRef。这样队列里的任务不会因为挂到 global 而在运行期长期保活，
// Context 析构时也能精确地一次性归还全部根（见 ~Context）。
void Context::retainTaskRoot(Value* entity) {
    size_t& refs = taskRootRefs_[entity];
    if (refs == 0) entity->scopeRefInc();  // 首个任务：成为 GC 根
    ++refs;
}

void Context::releaseTaskRoot(Value* entity) {
    auto it = taskRootRefs_.find(entity);
    if (it == taskRootRefs_.end()) return;
    if (--it->second == 0) {
        taskRootRefs_.erase(it);
        entity->scopeRefDec();  // 最后一个任务已出队/取消：解除 GC 根
    }
}

void Context::releaseAllTaskRoots() {
    // Context 析构专用：队列里未执行的任务不再有机会出队，必须在这里统一归还
    // 它们持有的 scopeRef，否则 GC 最终 collect 会把这些 Value 判为泄漏。
    for (auto& [entity, refs] : taskRootRefs_) {
        for (size_t i = 0; i < refs; ++i) entity->scopeRefDec();
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
    // 微任务优先：每次只取一个，排空微任务后才轮到宏任务
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

}  // namespace neo::core

std::unique_ptr<neo::core::IContext> neo::core::createContext(
    infra::IAllocator* alloc, infra::Console* console, infra::IClock* clock) {
    return std::make_unique<Context>(alloc, console, clock);
}
