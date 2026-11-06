// Context.h (private) - Context 实现声明
//
// Context 持有 GC 所有权（unique_ptr<IGCEngine>），注入 IAllocator/Console（无所有权），
// 创建并持有 global/root scope，管理 current 栈，另持一个纯展示用的调用栈（ADR-037）。
// 析构顺序：先销毁 global_scope（递归销毁整棵树），再销毁 GC（unique_ptr 析构），
// IAllocator/Console 不销毁（外部持有）。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、012（Scope 树）、030 阶段 4

#ifndef NEO_CORE_PRIVATE_CONTEXT_H
#define NEO_CORE_PRIVATE_CONTEXT_H

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Scope.h"
#include "core/Value.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace neo::core {

class Context : public IContext {
    infra::IAllocator* alloc_;              // 注入，无所有权
    std::unique_ptr<IGCEngine> gc_;         // Context 持有所有权
    infra::Console* console_;              // 注入，无所有权
    infra::IClock* clock_;                 // 注入，无所有权（事件循环时间源）

    Scope* global_ = nullptr;              // Context 创建，析构时销毁（递归销毁整棵树）
    Scope* root_ = nullptr;                // 当前模块根作用域（可被 setRootScope 切换）
    Scope* current_ = nullptr;             // 当前作用域（Scope 的 parent 链即作用域栈）
    // 调用栈（ADR-037）：纯展示信息，与 Scope 链无关。栈顶在末尾。
    // 用 std::string 存帧，内存走宿主堆而非 IAllocator——调用栈是诊断信息，
    // 不应在 OOM 时因 Engine 配额耗尽而无法记录。
    std::vector<CallFrame> callStack_;
    Value* oomValue_ = nullptr;            // OOM 哨兵（预分配 Fatal，挂 global，ADR-031）
    Value* wellKnownSymbols_[kWellKnownSymbolCount] = {};  // well-known symbols（挂 global）
    // 内建原型（ADR-035）：Object.prototype 是所有普通对象原型链的终点，
    // Function.prototype 是所有函数对象的原型（其自身原型是 Object.prototype）。
    // 由 bootstrapBuiltins 创建，挂 global 存活于 Context 生命周期。
    Value* objectPrototype_ = nullptr;
    Value* functionPrototype_ = nullptr;
    Value* arrayPrototype_ = nullptr;   // Array.prototype（自身是数组，ADR-018 §6）
    // 原始值包装对象的原型（ES2024 §20.3/§21.1.3/§21.1.3/§20.4.3）
    Value* booleanPrototype_ = nullptr;
    Value* numberPrototype_ = nullptr;
    Value* stringPrototype_ = nullptr;
    Value* symbolPrototype_ = nullptr;
    // 迭代器原型（ES2024 §23.1.5 / ES2025 §27.1.4.2）：
    // arrayIteratorPrototype_ 是 %ArrayIteratorPrototype%（其原型是 iteratorPrototype_），
    // iteratorWrapperPrototype_ 是 %WrapForValidIteratorPrototype%（同上）。
    Value* iteratorPrototype_ = nullptr;
    Value* iteratorCtor_ = nullptr;
    Value* arrayIteratorPrototype_ = nullptr;
    Value* iteratorWrapperPrototype_ = nullptr;
    Value* errorPrototype_ = nullptr;   // %Error.prototype%（ADR-038）
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
    // 任务实体作为 GC 根：同一实体被多个任务引用时只 add 一次、最后一个出队时 remove。
    std::unordered_map<Value*, size_t> taskRootRefs_;

public:
    Context(infra::IAllocator* alloc, infra::Console* console,
            infra::IClock* clock = nullptr);
    ~Context() override;

    infra::IAllocator* allocator() const override { return alloc_; }
    IGCEngine* gc() const override { return gc_.get(); }
    infra::Console* console() const override { return console_; }

    Scope* global() const override { return global_; }
    Scope* root() const override { return root_; }
    Scope* current() const override { return current_; }

    void setRootScope(Scope* s) override { root_ = s; }
    void setCurrentScope(Scope* s) override { current_ = s; }
    Scope* pushNewScope(Scope* parent) override;
    void popScope() override;

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

    Value* createUndefined(std::string_view name = {}) override;
    Value* createNull(std::string_view name = {}) override;
    Value* createBoolean(bool b, std::string_view name = {}) override;
    Value* createNumber(double n, std::string_view name = {}) override;
    Value* createString(std::string_view s, std::string_view name = {}) override;
    Value* createSymbol(std::string_view description = {},
                        std::string_view name = {}) override;
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

    // 函数调用
    Value* call(Value* fn, Value* thisArg, size_t argc, Value* argv[]) override;
    Value* construct(Value* fn, size_t argc, Value* argv[]) override;
    Value* createNativeFunction(NativeFn fn, size_t minArgc,
                                std::string_view bindName = {}) override;
    void setFunctionName(Value* fn, std::string_view name) override;
    void setFunctionConstructor(Value* fn, NativeFn ctor) override;
    Value* createBoundFunction(Value* target, Value* boundThis, size_t argc,
                               Value* argv[]) override;
    Value* setClosure(Value* fn, std::string_view name) override;

