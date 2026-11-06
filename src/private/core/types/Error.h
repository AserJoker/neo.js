// types/Error.h - ErrorType + ErrorData
//
// Error 对象（ES2024 §20.5）：普通对象 + [[ErrorData]] 内部槽。
// 命名属性/符号属性/原型链全部复用 ObjectData（与 ArrayData/FunctionData 一致）。
//
// [[ErrorData]] 是**标记性**内部槽（无数据负载），用于类型判定：
//   - Object.prototype.toString 的 tag（"[object Error]"）；
//   - 后续 Error.isError（ES2025）。
// 故 ErrorData 无自有字段，仅作为类型标记存在（继承 ObjectData 的全部容器）。
//
// TypeError/RangeError 等子类**共用本 Type**（同一 TypeKind::Error），
// 差异只在各自的 prototype 对象与 name 属性——与 ES 规范一致（§20.5.6 各子类
// 的实例都是 [[ErrorData]] 对象，通过原型链区分）。
//
// 自有属性（由 ErrorBuiltins 在构造时定义，不是 data 字段）：
//   - message（writable / 不可枚举 / 可配置）
//   - stack（writable / 不可枚举 / 可配置；由调用栈快照渲染）
// 这样属性语义（可写性、枚举、delete）完全走对象模型，无需特化。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、019（赋值语义 §2）、037（调用栈）

#ifndef NEO_CORE_PRIVATE_TYPES_ERROR_H
#define NEO_CORE_PRIVATE_TYPES_ERROR_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"  // ErrorData 继承 ObjectData
#include "infra/IAllocator.h"

namespace neo::core {

class Value;

class ErrorType : public IType {
public:
    // Error 具备对象布局（Data 派生自 ObjectData），故 objectLayout=true。
    ErrorType() : IType(TypeKind::Error, true) {}
    const char* name() const override { return "error"; }

    // 运算：走 ToPrimitive（与普通对象一致，得 "Error: msg"）
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;

    // Error 实例不是函数也不是构造函数
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;

    // 属性/原型操作全部委托 ObjectType
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

// Error 负载：继承 ObjectData 复用属性容器/原型链/GC。
// [[ErrorData]] 是标记性内部槽，无自有字段——存在性即标记。
class ErrorData : public ObjectData {
public:
    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_ERROR_H
