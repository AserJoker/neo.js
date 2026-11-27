// Context.h (private) - Context 实现声明
//
// Context 持有 GC 所有权（unique_ptr<IGCEngine>），注入 IAllocator/Console（无所有权），
// 创建 global/root scope 并管理 current 栈，另持一个纯展示用的调用栈（ADR-037）。
// global 作用域树的**所有权在 GC**（ADR-044）：Context 创建后交给 GC，析构时由
// ~MarkSweepGC 先销毁整棵树再 collect。IAllocator/Console 不销毁（外部持有）。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、012（Scope 树）、030 阶段 4、
//          044（可达性标记）

#ifndef NEO_CORE_PRIVATE_CONTEXT_H
#define NEO_CORE_PRIVATE_CONTEXT_H

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/ModuleEntry.h"
#include "core/Scope.h"
#include "core/Value.h"

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace neo::vm {
struct Code;
}  // namespace neo::vm

namespace neo::core {

// Value 创建路径统计（P0 GC 重构评估用，只读）。direct = GC 总分配数 -
// tracked - ephemeral，由探针计算。
struct ValueCreationStats {
    uint64_t tracked = 0;    // createTracked（登记到 current scope）
    uint64_t ephemeral = 0;  // createEphemeral（不登记 scope，但仍注册进 GC）
    // 按 TypeKind 的分配数。只在 Context 的真实创建路径统计，避免读取
    // 测试用占位 IType（reinterpret_cast）的 kind()。
    std::array<uint64_t, kTypeKindCount> byKind{};
};

class Context : public IContext {
    // 线程亲和（多线程模型）：Context/Scope/Value/GC 只允许在创建线程访问。
    // 跨线程只允许经过 MessageQueue 传递序列化消息。
    std::thread::id ownerThread_;
    // true = Worker 线程内创建的上下文（宿主策略用，如 Atomics.wait）。
    bool isWorkerContext_ = false;
    // VM 停止标记：跨线程写入（worker terminate / 调试器），VM 每条指令后读取。
    std::atomic<bool> stopRequested_ {false};
    infra::IAllocator* alloc_;              // 注入，无所有权
    std::unique_ptr<IGCEngine> gc_;         // Context 持有所有权
    ValueCreationStats creationStats_;      // 诊断统计（P0）
    infra::Console* console_;              // 注入，无所有权
    infra::IClock* clock_;                 // 注入，无所有权（事件循环时间源）

