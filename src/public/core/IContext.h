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
#include "core/PrivateName.h"
#include "core/SharedBuffer.h"
#include "core/SourceText.h"
#include "core/Task.h"
#include "core/WellKnownSymbol.h"
#include "infra/IAllocator.h"
#include "infra/Console.h"
#include "infra/IClock.h"
#include "neojs.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// BigInt 负载类型（纯 C++ 任意精度整数，见 private/bigint/BigInt.h）。
// 公共接口只按引用传递，调用方需包含该头才能构造实参。
namespace neo::bigint {
class BigInt;
}

namespace neo::vm {
struct Code;
}  // namespace neo::vm

// 正则编译产物（不可变，见 private/regex/RegexProgram.h）。
namespace neo::regex {
struct Program;
}  // namespace neo::regex

namespace neo::core {

// 前向声明
class IData;
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
    // async native：调用返回 Promise，函数体以协程阶段机执行。
    // 自动强制 constructible=false、hasPrototypeProperty=false。
    bool asyncNative = false;
    bool isClassConstructor = false;
    bool isDerived = false;
    Value* homeObject = nullptr;       // [[HomeObject]]（借用，GC 由函数遍历）
    Value* superConstructor = nullptr; // 派生类父构造器（借用，GC 由 ClassData 遍历）
    Value* ownerClass = nullptr;       // 所属 class 构造器（方法/字段初始化器，GC 遍历）
    // 定义点外层 class 构造器（私有名词法可见性）；未给出时创建期自动继承。
    Value* outerOwnerClass = nullptr;  // GC 遍历
    SourceSlice source{};              // 函数源码切片（创建时 retain）
};

// —— native class 注册（ADR-047 扩展库）——
// 描述一个由 native 实现的 class。registerClass 负责创建构造器/prototype、
// 链接原型链、定义方法描述符、注册字段与私有元素。
//
// 构造器约定：
//   - constructor 为 native 构造体，self = this；base 类应在函数体开头调用
//     initializeInstanceElements(self, ctx->currentFunction())；
//   - 派生类应在 super() 之后调用 initializeInstanceElements(this, ctx->currentFunction())；
//   - constructor 为空时仅允许 base 类，registerClass 会生成默认构造体自动初始化字段。
struct NativeClassMethod {
    std::string_view name;
    NativeFn fn = nullptr;
    size_t length = 0;
};

struct NativeClassField {
    std::string_view name;
    NativeFn initializer = nullptr;  // 以实例/构造器为 this 求值
};

struct NativeClassPrivateField {
    PrivateName* name = nullptr;     // 借用；registerClass 内部 retain
    NativeFn initializer = nullptr;
};

struct NativeClassPrivateMethod {
    PrivateName* name = nullptr;
    NativeFn fn = nullptr;
};

struct NativeClassPrivateAccessor {
    PrivateName* name = nullptr;
    NativeFn getter = nullptr;
    NativeFn setter = nullptr;
};

struct NativeClassDef {
    std::string_view name;
    NativeFn constructor = nullptr;
    size_t length = 0;
    Value* superCtor = nullptr;  // nullptr = base；Null Value = extends null
    std::vector<NativeClassMethod> methods;
    std::vector<NativeClassMethod> staticMethods;
    std::vector<NativeClassField> fields;
    std::vector<NativeClassField> staticFields;
    std::vector<NativeClassPrivateField> privateFields;
    std::vector<NativeClassPrivateField> staticPrivateFields;
    std::vector<NativeClassPrivateMethod> privateMethods;
    std::vector<NativeClassPrivateMethod> staticPrivateMethods;
    std::vector<NativeClassPrivateAccessor> privateAccessors;
    std::vector<NativeClassPrivateAccessor> staticPrivateAccessors;
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
    // 唯一的 JS 全局对象（Global Environment Record 的对象记录）。
    virtual Value* globalObject() const = 0;

    // —— 线程角色 ——
    // 嵌入方通过 createContext 创建的主上下文为 false；Worker 线程内创建的
    // 上下文为 true。用于宿主策略（如 Atomics.wait 仅 worker 可阻塞）。
    virtual bool isWorkerContext() const = 0;
    virtual void setWorkerContext(bool worker) = 0;

    // —— VM 停止标记（worker terminate / 调试器复用）——
    // 跨线程可请求停止当前 Context 的字节码执行；VM 在每条指令后检查。
    // 调试器后续可在同一检查点实现暂停/单步（扩展为带原因的标记即可）。
    virtual void requestStop() = 0;
    virtual bool isStopRequested() const = 0;
    virtual void clearStopRequest() = 0;

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

    // currentFunction: 当前正在执行的函数 Value（native/脚本统一）；顶层为 nullptr。
    // invoke 在调用期间设置、返回/Fatal/挂起时恢复。native 构造器/方法用它取回
    // 自身的 FunctionData（initializeInstanceElements 的 classCtor、homeObject 等）。
    // 借用引用：函数 Value 的存活仍由调用方作用域/调用点保证。
    virtual Value* currentFunction() const = 0;
    virtual void setCurrentFunction(Value* fn) = 0;

    // —— current scope 管理 ——
    // pushNewScope: 在 allocator 上新建 parent 的子 scope 并设为 current。
    // 用于函数调用的临时执行作用域（ADR-022）。返回新建的 scope，OOM 返回 nullptr。
    virtual Scope* pushNewScope(Scope* parent) = 0;

    // pushWithScope: 以 obj 为 with 对象创建 WithScope 并设为 current。
    // obj 先 ToObject（null/undefined -> TypeError；原始值 -> 包装对象）。
    // 严格模式禁止 with（SyntaxError）。成功返回 nullptr 并写 outScope；
    // 失败返回 Exception/Fatal（outScope 不变）。
    // checkStrict=false 由 Compiler 使用：Sema 已确认该 with 位于 sloppy 代码，
    // 但引擎默认作用域可能被标注 strict。
    virtual Value* pushWithScope(Value* obj, Scope** outScope,
                                 bool checkStrict = true) = 0;

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

