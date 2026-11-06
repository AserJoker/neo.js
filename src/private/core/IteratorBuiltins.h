// IteratorBuiltins.h (private) - Iterator 抽象类与数组迭代器内建注册
//
// 建立迭代器协议相关的内建（ES2024 §23.1.5 / ES2025 §27.1）：
//   %Iterator.prototype%          —— 所有迭代器的共同原型
//   Iterator 构造函数              —— 抽象类：call 报 requires new，construct 报不可直接构造
//   %ArrayIteratorPrototype%      —— 数组迭代器原型（next + @@toStringTag）
//   %WrapForValidIteratorPrototype% —— Iterator.from 的包装原型（next + return）
//
// 本阶段实现（核心抽象类，Iterator Helpers 留待后续）：
//   Iterator.from(v)              —— 提升任意 iterator-like 对象为 Iterator
//   Iterator.prototype: constructor 访问器 / @@iterator / @@toStringTag / @@dispose
//   Array.prototype: keys / values / entries / @@iterator（= values）
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）

#ifndef NEO_CORE_PRIVATE_ITERATOR_BUILTINS_H
#define NEO_CORE_PRIVATE_ITERATOR_BUILTINS_H

namespace neo::core {

class IContext;

// 建立迭代器内建并注册 Iterator 到 global。
// 必须在 Object/Function/Array 内建之后调用（依赖 Object.prototype /
// Function.prototype / Array.prototype 已就绪）。失败返回 false（调用方 abort）。
bool registerIteratorBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_ITERATOR_BUILTINS_H
