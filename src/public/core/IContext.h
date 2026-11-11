// IContext.h - Context 门面（引擎入口，整合 GC/IAllocator/Console + Scope 树）
//
// IContext 是引擎对外门面（ADR-011 §2/§3）：持有 GC 所有权（内部创建并销毁），
// 注入 IAllocator 和 Console（无所有权，由外部销毁），持有 global/root/current
// 三层 scope（ADR-012 §1）。
//
// GC 由 Context 持有所有权：Context 用注入的 IAllocator 创建 GC（createGcEngine(alloc)），
// 析构时由 Context 销毁 GC。IAllocator 和 Console 是注入资源，Context 不持有所有权。
//
// 三层 scope（ADR-012 + ADR-030 阶段 4）：
//   global —— 根作用域（parent=nullptr，永久，Context 存活期间一直在）
//   root   —— 模块根作用域（parent=global，Context 存活期间一直在）
//   current —— 当前作用域（Scope 的 parent 链即作用域栈，popScope 回到 parent），初始=root
//
// 调用栈（ADR-037）是**纯展示信息**，与 Scope 链无关：Context 另持一个 CallFrame 数组，
// 供错误报告/调试读取。变量解析仍只走 Scope 的 parent 链。
//
// 析构顺序：先销毁 global_scope（~Scope 递归销毁整棵 scope 树：current 栈残留 → root → global），
// 再销毁 GC（GC 析构时 collect 回收残留 Value，若有泄漏则 panic），
// IAllocator/Console 由外部销毁（Context 不持有所有权）。
//
// 接口-实现-工厂（ADR-010）：IContext 抽象接口在 public/core/，Context 在 private/core/，
// 工厂 createContext(alloc, console) 返回接口指针。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面 + 内存归属）、012（Scope 树）、030 阶段 4

#ifndef NEO_CORE_ICONTEXT_H
#define NEO_CORE_ICONTEXT_H

#include "core/CallStack.h"
#include "core/ErrorKind.h"
#include "core/InterruptKind.h"
#include "core/IteratorKind.h"
#include "core/Module.h"
#include "core/OperatorKind.h"
#include "core/SourceText.h"
#include "core/Task.h"
#include "core/WellKnownSymbol.h"
#include "infra/IAllocator.h"
#include "infra/Console.h"
#include "infra/IClock.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace neo::core {

// 前向声明
class IGCEngine;
class Scope;
class Value;

// native 函数指针签名（ADR-005 §3）：ctx=语言上下文，self=this 绑定，
// argc/argv=参数，返回 Value*（正常/异常同一通道）。
class IContext;
using NativeFn = Value* (*)(IContext* ctx, Value* self, size_t argc, Value* argv[]);

// 函数创建选项（ADR-047）：class 构造器/方法、无 prototype 的方法、strict 等。
// 默认值保持普通 native 函数行为。
struct FunctionCreateOptions {
    bool constructible = true;
    bool hasPrototypeProperty = true;
    bool strict = false;
    bool isClassConstructor = false;
    bool isDerived = false;
    Value* homeObject = nullptr;       // [[HomeObject]]（借用，GC 由函数遍历）
    Value* superConstructor = nullptr; // 派生类父构造器（借用，GC 由 ClassData 遍历）
    SourceSlice source{};              // 函数源码切片（创建时 retain）
};

// 引擎门面：持有 GC 所有权 + 注入 IAllocator/Console + Scope 树
class IContext {
public:
    virtual ~IContext() = default;

    // —— 资源访问 ——
    // allocator/console: 注入资源（无所有权，由外部销毁）
    // gc: Context 持有所有权（内部创建并销毁）
    virtual infra::IAllocator* allocator() const = 0;
    virtual IGCEngine* gc() const = 0;
    virtual infra::Console* console() const = 0;

    // —— 三层 scope 访问 ——
    virtual Scope* global() const = 0;
    virtual Scope* root() const = 0;
    virtual Scope* current() const = 0;

    // —— scope 切换（ADR-022 函数调用）——
    // Scope 的 parent 链本身就是作用域栈，Context 只需保存 root/current 两个指针，无须额外栈。
    // setRootScope: 切换当前模块根作用域（函数调用时切到被调函数的 rootScope）。
    // setCurrentScope: 切换当前作用域（函数调用时切到被调函数的临时执行作用域）。
    // 调用方负责保存/恢复。
    virtual void setRootScope(Scope* s) = 0;
    virtual void setCurrentScope(Scope* s) = 0;

    // currentFunctionEnv: 当前正在执行的函数环境记录（ADR-047）。
    // this / new.target 存放在该 ArgumentScope 的专用槽中；invoke 在调用期间
    // 设置，返回/Fatal/挂起时恢复调用方的函数环境。顶层为模块/全局环境。
    virtual Scope* currentFunctionEnv() const = 0;
    virtual void setCurrentFunctionEnv(Scope* s) = 0;

    // —— current scope 管理 ——
    // pushNewScope: 在 allocator 上新建 parent 的子 scope 并设为 current。
    // 用于函数调用的临时执行作用域（ADR-022）。返回新建的 scope，OOM 返回 nullptr。
    virtual Scope* pushNewScope(Scope* parent) = 0;

    // pushWithScope: 以 obj 为 with 对象创建 WithScope 并设为 current。
    // obj 先 ToObject（null/undefined -> TypeError；原始值 -> 包装对象）。
    // 严格模式禁止 with（SyntaxError）。成功返回 nullptr 并写 outScope；
    // 失败返回 Exception/Fatal（outScope 不变）。
    virtual Value* pushWithScope(Value* obj, Scope** outScope) = 0;