    // setExceptionPosition：用当前源码位置补全异常值的 stack。
    // VM 在指令失败点调用——Engine 原语创建错误时不知道 pc，只有 VM 能通过
    // SourceMap 反查位置。exception 为 Exception 且载荷为 Error 对象时，
    // 按 trace(position) 重渲染其 stack 属性；其余情况无操作。
    virtual void setExceptionPosition(Value* exception,
                                      const SourcePosition& position) = 0;

    // —— Value 创建便捷接口（ADR-030 阶段 4）——
    // 内部封装 gc->allocData<T> + gc->createValue(type, data)，把 Value 加入 current scope
    // 登记到 current scope（从 global 树可达）。可选 name：指定则同时 bind（命名绑定）。
    // 返回 Value*：正常结果，或内存耗尽时返回 OOM 哨兵（ADR-031，非 nullptr）。
    virtual Value* createUndefined(std::string_view name = {}) = 0;
    virtual Value* createNull(std::string_view name = {}) = 0;
    virtual Value* createBoolean(bool b, std::string_view name = {}) = 0;
    virtual Value* createNumber(double n, std::string_view name = {}) = 0;
    // createBigInt: 创建 bigint 原始值（ES2024 §6.1.4）。不可变，data 按共享语义克隆。
    virtual Value* createBigInt(const neo::bigint::BigInt& value,
                                std::string_view name = {}) = 0;
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
    // 已求值 super base 的读写：GetSuperBase 先于 ToPropertyKey 时使用。
    virtual Value* getSuperPropertyFromBase(Value* base, Value* key,
                                            Value* receiver) = 0;
    virtual Value* setSuperPropertyFromBase(Value* base, Value* key,
                                            Value* value, Value* receiver) = 0;

    // createUninitialized: 创建 TDZ 哨兵（派生类 this 初始化前使用）。
    virtual Value* createUninitialized() = 0;

    // —— class 字段初始化（ADR-047 §4）——
    // addInstanceField: 向 class 构造器注册 public 实例字段。VM 必须按源码声明
    // 顺序调用 addInstanceField/addPrivateField，二者共享同一有序列表。
    virtual void addInstanceField(Value* classCtor, Value* key, Value* initializer) = 0;
    // addClassInitializer: 注册类级初始化回调（装饰器 addInitializer，P3）。
    virtual void addClassInitializer(Value* classCtor, Value* initializer) = 0;
    // initializeInstanceElements: 加私有方法/访问器 brand，再按声明顺序初始化
    // public/private 实例字段，最后执行类级初始化回调。
    // 由 VM 在正确时机调用（基类构造体开头 / 派生类 super 返回后）。
    virtual Value* initializeInstanceElements(Value* instance, Value* classCtor) = 0;

    // —— 私有字段/方法/访问器（ADR-047 §5）——
    // createPrivateName: 创建私有名（refCount=1，调用方持有）。description 仅用于
    // 错误消息；身份是 PrivateName* 指针，不同 class 求值的同名私有名互不相等。
    virtual PrivateName* createPrivateName(std::string_view description) = 0;
    // retain/release: 引用计数。add* 会 retain 一次；调用方用完自己的引用后 release。
    virtual void retainPrivateName(PrivateName* name) = 0;
    virtual void releasePrivateName(PrivateName* name) = 0;

    // addPrivateField/addStaticPrivateField: 注册私有字段（按调用顺序初始化）。
    // initializer 以实例（静态字段以构造器）为 this 求值。
    virtual void addPrivateField(Value* classCtor, PrivateName* name,
                                 Value* initializer) = 0;
    virtual void addStaticPrivateField(Value* classCtor, PrivateName* name,
                                       Value* initializer) = 0;
    // addPrivateMethod/addPrivateAccessor: 注册私有方法/访问器（实例只持 brand）。
    virtual void addPrivateMethod(Value* classCtor, PrivateName* name,
                                  Value* method) = 0;
    virtual void addPrivateAccessor(Value* classCtor, PrivateName* name, Value* getter,
                                    Value* setter) = 0;
    virtual void addStaticPrivateMethod(Value* classCtor, PrivateName* name,
                                        Value* method) = 0;
    virtual void addStaticPrivateAccessor(Value* classCtor, PrivateName* name,
                                          Value* getter, Value* setter) = 0;

    // ensureClassPrivateName: 按 description 查找 class 已注册的私有名；
    // 不存在则新建。返回一个调用方持有的引用（用完 release）。
    virtual PrivateName* ensureClassPrivateName(Value* classCtor,
                                                std::string_view description) = 0;
    // definePrivateElement: 注册私有方法/访问器（kind: 0=method,1=getter,
    // 2=setter），设置 ownerClass/homeObject，并合并同名访问器。
    virtual void definePrivateElement(Value* classCtor, PrivateName* name,
                                      Value* fn, bool isStatic,
                                      uint32_t kind) = 0;
    // setFunctionOwnerClass: 设置函数所属 class（私有名解析用）。
    virtual void setFunctionOwnerClass(Value* fn, Value* ownerClass) = 0;

    // initializeStaticPrivateField: 初始化单个静态私有字段（以构造器为 this）。
    // VM 在每个静态私有字段的声明位置调用，以便与 static block/public static
    // field 保持源码顺序。
    virtual Value* initializeStaticPrivateField(Value* classCtor, PrivateName* name) = 0;
    // initializeStaticPrivateElements: 批量初始化尚未初始化的静态私有字段
    // （便捷入口；需要与 static block 交错时用单字段版本）。
    // 静态私有方法/访问器的 brand 在 add* 时已挂到构造器。
    virtual Value* initializeStaticPrivateElements(Value* classCtor) = 0;

    // getPrivate/setPrivate: 私有字段读写。对象缺 brand 或该名不是字段时抛 TypeError。
    virtual Value* getPrivate(Value* obj, PrivateName* name) = 0;
    virtual Value* setPrivate(Value* obj, PrivateName* name, Value* value) = 0;
    // privateIn: `#x in obj`。RHS 非对象抛 TypeError；否则返回 Boolean。
    virtual Value* privateIn(Value* obj, PrivateName* name) = 0;
    // hasPrivate: 引擎内部 brand 判定（不表达 TypeError）。
    virtual bool hasPrivate(Value* obj, PrivateName* name) = 0;
    // 私有方法/访问器读取：先校验 obj 有 brand，再从 class 定义表取函数。
    // 定义种类不匹配或访问器缺 getter/setter 时返回 TypeError（ES2024 PrivateGet/Set）。
    virtual Value* getPrivateMethod(Value* classCtor, Value* obj, PrivateName* name) = 0;
    virtual Value* getPrivateGetter(Value* classCtor, Value* obj, PrivateName* name) = 0;
    virtual Value* getPrivateSetter(Value* classCtor, Value* obj, PrivateName* name) = 0;