    Scope* global_ = nullptr;              // Context 创建，析构时销毁（递归销毁整棵树）
    // 全局对象（GlobalScope 的对象记录）：全局 var/函数声明与内建都落在它上面。
    Value* globalObject_ = nullptr;
    // 当前模块根作用域（可被 setRootScope 切换）。脚本顶层直接是 global_：
    // GlobalScope 同时承载对象记录与全局声明记录（B2）。
    Scope* root_ = nullptr;
    Scope* current_ = nullptr;             // 当前作用域（Scope 的 parent 链即作用域栈）
    // 当前函数环境（ADR-047）：this / new.target 的宿主 ArgumentScope。
    // invoke 保存/恢复；顶层指向模块根作用域。
    Scope* currentFunctionEnv_ = nullptr;
    // 当前正在执行的函数 Value（ADR-047 native 扩展库）：invoke 保存/恢复。
    Value* currentFunction_ = nullptr;
    // 内建 %eval% 函数对象（直接 eval 判定用，bootstrap 注册）。
    Value* evalFunction_ = nullptr;
    // eval 执行期间的变量环境：declare_var 优先落在这里（sloppy 直接 eval
    // 进入调用方函数环境）。nullptr 表示落在 current()。
    Scope* evalVarScope_ = nullptr;
    // 当前构造调用的 new.target（ADR-047）：Context::construct 保存/恢复。
    Value* newTarget_ = nullptr;
    // 调用栈（ADR-037）：纯展示信息，与 Scope 链无关。栈顶在末尾。
    // 用 std::string 存帧，内存走宿主堆而非 IAllocator——调用栈是诊断信息，
    // 不应在 OOM 时因 Engine 配额耗尽而无法记录。
    std::vector<CallFrame> callStack_;
    Value* oomValue_ = nullptr;            // OOM 哨兵（预分配 Fatal，挂 global，ADR-031）
    // 立即数化 primitive 单例（不可变，pinRef 作永久根，Context 析构前解除）：
    // 布尔/undefined/null 的 Value+data 不再逐次分配。它们从不作为可变绑定槽位
    // （declareVar 用 createMutableUndefined 新建，赋值走 replaceMeta），
    // 因此共享单例不会被原地改写。
    Value* trueValue_ = nullptr;
    Value* falseValue_ = nullptr;
    Value* undefinedValue_ = nullptr;
    Value* nullValue_ = nullptr;
    // 小整数立即数缓存（含 -0 排除）：[-1, 256] 的整数值共享 Value。
    static constexpr int kSmallIntMin = -1;
    static constexpr int kSmallIntMax = 256;
    static constexpr size_t kSmallIntCount =
        static_cast<size_t>(kSmallIntMax - kSmallIntMin + 1);
    Value* smallInts_[kSmallIntCount] = {};
    // 小索引字符串 data 缓存：数组索引键（"0".."1023"）在 builtin 中极高频。
    // 缓存 Value 仅作 data 宿主（pinRef 永久根）；createIndexString 每次新建
    // Value 共享该不可变 data，避免索引键 Value 被 replaceMeta 原地改写。
    static constexpr uint32_t kIndexStringCacheMax = 1023;
    Value* indexStringValues_[kIndexStringCacheMax + 1] = {};
    // %ThrowTypeError%（unmapped arguments 的 callee 访问器），惰性创建并挂 global。
    Value* throwTypeError_ = nullptr;
    Value* wellKnownSymbols_[kWellKnownSymbolCount] = {};  // well-known symbols（挂 global）
    // 内建原型（ADR-035）：Object.prototype 是所有普通对象原型链的终点，
    // Function.prototype 是所有函数对象的原型（其自身原型是 Object.prototype）。
    // 由 bootstrapBuiltins 创建，挂 global 存活于 Context 生命周期。
    Value* objectPrototype_ = nullptr;
    Value* weakRefPrototype_ = nullptr;
    Value* functionPrototype_ = nullptr;
    Value* arrayPrototype_ = nullptr;   // Array.prototype（自身是数组，ADR-018 §6）
    // 原始值包装对象的原型（ES2024 §20.3/§21.1.3/§21.1.3/§20.4.3）
    Value* booleanPrototype_ = nullptr;
    Value* numberPrototype_ = nullptr;
    Value* stringPrototype_ = nullptr;
    Value* symbolPrototype_ = nullptr;
    Value* bigIntPrototype_ = nullptr;  // %BigInt.prototype%（普通对象，非包装）
    // 迭代器原型（ES2024 §23.1.5 / ES2025 §27.1.4.2）：
    // arrayIteratorPrototype_ 是 %ArrayIteratorPrototype%（其原型是 iteratorPrototype_），
    // iteratorWrapperPrototype_ 是 %WrapForValidIteratorPrototype%（同上）。
    Value* iteratorPrototype_ = nullptr;
    Value* iteratorCtor_ = nullptr;
    Value* arrayIteratorPrototype_ = nullptr;
    Value* stringIteratorPrototype_ = nullptr;
    Value* iteratorWrapperPrototype_ = nullptr;
    Value* iteratorHelperPrototype_ = nullptr;
    // 集合原型与迭代器原型（ES2024 §24）
    Value* mapPrototype_ = nullptr;
    Value* setPrototype_ = nullptr;
    Value* weakMapPrototype_ = nullptr;
    Value* weakSetPrototype_ = nullptr;
    Value* mapIteratorPrototype_ = nullptr;
    Value* setIteratorPrototype_ = nullptr;
    Value* datePrototype_ = nullptr;
    Value* arrayBufferPrototype_ = nullptr;
    Value* sharedArrayBufferPrototype_ = nullptr;
    Value* dataViewPrototype_ = nullptr;
    // %Int8Array.prototype% … %BigUint64Array.prototype%，
    // 每种元素类型一个（下标即 TypedArrayKind）。
    Value* typedArrayPrototypes_[11] = {};
    // 宿主注入的 Intl 后端；nullptr 时使用内置最小后端。
    const NJS_IntlBackend* intlBackend_ = nullptr;
    Value* intlNumberFormatPrototype_ = nullptr;
    Value* intlDateTimeFormatPrototype_ = nullptr;
    Value* intlCollatorPrototype_ = nullptr;
    Value* intlPluralRulesPrototype_ = nullptr;
    Value* intlRelativeTimeFormatPrototype_ = nullptr;
    Value* intlListFormatPrototype_ = nullptr;
    Value* intlDisplayNamesPrototype_ = nullptr;
    Value* intlSegmenterPrototype_ = nullptr;
    Value* intlSegmentsPrototype_ = nullptr;
    Value* intlLocalePrototype_ = nullptr;
    Value* workerPrototype_ = nullptr;  // %Worker.prototype%
    Value* messagePortPrototype_ = nullptr;  // %MessagePort.prototype%
    Value* generatorPrototype_ = nullptr;  // %GeneratorPrototype%（ADR-045 §8）
    // %GeneratorFunction.prototype% / %AsyncGeneratorFunction.prototype%：
    // 生成器函数对象的 [[Prototype]]（其原型为 %Function.prototype%）。
    Value* generatorFunctionPrototype_ = nullptr;
    Value* asyncGeneratorFunctionPrototype_ = nullptr;
    Value* asyncIteratorPrototype_ = nullptr;   // %AsyncIteratorPrototype%
    Value* asyncGeneratorPrototype_ = nullptr;  // %AsyncGeneratorPrototype%
    Value* errorPrototype_ = nullptr;   // %Error.prototype%（ADR-038）
    Value* promisePrototype_ = nullptr; // %Promise.prototype%（ADR-043）
    Value* promiseCtor_ = nullptr;      // %Promise% 内建构造函数（异步机制用）
    Value* regexpPrototype_ = nullptr;  // %RegExp.prototype%（ADR-062 R4）
    Value* regexpCtor_ = nullptr;       // %RegExp% 构造函数
    // Symbol.for 全局注册表（ES2024 §20.4.2.1）：key → symbol Value。
    // 双向查找（keyFor）用 symbol data 指针反查，故另存反向表。
    std::unordered_map<std::string, Value*> symbolRegistry_;
    std::unordered_map<const void*, std::string> symbolRegistryReverse_;

    // —— 事件循环任务队列（双队列）——
    // microtasks_ 是 FIFO（无延迟）；macrotasks_ 按 (due, 入队序号) 升序维护，
    // 故队首即下一个到期宏任务。微任务永远优先于宏任务。
    struct QueuedTask {
        Task task;
        uint64_t seq = 0;  // 入队序号（稳定排序用）
    };
    std::vector<QueuedTask> microtasks_;
    std::vector<QueuedTask> macrotasks_;
    uint64_t nextTaskId_ = 1;
    uint64_t nextSeq_ = 0;
    // 外部任务队列（线程安全）：Worker 线程投递，owner 线程 runOneTask 执行。
    // 作为宏任务处理，排在微任务之后；mutex 保护跨线程 push。
    mutable std::mutex externalMutex_;
    std::deque<std::function<void(IContext*)>> externalTasks_;
    std::function<void()> externalWake_;
    bool externalClosed_ = false;
    // 任务实体作为**动态 GC 根**（ADR-042）：不挂任何 scope，由 Context 直接
    // 增减 pinRef。同一实体被多个任务引用时只 pinRefInc 一次，最后一个出队/
    // 取消时 pinRefDec。析构时由 releaseAllTaskRoots 统一归还。
    std::unordered_map<Value*, size_t> taskRootRefs_;
    // ES Module 表（id=绝对路径 → Module）。Module 借用 global 树下的 root scope
    // 与导出对象，不拥有它们；unique_ptr 只管理 Module 结构本身。
    std::unordered_map<std::string, std::unique_ptr<Module>> modules_;
    // 正在加载的模块栈（相对 import 的基准）。loader 执行期间压栈，退出时弹栈。
    std::vector<Module*> moduleStack_;
    // [[AsyncEvaluationOrder]] 全局单调计数（ADR-064）。
    uint64_t moduleAsyncEvaluationCount_ = 0;
    // 待上报的未处理拒绝（ADR-043 M4）：promise → 是否仍待上报。
    // 这些 promise 作为动态 GC 根持有（pinRef++），移出/上报/析构时归还。
    std::unordered_map<Value*, bool> pendingRejections_;
    // 当前正在恢复的协程栈（ADR-045 §7）：嵌套生成器要求取最内层。
    // runner 进入时 push、退出时 pop；getCurrentCoroutine 取栈顶。
    std::vector<Value*> coroutineStack_;
    // eval 编译产物：eval 内创建的函数可能逃逸并长期引用 Code，
    // 故 Code 由 Context 持有到析构（按 eval 调用次数增长，可接受）。
    std::vector<std::unique_ptr<vm::Code>> evalCodes_;

public:
    Context(infra::IAllocator* alloc, infra::Console* console,
            infra::IClock* clock = nullptr,
            const NJS_IntlBackend* intl = nullptr);
    ~Context() override;