    // popScope: 销毁 current scope 并回到其 parent（~Scope 递归释放子树 + allocator->free）。
    // global/root 不可 pop（parent 为 global 或为 nullptr 时无操作）。
    virtual void popScope() = 0;

    // —— 调用栈（纯展示信息，ADR-037）——
    // 变量解析走 Scope 的 parent 链，调用栈不参与语义，仅用于错误报告/调试。
    // push/pop 由调用点自行维护（VM 函数入口、native 包装），引擎不自动插入。
    //
    // 帧语义是「在某个函数的某处」，而调用点参数是「在某处调用某函数」，
    // 故 push 跨两帧：
    //   pushFrame(callSite, fnName)：先把 callSite 写入**当前栈顶帧**（调用者的位置），
    //   再压入只带 fnName 的新帧（新帧位置待其自身再调用或出错时确定）。
    //   栈空时（最外层调用）只有压入新帧这一步。
    // popFrame：弹出栈顶帧；栈空时无操作。
    // trace(callSite)：返回栈的副本，并用 callSite 补全**栈顶帧**的位置
    //   （出错点/挂起点），不改动 Context 的栈。
    // callStack：当前调用栈快照（自底向上，栈顶在末尾）。
    // setCallStack：整体替换——异常跨栈传播、async 恢复、生成器挂起时，
    //   由持有者保存/恢复自己的那份栈（ADR-023）。
    //
    // 位置用 SourcePosition（filename + line + column）而非预拼接字符串，便于调试器
    // 做断点匹配/源码跳转。callSite 传 {} 表示位置未知。
    virtual void pushFrame(const SourcePosition& callSite, std::string_view fnName) = 0;
    virtual void popFrame() = 0;
    virtual std::vector<CallFrame> trace(const SourcePosition& callSite) const = 0;
    virtual const std::vector<CallFrame>& callStack() const = 0;
    virtual void setCallStack(std::vector<CallFrame> stack) = 0;

    // —— Value 创建便捷接口（ADR-030 阶段 4）——
    // 内部封装 gc->allocData<T> + gc->createValue(type, data)，把 Value 加入 current scope
    // 登记到 current scope（从 global 树可达）。可选 name：指定则同时 bind（命名绑定）。
    // 返回 Value*：正常结果，或内存耗尽时返回 OOM 哨兵（ADR-031，非 nullptr）。
    virtual Value* createUndefined(std::string_view name = {}) = 0;
    virtual Value* createNull(std::string_view name = {}) = 0;
    virtual Value* createBoolean(bool b, std::string_view name = {}) = 0;
    virtual Value* createNumber(double n, std::string_view name = {}) = 0;
    virtual Value* createString(std::string_view s, std::string_view name = {}) = 0;

    // createSymbol: 创建 symbol 原始值（ES2024 §20.4.1.1）。
    // description 可空；每次调用分配独立的 data，故同描述的两个 symbol 不相等
    // （唯一性 = data 身份，不可变类型按 ADR-019 §4 共享 data）。
    // 返回 Value*：正常 symbol，或 OOM 哨兵（ADR-031）。
    virtual Value* createSymbol(std::string_view description = {},
                                std::string_view name = {}) = 0;

    // getWellKnownSymbol: 获取 well-known symbol（ES2024 §6.1.5.1）。
    // Context 创建时一次性生成全部 15 个，生命周期与 Context 一致，始终返回同一 Value。
    virtual Value* getWellKnownSymbol(WellKnownSymbol which) const = 0;

    // —— Symbol 注册表（ES2024 §20.4.2.1/§20.4.2.6）——
    // symbolFor: Symbol.for(key) —— 按字符串 key 查全局注册表，命中返回已有 symbol，
    //            否则创建新 symbol（description=key）并登记。同 key 永远返回同一 symbol。
    // symbolKeyFor: Symbol.keyFor(sym) —— 返回注册表中该 symbol 的 key；
    //               sym 不是 symbol 或未登记 → undefined。
    virtual Value* symbolFor(std::string_view key) = 0;
    virtual Value* symbolKeyFor(Value* sym) = 0;

    // —— 运算代理（ADR-016 §4/§7）——
    // 纯代理：ctx->add(a,b) → a->type()->add(this, a, b)。
    // 外部代码不直接调 type->op()，一律走 Context。
    virtual Value* add(Value* a, Value* b) = 0;
    virtual Value* sub(Value* a, Value* b) = 0;
    virtual Value* mul(Value* a, Value* b) = 0;
    virtual Value* div(Value* a, Value* b) = 0;
    virtual Value* toString(Value* self) = 0;

    // —— 转换原语（ES2024 §7.1）——
    // VM 的表达式求值/跳转直接调用这些入口，不自行编排转换链。
    // toBoolean 永不失败；其余返回 Value*（正常值或 Exception/Fatal）。
    virtual bool toBoolean(Value* v) = 0;
    virtual Value* toNumber(Value* v) = 0;
    virtual Value* toPrimitive(Value* v,
                               ToPrimitiveHint hint = ToPrimitiveHint::Default) = 0;
    // ToPropertyKey：Symbol 原样返回，其余 ToString。
    virtual Value* toPropertyKey(Value* v) = 0;

    // —— 运算符原语（ES2024 §7.2 / §13）——
    // 二元/一元运算符的完整 JS 语义（转换 + 计算 + 错误通道）。
    // 逻辑短路（&&/||/??）与赋值由 VM 用跳转/既有 store 组合，不在此列。
    virtual Value* binaryOp(BinaryOperator op, Value* a, Value* b) = 0;
    virtual Value* unaryOp(UnaryOperator op, Value* v) = 0;