    // setFunctionClassConstructor: 更新已由创建选项标记的 class 构造器的
    // isDerived/superConstructor（创建后再确定父构造器的场景）。
    virtual void setFunctionClassConstructor(Value* fn, bool isDerived,
                                             Value* superCtor) = 0;

    // —— native class 注册与元数据（ADR-047 扩展库）——
    // registerClass: 用 NativeClassDef 一次性创建 class 构造器并返回。
    // 返回构造器 Value，或 Exception/Fatal。派生类必须提供 constructor。
    virtual Value* registerClass(const NativeClassDef& def) = 0;
    // classSuperConstructor: 取 class 的父构造器；非 class 返回 nullptr。
    // classCtor 可以是 class 构造器，也可以是该 class 的成员函数。
    // 供 native 构造体在 super() 时使用。
    virtual Value* classSuperConstructor(Value* classCtor) = 0;
    // classPrivateName: 按 description 查找 class 声明的私有名（借用引用）。
    // classCtor 可以是 class 构造器，也可以是该 class 的成员函数（经 ownerClass
    // 解析）。供 native 方法体取 this.#x 的 PrivateName*；未找到返回 nullptr。
    virtual PrivateName* classPrivateName(Value* classCtor,
                                          std::string_view description) = 0;
    // resolveWithObject: 沿当前作用域链查找以 with 对象绑定 name 的作用域，
    // 命中返回该 with 对象，否则 nullptr；出错返回 Exception/Fatal。
    virtual Value* resolveWithObject(std::string_view name) = 0;
    // resolvePrivateName: 从函数所属 class 起沿定义点外层 class 链解析私有名
    // （内部函数/嵌套 class 可访问外层 class 的私有元素）。未找到返回 nullptr。
    virtual PrivateName* resolvePrivateName(Value* fn,
                                            std::string_view description) = 0;
    // functionHomeObject: 取函数的 [[HomeObject]]；无则 nullptr。
    virtual Value* functionHomeObject(Value* fn) = 0;

    // runStaticBlock: 创建并立即执行一个 native static block（this = classCtor）。
    // 固定选项：strict、不可构造、无 prototype、homeObject/ownerClass = classCtor。
    // 返回执行结果（通常 undefined）或 Exception/Fatal。
    // VM 对脚本 static block 用同一组 FunctionCreateOptions 创建脚本函数，并在
    // 静态元素声明位置调用；Sema 负责 arguments/return/await/yield/super() 等
    // early error，以及 var/函数声明只属于块作用域。
    virtual Value* runStaticBlock(Value* classCtor, NativeFn body) = 0;

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
    // 函数名不在此设置（缺省空串），由 setFunctionName 单独设置——
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

    // 具名函数表达式：设置 name，并在函数闭包作用域内建立同名不可变绑定
    // （§15.2.5 FunctionExpression 的 FunctionEnvironment）。
    virtual void setFunctionNameBinding(Value* fn,
                                        std::string_view name) = 0;

    // NamedEvaluation（§8.5.4）：仅当函数没有自有 "name" 属性时设置名字。
    // 用于 class 表达式——静态 name 方法会占用该属性，此时不得覆盖。
    virtual void setFunctionNameIfAbsent(Value* fn, std::string_view name) = 0;

    // SetFunctionName(fn, propKey)：propKey 是 Symbol 时名字为
    // "[" + description + "]"（无描述则为空串），否则 ToString 后设置。
    // 用于 computed 属性键的 NamedEvaluation，键在运行时才确定。
    virtual void setFunctionNameFromKey(Value* fn, Value* key,
                                        std::string_view prefix = {}) = 0;

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

    // setFunctionAsync: 标记 script async 函数。call 创建协程并返回 Promise；
    // await 产生 Interrupt(Await)。不可构造、无 prototype 属性。
    virtual void setFunctionAsync(Value* fn, bool async) = 0;

    // setFunctionArguments: 标记 script 函数在调用时创建 arguments 对象。
    // mapped 为 true 表示非严格简单形参：索引与形参绑定保持活别名。
    virtual void setFunctionArguments(Value* fn, bool needsArguments,
                                      bool mapped) = 0;

    // setFunctionInFieldInitializer: 标记函数是 class 字段初始化器（或其中
    // 定义的箭头函数）。直接 eval 在该函数内调用时应用 ContainsArguments
    // 早期错误（§15.7.1 / §19.2.1.1）。仅对 Function 类型有效。
    virtual void setFunctionInFieldInitializer(Value* fn, bool inFieldInitializer) = 0;

    // setFunctionParamNames: 记录形参名字（mapped arguments 的映射表来源）。
    virtual void setFunctionParamNames(
        Value* fn, const std::vector<std::string>& names) = 0;

    // —— 对象与属性（ADR-018 对象模型）——
    // createObject: 创建空对象 Value（properties/order/symbols 空）。
    // 原型自动挂 Object.prototype（ADR-035 完整原型链）。
    // 返回 Value*：正常对象，或 OOM 哨兵（ADR-031）。
    virtual Value* createObject(std::string_view bindName = {}) = 0;

    // createProxy: 创建 Proxy exotic 对象（ES2024 §10.5.1 ProxyCreate）。
    // target 必须是对象；handler 必须是对象（可为 Proxy）。失败返回 Exception/OOM。
    // 返回的代理可调用/可构造性继承自 target。
    virtual Value* createProxy(Value* target, Value* handler,
                               std::string_view bindName = {}) = 0;