    infra::IAllocator* allocator() const override { return alloc_; }
    IGCEngine* gc() const override { return gc_.get(); }
    // 诊断统计（P0）：Value 创建路径计数。
    const ValueCreationStats& creationStats() const { return creationStats_; }
    infra::Console* console() const override { return console_; }
    const NJS_IntlBackend* intlBackend() const override { return intlBackend_; }
    void setIntlNumberFormatPrototype(Value* proto) override {
        intlNumberFormatPrototype_ = proto;
    }
    Value* intlNumberFormatPrototype() const override {
        return intlNumberFormatPrototype_;
    }
    void setIntlDateTimeFormatPrototype(Value* proto) override {
        intlDateTimeFormatPrototype_ = proto;
    }
    Value* intlDateTimeFormatPrototype() const override {
        return intlDateTimeFormatPrototype_;
    }
    void setIntlCollatorPrototype(Value* proto) override {
        intlCollatorPrototype_ = proto;
    }
    Value* intlCollatorPrototype() const override { return intlCollatorPrototype_; }
    void setIntlPluralRulesPrototype(Value* proto) override {
        intlPluralRulesPrototype_ = proto;
    }
    Value* intlPluralRulesPrototype() const override {
        return intlPluralRulesPrototype_;
    }
    void setIntlRelativeTimeFormatPrototype(Value* proto) override {
        intlRelativeTimeFormatPrototype_ = proto;
    }
    Value* intlRelativeTimeFormatPrototype() const override {
        return intlRelativeTimeFormatPrototype_;
    }
    void setIntlListFormatPrototype(Value* proto) override {
        intlListFormatPrototype_ = proto;
    }
    Value* intlListFormatPrototype() const override {
        return intlListFormatPrototype_;
    }
    void setIntlDisplayNamesPrototype(Value* proto) override {
        intlDisplayNamesPrototype_ = proto;
    }
    Value* intlDisplayNamesPrototype() const override {
        return intlDisplayNamesPrototype_;
    }
    void setIntlSegmenterPrototype(Value* proto) override {
        intlSegmenterPrototype_ = proto;
    }
    Value* intlSegmenterPrototype() const override {
        return intlSegmenterPrototype_;
    }
    void setIntlSegmentsPrototype(Value* proto) override {
        intlSegmentsPrototype_ = proto;
    }
    Value* intlSegmentsPrototype() const override {
        return intlSegmentsPrototype_;
    }
    void setIntlLocalePrototype(Value* proto) override {
        intlLocalePrototype_ = proto;
    }
    Value* intlLocalePrototype() const override { return intlLocalePrototype_; }
    void setWorkerPrototype(Value* proto) override { workerPrototype_ = proto; }
    Value* workerPrototype() const override { return workerPrototype_; }
    void setMessagePortPrototype(Value* proto) override {
        messagePortPrototype_ = proto;
    }
    Value* messagePortPrototype() const override {
        return messagePortPrototype_;
    }

    Scope* global() const override { return global_; }
    Value* globalObject() const override { return globalObject_; }
    bool isWorkerContext() const override { return isWorkerContext_; }
    void setWorkerContext(bool worker) override { isWorkerContext_ = worker; }
    void requestStop() override {
        stopRequested_.store(true, std::memory_order_release);
    }
    bool isStopRequested() const override {
        return stopRequested_.load(std::memory_order_acquire);
    }
    void clearStopRequest() override {
        stopRequested_.store(false, std::memory_order_release);
    }
    Scope* root() const override { return root_; }
    Scope* current() const override { return current_; }

    void setRootScope(Scope* s) override { root_ = s; }
    void setCurrentScope(Scope* s) override { current_ = s; }
    Scope* currentFunctionEnv() const override { return currentFunctionEnv_; }
    void setCurrentFunctionEnv(Scope* s) override { currentFunctionEnv_ = s; }
    Value* currentFunction() const override { return currentFunction_; }
    void setCurrentFunction(Value* fn) override { currentFunction_ = fn; }
    Scope* pushNewScope(Scope* parent) override;
    Value* pushWithScope(Value* obj, Scope** outScope,
                         bool checkStrict = true) override;
    void popScope() override;

    // popScope 的确定性释放（ADR-065 §3）：释放本作用域唯一持有的 Value。
    void releaseScopeValues(Scope* scope);

    // 调用栈（ADR-037）：纯展示信息，不参与变量解析
    void pushFrame(const SourcePosition& callSite, std::string_view fnName) override;
    void popFrame() override {
        if (!callStack_.empty()) callStack_.pop_back();
    }
    std::vector<CallFrame> trace(const SourcePosition& callSite) const override;
    const std::vector<CallFrame>& callStack() const override { return callStack_; }
    void setCallStack(std::vector<CallFrame> stack) override {
        callStack_ = std::move(stack);
    }
    void setExceptionPosition(Value* exception,
                              const SourcePosition& position) override;

