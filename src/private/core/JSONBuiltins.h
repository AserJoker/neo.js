// JSONBuiltins.h - JSON 命名空间（ES2024 §25.5）
//
// JSON.parse/stringify 不是 DOM 转换：parse 直接构造引擎 Value*，reviver 与
// stringify 全程经内部方法（[[Get]]/[[OwnPropertyKeys]]/[[Delete]]/
// [[DefineOwnProperty]]）访问活对象，Proxy/getter 可观察。
//
// 关联 ADR: 020（标准库组织）、048（内部方法层）

#ifndef NEO_CORE_PRIVATE_JSON_BUILTINS_H
#define NEO_CORE_PRIVATE_JSON_BUILTINS_H

namespace neo::core {

class IContext;

// 注册 JSON 对象（parse/stringify + @@toStringTag）。
// 依赖 Object.prototype / Array.prototype 已就绪。
bool registerJSONBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_JSON_BUILTINS_H
