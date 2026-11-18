// ArrayBufferBuiltins.h - ArrayBuffer 与 DataView（ES2024 §25.1 / §25.3）
//
// 关联 ADR: 020（标准库组织）

#ifndef NEO_CORE_PRIVATE_ARRAY_BUFFER_BUILTINS_H
#define NEO_CORE_PRIVATE_ARRAY_BUFFER_BUILTINS_H

namespace neo::core {

class IContext;

// 注册 ArrayBuffer、DataView 构造函数与原型。
bool registerArrayBufferBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_ARRAY_BUFFER_BUILTINS_H