    Value* createUndefined(std::string_view name = {}) override;
    Value* createNull(std::string_view name = {}) override;
    Value* createBoolean(bool b, std::string_view name = {}) override;
    Value* createNumber(double n, std::string_view name = {}) override;
    Value* createBigInt(const neo::bigint::BigInt& value,
                        std::string_view name = {}) override;
    Value* createString(std::string_view s, std::string_view name = {}) override;
    Value* createEphemeralUndefined() override;
    Value* createEphemeralNull() override;
    Value* createEphemeralBoolean(bool b) override;
    Value* createEphemeralNumber(double n) override;
    Value* createEphemeralBigInt(const neo::bigint::BigInt& value) override;
    Value* createEphemeralString(std::string_view s) override;
    Value* createSymbol(std::string_view description = {},
                        std::string_view name = {},
                        bool hasDescription = true) override;
    Value* getWellKnownSymbol(WellKnownSymbol which) const override {
        return wellKnownSymbols_[static_cast<uint8_t>(which)];
    }
    Value* symbolFor(std::string_view key) override;
    Value* symbolKeyFor(Value* sym) override;

    // 运算代理（纯代理，ADR-016 §4）
    Value* add(Value* a, Value* b) override;
    Value* sub(Value* a, Value* b) override;
    Value* mul(Value* a, Value* b) override;
    Value* div(Value* a, Value* b) override;
    Value* toString(Value* self) override;

    // 转换与运算符原语（ES2024 §7.1 / §7.2 / §13）
    bool toBoolean(Value* v) override;
    Value* toNumber(Value* v) override;
    Value* toPrimitive(Value* v,
                       ToPrimitiveHint hint = ToPrimitiveHint::Default) override;
    Value* toPropertyKey(Value* v) override;
    Value* binaryOp(BinaryOperator op, Value* a, Value* b) override;
    Value* unaryOp(UnaryOperator op, Value* v) override;

    // 函数调用
    Value* call(Value* fn, Value* thisArg, size_t argc, Value* argv[]) override;
    Value* construct(Value* fn, size_t argc, Value* argv[]) override;
    Value* construct(Value* fn, Value* newTarget, size_t argc, Value* argv[]) override;
    Value* constructSuper(Value* superCtor, Value* newTarget, size_t argc,
                          Value* argv[]) override;
    Value* getThis() override;
    void initializeThis(Value* value) override;
    bool isThisInitialized() override;
    Value* newTarget() override { return newTarget_; }
    Value* getSuperProperty(Value* homeObject, Value* key, Value* receiver) override;
    Value* getSuperPropertyFromBase(Value* base, Value* key,
                                    Value* receiver) override;
    Value* setSuperPropertyFromBase(Value* base, Value* key, Value* value,
                                    Value* receiver) override;
    Value* setSuperProperty(Value* homeObject, Value* key, Value* value,
                            Value* receiver) override;
    Value* createUninitialized() override;
    void addInstanceField(Value* classCtor, Value* key, Value* initializer) override;
    void addClassInitializer(Value* classCtor, Value* initializer) override;
    Value* initializeInstanceElements(Value* instance, Value* classCtor) override;

    // 私有字段/方法/访问器（ADR-047 §5）
    PrivateName* createPrivateName(std::string_view description) override;
    void retainPrivateName(PrivateName* name) override;
    void releasePrivateName(PrivateName* name) override;
    void addPrivateField(Value* classCtor, PrivateName* name,
                         Value* initializer) override;
    void addStaticPrivateField(Value* classCtor, PrivateName* name,
                               Value* initializer) override;
    void addPrivateMethod(Value* classCtor, PrivateName* name, Value* method) override;
    void addPrivateAccessor(Value* classCtor, PrivateName* name, Value* getter,
                            Value* setter) override;
    void addStaticPrivateMethod(Value* classCtor, PrivateName* name,
                                Value* method) override;
    void addStaticPrivateAccessor(Value* classCtor, PrivateName* name, Value* getter,
                                  Value* setter) override;
    PrivateName* ensureClassPrivateName(Value* classCtor,
                                        std::string_view description) override;
    void definePrivateElement(Value* classCtor, PrivateName* name, Value* fn,
                              bool isStatic, uint32_t kind) override;
    void setFunctionOwnerClass(Value* fn, Value* ownerClass) override;
    Value* initializeStaticPrivateField(Value* classCtor, PrivateName* name) override;
    Value* initializeStaticPrivateElements(Value* classCtor) override;
    Value* getPrivate(Value* obj, PrivateName* name) override;
    Value* setPrivate(Value* obj, PrivateName* name, Value* value) override;
    Value* privateIn(Value* obj, PrivateName* name) override;
    bool hasPrivate(Value* obj, PrivateName* name) override;
    Value* getPrivateMethod(Value* classCtor, Value* obj, PrivateName* name) override;
    Value* getPrivateGetter(Value* classCtor, Value* obj, PrivateName* name) override;
    Value* getPrivateSetter(Value* classCtor, Value* obj, PrivateName* name) override;
    void setFunctionClassConstructor(Value* fn, bool isDerived,
                                     Value* superCtor) override;
    Value* registerClass(const NativeClassDef& def) override;
    Value* classSuperConstructor(Value* classCtor) override;
    PrivateName* classPrivateName(Value* classCtor,
                                  std::string_view description) override;
    Value* resolveWithObject(std::string_view name) override;
    PrivateName* resolvePrivateName(Value* fn,
                                    std::string_view description) override;
    Value* functionHomeObject(Value* fn) override;
    Value* runStaticBlock(Value* classCtor, NativeFn body) override;
    Value* instanceOf(Value* value, Value* target) override;
    Value* createNativeFunction(NativeFn fn, size_t minArgc,
                                std::string_view bindName = {},
                                const FunctionCreateOptions& opts = {}) override;
    void setFunctionName(Value* fn, std::string_view name) override;
    void setFunctionNameBinding(Value* fn, std::string_view name) override;
    void setFunctionNameIfAbsent(Value* fn, std::string_view name) override;
    void setFunctionNameFromKey(Value* fn, Value* key,
                                std::string_view prefix = {}) override;
    void setFunctionStrict(Value* fn) override;
    void setFunctionCapturesEnvironment(Value* fn) override;
    void setFunctionHomeObject(Value* fn, Value* homeObject) override;
    void setFunctionSource(Value* fn, const SourceSlice& slice) override;
    void setFunctionConstructor(Value* fn, NativeFn ctor) override;
    void setFunctionConstructible(Value* fn, bool constructible) override;
    void setFunctionGenerator(Value* fn, bool generator) override;
    void setFunctionAsync(Value* fn, bool async) override;
    void setFunctionAsyncGenerator(Value* fn, bool asyncGenerator) override;
    void setFunctionArguments(Value* fn, bool needsArguments,
                              bool mapped) override;
    void setFunctionInFieldInitializer(Value* fn,
                                       bool inFieldInitializer) override;
    void setFunctionArrow(Value* fn, bool arrow) override;
    void setFunctionParamNames(Value* fn,
                               const std::vector<std::string>& names) override;
    Value* createBoundFunction(Value* target, Value* boundThis, size_t argc,
                               Value* argv[]) override;
    Value* createScriptFunction(const vm::Code* code, uint32_t entryPc,
                                size_t paramCount, bool hasRest,
                                bool constructible, bool hasPrototypeProperty,
                                std::string_view bindName = {}) override;
    Value* createScriptClassConstructor(const vm::Code* code, uint32_t entryPc,
                                        size_t paramCount, bool hasRest,
                                        bool isDerived, Value* superCtor,
                                        std::string_view name) override;
    Value* defineClassMethod(Value* classCtor, Value* key, Value* fn,
                             bool isStatic, uint32_t kind) override;
    Value* defineClassField(Value* classCtor, Value* key, Value* initializer,
                            bool isStatic) override;
    void bindFunctionThis(Value* fn, Value* thisValue) override;
    Value* cloneValue(Value* source) override;
    Value* cloneValueEphemeral(Value* source) override;
    Value* createIndexString(uint32_t index) override;
    Value* copyForStorage(Value* source) override;
    Value* setClosure(Value* fn, std::string_view name) override;
    Value* captureValue(Value* fn, std::string_view name, Value* value) override;

