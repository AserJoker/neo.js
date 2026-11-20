// AtomicsBuiltins.h - Atomics 对象（ES2024 §25.4）
//
// 关联 ADR: 020（标准库组织）、006（异常即值）

#ifndef NEO_CORE_PRIVATE_ATOMICS_BUILTINS_H
#define NEO_CORE_PRIVATE_ATOMICS_BUILTINS_H

namespace neo::core {

class IContext;

// 注册 Atomics 全局对象。依赖 SharedArrayBuffer / TypedArray 已就绪。
bool registerAtomicsBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_ATOMICS_BUILTINS_H
