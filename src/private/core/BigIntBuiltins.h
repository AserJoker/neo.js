// BigIntBuiltins.h - BigInt 构造函数与原型（ES2024 §21.2）
//
// 注册内容：
//   - BigInt(value)：可调用、不可构造；Number 必须为整数，String 走 StringToBigInt
//   - BigInt.prototype：普通对象（非包装），toString/valueOf/toLocaleString/@@toStringTag
//   - BigInt.asIntN / BigInt.asUintN
//
// 关联 ADR: 020（标准库组织）、051（BigInt 引擎接入）

#ifndef NEO_CORE_PRIVATE_BIGINT_BUILTINS_H
#define NEO_CORE_PRIVATE_BIGINT_BUILTINS_H

namespace neo::core {

class IContext;

// 注册 BigInt 内建。依赖 Object.prototype / Function.prototype 已就绪，
// 且 createPrimitiveWrapper 已能按 BigInt 挂链（本函数内部设置原型）。
bool registerBigIntBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_BIGINT_BUILTINS_H