    // 对象与属性
    // 引入基类的字符串键便捷重载（override 会隐藏同名基类重载）
    using IContext::getProperty;
    using IContext::setProperty;
    using IContext::hasProperty;
    using IContext::deleteProperty;
    using IContext::defineProperty;
    using IContext::defineAccessor;

    Value* createObject(std::string_view bindName = {}) override;
    Value* createProxy(Value* target, Value* handler,
                       std::string_view bindName = {}) override;
    Value* createArray(std::string_view bindName = {}) override;
    Value* createArgumentsObject(Value* callee, size_t argc, Value* argv[],
                                 bool mapped) override;
    Value* storeBinding(Scope* scope, std::string_view name,
                        Value* source) override;
    Value* createTemplateObject(
        const std::vector<std::pair<std::optional<std::string>, std::string>>&
            quasis) override;
    Value* arrayAppend(Value* array, Value* value) override;
    Value* copyDataProperties(Value* target, Value* source,
                              const std::vector<Value*>& excluded = {})
        override;
    Value* createPrimitiveWrapper(Value* primitive,
                                  std::string_view bindName = {}) override;
    Value* createArrayIterator(Value* iteratedObject, ArrayIterationKind kind,
                               std::string_view bindName = {}) override;
    Value* createIteratorWrapper(Value* iterated, Value* nextMethod,
                                 std::string_view bindName = {}) override;
    Value* createStringIterator(Value* string,
                                std::string_view bindName = {}) override;
    Value* createIteratorHelper(Value* underlying, Value* nextMethod,
                                IteratorHelperKind kind, Value* callback,
                                double counter,
                                std::string_view bindName = {}) override;
    Value* createWeakRef(Value* target,
                         std::string_view bindName = {}) override;
    void setWeakRefPrototype(Value* proto) override { weakRefPrototype_ = proto; }

    Value* createCollection(CollectionKind kind,
                            std::string_view bindName = {}) override;
    Value* createCollectionIterator(Value* collection,
                                    CollectionIterationKind kind,
                                    std::string_view bindName = {}) override;
    Value* createDate(double timeValue, std::string_view bindName = {}) override;
    Value* createArrayBuffer(size_t byteLength,
                             std::string_view bindName = {}) override;
    Value* createSharedArrayBuffer(std::shared_ptr<SharedBufferBlock> block,
                                   std::string_view bindName = {}) override;
    Value* createDataView(Value* buffer, size_t byteOffset, size_t byteLength,
                          std::string_view bindName = {}) override;
    Value* createTypedArray(Value* buffer, size_t byteOffset, size_t length,
                            TypedArrayKind kind,
                            std::string_view bindName = {}) override;
    Value* createIntlNumberFormat(const NJS_IntlBackend* backend,
                                  NJS_IntlFormatter* formatter,
                                  std::string_view locale,
                                  std::string_view bindName = {}) override;
    Value* createIntlDateTimeFormat(const NJS_IntlBackend* backend,
                                    NJS_IntlFormatter* formatter,
                                    std::string_view locale,
                                    std::string_view bindName = {}) override;
    Value* createIntlCollator(const NJS_IntlBackend* backend,
                              NJS_IntlCollator* collator,
                              std::string_view locale,
                              std::string_view bindName = {}) override;
    Value* createIntlPluralRules(const NJS_IntlBackend* backend,
                                 NJS_IntlPluralRules* rules,
                                 std::string_view locale,
                                 std::string_view bindName = {}) override;
    Value* createIntlRelativeTimeFormat(
        const NJS_IntlBackend* backend, NJS_IntlRelativeTimeFormat* formatter,
        std::string_view locale, std::string_view bindName = {}) override;
    Value* createIntlListFormat(const NJS_IntlBackend* backend,
                                NJS_IntlListFormat* formatter,
                                std::string_view locale,
                                std::string_view bindName = {}) override;
    Value* createIntlDisplayNames(const NJS_IntlBackend* backend,
                                  NJS_IntlDisplayNames* names,
                                  std::string_view locale,
                                  std::string_view bindName = {}) override;
    Value* createIntlSegmenter(const NJS_IntlBackend* backend,
                               NJS_IntlSegmenter* segmenter,
                               std::string_view locale,
                               std::string_view bindName = {}) override;
    Value* createIntlSegments(std::string_view input,
                              std::vector<size_t> starts,
                              std::vector<size_t> lengths, Value* records,
                              std::string_view bindName = {}) override;
    Value* createIntlLocale(std::string_view localeName,
                            std::string_view timeZone,
                            std::string_view bindName = {}) override;
    Value* createWorkerObject(IData* data,
                              std::string_view bindName = {}) override;
    Value* createMessagePortObject(IData* data,
                                   std::string_view bindName = {}) override;
    Value* createRegExp(std::string_view pattern, std::string_view flags,
                        std::string_view bindName = {}) override;
    Value* createRegExp(
        std::string_view pattern, std::string_view flags,
        const std::shared_ptr<const neo::regex::Program>& program,
        std::string_view bindName = {}) override;
    Value* createRegExpStringIterator(Value* regexp, Value* string, bool global,
                                      bool unicode,
                                      std::string_view bindName = {}) override;
    Value* regexpPrototype() const override { return regexpPrototype_; }
    void setRegExpPrototype(Value* proto) override { regexpPrototype_ = proto; }
    Value* regexpCtor() const override { return regexpCtor_; }
    void setRegExpCtor(Value* ctor) override { regexpCtor_ = ctor; }

