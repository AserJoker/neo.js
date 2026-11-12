// IType.h - Value 的 meta（IType）抽象接口（ADR-016 修订）
//
// IType 封装 Value 的多态行为，无状态，每种类型一个全局单例。Value 持 IType* 共享，
// 无须引用计数，GC 不跟踪 IType（生命周期与进程一致）。
// TypeKind 枚举用于快速判同类型、分派调试、序列化。
//
// 本阶段（M2 阶段 3）只实现 kind + name，行为方法（add/call/get_property/...）
// 签名带 Context*，待 Context（阶段 4）引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）、006（异常即值）

#ifndef NEO_CORE_ITYPE_H
#define NEO_CORE_ITYPE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neo::core {

// 前向声明
class IContext;
class Value;

// 属性描述符（ADR-018 §2 / ES2024 §6.2.6）。
// 数据属性：value + writable；访问器属性：getter/setter（任一非空即访问器）。
// 位于公共头：内部方法层（IType::getOwnProperty）与 Proxy 需要跨层传递描述符。
struct Property {
    Value* value = nullptr;   // 属性值（数据属性）
    bool writable = true;
    bool enumerable = true;
    bool configurable = true;
    Value* getter = nullptr;  // 访问器属性 getter
    Value* setter = nullptr;  // 访问器属性 setter
};

// 规范属性键（ES2024 §6.1.7）：String 或 Symbol。
// 内部方法层用它避免在原型链每一跳重复 ToPropertyKey；
// value 是规范键本身（Proxy trap 需要原样传给用户代码）。
struct PropertyKey {
    Value* value = nullptr;        // String 或 Symbol Value*
    bool isSymbol = false;
    std::string name;              // isSymbol == false 时的字符串键
    const void* symbol = nullptr;  // isSymbol == true 时的 SymbolData* 身份
};

// 类型枚举：快速判同类型、分派调试、序列化
enum class TypeKind : uint8_t {
    Undefined,
    Null,
    Boolean,
    Number,
    String,
    Exception,      // 可捕获异常（ADR-017 §3）
    Fatal,          // 不可捕获致命错误（ADR-017 §4）
    Uninitialized,  // TDZ 哨兵（let/const 初始化前，VM load 时消费）
    Function,       // 函数对象（ADR-021，本阶段仅 native）
    Object,         // 普通对象（ADR-018 对象模型）
    Array,          // 数组（ADR-018 §6 独立 elements 容器）
    Symbol,         // 符号原始类型（ES2024 §6.1.5）
    // 原始值包装对象（ES2024 §10.4）：Boolean/Number/String/Symbol 的 new 形式。
    // 持 [[PrimitiveValue]] 内部槽，其余对象行为（属性/原型/GC）与普通对象一致。
    PrimitiveWrapper,
    // 数组迭代器（ES2024 §23.1.5）：Array.prototype.keys/values/entries 的产物。
    // 持 [[IteratedObject]]/[[ArrayIteratorNextIndex]]/[[ArrayIterationKind]] 内部槽。
    ArrayIterator,
    // 迭代器包装对象（ES2025 §27.1.4.2 Iterator.from 的 %WrapForValidIteratorPrototype% 实例）。
    // 持 [[Iterated]] 内部槽（iterator + nextMethod），把任意 iterator-like 对象提升为 Iterator。
    IteratorWrapper,
    // Error 对象（ES2024 §20.5）：普通对象 + [[ErrorData]] 内部槽。
    // 内部槽是标记性的（无状态），用于类型判定（Object.prototype.toString 的 tag、
    // 后续 Error.isError）。TypeError/RangeError 等子类共用本 kind，只差原型与 name。
    Error,
    // Promise 对象（ES2024 §27.2）：普通对象 + Promise 内部槽
    // （[[PromiseState]]/[[PromiseResult]]/[[PromiseReactions]]/[[PromiseIsHandled]]）。
    Promise,
    // 控制流信号值（ADR-046）：kind + offset + value。与 Exception/Fatal 正交，
    // 不是错误；只由协程函数体 / VM 执行循环产生、由协程 runner 消费。
    // 非对象类型（objectLayout=false），行为方法全部透传 self。
    Interrupt,
    // 协程对象（ADR-045）：可恢复的计算句柄，持上下文（孤立执行作用域 + 调用栈）、
    // 断点、状态。对象布局复用 ObjectData（属性/原型/GC）。
    Coroutine,
    // 异步生成器对象（ADR-045 §8 / ES2024 §27.6）：Coroutine + Promise 请求队列。
    // 对象布局复用 ObjectData；内部槽在 CoroutineData 基础上增加异步状态与队列。
    AsyncGenerator,
    // 模块命名空间 exotic 对象（ES2024 §10.4.6）：原型为 null，导出属性为
    // 不可写/不可配置的活绑定——Property 直接持有模块作用域的 Value*，不 clone。
    ModuleNamespace,
    // Proxy exotic 对象（ES2024 §10.5）：[[ProxyTarget]] + [[ProxyHandler]]，
    // 所有内部方法转发到 handler 上的 trap。可调用/可构造性继承自 target。
    Proxy,
    // 后续阶段：BigInt/...
};

