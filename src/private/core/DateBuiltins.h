// DateBuiltins.h - Date 构造函数与原型（ES2024 §21.4）
//
// 关联 ADR: 020（标准库组织）

#ifndef NEO_CORE_PRIVATE_DATE_BUILTINS_H
#define NEO_CORE_PRIVATE_DATE_BUILTINS_H

namespace neo::core {

class IContext;

// 注册 Date 构造函数、静态方法与 Date.prototype。
bool registerDateBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_DATE_BUILTINS_H