    // Error 对象（ADR-038）
    Value* createError(std::string_view message, std::string_view name = "Error",
                       const SourcePosition& position = {},
                       std::string_view bindName = {}) override;
    Value* errorPrototype() const override { return errorPrototype_; }
    void setErrorPrototype(Value* proto) override { errorPrototype_ = proto; }

    // Promise（ADR-043）
    Value* createPromise(std::string_view bindName = {}) override;
    Value* createInterrupt(InterruptKind kind, uint64_t offset, Value* value) override;

    // 协程（ADR-045）
    Value* createCoroutine(Value* target, Value* receiver, size_t argc, Value* argv[],
                           std::string_view bindName = {}) override;
    Value* getCurrentCoroutine() const override {
        return coroutineStack_.empty() ? nullptr : coroutineStack_.back();
    }
    Value* resumeCoroutine(Value* coroutine, Value* sentValue) override;
    Value* createAsyncGenerator(Value* target, Value* receiver, size_t argc,
                                Value* argv[], std::string_view bindName = {}) override;
    Value* promisePrototype() const override { return promisePrototype_; }
    void setPromisePrototype(Value* proto) override { promisePrototype_ = proto; }
    void setPromiseCtor(Value* ctor) override { promiseCtor_ = ctor; }
    Value* promiseCtor() const override { return promiseCtor_; }

    // —— ES Module 基础设施 ——
    bool hasModule(std::string_view id) const override;
    // 取/创建 id 对应的 Module；失败返回 nullptr。异步模块回调按 id 反查用。
    Module* findModule(std::string_view id);
    // AsyncModuleExecutionFulfilled / Rejected（§16.2.1.6.1.10/11）。
    // 异步求值回调按 id 反查模块后调用，故对外可见。
    void asyncModuleExecutionFulfilled(Module* mod);
    void asyncModuleExecutionRejected(Module* mod, Value* error);
    ModuleInfo getModule(std::string_view id) override;
    void setModuleLoader(std::string_view id, ModuleLoader loader) override;
    bool setModuleCompilation(std::string_view id,
                              ModuleCompilation compilation) override;
    Value* importModule(std::string_view id) override;
    Value* importModuleDynamic(Value* specifier,
                               std::string_view referrer) override;
    Value* createModuleNamespace(std::string_view bindName = {}) override;
    Value* importMeta() override;
    Value* exportBinding(Value* exports, std::string_view exportName,
                         std::string_view localName) override;
    Value* exportSlot(Value* exports, std::string_view exportName,
                      Value* slot) override;
    Value* getExportSlot(Value* exports,
                         std::string_view exportName) override;
    Value* bindImportSlot(std::string_view localName, Value* slot) override;
    Value* bindImport(std::string_view localName, Value* namespaceObj,
                      std::string_view exportName) override;

