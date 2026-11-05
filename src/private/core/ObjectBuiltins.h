// ObjectBuiltins.h (private) - Object / Function 构造函数与内建原型注册
//
// ADR-035 完整原型链：
//   Object.prototype —— 所有普通对象原型链的终点
//   Function.prototype —— 所有函数对象的原型（其自身原型为 Object.prototype）
//   Object / Function 构造函数挂 global
//
// 本阶段：
//   Object(value)：无参/undefined/null → 新建空对象；Object/Function → 原样返回；
//                  其他原始值需包装对象（未实现）→ Exception
//   Function(...)：new Function 暂不支持，调用/构造均返回 Exception("not implemented")
//   Object.prototype: toString / valueOf / hasOwnProperty
//   Function.prototype: toString / call / apply / bind
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）

#ifndef NEO_CORE_PRIVATE_OBJECT_BUILTINS_H
#define NEO_CORE_PRIVATE_OBJECT_BUILTINS_H

namespace neo::core {

class IContext;

// 建立内建原型并注册 Object / Function 构造函数到 global。
// Context 构造末尾调用一次。OOM 或注册失败返回 false（调用方 abort）。
bool registerObjectFunctionBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_OBJECT_BUILTINS_H