    // createArray: 创建空数组 Value（elements 空，length=0）。
    // 原型自动挂 Array.prototype（ADR-018 §6）。bootstrap 期间原型未就绪时跳过。
    // 返回 Value*：正常数组，或 OOM 哨兵（ADR-031）。
    virtual Value* createArray(std::string_view bindName = {}) = 0;

    // createArgumentsObject: 为当前 script 函数调用创建 arguments 对象
    // （ES2024 §10.4.4）。使用 currentFunctionEnv() 作为映射/保活作用域；
    // callee 为当前函数，argv 为实际实参。mapped 时按函数记录的形参名建立
    // 索引→绑定映射。返回正常对象或 OOM 哨兵。
    virtual Value* createArgumentsObject(Value* callee, size_t argc,
                                         Value* argv[], bool mapped) = 0;

    // storeBinding: 向指定作用域的绑定赋值（mapped arguments 写回形参）。
    // 处理 TDZ/不可赋值；成功返回绑定槽位 Value*，失败返回 Exception/OOM。
    virtual Value* storeBinding(Scope* scope, std::string_view name,
                                Value* source) = 0;

    // createTemplateObject: 构造标签模板的模板对象（ES2024 §13.2.8.2）。
    // quasis 为 (cooked, raw) 对；cooked 为空表示非法转义（undefined）。
    // 返回冻结的 cooked 数组，其 raw 属性指向冻结的 raw 数组。
    // 调用点缓存由 VM 按 Code+pc 维护。
    // 返回 Value*：正常数组，或 OOM/异常值。
    virtual Value* createTemplateObject(
        const std::vector<std::pair<std::optional<std::string>, std::string>>&
            quasis) = 0;

    // arrayAppend: 在数组末尾追加一个元素（CreateDataPropertyOrThrow(length, v)）。
    // 返回 array 本身或错误值。展开语法与解构 rest 共用。
    virtual Value* arrayAppend(Value* array, Value* value) = 0;

    // copyDataProperties: ES2024 §7.3.23，把 source 的自有 enumerable 属性
    // （字符串 + symbol）复制到 target；excluded 中的键跳过。
    // source 为 null/undefined 时不做任何事。返回 target 或错误值。
    virtual Value* copyDataProperties(
        Value* target, Value* source,
        const std::vector<Value*>& excluded = {}) = 0;

    // createPrimitiveWrapper: 创建原始值包装对象（ES2024 §10.4）。
    // primitive 必须是 Boolean/Number/BigInt/String/Symbol 之一，成为 [[PrimitiveValue]]。
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

    // createStringIterator: 创建字符串迭代器（ES2024 §22.1.5.1）。
    // string 必须是 String 原始值；按码点迭代（代理对作为一个字符）。
    // 原型自动挂 %StringIteratorPrototype%；bootstrap 期间尚未建立时跳过。
    virtual Value* createStringIterator(Value* string,
                                        std::string_view bindName = {}) = 0;

    // createIteratorHelper: 创建 Iterator 辅助方法的惰性迭代器
    // （ES2025 §27.1.3.3）。underlying/nextMethod 是被消费的迭代器记录，
    // callback 是 mapper/predicate，counter 用于 take 剩余量 / drop 待丢弃量。
    // 原型自动挂 %IteratorHelperPrototype%；bootstrap 期间未就绪时跳过。
    virtual Value* createIteratorHelper(Value* underlying, Value* nextMethod,
                                        IteratorHelperKind kind, Value* callback,
                                        double counter,
                                        std::string_view bindName = {}) = 0;

    // setIteratorHelperPrototype / iteratorHelperPrototype:
    // %IteratorHelperPrototype%（原型是 %Iterator.prototype%）。
    virtual void setIteratorHelperPrototype(Value* proto) = 0;
    virtual Value* iteratorHelperPrototype() const = 0;

    // createCollection: 创建空集合对象（ES2024 §24.1.1.1 等）。
    // kind 决定原型（Map/Set/WeakMap/WeakSet.prototype）。
    virtual Value* createCollection(CollectionKind kind,
                                    std::string_view bindName = {}) = 0;

    // createCollectionIterator: 创建集合迭代器（§24.1.5.1 / §24.2.5.1）。
    // 按 collection 的 CollectionKind 自动挂 %MapIteratorPrototype% 或
    // %SetIteratorPrototype%。
    virtual Value* createCollectionIterator(Value* collection,
                                            CollectionIterationKind kind,
                                            std::string_view bindName = {}) = 0;

    // setCollectionPrototypes: bootstrap 专用——集合原型与迭代器原型建立后设置。
    virtual void setCollectionPrototypes(Value* mapProto, Value* setProto,
                                         Value* weakMapProto, Value* weakSetProto,
                                         Value* mapIteratorProto,
                                         Value* setIteratorProto) = 0;

    // createDate: 创建 Date 对象（ES2024 §21.4.2.1），timeValue 为毫秒时间值
    // （NaN 表示 Invalid Date）。原型自动挂 datePrototype()。
    virtual Value* createDate(double timeValue,
                              std::string_view bindName = {}) = 0;

    // setDatePrototype / datePrototype: %Date.prototype%。
    virtual void setDatePrototype(Value* proto) = 0;
    virtual Value* datePrototype() const = 0;

    // createArrayBuffer: 创建 ArrayBuffer（ES2024 §25.1.3.1）。
    // byteLength 为字节数；分配失败返回 OOM 哨兵。
    virtual Value* createArrayBuffer(size_t byteLength,
                                     std::string_view bindName = {}) = 0;

    // createDataView: 创建 DataView（§25.3.2.1）。调用方负责校验
    // byteOffset/byteLength 是否落在 buffer 范围内。
    virtual Value* createDataView(Value* buffer, size_t byteOffset,
                                  size_t byteLength,
                                  std::string_view bindName = {}) = 0;

    // createSharedArrayBuffer: 创建 SharedArrayBuffer（ES2024 §25.1.3.1）。
    // 共享块由调用方提供（构造时新建，还原消息时复用发送方的 block）；
    // 包装对象持有 block 的一个 shared_ptr 引用。
    virtual Value* createSharedArrayBuffer(
        std::shared_ptr<SharedBufferBlock> block,
        std::string_view bindName = {}) = 0;

