// MathBuiltins.h - Math 命名空间（ES2024 §21.3）
//
// Math 是普通对象：方法 writable/不可枚举/可配置，常量只读/不可枚举/不可配置。
// 所有方法先 ToNumber，错误（Symbol/BigInt）原样透传。

#ifndef NEO_CORE_PRIVATE_MATH_BUILTINS_H
#define NEO_CORE_PRIVATE_MATH_BUILTINS_H

namespace neo::core {

class IContext;

// 注册 Math 对象（方法 + 常量 + @@toStringTag）。
// 依赖 Object.prototype / Function.prototype 已就绪。
bool registerMathBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_MATH_BUILTINS_H
