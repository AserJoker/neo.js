// PrimitiveWrapperBuiltins.h (private) - Boolean/Number/String/Symbol 包装类注册
//
// 原始值包装对象（ES2024 §10.4）：四种包装对象共用 PrimitiveWrapperType/Data，
// 差异只在 [[PrimitiveValue]] 的类型与各自的 prototype 对象。
//
// 本阶段最小实现（保证正确）：
//   Boolean(value) / new Boolean(value)   —— call 返回 ToBoolean，new 返回包装对象
//   Number(value)  / new Number(value)    —— call 返回 ToNumber，new 返回包装对象
//   String(value)  / new String(value)    —— call 返回 ToString，new 返回包装对象
//   Symbol 的包装语义并入 SymbolBuiltins（Symbol 不是构造函数，无法 new）
//   *Prototype.valueOf()  —— 返回 [[PrimitiveValue]]（this 须为对应包装对象或原始值）
//   *Prototype.toString() —— 原始值的字符串形式
//
// 注册到 global scope（ADR-020 §2 标准库是全局变量）。
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）

#ifndef NEO_CORE_PRIVATE_PRIMITIVE_WRAPPER_BUILTINS_H
#define NEO_CORE_PRIVATE_PRIMITIVE_WRAPPER_BUILTINS_H

namespace neo::core {

class IContext;

// 在 global scope 注册 Boolean/Number/String 包装类（构造函数 + prototype 方法），
// 并建立四个包装原型（含 Symbol.prototype，供 Context::createPrimitiveWrapper 挂链）。
// Context 构造末尾调用一次（Array 内建之后）。失败返回 false（调用方 abort）。
bool registerPrimitiveWrapperBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_PRIMITIVE_WRAPPER_BUILTINS_H