    // setArrayBufferPrototype / setDataViewPrototype: bootstrap 专用。
    virtual void setArrayBufferPrototype(Value* proto) = 0;
    virtual void setSharedArrayBufferPrototype(Value* proto) = 0;
    virtual void setDataViewPrototype(Value* proto) = 0;

    // createTypedArray: 创建 TypedArray 视图（ES2024 §25.2.5.1）。
    // 调用方负责校验 byteOffset/length 是否落在 buffer 范围内且对齐。
    // 原型按 kind 挂对应的 %Int8Array.prototype% 等。
    virtual Value* createTypedArray(Value* buffer, size_t byteOffset, size_t length,
                                    TypedArrayKind kind,
                                    std::string_view bindName = {}) = 0;

    // setTypedArrayPrototype / typedArrayPrototype: bootstrap 专用，
    // 每种元素类型一个原型对象（其 [[Prototype]] 是 %TypedArray.prototype%）。
    virtual void setTypedArrayPrototype(TypedArrayKind kind, Value* proto) = 0;
    virtual Value* typedArrayPrototype(TypedArrayKind kind) const = 0;

    // intlBackend: 宿主注入的 Intl 后端；未注入时为内置最小后端。
    virtual const NJS_IntlBackend* intlBackend() const = 0;

    // createIntlNumberFormat: 创建 Intl.NumberFormat 实例（ECMA-402 §15）。
    // formatter 的所有权转移给 Data，Data 析构时调用 backend->formatter_destroy。
    virtual Value* createIntlNumberFormat(const NJS_IntlBackend* backend,
                                          NJS_IntlFormatter* formatter,
                                          std::string_view locale,
                                          std::string_view bindName = {}) = 0;

    // setIntlNumberFormatPrototype / intlNumberFormatPrototype: bootstrap 专用。
    virtual void setIntlNumberFormatPrototype(Value* proto) = 0;
    virtual Value* intlNumberFormatPrototype() const = 0;

    // createIntlDateTimeFormat: 创建 Intl.DateTimeFormat 实例（ECMA-402 §15）。
    // formatter 所有权转移给 Data。
    virtual Value* createIntlDateTimeFormat(const NJS_IntlBackend* backend,
                                            NJS_IntlFormatter* formatter,
                                            std::string_view locale,
                                            std::string_view bindName = {}) = 0;
    virtual void setIntlDateTimeFormatPrototype(Value* proto) = 0;
    virtual Value* intlDateTimeFormatPrototype() const = 0;

    // createIntlCollator: 创建 Intl.Collator 实例（ECMA-402 §15）。
    virtual Value* createIntlCollator(const NJS_IntlBackend* backend,
                                      NJS_IntlCollator* collator,
                                      std::string_view locale,
                                      std::string_view bindName = {}) = 0;
    virtual void setIntlCollatorPrototype(Value* proto) = 0;
    virtual Value* intlCollatorPrototype() const = 0;

    // createIntlPluralRules: 创建 Intl.PluralRules 实例（ECMA-402 §15）。
    virtual Value* createIntlPluralRules(const NJS_IntlBackend* backend,
                                         NJS_IntlPluralRules* rules,
                                         std::string_view locale,
                                         std::string_view bindName = {}) = 0;
    virtual void setIntlPluralRulesPrototype(Value* proto) = 0;
    virtual Value* intlPluralRulesPrototype() const = 0;

    // createIntlRelativeTimeFormat: 创建 Intl.RelativeTimeFormat 实例
    // （ECMA-402 §16）。
    virtual Value* createIntlRelativeTimeFormat(
        const NJS_IntlBackend* backend, NJS_IntlRelativeTimeFormat* formatter,
        std::string_view locale, std::string_view bindName = {}) = 0;
    virtual void setIntlRelativeTimeFormatPrototype(Value* proto) = 0;
    virtual Value* intlRelativeTimeFormatPrototype() const = 0;

    // createIntlListFormat: 创建 Intl.ListFormat 实例（ECMA-402 §13）。
    virtual Value* createIntlListFormat(const NJS_IntlBackend* backend,
                                        NJS_IntlListFormat* formatter,
                                        std::string_view locale,
                                        std::string_view bindName = {}) = 0;
    virtual void setIntlListFormatPrototype(Value* proto) = 0;
    virtual Value* intlListFormatPrototype() const = 0;

    // createIntlDisplayNames: 创建 Intl.DisplayNames 实例（ECMA-402 §12）。
    virtual Value* createIntlDisplayNames(const NJS_IntlBackend* backend,
                                          NJS_IntlDisplayNames* names,
                                          std::string_view locale,
                                          std::string_view bindName = {}) = 0;
    virtual void setIntlDisplayNamesPrototype(Value* proto) = 0;
    virtual Value* intlDisplayNamesPrototype() const = 0;

    // createIntlSegmenter: 创建 Intl.Segmenter 实例（ECMA-402 §18）。
    virtual Value* createIntlSegmenter(const NJS_IntlBackend* backend,
                                       NJS_IntlSegmenter* segmenter,
                                       std::string_view locale,
                                       std::string_view bindName = {}) = 0;
    virtual void setIntlSegmenterPrototype(Value* proto) = 0;
    virtual Value* intlSegmenterPrototype() const = 0;

    // createIntlSegments: 创建 Segments 对象（ECMA-402 §18.4）。
    // starts/lengths 以 UTF-16 码元为单位；records 为记录数组。
    virtual Value* createIntlSegments(std::string_view input,
                                      std::vector<size_t> starts,
                                      std::vector<size_t> lengths, Value* records,
                                      std::string_view bindName = {}) = 0;
    virtual void setIntlSegmentsPrototype(Value* proto) = 0;
    virtual Value* intlSegmentsPrototype() const = 0;

    // createIntlLocale: 创建 Intl.Locale 实例（ECMA-402 §14）。
    // localeName 为含 unicode 扩展的完整规范标签；timeZone 为空串表示 undefined。
    virtual Value* createIntlLocale(std::string_view localeName,
                                    std::string_view timeZone,
                                    std::string_view bindName = {}) = 0;
    virtual void setIntlLocalePrototype(Value* proto) = 0;
    virtual Value* intlLocalePrototype() const = 0;

