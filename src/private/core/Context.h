// Context.h (private) - Context 实现声明
//
// Context 持有 GC 所有权（unique_ptr<IGCEngine>），注入 IAllocator/Console（无所有权），
// 创建并持有 global/root scope，管理 current 栈。
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

namespace neo::core {

class Context : public IContext {
    infra::IAllocator* alloc_;              // 注入，无所有权
    std::unique_ptr<IGCEngine> gc_;         // Context 持有所有权
    infra::Console* console_;              // 注入，无所有权

    Scope* global_ = nullptr;              // Context 创建，析构时销毁（递归销毁整棵树）
    Scope* root_ = nullptr;                // 当前模块根作用域（可被 setRootScope 切换）
    Scope* current_ = nullptr;             // 当前作用域（Scope 的 parent 链即调用栈，无需额外栈）
    Value* oomValue_ = nullptr;            // OOM 哨兵（预分配 Fatal，挂 global，ADR-031）
    Value* wellKnownSymbols_[kWellKnownSymbolCount] = {};  // well-known symbols（挂 global）
    // Symbol.for 全局注册表（ES2024 §20.4.2.1）：key → symbol Value。
    // 双向查找（keyFor）用 symbol data 指针反查，故另存反向表。
    std::unordered_map<std::string, Value*> symbolRegistry_;
    std::unordered_map<const void*, std::string> symbolRegistryReverse_;

public:
    Context(infra::IAllocator* alloc, infra::Console* console);
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
    Value* setClosure(Value* fn, std::string_view name) override;

    // 对象与属性
    // 引入基类的字符串键便捷重载（override 会隐藏同名基类重载）
    using IContext::getProperty;
    using IContext::setProperty;
    using IContext::hasProperty;
    using IContext::deleteProperty;
    using IContext::defineProperty;

    Value* createObject(std::string_view bindName = {}) override;
    Value* getProperty(Value* obj, Value* key) override;
    Value* setProperty(Value* obj, Value* key, Value* value) override;
    bool hasProperty(Value* obj, Value* key) override;
    Value* deleteProperty(Value* obj, Value* key) override;
    Value* defineProperty(Value* obj, Value* key, Value* value,
                          bool writable = true, bool enumerable = true,
                          bool configurable = true) override;
    Value* getPrototype(Value* obj) override;
    Value* setPrototype(Value* obj, Value* proto) override;

    // 错误值创建
    Value* createException(Value* child) override;
    Value* createFatal(const char* msg) override;

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

private:
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