    // —— 函数调用（ADR-022）——
    // call: 纯代理 ctx->call(fn, thisArg, argc, argv) → fn->type()->call(this, fn, thisArg, argc, argv)。
    //       非函数类型返回 Exception("xxx is not a function")。
    virtual Value* call(Value* fn, Value* thisArg, size_t argc, Value* argv[]) = 0;

    // construct: new 操作入口（ADR-034），纯代理
    //   ctx->construct(fn, argc, argv) → fn->type()->construct(this, fn, argc, argv)。
    // 新建 this 对象（原型 = fn.prototype）后以它为 thisArg 调函数；
    // 函数返回 Object/Function 则用它，否则用 this。非函数返回 Exception。
    virtual Value* construct(Value* fn, size_t argc, Value* argv[]) = 0;

    // construct(fn, newTarget, ...)：带 new.target 的构造入口（ADR-047）。
    // newTarget 为 nullptr 时取 fn；Reflect.construct / super() 可传不同值。
    virtual Value* construct(Value* fn, Value* newTarget, size_t argc, Value* argv[]) = 0;

    // —— class 构造原语（ADR-047）——
    // constructSuper: 执行派生类构造器的 super(...)；用当前 new.target 构造父类，
    // 成功后把当前函数环境的 this 槽原地替换为返回对象。super 两次抛 ReferenceError。
    virtual Value* constructSuper(Value* superCtor, Value* newTarget, size_t argc,
                                  Value* argv[]) = 0;

    // getThis: 读取当前函数环境的 this；派生类 super() 前为 Uninitialized，抛 ReferenceError。
    virtual Value* getThis() = 0;
    // initializeThis: 初始化当前函数环境的 this（引擎构造路径/VM 使用）。
    virtual void initializeThis(Value* value) = 0;
    virtual bool isThisInitialized() = 0;

    // newTarget: 当前构造调用的 new.target（非构造上下文为 nullptr）。
    virtual Value* newTarget() = 0;

    // super 属性访问：从 homeObject 的原型开始查找，receiver 作为 getter/setter 的 this。
    virtual Value* getSuperProperty(Value* homeObject, Value* key, Value* receiver) = 0;
    virtual Value* setSuperProperty(Value* homeObject, Value* key, Value* value,
                                    Value* receiver) = 0;

    // createUninitialized: 创建 TDZ 哨兵（派生类 this 初始化前使用）。
    virtual Value* createUninitialized() = 0;

    // —— class 字段初始化（ADR-047 §4）——
    // addInstanceField: 向 class 构造器注册实例字段（按调用顺序初始化）。
    // initializer 为初始化函数（nullptr = undefined）；isPrivate 预留给 P2。
    virtual void addInstanceField(Value* classCtor, Value* key, Value* initializer,
                                  bool isPrivate = false) = 0;
    // addClassInitializer: 注册类级初始化回调（装饰器 addInitializer，P3）。
    virtual void addClassInitializer(Value* classCtor, Value* initializer) = 0;
    // initializeInstanceFields: 按 ClassData 顺序初始化实例字段并执行类级初始化回调。
    // 由 VM 在正确时机调用（基类构造体开头 / 派生类 super 返回后）。
    virtual Value* initializeInstanceFields(Value* instance, Value* classCtor) = 0;

    // setFunctionClassConstructor: 更新已由创建选项标记的 class 构造器的
    // isDerived/superConstructor（创建后再确定父构造器的场景）。
    virtual void setFunctionClassConstructor(Value* fn, bool isDerived,
                                             Value* superCtor) = 0;

    // instanceOf: `value instanceof target`（ES2024 §13.10.2）。
    // 优先走 target[@@hasInstance]（存在且非 null/undefined 时以其 ToBoolean 结果为准），
    // 否则按 OrdinaryHasInstance（§7.3.21）沿 value 的原型链找 target.prototype。
    // 返回 Boolean，或 Exception/Fatal（target 非对象、@@hasInstance 非函数、
    // target 不可调用、prototype 非对象）。
    virtual Value* instanceOf(Value* value, Value* target) = 0;

    // createNativeFunction: 创建 native 函数 Value。
    //   closureScope = 新建的**孤立**作用域（parent=nullptr，Sema 收集的穿透闭包变量
    //                  绑定于此；生命周期随函数 data，调用期间临时挂到 rootScope，ADR-036）；
    //   rootScope = 当前 root scope（借用）；
    //   minArgc = 最少参数数量（调用时校验）；
    //   bindName = 变量绑定名（非空则 bind 到 current scope）。
    // 函数名不在此设置（缺省 "<anonymous>"），由 setFunctionName 单独设置——
    // 对应 VM 字节码序列 define → store → setname。
    // 同时自动创建 fn.prototype（普通对象）并定义 proto.constructor 指回函数（ADR-034）。
    // 返回 Value*：正常函数值，或 OOM 哨兵（ADR-031）。
    virtual Value* createNativeFunction(NativeFn fn, size_t minArgc,
                                        std::string_view bindName = {},
                                        const FunctionCreateOptions& opts = {}) = 0;

    // createBoundFunction: 创建绑定函数（ES2024 §10.4.1.3 BoundFunctionCreate）。
    // 返回的新函数调用时以 boundThis 为 this、前置 boundArgs 再拼接调用实参。
    // 新函数无 prototype 属性（绑定函数不可 new）。失败返回 OOM 哨兵。
    virtual Value* createBoundFunction(Value* target, Value* boundThis,
                                       size_t argc, Value* argv[]) = 0;