    // createWorkerObject: 创建 Worker 实例（WorkerBuiltins 专用）。
    // data 必须是 WorkerData（gc->allocData 分配），所有权转移给 GC。
    virtual Value* createWorkerObject(IData* data,
                                      std::string_view bindName = {}) = 0;
    virtual void setWorkerPrototype(Value* proto) = 0;
    virtual Value* workerPrototype() const = 0;

    // createMessagePortObject: 创建 MessagePort 实例（PortBuiltins 专用）。
    // data 必须是 MessagePortData（gc->allocData 分配），所有权转移给 GC。
    virtual Value* createMessagePortObject(IData* data,
                                           std::string_view bindName = {}) = 0;
    virtual void setMessagePortPrototype(Value* proto) = 0;
    virtual Value* messagePortPrototype() const = 0;

    // createRegExp: 编译 pattern/flags 并创建 RegExp 对象（ES2024 §22.2.3.1）。
    // 编译失败返回 SyntaxError 异常值；成功返回 RegExp（lastIndex 初始化为 0）。
    // 原型自动挂 regexpPrototype()。
    virtual Value* createRegExp(std::string_view pattern, std::string_view flags,
                                std::string_view bindName = {}) = 0;

    // 复用 Compiler 预编译的 Program（字面量路径）：program 非空时跳过编译，
    // 仅解析 flags 填充 RegExpData 的布尔槽位。program 为空等价于上面的重载。
    virtual Value* createRegExp(
        std::string_view pattern, std::string_view flags,
        const std::shared_ptr<const neo::regex::Program>& program,
        std::string_view bindName = {}) = 0;

    // createRegExpStringIterator: 创建 @@matchAll 的惰性迭代器
    // （ES2024 §22.2.9）。原型挂 %Iterator.prototype%，next 由调用方定义。
    virtual Value* createRegExpStringIterator(Value* regexp, Value* string,
                                              bool global, bool unicode,
                                              std::string_view bindName = {}) = 0;

    // regexpPrototype / regexpCtor: %RegExp.prototype% 与 %RegExp% 构造函数。
    // 由 RegExpBuiltins 建立，生命周期与 Context 一致。
    virtual Value* regexpPrototype() const = 0;
    virtual void setRegExpPrototype(Value* proto) = 0;
    virtual Value* regexpCtor() const = 0;
    virtual void setRegExpCtor(Value* ctor) = 0;

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

    // setBigIntPrototype: bootstrap 专用——BigInt.prototype 建立后设置，
    // 此后 createPrimitiveWrapper(BigInt) 自动挂链。宿主不应调用。
    virtual void setBigIntPrototype(Value* proto) = 0;