    // 对象与属性
    // 引入基类的字符串键便捷重载（override 会隐藏同名基类重载）
    using IContext::getProperty;
    using IContext::setProperty;
    using IContext::hasProperty;
    using IContext::deleteProperty;
    using IContext::defineProperty;
    using IContext::defineAccessor;

    Value* createObject(std::string_view bindName = {}) override;
    Value* createArray(std::string_view bindName = {}) override;
    Value* createPrimitiveWrapper(Value* primitive,
                                  std::string_view bindName = {}) override;
    Value* createArrayIterator(Value* iteratedObject, ArrayIterationKind kind,
                               std::string_view bindName = {}) override;
    Value* createIteratorWrapper(Value* iterated, Value* nextMethod,
                                 std::string_view bindName = {}) override;

    // Error 对象（ADR-038）
    Value* createError(std::string_view message, std::string_view name = "Error",
                       const SourcePosition& position = {},
                       std::string_view bindName = {}) override;
    Value* errorPrototype() const override { return errorPrototype_; }
    void setErrorPrototype(Value* proto) override { errorPrototype_ = proto; }

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
    void setIteratorBuiltins(Value* iteratorProto, Value* iteratorCtor,
                             Value* arrayIteratorProto,
                             Value* iteratorWrapperProto) override {
        iteratorPrototype_ = iteratorProto;
        iteratorCtor_ = iteratorCtor;
        arrayIteratorPrototype_ = arrayIteratorProto;
        iteratorWrapperPrototype_ = iteratorWrapperProto;
    }
    Value* iteratorPrototype() const override { return iteratorPrototype_; }
    Value* iteratorCtor() const override { return iteratorCtor_; }
    Value* getProperty(Value* obj, Value* key) override;
    Value* setProperty(Value* obj, Value* key, Value* value) override;
    bool hasProperty(Value* obj, Value* key) override;
    Value* deleteProperty(Value* obj, Value* key) override;
    Value* defineProperty(Value* obj, Value* key, Value* value,
                          bool writable = true, bool enumerable = true,
                          bool configurable = true) override;
    Value* defineAccessor(Value* obj, Value* key, Value* getter, Value* setter,
                          bool enumerable = false, bool configurable = true) override;
    Value* getPrototype(Value* obj) override;
    Value* setPrototype(Value* obj, Value* proto) override;
    Value* ownPropertyKeys(Value* obj, bool enumerableOnly,
                           bool includeSymbols) override;
    Value* ownEnumerableStringKeys(Value* obj) override;

    // 错误值创建
    Value* createException(Value* child) override;
    Value* createFatal(const char* msg) override;
    Value* createErrorException(ErrorKind kind, std::string_view message,
                                const SourcePosition& position = {}) override;

    // OOM 哨兵
    Value* oomValue() const override { return oomValue_; }

    // 变量定义与赋值
    Value* define(std::string_view name, Value* source) override;
    Value* store(std::string_view name, Value* source) override;

    // 可赋值性
    void setMutable(Value* v, bool m) override;
    bool isMutable(Value* v) const override;

    // 作用域标注
    void setFlag(std::string_view name) override;
    bool hasFlag(std::string_view name) const override;

    // 事件循环
    uint64_t postMicrotask(Value* entity, TaskRunner run = defaultTaskRunner) override;
    uint64_t postTask(Value* entity, uint64_t timeout = 0,
                      TaskRunner run = defaultTaskRunner) override;
    uint64_t postLoopTask(Value* entity, uint64_t timeout = 0,
                          TaskRunner run = defaultTaskRunner) override;
    bool cancelTask(uint64_t id) override;
    bool hasTasks() const override { return !microtasks_.empty() || !macrotasks_.empty(); }
    bool hasRunnableTask() const override;
    Value* runOneTask() override;

private:
    // 入队（loop 标记由调用方指定）。返回任务 id，OOM 返回 0。
    uint64_t enqueue(Value* entity, uint64_t timeout, bool loop, TaskRunner run);
    // 任务实体根的引用计数管理：首次入队 add，最后一个任务出队/取消时 remove。
    void retainTaskRoot(Value* entity);
    void releaseTaskRoot(Value* entity);
    // 宏任务按 (due, seq) 插入到有序位置（二分查找保持稳定顺序）。
    void insertOrdered(const Task& task);
    // 从两个队列中移除指定 id 的任务（含根引用释放）。返回是否找到。
    bool eraseTask(uint64_t id);
    // 执行一个已取出的任务（含循环重入队、Fatal 判定、根引用释放）。
    Value* executeTask(const Task& task);

    // 创建 Value 并跟踪（data 已分配）。失败返回 OOM 哨兵，回收游离 data（ADR-031）。
    Value* createTracked(IType* type, IData* data, std::string_view name);
    // 为新 Value 接管而克隆 data。
    // 简单类型：clone 分配新 data（ref=0），由新 Value 构造计入引用。
    // 复杂对象：clone 共享 data 并 ref++（ADR-019 §2），与新 Value 构造的引用重复，
    //           故抵消一次，保证 ref 数 = 指向该 data 的 Value 数。
    // 失败返回 nullptr（OOM）。
    IData* cloneForNewValue(Value* source);
    // clone source 的 data 到 scope 定义新槽位。失败返回 OOM 哨兵（ADR-031）。
    Value* defineCloned(Scope* scope, std::string_view name, Value* source);
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_CONTEXT_H