    // setFunctionConstructor: 为函数设置构造钩子（ES2024 内建的 call/new 语义差异）。
    // 内建包装类（Boolean/Number/String）需要：call 返回原始值、new 返回包装对象。
    // 设置后 construct 直接调 ctor（thisArg 为 undefined），并用其返回的对象。
    // 仅对 Function 类型有效，其他类型忽略。
    virtual void setFunctionConstructor(Value* fn, NativeFn ctor) = 0;

    // setFunctionName: 设置函数名（FunctionData.name）。仅对 Function 类型有效，
    // 其他类型忽略。对应 VM 的 setname 字节码。
    virtual void setFunctionName(Value* fn, std::string_view name) = 0;

    // setFunctionStrict: 把函数标记为严格模式（在 closureScope 上标注 "strict"）。
    // 用于内部算法（如 Object.assign）需要 Throw=true 的 Set 语义，
    // 避免受调用方 sloppy 影响。仅对 Function 类型有效，其他类型忽略。
    virtual void setFunctionStrict(Value* fn) = 0;

    // setFunctionCapturesEnvironment: 把函数切换为 Environment 模式闭包。
    // closureScope 以 lookup-only 方式挂到**当前作用域**（定义点环境链），并沿
    // current → ... → root 逐层 captureRef++。此后调用期不再 reparent，函数可
    // 动态看到定义点作用域（含直接 eval 注入的绑定）；析构时自底向上解 pin。
    // 用于 Sema 标记含直接 eval 的函数，或测试模拟「作用域内存在 eval」。
    // 已处于 Environment 模式时无操作；仅对 Function 类型有效。
    virtual void setFunctionCapturesEnvironment(Value* fn) = 0;

    // setFunctionHomeObject: 设置 [[HomeObject]]（class 方法/构造器/字段初始化器）。
    // 仅对 Function 类型有效，其他类型忽略。
    virtual void setFunctionHomeObject(Value* fn, Value* homeObject) = 0;

    // setFunctionSource: 设置函数源码切片（Function.prototype.toString）。
    // 内部 retain 新 source、release 旧 source；仅对 Function 类型有效。
    virtual void setFunctionSource(Value* fn, const SourceSlice& slice) = 0;

    // setFunctionConstructible: 显式标记函数是否可构造（ES2024 §7.2.4 IsConstructor）。
    // native 函数默认可构造；箭头函数、方法、生成器等不可构造者置 false。
    // 绑定函数在创建时按 target 的可构造性继承，此后仍可显式覆盖。
    // 仅对 Function 类型有效，其他类型忽略。
    virtual void setFunctionConstructible(Value* fn, bool constructible) = 0;

    // setFunctionGenerator: 标记函数为生成器函数（function*，ADR-045 §8）。
    // 标记后 call 不执行函数体，而是创建 Suspended 协程返回；生成器函数不可构造。
    // 仅对 Function 类型有效，其他类型忽略。
    virtual void setFunctionGenerator(Value* fn, bool generator) = 0;

    // setFunctionAsyncGenerator: 标记函数为异步生成器函数（async function*）。
    // 同时置 generator/asyncGenerator；call 创建 AsyncGenerator 对象；不可构造。
    virtual void setFunctionAsyncGenerator(Value* fn, bool asyncGenerator) = 0;

    // —— 对象与属性（ADR-018 对象模型）——
    // createObject: 创建空对象 Value（properties/order/symbols 空）。
    // 原型自动挂 Object.prototype（ADR-035 完整原型链）。
    // 返回 Value*：正常对象，或 OOM 哨兵（ADR-031）。
    virtual Value* createObject(std::string_view bindName = {}) = 0;

    // createArray: 创建空数组 Value（elements 空，length=0）。
    // 原型自动挂 Array.prototype（ADR-018 §6）。bootstrap 期间原型未就绪时跳过。
    // 返回 Value*：正常数组，或 OOM 哨兵（ADR-031）。
    virtual Value* createArray(std::string_view bindName = {}) = 0;

    // createPrimitiveWrapper: 创建原始值包装对象（ES2024 §10.4）。
    // primitive 必须是 Boolean/Number/String/Symbol 之一，成为 [[PrimitiveValue]]。
    // 原型按 primitive 的类型自动挂对应的 *Prototype；bootstrap 期间未就绪时跳过。
    // 其余对象行为（属性/原型/GC）与普通对象一致。
    // 返回 Value*：正常包装对象，或 OOM 哨兵（ADR-031）。
    virtual Value* createPrimitiveWrapper(Value* primitive,
                                          std::string_view bindName = {}) = 0;

    // createArrayIterator: 创建数组迭代器（ES2024 §23.1.5.1 CreateArrayIterator）。
    // iteratedObject 是被迭代的数组（可为任意对象，next 时按 arrayLike 读取）；
    // kind 决定产出索引 / 元素值 / [index, value] 对。
    // 原型自动挂 %ArrayIteratorPrototype%；bootstrap 期间未就绪时跳过。
    // 返回 Value*：正常迭代器，或 OOM 哨兵（ADR-031）。
    virtual Value* createArrayIterator(Value* iteratedObject, ArrayIterationKind kind,
                                       std::string_view bindName = {}) = 0;

    // createIteratorWrapper: 创建迭代器包装对象（ES2025 §27.1.4.2 Iterator.from）。
    // iterated 是被包装的 iterator-like 对象，nextMethod 是其 next 方法。
    // 原型自动挂 %WrapForValidIteratorPrototype%；bootstrap 期间未就绪时跳过。
    // 返回 Value*：正常包装对象，或 OOM 哨兵（ADR-031）。
    virtual Value* createIteratorWrapper(Value* iterated, Value* nextMethod,
                                         std::string_view bindName = {}) = 0;

