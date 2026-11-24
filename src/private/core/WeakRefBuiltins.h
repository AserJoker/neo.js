// WeakRefBuiltins.h - WeakRef（ES2024 §26.1）
//
// 关联 ADR: 020（标准库组织）

#ifndef NEO_CORE_PRIVATE_WEAKREF_BUILTINS_H
#define NEO_CORE_PRIVATE_WEAKREF_BUILTINS_H

namespace neo::core {

class IContext;

// 注册 WeakRef 构造函数与 prototype.deref。
// 依赖 Object.prototype / Function.prototype 已就绪。
bool registerWeakRefBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_WEAKREF_BUILTINS_H