// Value 的 meta：抽象接口，每种 JS 类型一个实现类（单例）。
// 无状态——所有行为纯函数（按 ctx + 操作数计算），本阶段仅 kind + name。
class IType {
    TypeKind kind_;
    bool objectLayout_;  // 是否具备对象布局（见 hasObjectLayout）

public:
    // objectLayout：该类型的 Data 是否派生自 ObjectData（即具备
    // properties/order/symbols/prototype 布局与完整对象行为）。
    // 由各 Type 子类在构造时声明，替代按 TypeKind 手写枚举的中心清单——
    // 新增 exotic 对象类型（Map/Set/Promise/RegExp/Generator/TypedArray...）
    // 只需在自己的 Type 构造里传 true，所有对象模型分派点自动覆盖。
    explicit IType(TypeKind k, bool objectLayout = false)
        : kind_(k), objectLayout_(objectLayout) {}
    virtual ~IType() = default;

    TypeKind kind() const { return kind_; }
    virtual const char* name() const = 0;

    // 是否具备对象布局（可走属性/原型链/ToPrimitive/ToObject 等对象路径）。
    // 原始值类型（Undefined/Null/Boolean/Number/String/Symbol/Exception/Fatal/
    // Uninitialized）为 false；Object/Function/Array/PrimitiveWrapper 及后续
    // 所有 exotic 对象类型为 true。
    bool hasObjectLayout() const { return objectLayout_; }

    // —— 索引型 exotic 对象能力（ES2024 §10.4.2 / §10.4.3）——
    // 具备「整数索引自有属性 + length 自有属性」的类型覆写下列方法：
    // Array、String 包装，以及后续 TypedArray、arguments、Module Namespace 等。
    // 对象模型的枚举/自有属性判定/索引查找全部经此访问，新增此类类型无需改中心代码。
    //
    // hasIndexedElements：是否具备索引元素与 length 自有属性（默认 false）。
    // indexedLength：索引元素个数（即 length 属性值）。
    // indexedElement：第 index 个元素；不存在（空洞/越界）返回 nullptr。
    // 索引元素恒为 enumerable 的数据属性（Array/TypedArray/String 均如此）。
    virtual bool hasIndexedElements() const { return false; }
    // 是否具备 "length" 自有属性。默认与 hasIndexedElements 一致；
    // String 包装只有真正包装字符串时才有 length（Boolean/Number/Symbol 包装没有）。
    virtual bool hasIndexedLength(Value* /*self*/) const { return hasIndexedElements(); }
    virtual uint32_t indexedLength(Value* /*self*/) const { return 0; }
    virtual Value* indexedElement(IContext* /*ctx*/, Value* /*self*/,
                                  uint32_t /*index*/) const {
        return nullptr;
    }
    // 索引元素与 length 的属性特性（seal/freeze 会收紧）。
    // 默认：索引可写/可配置，length 只读；Array 覆写为自身状态。
    virtual bool indexedWritable(Value* /*self*/) const { return true; }
    virtual bool indexedConfigurable(Value* /*self*/) const { return true; }
    virtual bool indexedLengthWritable(Value* /*self*/) const { return false; }
    // SetIntegrityLevel 钩子：索引型 exotic 收紧索引/length 的特性。
    // 默认无操作；Proxy 在无 defineProperty trap 时转发 target。
    virtual void setIndexedIntegrity(IContext* /*ctx*/, Value* /*self*/,
                                     bool /*frozen*/) {}