    // createError: 创建 Error 对象（ES2024 §20.5.1.1 Error 构造的简化版）。
    //   message  —— 消息（空则不定义 message 属性）；
    //   name     —— 渲染 stack 首行的错误名（"Error"/"TypeError"...）；
    //   position —— 出错点（栈顶帧此刻的位置，只有 VM 知道；{} = 未知）；
    //   bindName —— 变量绑定名（非空则 bind 到 current scope）。
    // 原型自动挂 errorPrototype()；定义自有属性 message 与 stack
    // （均 writable / 不可枚举 / 可配置）。stack 由 trace(position) 渲染
    // ——clone 当前调用栈并用 position 补全栈顶帧，不改动 Context 的栈（ADR-037）。
    // 返回 Value*：正常 Error 对象，或 OOM 哨兵（ADR-031）。
    virtual Value* createError(std::string_view message, std::string_view name = "Error",
                               const SourcePosition& position = {},
                               std::string_view bindName = {}) = 0;

    // errorPrototype: %Error.prototype%（Error 实例的原型，也是各子类原型的原型）。
    // 由 ErrorBuiltins 建立，生命周期与 Context 一致。
    virtual Value* errorPrototype() const = 0;

    // setErrorPrototype: bootstrap 专用——Error.prototype 建立后设置，
    // 此后 createError 自动挂链。宿主不应调用。
    virtual void setErrorPrototype(Value* proto) = 0;

    // 内建原型访问（ADR-035）：
    // objectPrototype —— 所有普通对象原型链的终点；
    // functionPrototype —— 所有函数对象的原型（其原型为 objectPrototype）；
    // arrayPrototype —— 所有数组的原型（其原型为 objectPrototype）。
    // 由 Context 构造时 bootstrap，生命周期与 Context 一致。
    virtual Value* objectPrototype() const = 0;
    virtual Value* functionPrototype() const = 0;
    virtual Value* arrayPrototype() const = 0;

    // 原始值包装对象的原型（ES2024 §20.3/§21.1.3/§21.1.3/§20.4.3）：
    // booleanPrototype / numberPrototype / stringPrototype / symbolPrototype。
    // 由 Context 构造时 bootstrap，生命周期与 Context 一致。
    virtual Value* booleanPrototype() const = 0;
    virtual Value* numberPrototype() const = 0;
    virtual Value* stringPrototype() const = 0;
    virtual Value* symbolPrototype() const = 0;

    // setBuiltinPrototypes: bootstrap 专用——原型对象建立后设置，
    // 此后 createObject/createNativeFunction 自动挂链。宿主不应调用。
    virtual void setBuiltinPrototypes(Value* objectProto, Value* functionProto) = 0;

    // setArrayPrototype: bootstrap 专用——Array.prototype 建立后设置，
    // 此后 createArray 自动挂链。宿主不应调用。
    virtual void setArrayPrototype(Value* arrayProto) = 0;

    // setWrapperPrototypes: bootstrap 专用——四个包装原型建立后设置，
    // 此后 createPrimitiveWrapper 按 primitive 类型自动挂链。宿主不应调用。
    virtual void setWrapperPrototypes(Value* booleanProto, Value* numberProto,
                                      Value* stringProto, Value* symbolProto) = 0;

    // setIteratorBuiltins: bootstrap 专用——迭代器原型与构造函数建立后设置，
    // 此后 createArrayIterator / createIteratorWrapper 自动挂链。宿主不应调用。
    virtual void setIteratorBuiltins(Value* iteratorProto, Value* iteratorCtor,
                                     Value* arrayIteratorProto,
                                     Value* iteratorWrapperProto) = 0;

    // iteratorPrototype / iteratorCtor: %Iterator.prototype% 与 Iterator 构造函数。
    // 由 IteratorBuiltins 建立，生命周期与 Context 一致。
    virtual Value* iteratorPrototype() const = 0;
    virtual Value* iteratorCtor() const = 0;

    // setGeneratorPrototype / generatorPrototype: %GeneratorPrototype%（ADR-045 §8）。
    // bootstrap 专用；建立后生成器函数标记会把 fn.prototype 挂到它下面。
    virtual void setGeneratorPrototype(Value* proto) = 0;
    virtual Value* generatorPrototype() const = 0;

    // %AsyncIteratorPrototype% / %AsyncGeneratorPrototype%（ES2024 §27.6）。
    virtual void setAsyncIteratorPrototype(Value* proto) = 0;
    virtual Value* asyncIteratorPrototype() const = 0;
    virtual void setAsyncGeneratorPrototype(Value* proto) = 0;
    virtual Value* asyncGeneratorPrototype() const = 0;

    // 属性操作代理：obj->type()->op(this, obj, ...) 分派。
    // key 是 Value*：symbol 保持符号身份（symbols 容器），其他类型 ToPropertyKey
    // 转字符串（properties 容器）。非对象类型：get 返回 undefined，
    // set/define 返回 Exception，has 返回 false。
    virtual Value* getProperty(Value* obj, Value* key) = 0;
    virtual Value* setProperty(Value* obj, Value* key, Value* value) = 0;
    virtual bool hasProperty(Value* obj, Value* key) = 0;
    virtual Value* deleteProperty(Value* obj, Value* key) = 0;
    virtual Value* defineProperty(Value* obj, Value* key, Value* value,
                                  bool writable = true, bool enumerable = true,
                                  bool configurable = true) = 0;

    // defineAccessor: 定义访问器属性（getter/setter 须为 Function 或 null）。
    // 成功返回 obj，失败返回 Exception。
    virtual Value* defineAccessor(Value* obj, Value* key, Value* getter, Value* setter,
                                  bool enumerable = false, bool configurable = true) = 0;

