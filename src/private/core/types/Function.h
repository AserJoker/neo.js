// types/function.h - FunctionType + FunctionData
//
// 函数对象（ADR-021 / ADR-022）。本阶段仅 native 函数（script 路径留 VM 阶段）。
//
// FunctionData：
//   - closureScope：闭包作用域（当前 root scope 的子作用域，Sema 收集的穿透闭包变量绑定于此）
//   - rootScope：创建时从作用域中获取的模块根作用域（借用，不拥有）
//   - native：native 函数指针（ADR-005 签名）
//   - minArgc：最少参数数量（调用时校验，不足返回 Exception）
//   - name：函数名（创建时可选，缺省 "<anonymous>"）
//
// Function 是孤立的：Sema 收集穿透的闭包变量，绑定到 closureScope，
// 运行时查找 closureScope → rootScope → global，不依赖定义时的中间词法链（ADR-013 §3）。
//
// 本阶段 FunctionType 只实现 call；其他行为（add/sub/...）返回 Exception("not implement")，
// 待未来包装类（对象/属性）阶段补齐。
//
// 关联 ADR: 005（native 等价）、013（闭包三层作用域）、021（Function 结构）、022（调用）

#ifndef NEO_CORE_PRIVATE_TYPES_FUNCTION_H
#define NEO_CORE_PRIVATE_TYPES_FUNCTION_H

#include "core/IContext.h"  // NativeFn（public 层定义）
#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <cstddef>
#include <new>
#include <string>
#include <string_view>

namespace neo::core {

class Scope;
class Value;

class FunctionType : public IType {
public:
    FunctionType() : IType(TypeKind::Function) {}
    const char* name() const override { return "function"; }

    // 本阶段仅实现 call；其余行为返回 Exception("not implement")。
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;

    // 调用：校验 minArgc 后调 native 指针。
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* getProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* setProperty(IContext* ctx, Value* self, std::string_view key, Value* value) override;
    bool hasProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* deleteProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* defineProperty(IContext* ctx, Value* self, std::string_view key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

class FunctionData : public IData {
public:
    Scope* closureScope;   // 闭包作用域（root 的子作用域，Sema 收集的穿透变量）
    Scope* rootScope;      // 模块根作用域（借用，不拥有）
    NativeFn native;       // native 函数指针
    size_t minArgc;        // 最少参数数量（调用时校验）
    std::string name;      // 函数名（缺省 "<anonymous>"）

    FunctionData(Scope* closure, Scope* root, NativeFn fn, size_t minArgs,
                 std::string_view fnName)
        : closureScope(closure), rootScope(root), native(fn), minArgc(minArgs),
          name(fnName.empty() ? "<anonymous>" : std::string(fnName)) {}

    // clone：共享 scope 指针（作用域由作用域树管理，非 data 拥有）+ 复制函数指针/参数描述/名字。
    IData* clone(neo::infra::IAllocator* alloc) const override {
        void* p = alloc->alloc(sizeof(FunctionData));
        return new (p) FunctionData(closureScope, rootScope, native, minArgc, name);
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_FUNCTION_H