    // 内建原型访问（ADR-035）：create* 自动挂载对应原型。
    Value* objectPrototype() const override { return objectPrototype_; }
    Value* functionPrototype() const override { return functionPrototype_; }
    Value* arrayPrototype() const override { return arrayPrototype_; }
    Value* booleanPrototype() const override { return booleanPrototype_; }
    Value* numberPrototype() const override { return numberPrototype_; }
    Value* stringPrototype() const override { return stringPrototype_; }
    Value* symbolPrototype() const override { return symbolPrototype_; }
    void setBuiltinPrototypes(Value* objectProto, Value* functionProto) override {
        objectPrototype_ = objectProto;
        functionPrototype_ = functionProto;
    }
    void setArrayPrototype(Value* arrayProto) override { arrayPrototype_ = arrayProto; }
    void setWrapperPrototypes(Value* booleanProto, Value* numberProto,
                              Value* stringProto, Value* symbolProto) override {
        booleanPrototype_ = booleanProto;
        numberPrototype_ = numberProto;
        stringPrototype_ = stringProto;
        symbolPrototype_ = symbolProto;
    }
    void setBigIntPrototype(Value* proto) override { bigIntPrototype_ = proto; }
    void setIteratorBuiltins(Value* iteratorProto, Value* iteratorCtor,
                             Value* arrayIteratorProto,
                             Value* stringIteratorProto,
                             Value* iteratorWrapperProto) override {
        iteratorPrototype_ = iteratorProto;
        iteratorCtor_ = iteratorCtor;
        arrayIteratorPrototype_ = arrayIteratorProto;
        stringIteratorPrototype_ = stringIteratorProto;
        iteratorWrapperPrototype_ = iteratorWrapperProto;
    }
    Value* iteratorPrototype() const override { return iteratorPrototype_; }
    Value* iteratorCtor() const override { return iteratorCtor_; }
    void setIteratorHelperPrototype(Value* proto) override {
        iteratorHelperPrototype_ = proto;
    }
    Value* iteratorHelperPrototype() const override {
        return iteratorHelperPrototype_;
    }
    void setCollectionPrototypes(Value* mapProto, Value* setProto,
                                 Value* weakMapProto, Value* weakSetProto,
                                 Value* mapIteratorProto,
                                 Value* setIteratorProto) override {
        mapPrototype_ = mapProto;
        setPrototype_ = setProto;
        weakMapPrototype_ = weakMapProto;
        weakSetPrototype_ = weakSetProto;
        mapIteratorPrototype_ = mapIteratorProto;
        setIteratorPrototype_ = setIteratorProto;
    }
    void setDatePrototype(Value* proto) override { datePrototype_ = proto; }
    Value* datePrototype() const override { return datePrototype_; }
    void setArrayBufferPrototype(Value* proto) override {
        arrayBufferPrototype_ = proto;
    }
    void setSharedArrayBufferPrototype(Value* proto) override {
        sharedArrayBufferPrototype_ = proto;
    }
    void setDataViewPrototype(Value* proto) override { dataViewPrototype_ = proto; }
    void setTypedArrayPrototype(TypedArrayKind kind, Value* proto) override {
        typedArrayPrototypes_[static_cast<size_t>(kind)] = proto;
    }
    Value* typedArrayPrototype(TypedArrayKind kind) const override {
        return typedArrayPrototypes_[static_cast<size_t>(kind)];
    }
    void setGeneratorPrototype(Value* proto) override { generatorPrototype_ = proto; }
    Value* generatorPrototype() const override { return generatorPrototype_; }
    void setGeneratorFunctionPrototype(Value* proto) override {
        generatorFunctionPrototype_ = proto;
    }
    Value* generatorFunctionPrototype() const override {
        return generatorFunctionPrototype_;
    }
    void setAsyncGeneratorFunctionPrototype(Value* proto) override {
        asyncGeneratorFunctionPrototype_ = proto;
    }
    Value* asyncGeneratorFunctionPrototype() const override {
        return asyncGeneratorFunctionPrototype_;
    }
    void setAsyncIteratorPrototype(Value* proto) override {
        asyncIteratorPrototype_ = proto;
    }
    Value* asyncIteratorPrototype() const override { return asyncIteratorPrototype_; }
    void setAsyncGeneratorPrototype(Value* proto) override {
        asyncGeneratorPrototype_ = proto;
    }
    Value* asyncGeneratorPrototype() const override { return asyncGeneratorPrototype_; }
    Value* getProperty(Value* obj, Value* key) override;
    Value* setProperty(Value* obj, Value* key, Value* value) override;
    Value* hasProperty(Value* obj, Value* key) override;
    Value* deleteProperty(Value* obj, Value* key) override;
    Value* deleteIdentifier(std::string_view name) override;
    Value* defineProperty(Value* obj, Value* key, Value* value,
                          bool writable = true, bool enumerable = true,
                          bool configurable = true) override;
    Value* defineAccessor(Value* obj, Value* key, Value* getter, Value* setter,
                          bool enumerable = false, bool configurable = true) override;
    Value* defineAccessorPartial(Value* obj, Value* key, Value* getter,
                                 bool hasGetter, Value* setter, bool hasSetter,
                                 bool enumerable, bool configurable) override;
    Value* getPrototype(Value* obj) override;
    Value* setPrototype(Value* obj, Value* proto) override;
    Value* ownPropertyKeys(Value* obj, bool enumerableOnly,
                           bool includeSymbols) override;
    Value* ownEnumerableStringKeys(Value* obj) override;
    Value* forInKeys(Value* obj) override;
    Value* isEnumerableOwn(Value* obj, Value* key) override;

    // 错误值创建
    Value* createException(Value* child) override;
    Value* createFatal(const char* msg) override;
    Value* createErrorException(ErrorKind kind, std::string_view message,
                                const SourcePosition& position = {}) override;

    // OOM 哨兵
    Value* oomValue() const override { return oomValue_; }
    Value* throwTypeError() override;

    // 变量定义与赋值
    Value* define(std::string_view name, Value* source,
                  bool mutableBinding = true,
                  bool initialized = true,
                  bool varBinding = false) override;
    Value* store(std::string_view name, Value* source) override;
    Value* storeVar(std::string_view name, Value* source) override;
    Value* storeGlobalVar(std::string_view name, Value* source) override;
    Value* initialize(std::string_view name, Value* source) override;
    Value* load(std::string_view name) override;
    Value* loadSoft(std::string_view name) override;
    Value* resolveBindingSlot(std::string_view name) override;
    Value* storeToSlot(Value* slot, std::string_view name,
                       Value* source) override;
    Value* loadFromSlot(Value* slot, std::string_view name) override;

    // eval（§19.2.1）
    Value* declareVar(std::string_view name) override;
    // 全局环境记录（§9.1.1.4）：对象记录落在 globalObject_，声明记录落在 global_。
    bool defineGlobal(std::string_view name, Value* value, bool writable,
                      bool enumerable, bool configurable) override;
    Value* defineGlobalLexical(std::string_view name, Value* value,
                               bool mutableBinding = true,
                               bool initialized = true) override;
    Value* declareGlobalVar(std::string_view name, bool configurable) override;
    Value* declareGlobalFunction(std::string_view name,
                                 bool configurable) override;
    Value* checkGlobalFunction(std::string_view name) override;
    Value* checkGlobalVar(std::string_view name) override;
    Value* storeEvalFunction(std::string_view name, Value* value) override;
    Value* checkGlobalLexical(std::string_view name) override;
    Value* evalSource(Value* source, Scope* callerScope, bool direct) override;
    Value* evalFunction() const override { return evalFunction_; }
    void setEvalFunction(Value* fn) override { evalFunction_ = fn; }
    void retainCode(std::unique_ptr<vm::Code> code) override;

    // 可赋值性（绑定维度）
    void setBindingMutable(Scope* scope, std::string_view name,
                           bool m) override;
    bool isBindingMutable(const Scope* scope,
                          std::string_view name) const override;

