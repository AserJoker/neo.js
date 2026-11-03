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
#include <string_view>
#include <vector>

namespace neo::core {

class Context : public IContext {
    infra::IAllocator* alloc_;              // 注入，无所有权
    std::unique_ptr<IGCEngine> gc_;         // Context 持有所有权
    infra::Console* console_;              // 注入，无所有权

    Scope* global_ = nullptr;              // Context 创建，析构时销毁（递归销毁整棵树）
    Scope* root_ = nullptr;                // Context 创建，作为 global 的子，global 析构时递归销毁
    std::vector<Scope*> scopeStack_;      // current 栈（global 不入栈，root 是栈底）

public:
    Context(infra::IAllocator* alloc, infra::Console* console);
    ~Context() override;

    infra::IAllocator* allocator() const override { return alloc_; }
    IGCEngine* gc() const override { return gc_.get(); }
    infra::Console* console() const override { return console_; }

    Scope* global() const override { return global_; }
    Scope* root() const override { return root_; }
    Scope* current() const override;

    void pushScope(Scope* s) override;
    void popScope() override;

    Value* createUndefined(std::string_view name = {}) override;
    Value* createNull(std::string_view name = {}) override;
    Value* createBoolean(bool b, std::string_view name = {}) override;
    Value* createNumber(double n, std::string_view name = {}) override;
    Value* createString(std::string_view s, std::string_view name = {}) override;

    // 运算代理（纯代理，ADR-016 §4）
    Value* add(Value* a, Value* b) override;
    Value* sub(Value* a, Value* b) override;
    Value* mul(Value* a, Value* b) override;
    Value* div(Value* a, Value* b) override;
    Value* toString(Value* self) override;

    // 错误值创建
    Value* createException(Value* child) override;
    Value* createFatal(const char* msg) override;
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_CONTEXT_H
