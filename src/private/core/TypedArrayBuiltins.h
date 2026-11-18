// TypedArrayBuiltins.h - TypedArray 构造函数与原型方法（ES2024 §25.2）
//
// 关联 ADR: 020（标准库组织）

#ifndef NEO_CORE_PRIVATE_TYPED_ARRAY_BUILTINS_H
#define NEO_CORE_PRIVATE_TYPED_ARRAY_BUILTINS_H

namespace neo::core {

class IContext;

// 注册 11 个 TypedArray 构造函数、%TypedArray.prototype% 与各元素类型原型。
bool registerTypedArrayBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPED_ARRAY_BUILTINS_H