    // 作用域标注
    void setFlag(std::string_view name) override;
    bool hasFlag(std::string_view name) const override;

    // 事件循环
    bool postExternalTask(std::function<void(IContext*)> task) override;
    void setExternalTaskWake(std::function<void()> wake) override;
    uint64_t postMicrotask(Value* entity, TaskRunner run = defaultTaskRunner) override;
    uint64_t postTask(Value* entity, uint64_t timeout = 0,
                      TaskRunner run = defaultTaskRunner) override;
    uint64_t postLoopTask(Value* entity, uint64_t timeout = 0,
                          TaskRunner run = defaultTaskRunner) override;
    bool cancelTask(uint64_t id) override;
    bool hasTasks() const override {
        if (!microtasks_.empty() || !macrotasks_.empty()) return true;
        std::lock_guard<std::mutex> lk(externalMutex_);
        return !externalTasks_.empty();
    }
    bool hasRunnableTask() const override;
    Value* runOneTask() override;

    // 未处理拒绝上报（ADR-043 M4）
    void hostPromiseRejectionTracker(Value* promise, bool handled) override;
    void reportUnhandledRejections() override;

private:
    // 直接 eval 在变量环境新增 var 后，回溯把该作用域子树中已创建、尚未
    // 捕获环境的闭包挂到定义点环境链，使其能看到后加入的绑定。
    void captureClosuresInScopeTree(Scope* scope);
    // 线程亲和断言：debug 构建下校验当前线程是该 Context 的 owner。
    void assertOwnerThread() const;
    // 入队（loop 标记由调用方指定）。返回任务 id，OOM 返回 0。
    uint64_t enqueue(Value* entity, uint64_t timeout, bool loop, TaskRunner run);
    // 任务实体根的引用计数管理（ADR-042/044）：实体不挂 scope，直接增减 pinRef。
    // 首次入队 pinRefInc，最后一个任务出队/取消时 pinRefDec。
    void retainTaskRoot(Value* entity);
    void releaseTaskRoot(Value* entity);
    // 析构专用：归还队列中所有任务持有的 pinRef 并清空队列。
    void releaseAllTaskRoots();
    // 宏任务按 (due, seq) 插入到有序位置（二分查找保持稳定顺序）。
    void insertOrdered(const Task& task);
    // 从两个队列中移除指定 id 的任务（含根引用释放）。返回是否找到。
    bool eraseTask(uint64_t id);
    // 执行一个已取出的任务（含循环重入队、Fatal 判定、根引用释放）。
    Value* executeTask(const Task& task);

    // 创建 Value 的底层入口：登记诊断统计后交给 GC。type 必须是真实 IType。
    Value* createValueRaw(IType* type, IData* data);
    // 创建 Value 并跟踪（data 已分配）。失败返回 OOM 哨兵，回收游离 data（ADR-031）。
    Value* createTracked(IType* type, IData* data, std::string_view name);
    // 可变绑定槽专用：每次新建 undefined（槽位会被 replaceMeta 原地改写）。
    Value* createMutableUndefined();
    // 命中 [-1,256] 的整数值时返回共享单例；不适用返回 nullptr（-0 不缓存）。
    Value* internSmallInt(double n);
    // 创建 Value 但不登记到 current scope（表达式临时值，ADR-065 §6）。
    // data 已分配；失败返回 OOM 哨兵并回收游离 data。
    Value* createEphemeral(IType* type, IData* data);
    // 为新 Value 接管而克隆 data。
    // 简单类型：clone 分配新 data（ref=0），由新 Value 构造计入引用。
    // 复杂对象：clone 共享 data 并 ref++（ADR-019 §2），与新 Value 构造的引用重复，
    //           故抵消一次，保证 ref 数 = 指向该 data 的 Value 数。
    // 失败返回 nullptr（OOM）。
    IData* cloneForNewValue(Value* source);
    // clone source 的 data 到 scope 定义新槽位。失败返回 OOM 哨兵（ADR-031）。
    Value* defineCloned(Scope* scope, std::string_view name, Value* source,
                        bool mutableBinding = true,
                        bool initialized = true,
                        bool varBinding = false);

    // —— 编译模块的声明/实例化/求值（ADR-059 §5）——
    // declareModule: 递归执行依赖的符号定义区（只建槽，不绑定 import）。
    Value* declareModule(Module* mod);
    // instantiateModule: 绑定 import/export 槽位 + 创建函数对象。
    // 成功返回 nullptr，失败返回错误值。
    Value* instantiateModule(Module* mod);
    // evaluateModule: 按依赖 DFS 后序执行模块体。
    // 同步成功返回 nullptr；异步求值返回其 Promise；失败返回错误值。
    Value* evaluateModule(Module* mod);
    // 在模块 root scope 下执行 code 的某一段。
    Value* runModuleCode(Module* mod, uint32_t entryPc);
    // —— 模块顶层 await（ADR-064）：SCC + 异步依赖调度 ——
    // 以协程持有 VM 挂起状态。
    Value* createModuleCoroutine(Module* mod);
    static Value* resumeModuleCoroutine(IContext* ctx, Value* coroutine,
                                        Value* sentValue);
    // InnerModuleEvaluation（§16.2.1.6.1.7）：递归 DFS + SCC 展开。
    Value* innerModuleEvaluation(Module* mod, std::vector<Module*>& stack,
                                 size_t& index);
    // ExecuteAsyncModule：启动模块协程并订阅其完成。
    void executeAsyncModule(Module* mod);
    // 同步执行模块体（HasTLA=false 且异步依赖已就绪）。
    Value* executeModuleSync(Module* mod);
    // GatherAvailableAncestors（§16.2.1.6.1.9）。
    void gatherAvailableAncestors(Module* mod, std::vector<Module*>& execList);
    // 环根 TopLevelCapability 的创建/结算。
    Value* ensureTopLevelCapability(Module* mod);
    void resolveTopLevelCapability(Module* mod);
    void rejectTopLevelCapability(Module* mod, Value* error);
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_CONTEXT_H