    // —— 对象内部方法（ES2024 §10.1，Proxy 的落点）——
    // 属性算法（Get/Set/Has/Delete/OwnPropertyKeys）必须逐跳经此分派，
    // 否则原型链上的 Proxy 会被绕过。默认实现是 OrdinaryGetOwnProperty /
    // OrdinaryOwnPropertyKeys（在 private/core/types/Object.cpp）。
    //
    // getOwnProperty（§10.1.5）：存在自有属性时 exists=true 并填充 out。
    // 成功返回 nullptr；trap 抛错返回 Exception/Fatal。
    virtual Value* getOwnProperty(IContext* ctx, Value* self, const PropertyKey& key,
                                  bool& exists, Property& out) const;
    // ownKeys（§10.1.11）：按规范顺序返回全部自有键（含 symbol）。
    // 成功返回 nullptr 并写入 out；trap 抛错返回 Exception/Fatal。
    virtual Value* ownKeys(IContext* ctx, Value* self,
                           std::vector<Value*>& out) const;

    // getWithReceiver（§10.1.8）：[[Get]]，receiver 作为访问器 this。
    // 原型链上的 Proxy 必须经此触发 get trap（不能只查 getOwnProperty）。
    virtual Value* getWithReceiver(IContext* ctx, Value* self, const PropertyKey& key,
                                   Value* receiver);
    // setWithReceiver（§10.1.9）：[[Set]]。返回 Boolean（成功/失败）或
    // Exception/Fatal（真正抛出）。调用方按严格模式决定 false 是否转 TypeError。
    virtual Value* setWithReceiver(IContext* ctx, Value* self, const PropertyKey& key,
                                   Value* value, Value* receiver);

    // isExtensible（§10.1.3）/ preventExtensions（§10.1.4）：返回 Boolean 或错误值。
    // 默认实现读写 ObjectData::extensible；Proxy 转发 trap 并校验不变量。
    virtual Value* isExtensible(IContext* ctx, Value* self);
    virtual Value* preventExtensions(IContext* ctx, Value* self);

    // —— 行为接口（ADR-016 修订 §3）——
    // 统一签名 Value* op(IContext* ctx, Value* a, Value* b) / Value* op(IContext* ctx, Value* self)。
    // 分派规则：永远左操作数的 type 执行（ctx->add(a,b) → a->type()->add(ctx,a,b)）。
    // 跨类型转换：左 type 按所实现操作的 JS 规则主动调换左右（ctx->add(b,a)）。
    // 兜底：返回 NaN 或 Exception（IType 自负责操作有效性）。
    // 全部返回 Value*（正常/Exception/Fatal 同一通道，调用点 Fatal 检查，ADR-006 + ADR-017 §4）。
    // Exception/Fatal 不参与计算——行为方法入口拦截透传（ADR-006 异常即值 + ADR-017 §4）。

    // 二元运算（左操作数 type 执行）
    virtual Value* add(IContext* ctx, Value* a, Value* b) = 0;
    virtual Value* sub(IContext* ctx, Value* a, Value* b) = 0;
    virtual Value* mul(IContext* ctx, Value* a, Value* b) = 0;
    virtual Value* div(IContext* ctx, Value* a, Value* b) = 0;

    // 单目
    virtual Value* toString(IContext* ctx, Value* self) = 0;

    // 调用（ADR-005 native 等价 + ADR-022 统一入口）：
    // ctx->call(fn, self, argc, argv) → fn->type()->call(ctx, fn, self, argc, argv)。
    // 非函数类型的 call 返回 Exception("xxx is not a function")。
    // FunctionType 校验 minArgc 后调 native 指针。
    // Exception/Fatal 透传 self（不参与调用）。
    virtual Value* call(IContext* ctx, Value* self, Value* thisArg,
                        size_t argc, Value* argv[]) = 0;

