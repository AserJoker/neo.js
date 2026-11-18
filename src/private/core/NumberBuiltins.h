// NumberBuiltins.h - Number 构造函数的静态成员与 Number.prototype 数值格式化
//
// 关联 ADR: 020（标准库组织）

#ifndef NEO_CORE_PRIVATE_NUMBER_BUILTINS_H
#define NEO_CORE_PRIVATE_NUMBER_BUILTINS_H

namespace neo::core {

class IContext;
class Value;

// 注册 Number 静态成员与 Number.prototype 的格式化方法。
// numberFn 是已建立的 Number 构造函数，proto 是其 prototype。
bool registerNumberBuiltins(IContext* ctx, Value* numberFn, Value* proto);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_NUMBER_BUILTINS_H
