// ErrorBuiltins.h (private) - Error 基类与 NativeError 子类注册
//
// bootstrap 顺序（依赖 Object/Function 内建已就绪）：
//   1. Error.prototype（普通对象，原型 = Object.prototype）
//   2. setErrorPrototype —— 此后 createError 自动挂链
//   3. Error 构造函数（prototype 属性指向 Error.prototype，双向 constructor）
//   4. Error.prototype.name / message / toString
//   5. 一般性错误子类：TypeError / RangeError / ReferenceError / SyntaxError / URIError
//   6. 注册到 global
//
// 构造函数语义（ES2024 §20.5.1.1 / §20.5.6.1）：
//   Error(message) / new Error(message) —— 都返回新 Error 对象；
//   子类同理，只是原型与 name 不同。message 为 undefined 时不定义 message 属性，
//   否则 ToString 后定义。
//
// 范围（ES2024 §20.5.6）：EvalError 已不在规范中，不实现；
// AggregateError / SuppressedError 不在本阶段范围。
// 所有子类实例共用 TypeKind::Error（都是 [[ErrorData]] 对象，靠原型链区分）。
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）、038（Error 基类）

#ifndef NEO_CORE_PRIVATE_ERROR_BUILTINS_H
#define NEO_CORE_PRIVATE_ERROR_BUILTINS_H

namespace neo::core {

class IContext;

// 建立 %Error.prototype% 并注册 Error 构造函数到 global。
// Context 构造末尾调用一次。OOM 或注册失败返回 false（调用方 abort）。
bool registerErrorBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_ERROR_BUILTINS_H