    // 字符串键便捷重载：内部 createString(key) 后走上面的 Value* 版本。
    // 用于 C++ 侧固定键（如 "prototype"/"constructor"/"name"）。
    Value* getProperty(Value* obj, std::string_view key) {
        return getProperty(obj, createString(key));
    }
    Value* setProperty(Value* obj, std::string_view key, Value* value) {
        return setProperty(obj, createString(key), value);
    }
    bool hasProperty(Value* obj, std::string_view key) {
        return hasProperty(obj, createString(key));
    }
    Value* deleteProperty(Value* obj, std::string_view key) {
        return deleteProperty(obj, createString(key));
    }
    Value* defineProperty(Value* obj, std::string_view key, Value* value,
                          bool writable = true, bool enumerable = true,
                          bool configurable = true) {
        return defineProperty(obj, createString(key), value, writable, enumerable,
                              configurable);
    }
    Value* defineAccessor(Value* obj, std::string_view key, Value* getter, Value* setter,
                          bool enumerable = false, bool configurable = true) {
        return defineAccessor(obj, createString(key), getter, setter, enumerable,
                              configurable);
    }

    // 原型链代理（ADR-018 §1）：
    // getPrototype: 返回对象的原型（无原型返回 null；非对象返回 undefined）。
    // setPrototype: 设置原型（proto 须为 Object 或 null），成功返回 obj；
    //               非对象 / proto 类型非法 / 成环 返回 Exception。
    virtual Value* getPrototype(Value* obj) = 0;
    virtual Value* setPrototype(Value* obj, Value* proto) = 0;

    // —— 自有属性枚举（ADR-018 §7，ES2024 §10.1.11）——
    // ownPropertyKeys: 返回 obj 的自有属性键（Value* 数组，按规范顺序：
    //   数组索引升序 → 字符串键插入序 → symbol 键插入序）。
    //   enumerableOnly=true 只含 enumerable 键（Object.keys 语义）；
    //   includeSymbols=true 包含 symbol 键（Reflect.ownKeys 语义）。
    //   非对象返回空数组。返回的键 Value 已归属 current scope。
    virtual Value* ownPropertyKeys(Value* obj, bool enumerableOnly,
                                   bool includeSymbols) = 0;

    // ownEnumerableStringKeys: 便捷形式——只含 enumerable 的字符串键（Object.keys）。
    virtual Value* ownEnumerableStringKeys(Value* obj) = 0;

    // setClosure: 把当前作用域链上名为 name 的变量加入函数的闭包作用域（ADR-013 §2）。
    // 绑定 = 共享同一 Value（不复制），closureScope 登记同一 Value。
    // 查找沿 current 的 parent 链（lookupChain）。
    // 返回 fn（成功），或 Exception（找不到变量 / fn 非函数）。
    virtual Value* setClosure(Value* fn, std::string_view name) = 0;

    // captureValue: 直接把任意 Value 注入函数的闭包作用域（ADR-043）。
    // NativeFn 是裸函数指针，无法捕获 C++ 状态；内建（Promise 的 resolve/reject、
    // reaction job 等）需要携带运行时对象，故用闭包注入代替 userdata：
    // 注入后 native 内可用 fn->closureScope->lookup(name) 取回。
    // 与 setClosure 的区别：不查作用域链，value 由调用方直接给定。
    // 返回 fn（成功），或 Exception（fn 非函数 / 重复注入同名）。
    virtual Value* captureValue(Value* fn, std::string_view name, Value* value) = 0;

    // —— Promise（ES2024 §27.2，ADR-043）——
    // createPromise: 创建 Pending 状态的 Promise 对象（原型 = promisePrototype()）。
    // 返回 Value*：正常 Promise，或 OOM 哨兵。
    virtual Value* createPromise(std::string_view bindName = {}) = 0;

    // promisePrototype: %Promise.prototype%。由 PromiseBuiltins 建立，
    // 生命周期与 Context 一致。
    virtual Value* promisePrototype() const = 0;

    // setPromisePrototype: bootstrap 专用——Promise.prototype 建立后设置，
    // 此后 createPromise 自动挂链。宿主不应调用。
    virtual void setPromisePrototype(Value* proto) = 0;
    // %Promise% 内建构造函数（AsyncGenerator 等异步机制用它创建 capability）。
    virtual void setPromiseCtor(Value* ctor) = 0;
    virtual Value* promiseCtor() const = 0;

    // hostPromiseRejectionTracker（ES2024 §27.2.1.9 HostPromiseRejectionTracker）：
    //   handled=false —— promise 被 reject 且 [[PromiseIsHandled]] 为 false，
    //                    登记为「待上报的未处理拒绝」；
    //   handled=true  —— promise 的拒绝被 then/catch 接住，移出待上报集合。
    // 登记的 promise 由 Context 作为动态 GC 根持有（pinRef++），
    // 上报或 Context 析构时归还。
    virtual void hostPromiseRejectionTracker(Value* promise, bool handled) = 0;

    // reportUnhandledRejections：把当前所有待上报的未处理拒绝打印到 console
    // （"Unhandled promise rejection: <reason>"），并清空集合。
    //
    // 这是宿主的 **microtask checkpoint** 职责（§27.2.1.9）：时机应为
    // 「宏任务结束 / 脚本执行结束」，而不是「每排空一批微任务」。`runOneTask`
    // 不再自动调用它——否则会过早上报那些计划在下一个宏任务里 catch 的拒绝。
    // 宿主循环的典型写法：
    //   while (ctx->hasTasks()) { while (ctx->hasRunnableTask()) ctx->runOneTask();
    //                             ctx->reportUnhandledRejections(); }
    // Context 析构时也兜底调用一次（覆盖宿主未排空事件循环就销毁的情形）。
    virtual void reportUnhandledRejections() = 0;

