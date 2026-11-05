// ArrayBuiltins.h (private) - Array 构造函数与 Array.prototype 注册
//
// ADR-018 §6 数组：
//   Array.prototype —— 自身是数组（length=0），原型为 Object.prototype；
//   Array 构造函数挂 global，原型为 Function.prototype。
//
// 本阶段实现：
//   Array() / new Array()            → 空数组
//   new Array(n)                     → 长度 n（n 须为 0..2^32-1 整数，否则 Exception）
//   new Array(a, b, c)               → 元素数组
//   Array.isArray(v)                 → TypeKind::Array
//   Array.prototype: push / pop / join / slice / indexOf / toString
//
// 关联 ADR: 020（标准库组织）、018（对象模型 §6）、034（原型-构造函数）

#ifndef NEO_CORE_PRIVATE_ARRAY_BUILTINS_H
#define NEO_CORE_PRIVATE_ARRAY_BUILTINS_H

namespace neo::core {

class IContext;

// 建立 Array.prototype 并注册 Array 构造函数到 global。
// Context 构造末尾调用一次（Object/Function 内建之后）。失败返回 false（调用方 abort）。
bool registerArrayBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_ARRAY_BUILTINS_H