    // 构造调用（new 操作，ADR-034）：
    // ctx->construct(fn, argc, argv) → fn->type()->construct(ctx, fn, argc, argv)。
    // FunctionType：新建 this 对象（原型 = fn.prototype），以 this 为 thisArg 调 native，
    //               返回值是 Object/Function 则用它，否则用 this。
    // 非函数类型返回 Exception("xxx is not a constructor")。
    // Exception/Fatal 透传 self。
    virtual Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) = 0;

    // —— 属性操作（ADR-018 对象模型）——
    // 分派规则同运算：ctx->getProperty(obj, key) → obj->type()->getProperty(ctx, obj, key)。
    // key 是 Value*：Symbol 保持符号身份（走 symbols 容器），其他类型按 ToPropertyKey
    // 转成字符串（走 properties 容器）。Object/Function 作 key 需 ToPrimitive，暂不支持。
    // 非对象类型的属性操作：get 返回 undefined，set/has/delete 返回 Exception 或 false。
    // Object 的 get/has 沿原型链查找（JS `in` 语义）；set 按 OrdinarySetWithOwnDescriptor
    // 语义处理原型链（含访问器 setter 调用）；delete/define 只操作自有属性。
    // 访问器属性：get 命中 getter 时以 receiver 为 this 调用，set 命中 setter 同理。
    virtual Value* getProperty(IContext* ctx, Value* self, Value* key) = 0;
    virtual Value* setProperty(IContext* ctx, Value* self, Value* key, Value* value) = 0;
    // hasProperty（`in` / [[HasProperty]]）：返回 Boolean，或 Exception/Fatal。
    // 走错误通道，Proxy 的 has trap 抛错可原样透传。
    virtual Value* hasProperty(IContext* ctx, Value* self, Value* key) = 0;
    virtual Value* deleteProperty(IContext* ctx, Value* self, Value* key) = 0;
    // [[DefineOwnProperty]]（ES2024 §10.1.6）：返回 Boolean（操作成功/失败），
    // 或 Exception/Fatal（真正抛出）。Boolean 结果不携带属性值，公共 API
    // （Context::defineProperty）在成功时返回被定义的值。
    virtual Value* defineProperty(IContext* ctx, Value* self, Value* key,
                                  Value* value, bool writable, bool enumerable,
                                  bool configurable) = 0;

    // 定义访问器属性（getter/setter 至少一个非空，ES2024 §10.1.6.3）。
    // getter/setter 须为 Function 或 null。返回值同 [[DefineOwnProperty]]：
    // Boolean 表示结果，Exception 表示抛出。
    virtual Value* defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                                  Value* setter, bool enumerable, bool configurable) = 0;

    // —— 原型链（ADR-018 §1）——
    // getPrototype: 返回对象的原型；无原型返回 null。非对象类型返回 undefined。
    // setPrototype（[[SetPrototypeOf]]）：返回 Boolean（成功/失败）或 Exception。
    //               公共 API（Context::setPrototype）成功时返回 self。
    virtual Value* getPrototype(IContext* ctx, Value* self) = 0;
    virtual Value* setPrototype(IContext* ctx, Value* self, Value* proto) = 0;
};

// —— IType 单例获取（具体实现在 private/core/Types.cpp）——
// 每种类型一个全局单例，Value 持 IType* 共享。返回的指针生命周期与进程一致。
IType* getUndefinedType();
IType* getNullType();
IType* getBooleanType();
IType* getNumberType();
IType* getStringType();
IType* getExceptionType();
IType* getFatalType();
IType* getUninitializedType();
IType* getFunctionType();
IType* getObjectType();
IType* getArrayType();
IType* getSymbolType();
IType* getPrimitiveWrapperType();
IType* getArrayIteratorType();
IType* getIteratorWrapperType();
IType* getErrorType();
IType* getPromiseType();
IType* getInterruptType();
IType* getCoroutineType();
IType* getAsyncGeneratorType();
IType* getModuleNamespaceType();
IType* getProxyType();

// —— TypeKind → Type 单例（Types.cpp 中的唯一映射表）——
// 未注册的 kind 返回 nullptr。新增类型时只需在此加一行映射，
// 对象模型分派点（isObjectKind/hasObjectLayout）随之自动覆盖。
IType* getTypeByKind(TypeKind k);

// 该 kind 是否具备对象布局（即其 Type 声明了 objectLayout）。
// 这是 isObjectKind 的唯一真相源：新增 exotic 对象类型时，
// 只要其 Type 构造传 objectLayout=true，此处无需改动。
bool kindHasObjectLayout(TypeKind k);

}  // namespace neo::core

#endif  // NEO_CORE_ITYPE_H