    // —— 控制流信号（ADR-046）——
    // createInterrupt: 创建 Interrupt 值（kind + offset + value）。
    // 只由协程函数体 / VM 执行循环产生；正常业务路径不应调用。
    // offset 对抽象层是不透明整数（native=stage，VM=PC）。
    // 返回 Value*：正常 Interrupt，或 OOM 哨兵。
    virtual Value* createInterrupt(InterruptKind kind, uint64_t offset,
                                   Value* value) = 0;

    // —— 协程（ADR-045）——
    // createCoroutine: 以 target 函数创建 Suspended 协程对象（不执行函数体）。
    //   receiver 为执行时的 this（nullptr 归一为 undefined）；
    //   argc/argv 为首次运行的实参（协程持有副本）。
    //   恢复驱动按 target 种类选择（native 函数 → resumeNativeCoroutine）。
    //   返回 Value*：正常协程对象，或 Exception（target 非函数/绑定函数），或 OOM 哨兵。
    virtual Value* createCoroutine(Value* target, Value* receiver, size_t argc,
                                   Value* argv[], std::string_view bindName = {}) = 0;

    // getCurrentCoroutine: 当前最内层正在恢复的协程（嵌套生成器场景）。
    // 不在协程内返回 nullptr（调用方用 asCoroutine 判定）。
    virtual Value* getCurrentCoroutine() const = 0;

    // resumeCoroutine: 恢复协程一次（Generator 的 next/throw/return 共用入口）。
    //   sentValue 为本次恢复传入的值（可能是 Interrupt 的 throw/return 注入）；
    //   nullptr 归一为 undefined。
    // 返回函数体的原始结果：挂起时为 Interrupt(Yield)，完成时为最终值/异常。
    // 协程已 Completed 时返回 undefined；正在运行（重入）返回 Exception。
    virtual Value* resumeCoroutine(Value* coroutine, Value* sentValue) = 0;

    // createAsyncGenerator: 以 target 创建 SuspendedStart 的异步生成器对象。
    // 与 createCoroutine 同构，额外持 Promise 请求队列（ES2024 §27.6）。
    virtual Value* createAsyncGenerator(Value* target, Value* receiver, size_t argc,
                                        Value* argv[],
                                        std::string_view bindName = {}) = 0;

    // —— ES Module 基础设施 ——
    // 模块以绝对路径为唯一 id。Context 持有 id → Module 映射；每个 Module 借用
    // 一棵以 global 为父的 root scope，并持有一个原型为 null 的导出对象。
    // import 的本质是「惰性加载 + 取 Module + 取导出对象」。
    virtual bool hasModule(std::string_view id) const = 0;

    // getModule: 取模块；不存在则惰性创建（root scope + 原型为 null 的导出对象）。
    // 返回的 scope/exports 由 Context 持有，调用方只借用。
    // OOM 时返回 {nullptr, nullptr}。
    virtual ModuleInfo getModule(std::string_view id) = 0;

    // setModuleLoader: 为模块登记加载器（模块不存在则先创建）。
    // loader 在模块 root scope 下执行；已加载的模块不会被重新执行。
    virtual void setModuleLoader(std::string_view id, ModuleLoader loader) = 0;

    // importModule: 取模块；若尚未加载且已登记 loader，则在模块 root scope 下
    // 执行 loader 并标记已加载。循环依赖时直接返回当前（部分）导出对象。
    // specifier 可为绝对路径，或以 ./ ../ 开头的相对路径——后者以当前正在
    // 加载的模块目录为基准解析（模块记录自己的 dirname）。
    // 返回导出对象；loader 失败时原样返回其 Exception/Fatal。
    virtual Value* importModule(std::string_view specifier) = 0;

    // createModuleNamespace: 创建模块命名空间 exotic 对象（原型为 null，
    // @@toStringTag = "Module"）。导出属性是活绑定，见 exportBinding。
    virtual Value* createModuleNamespace(std::string_view bindName = {}) = 0;

    // exportBinding: 把当前作用域链上的 localName 槽位以 exportName 暴露到
    // exports（模块命名空间对象）。属性直接持有该槽位 Value*，**不 clone**；
    // 后续 store 原地替换槽位的 meta+data，导入方读取即可见最新值。
    // 返回 exportName 对应的 Value（即槽位 Value），或 Exception。
    virtual Value* exportBinding(Value* exports, std::string_view exportName,
                                 std::string_view localName) = 0;

    // bindImport: 在当前作用域建立导入绑定 localName -> namespaceObj[exportName]。
    // **间接绑定**：读取时经命名空间实时解析，故导出方后续更新、循环依赖中晚到的
    // 导出都能被看到；写入恒返回 TypeError（导入绑定只读）。
    // 只读在运行时绑定层强制，eval/with 等动态路径同样受约束。
    // namespaceObj 必须是模块命名空间；重复声明返回 SyntaxError。
    // 返回 localName 当前解析值（导出尚不存在时为 undefined），或 Exception。
    virtual Value* bindImport(std::string_view localName, Value* namespaceObj,
                              std::string_view exportName) = 0;

    // —— 错误值创建（ADR-006 异常即值 + ADR-017 §4 Fatal）——
    // createException: 创建可捕获异常 Value（child 永远不为 nullptr，无错误对象时传 undefined Value）。
    // createFatal: 创建不可捕获致命错误 Value（字面量消息，静态存储期）。
    virtual Value* createException(Value* child) = 0;
    virtual Value* createFatal(const char* msg) = 0;

