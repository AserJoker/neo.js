// types/function.h - FunctionType + FunctionData
//
// 函数对象（ADR-021 / ADR-022）。本阶段仅 native 函数（script 路径留 VM 阶段）。
//
// FunctionData 继承 ObjectData（ADR-021 §1「类似对象的字段，继承对象模型」）：
//   函数也是对象——properties/order/symbols/prototype 直接复用对象模型，
//   属性查找/原型链/GC 遍历/clone 全部走 ObjectData 的实现。
//
// FunctionData 自有字段：
//   - closureScope：闭包作用域（当前 root scope 的子作用域，Sema 收集的穿透闭包变量绑定于此）
//   - rootScope：创建时从作用域中获取的模块根作用域（借用，不拥有）
//   - native：native 函数指针（ADR-005 签名）
//   - minArgc：最少参数数量（调用时校验，不足返回 Exception）
//   - constructFn：可选构造钩子。非空时 construct 直接调它并用其对象返回值，
//     用于 call/construct 语义必须不同的内建（如 Boolean/Number/String 包装类：
//     call 返回原始值，new 返回包装对象）。
//
// 函数名不是 data 字段，而是函数对象上的 "name" 属性（writable=false /
// enumerable=false / configurable=true，ES2024 §20.2.4.2），由 createNativeFunction
// 初始化为 "<anonymous>"、setFunctionName 更新。
//
// Function 是孤立的：Sema 收集穿透的闭包变量，绑定到 closureScope，
// 运行时查找 closureScope → rootScope → global，不依赖定义时的中间词法链（ADR-013 §3）。
// closureScope 是 Scope*（非 Value*），按 ADR-021 §4 不由 GC 从函数出发标记。
//
// 本阶段 FunctionType 只实现 call 与属性/原型操作（委托 ObjectType）；
// 运算行为（add/sub/...）返回 Exception("not implement")。
//
// 关联 ADR: 005（native 等价）、013（闭包三层作用域）、018（对象模型）、
//          021（Function 结构）、022（调用）

#ifndef NEO_CORE_PRIVATE_TYPES_FUNCTION_H
#define NEO_CORE_PRIVATE_TYPES_FUNCTION_H

#include "core/IContext.h"  // NativeFn（public 层定义）
#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"  // FunctionData 继承 ObjectData
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
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;
    Value* getProperty(IContext* ctx, Value* self, Value* key) override;
    Value* setProperty(IContext* ctx, Value* self, Value* key, Value* value) override;
    bool hasProperty(IContext* ctx, Value* self, Value* key) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
    Value* defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                          Value* setter, bool enumerable, bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

// 函数对象 data：继承 ObjectData 复用属性容器/原型链/GC 遍历/clone。
class FunctionData : public ObjectData {
public:
    Scope* closureScope;   // 闭包作用域（root 的子作用域，Sema 收集的穿透变量）
    Scope* rootScope;      // 模块根作用域（借用，不拥有）
    NativeFn native;       // native 函数指针
    size_t minArgc;        // 最少参数数量（调用时校验）
    NativeFn constructFn;  // 可选构造钩子（nullptr = native + 默认 this 语义）

    FunctionData(Scope* closure, Scope* root, NativeFn fn, size_t minArgs,
                 NativeFn ctor = nullptr)
        : closureScope(closure), rootScope(root), native(fn), minArgc(minArgs),
          constructFn(ctor) {}

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this，属性容器随之共享。
    // closureScope/rootScope 由作用域树管理，与 data 共享无关。
    IData* clone(neo::infra::IAllocator* alloc) const override {
        return ObjectData::clone(alloc);
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_FUNCTION_H
