// PromiseBuiltins.h (private) - Promise 构造函数与 %Promise.prototype% 注册
//
// bootstrap 顺序（依赖 Object/Function 内建已就绪）：
//   1. Promise.prototype（普通对象，原型 = Object.prototype）
//   2. setPromisePrototype —— 此后 createPromise 自动挂链
//   3. Promise 构造函数（prototype 属性指向 Promise.prototype，双向 constructor）
//   4. Promise.prototype[@@toStringTag] = "Promise"
//   5. 注册到 global
//
// 构造函数语义（ES2024 §27.2.3.1）：
//   new Promise(executor) —— executor 必须是可调用；同步调用 executor(resolve, reject)，
//   executor 抛错则 reject。Promise 不是函数（无 new 调用报 TypeError）。
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）、043（Promise）

#ifndef NEO_CORE_PRIVATE_PROMISE_BUILTINS_H
#define NEO_CORE_PRIVATE_PROMISE_BUILTINS_H

namespace neo::core {

class IContext;

// 建立 %Promise.prototype% 并注册 Promise 构造函数到 global。
// Context 构造末尾调用一次。OOM 或注册失败返回 false（调用方 abort）。
bool registerPromiseBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_PROMISE_BUILTINS_H