    // createErrorException: 创建「规范错误」异常（ADR-039）——引擎内部报错的标准入口。
    // 按 kind 创建对应 Error 对象（Error/TypeError/RangeError/ReferenceError/
    // SyntaxError/URIError），再按 ADR-006 包进 Exception 通道返回。
    //   message  —— 错误消息（会进 message 属性与 stack 首行）；
    //   position —— 出错点（栈顶帧位置，只有 VM 知道；{} = 未知）。
    // 返回 Value*：Exception（携带 Error 对象），或 OOM 哨兵。
    virtual Value* createErrorException(ErrorKind kind, std::string_view message,
                                        const SourcePosition& position = {}) = 0;

    // —— OOM 哨兵（ADR-031）——
    // Context 构造时预分配一个 Fatal 类型 Value，挂载到 global（存活于 Context 生命周期）。
    // create* 内存耗尽时返回此单例，调用方用指针比较判断是否 OOM（v == ctx->oomValue()）。
    // OOM 属 Fatal（不可捕获），调用点检查到 Fatal 立即透传（ADR-017 §4）。
    virtual Value* oomValue() const = 0;

    // —— 变量定义与赋值（ADR-030 阶段 5）——
    // define: 在 current scope 定义变量名。clone source 的 data 进新槽位。
    //         同 scope 重复定义无条件返回 Exception（var/let/const 差异已在 Sema 抹平）。
    //         source 若是 Exception/Fatal 直接透传。
    // store: 从 current scope 开始向上查找 name，找到则 clone source 的 data 替换其 type+data。
    //        找不到时：按 "strict" 标注分派——严格返回 Exception，非严格在 global 定义。
    //        source 若是 Exception/Fatal 直接透传。
    virtual Value* define(std::string_view name, Value* source) = 0;
    virtual Value* store(std::string_view name, Value* source) = 0;

    // —— 可赋值性（ADR-030 阶段 5）——
    // setMutable: 设置 Value 是否可赋值（const 声明由 Sema 置 false）。
    // isMutable: 查询 Value 是否可赋值。
    // store 对 mutable=false 的 target 返回 Exception（禁止赋值）。
    virtual void setMutable(Value* v, bool m) = 0;
    virtual bool isMutable(Value* v) const = 0;

    // —— 作用域标注（ADR-030 阶段 5）——
    // setFlag: 在当前 scope 标注 name（如 "strict"）。语法上仅模块作用域/函数体可标注。
    // hasFlag: 从当前 scope 沿 parent 链向上查找标注（内层继承外层）。
    // 当前 global scope 默认标注 "strict"（保持默认严格模式）。
    virtual void setFlag(std::string_view name) = 0;
    virtual bool hasFlag(std::string_view name) const = 0;

    // —— 事件循环（非阻塞调度，双队列）——
    // 微任务队列（microtasks）：FIFO，无延迟，永远优先于宏任务。
    // 宏任务队列（macrotasks）：按到期时刻（start + timeout）升序；同到期时刻按入队顺序。
    // runOneTask 每次只取一个任务：微任务队列非空则取微任务，否则取已到期的宏任务。
    // 宿主循环 `while (hasRunnableTask()) runOneTask();` 自然得到
    // 「宏任务 → 排空微任务 → 下一个宏任务」的规范顺序。
    // 每个宏任务结束后宿主应调用 reportUnhandledRejections()（microtask checkpoint）。
    // 任务实体被任务队列作为 GC 根持有（pinRef++），出队执行后释放。
    //
    // postMicrotask: 入队一个微任务（Promise 回调 / queueMicrotask 等）。返回任务 id。
    // postTask: 入队一个宏任务。timeout 为相对当前时刻的延迟（毫秒，0 = 尽快执行）。
    //           返回任务 id（> 0），失败（OOM）返回 0。
    // postLoopTask: 入队一个循环宏任务：每次执行完成后以当前时刻重新入队。
    //           返回任务 id。取消后不再重新入队。
    // cancelTask: 按 id 取消任务（两个队列都查）。返回是否找到并取消。
    // hasTasks: 是否还有任务（含未到期）。
    // hasRunnableTask: 是否有可立即执行的任务（微任务非空，或宏任务已到期）。
    // runOneTask: 执行一个可运行任务，返回其执行结果；无任务返回 undefined。
    //           任务抛出的 Exception 由 run 打印为未捕获异常后返回 undefined；
    //           Fatal 原样返回（调用方须透传）。
    virtual uint64_t postMicrotask(Value* entity,
                                   TaskRunner run = defaultTaskRunner) = 0;
    virtual uint64_t postTask(Value* entity, uint64_t timeout = 0,
                              TaskRunner run = defaultTaskRunner) = 0;
    virtual uint64_t postLoopTask(Value* entity, uint64_t timeout = 0,
                                  TaskRunner run = defaultTaskRunner) = 0;
    virtual bool cancelTask(uint64_t id) = 0;
    virtual bool hasTasks() const = 0;
    virtual bool hasRunnableTask() const = 0;
    virtual Value* runOneTask() = 0;
};

// 工厂：创建 Context，注入 IAllocator/Console/IClock（无所有权，外部销毁）。
// Context 内部用 IAllocator 创建 GC（持有所有权）+ global/root scope。
// clock 为 nullptr 时使用进程级默认时钟（infra::defaultClock()）。
std::unique_ptr<IContext> createContext(infra::IAllocator* alloc,
                                         infra::Console* console,
                                         infra::IClock* clock = nullptr);

}  // namespace neo::core

#endif  // NEO_CORE_ICONTEXT_H