    // setIteratorBuiltins: bootstrap 专用——迭代器原型与构造函数建立后设置，
    // 此后 createArrayIterator / createStringIterator / createIteratorWrapper
    // 自动挂链。宿主不应调用。
    virtual void setIteratorBuiltins(Value* iteratorProto, Value* iteratorCtor,
                                     Value* arrayIteratorProto,
                                     Value* stringIteratorProto,
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
    // hasProperty: `in` / [[HasProperty]]，返回 Boolean 或 Exception/Fatal。
    virtual Value* hasProperty(Value* obj, Value* key) = 0;
    virtual Value* deleteProperty(Value* obj, Value* key) = 0;

    // deleteIdentifier: sloppy `delete Identifier`（§13.5.1.2）。
    // 声明绑定（含全局声明记录）不可删除返回 false；with 属性与全局对象属性
    // 走 DeleteProperty；未解析引用返回 true。strict 下由 Sema 提前报错。
    virtual Value* deleteIdentifier(std::string_view name) = 0;
    virtual Value* defineProperty(Value* obj, Value* key, Value* value,
                                  bool writable = true, bool enumerable = true,
                                  bool configurable = true) = 0;

    // defineGlobal: 在全局对象上定义属性（内建注册用）。
    // 成功返回 true；OOM/定义失败返回 false。
    virtual bool defineGlobal(std::string_view name, Value* value,
                              bool writable = true, bool enumerable = false,
                              bool configurable = true) = 0;

    // defineGlobalLexical: 在全局声明记录（GlobalScope）定义 let/const/class。
    // 与 define 的区别是目标固定为全局声明记录，不随 current() 变化。
    // 成功返回值槽；重复声明返回 SyntaxError。
    virtual Value* defineGlobalLexical(std::string_view name, Value* value,
                                       bool mutableBinding = true,
                                       bool initialized = true) = 0;

    // defineAccessor: 定义访问器属性（getter/setter 须为 Function 或 null）。
    // 成功返回 obj，失败返回 Exception。
    virtual Value* defineAccessor(Value* obj, Value* key, Value* getter, Value* setter,
                                  bool enumerable = false, bool configurable = true) = 0;

    // defineAccessorPartial: 只定义实际存在的一侧，缺失侧保留已有描述符
    // （对象字面量 get/set 合并，ES2024 §13.2.5.5）。成功返回 obj，失败返回 Exception。
    virtual Value* defineAccessorPartial(Value* obj, Value* key, Value* getter,
                                         bool hasGetter, Value* setter,
                                         bool hasSetter, bool enumerable,
                                         bool configurable) = 0;

    // 字符串键便捷重载：内部 createString(key) 后走上面的 Value* 版本。
    // 用于 C++ 侧固定键（如 "prototype"/"constructor"/"name"）。
    Value* getProperty(Value* obj, std::string_view key) {
        return getProperty(obj, createString(key));
    }
    Value* setProperty(Value* obj, std::string_view key, Value* value) {
        return setProperty(obj, createString(key), value);
    }
    Value* hasProperty(Value* obj, std::string_view key) {
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

    // forInKeys: for-in 的候选条目（ES2024 §14.7.5.9 EnumerateObjectProperties）。
    // 沿原型链收集所有自有字符串键（含非 enumerable），跳过已被前层处理过的
    // 同名键；返回扁平数组 [owner0, key0, owner1, key1, ...]。
    // 是否产出由 isEnumerableOwn 在处理时再次判定（删除/改属性可见）。
    // null/undefined 返回空数组，原始值先 ToObject。返回数组或错误值。
    virtual Value* forInKeys(Value* obj) = 0;

    // isEnumerableOwn: 处理 for-in 候选时检查自有属性仍存在且 enumerable。
    // 返回 Boolean 或错误值。
    virtual Value* isEnumerableOwn(Value* obj, Value* key) = 0;

    // createScriptFunction: 创建 script 函数 Value（ADR-060 §9）。
    //   code/entryPc 借用（Module 持有 Code）；paramCount 用于 length 与实参补齐；
    //   hasRest 标记 rest 参数；constructible/hasPrototypeProperty 区分普通函数
    //   与箭头函数/方法。返回函数值或 OOM 哨兵。
    virtual Value* createScriptFunction(const vm::Code* code, uint32_t entryPc,
                                        size_t paramCount, bool hasRest,
                                        bool constructible,
                                        bool hasPrototypeProperty,
                                        std::string_view bindName = {}) = 0;

    // createScriptClassConstructor: 创建脚本 class 构造器（ClassData）。
    // superCtor 为 nullptr 表示 base；Null Value 表示 extends null。负责创建
    // prototype、链接原型链并设置 name/ownerClass。
    virtual Value* createScriptClassConstructor(const vm::Code* code,
                                                uint32_t entryPc,
                                                size_t paramCount, bool hasRest,
                                                bool isDerived, Value* superCtor,
                                                std::string_view name) = 0;

    // defineClassMethod: 把已创建的脚本函数定义为 class 方法/访问器。
    // isStatic=false 落到 prototype，否则落到构造器自身；kind: 0=data,
    // 1=getter, 2=setter。设置 [[HomeObject]]/ownerClass 并标记不可构造。
    virtual Value* defineClassMethod(Value* classCtor, Value* key, Value* fn,
                                     bool isStatic, uint32_t kind) = 0;

    // defineClassField: 注册 public 字段。initializer 为脚本函数或 undefined
    // （无初始化器）。实例字段延后到构造期初始化；静态字段立即以 classCtor
    // 为 this 求值并按源码顺序定义。
    virtual Value* defineClassField(Value* classCtor, Value* key,
                                    Value* initializer, bool isStatic) = 0;

    // bindFunctionThis: 把 this 焊死到函数（箭头函数/显式绑定）。
    // 调用时 invoke 优先使用该值，忽略调用点传入的 thisArg。
    virtual void bindFunctionThis(Value* fn, Value* thisValue) = 0;

    // cloneValue: 复制一个 Value（data 层 clone）并登记进当前作用域。
    // 用于需要快照旧值的场景（如后缀 ++/--：绑定槽会被原地改写）。
    virtual Value* cloneValue(Value* source) = 0;

    // copyForStorage: 创建 data-owned 副本（新 Value 共享同一 data，不登记当前
    // 作用域），用于 scope→data 跨域存储（对象属性/数组元素/绑定实参等）。
    // 两域所有权见 ADR-065 §4：跨域必须复制 Value，避免作用域释放后悬垂。
    virtual Value* copyForStorage(Value* source) = 0;

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

    // setModuleCompilation: 绑定模块的编译产物与静态链接元数据（模块不存在则
    // 先创建）。importModule 首次加载时依次执行：
    //   1. instantiate：符号定义区 + 函数定义区（建槽、创建函数对象）；
    //   2. link：递归实例化依赖，建立 import 槽位别名与 export 槽位映射；
    //   3. evaluate：按依赖 DFS 后序执行模块体。
    // 成功返回 true；模块创建失败（OOM）返回 false。
    virtual bool setModuleCompilation(std::string_view id,
                                      ModuleCompilation compilation) = 0;

    // importModule: 取模块；若尚未加载且已登记 loader，则在模块 root scope 下
    // 执行 loader 并标记已加载。循环依赖时直接返回当前（部分）导出对象。
    // specifier 可为绝对路径，或以 ./ ../ 开头的相对路径——后者以当前正在
    // 加载的模块目录为基准解析（模块记录自己的 dirname）。
    // 返回导出对象；loader 失败时原样返回其 Exception/Fatal。
    virtual Value* importModule(std::string_view specifier) = 0;

    // importModuleDynamic: 动态 import()。specifier 经 ToString 后解析（相对路径
    // 以 referrer 模块 id 的目录为基准），返回 Promise：
    //   - 成功 → 以模块命名空间 fulfill；
    //   - 说明符转换或加载失败 → 以错误对象 reject。
    // 本引擎的加载器是同步的，Promise 在返回前已 settle（reaction 仍走微任务）。
    virtual Value* importModuleDynamic(Value* specifier,
                                       std::string_view referrer) = 0;

    // createModuleNamespace: 创建模块命名空间 exotic 对象（原型为 null，
    // @@toStringTag = "Module"）。导出属性是活绑定，见 exportBinding。
    virtual Value* createModuleNamespace(std::string_view bindName = {}) = 0;

    // importMeta: 当前正在执行的模块的 import.meta 对象（首次访问时创建并
    // 缓存在 Module 上）。对象含 url 属性，值为模块 id。非模块上下文返回
    // undefined（Sema 已禁止该用法，此处仅作防御）。
    virtual Value* importMeta() = 0;

    // exportBinding: 把当前作用域链上的 localName 槽位以 exportName 暴露到
    // exports（模块命名空间对象）。属性直接持有该槽位 Value*，**不 clone**；
    // 后续 store 原地替换槽位的 meta+data，导入方读取即可见最新值。
    // 返回 exportName 对应的 Value（即槽位 Value），或 Exception。
    virtual Value* exportBinding(Value* exports, std::string_view exportName,
                                 std::string_view localName) = 0;

    // exportSlot: 把导出名直接映射到绑定槽位（link 期建立，不 clone）。
    // 槽位可以是本地绑定、导入别名或 re-export 的源槽位。
    virtual Value* exportSlot(Value* exports, std::string_view exportName,
                              Value* slot) = 0;

    // getExportSlot: 取导出名对应的绑定槽位（不读取当前值）。
    // 不存在返回 ReferenceError。
    virtual Value* getExportSlot(Value* exports,
                                 std::string_view exportName) = 0;

    // bindImportSlot: 在当前作用域建立导入绑定 localName -> 共享槽位。
    // 绑定只读；读取直接返回槽位当前值，导出方 store 原地替换即可见。
    // 重复声明返回 SyntaxError。
    virtual Value* bindImportSlot(std::string_view localName, Value* slot) = 0;

    // bindImport: bindImportSlot 的便捷形式——从 namespace 解析 exportName
    // 对应的槽位后建立别名。导出不存在返回 ReferenceError。
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
    // mutableBinding/initialized 用于模块提升：lexical 声明先以
    // <uninitialized> 建槽（initialized=false），求值期再 initialize。
    // varBinding: 标记变量环境绑定（var/函数声明/形参），eval 的 var
    // 声明可与之同名复用，而非报词法冲突。
    virtual Value* define(std::string_view name, Value* source,
                          bool mutableBinding = true,
                          bool initialized = true,
                          bool varBinding = false) = 0;
    virtual Value* store(std::string_view name, Value* source) = 0;

    // storeVar: 写入变量环境绑定（跳过中间的词法作用域）。
    // 目标依次为 eval 变量环境、当前函数环境、模块根。用于 Annex B.3.3
    // 块级函数同步到外层 var 绑定。
    virtual Value* storeVar(std::string_view name, Value* source) = 0;

    // initialize: 初始化一个已提升但尚未初始化的绑定（TDZ 解除）。
    // 允许写 <uninitialized> 槽位，不受 const 的 mutableBinding=false 限制；
    // 已初始化或绑定不存在返回 Exception。
    virtual Value* initialize(std::string_view name, Value* source) = 0;

    // load: 语言级标识符读取。未初始化绑定返回 ReferenceError；
    // 未找到返回 ReferenceError。内部槽位访问用 Scope::lookup。
    virtual Value* load(std::string_view name) = 0;

    // loadSoft: typeof 专用标识符读取。名字完全未解析时返回 undefined
    // （§13.5.3）；TDZ/未初始化 import 仍返回 ReferenceError。
    virtual Value* loadSoft(std::string_view name) = 0;

    // —— eval（§19.2.1）——
    // declareVar: eval 的 var 声明——只在变量环境缺失时建 undefined 槽，
    // 已有绑定保持原值。成功返回 nullptr，失败返回错误值。
    virtual Value* declareVar(std::string_view name) = 0;

    // —— 全局声明（§16.1.7 / §19.2.1.3）——
    // declareGlobalVar: CreateGlobalVarBinding。全局对象已有同名属性则复用
    // （不初始化）；否则按 configurable 创建 undefined 数据属性。
    virtual Value* declareGlobalVar(std::string_view name,
                                    bool configurable) = 0;
    // declareGlobalFunction: CreateGlobalFunctionBinding 的声明阶段。
    // 校验/重配置全局对象属性，函数对象随后由 store 写入。
    virtual Value* declareGlobalFunction(std::string_view name,
                                         bool configurable) = 0;
    // checkGlobalLexical: HasRestrictedGlobalProperty。全局 let/const 名与
    // 全局对象上不可配置的自有属性冲突时返回 SyntaxError。
    virtual Value* checkGlobalLexical(std::string_view name) = 0;

    // evalSource: 执行 eval 源码。source 非 String 时原样返回。
    // direct=true 时在 callerScope 的词法环境中执行，var 进入调用方变量环境；
    // direct=false（间接 eval）在 global 执行，var 进入 global。
    // 解析/编译错误返回 SyntaxError。
    virtual Value* evalSource(Value* source, Scope* callerScope, bool direct) = 0;

    // evalFunction: 内建 %eval% 函数对象（直接 eval 判定用）。
    virtual Value* evalFunction() const = 0;
    virtual void setEvalFunction(Value* fn) = 0;

    // retainCode: 接管一段编译产物，生命周期与 Context 一致。
    // 脚本创建的函数可能逃逸并长期引用 Code（VM/Function 只借用 const Code*），
    // 故执行前必须把 Code 交给 Context 持有。用于 Worker 脚本等宿主侧执行。
    virtual void retainCode(std::unique_ptr<vm::Code> code) = 0;

    // —— 可赋值性（ADR-030 阶段 5）——
    // mutability 属于作用域绑定而不是 Value：同一个 Value* 可以在不同绑定中
    // 具有不同的可变性（例如 export let 在导入方是只读）。
    // setBindingMutable: 设置 scope 中 name 绑定的可变性（const 为 false）。
    // isBindingMutable: 查询绑定可变性；绑定不存在时返回 true。
    // store 对 mutableBinding=false 的 target 返回 Exception（禁止赋值）。
    virtual void setBindingMutable(Scope* scope, std::string_view name,
                                   bool m) = 0;
    virtual bool isBindingMutable(const Scope* scope,
                                  std::string_view name) const = 0;

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
    // postExternalTask: 线程安全的外部任务投递（Worker 消息唤醒）。
    // 任意线程可调用；任务在 owner 线程的 runOneTask 中执行（作为宏任务，
    // 排在微任务之后）。task 接收 owner 线程的 Context，可安全创建/访问 Value。
    // 返回 false 表示 Context 已关闭（析构中）。
    virtual bool postExternalTask(std::function<void(IContext*)> task) = 0;
    // setExternalTaskWake: 设置外部任务入队后的唤醒回调（线程安全）。
    // Worker 用它把外部任务唤醒映射到自己的阻塞消息循环。回调在投递线程执行，
    // 不得访问 Context/Value。
    virtual void setExternalTaskWake(std::function<void()> wake) = 0;
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
// intl 为宿主 Intl 后端（无所有权，生命周期必须覆盖 Context）；nullptr 或
// ABI 不兼容时使用内置最小后端（NJS_MinimalIntlBackend）。
std::unique_ptr<IContext> createContext(infra::IAllocator* alloc,
                                         infra::Console* console,
                                         infra::IClock* clock = nullptr,
                                         const NJS_IntlBackend* intl = nullptr);

}  // namespace neo::core

#endif  // NEO_CORE_ICONTEXT_H
