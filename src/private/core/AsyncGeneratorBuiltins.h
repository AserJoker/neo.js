// AsyncGeneratorBuiltins.h (private) - %AsyncIteratorPrototype% / %AsyncGeneratorPrototype%
//
// 建立异步生成器协议内建（ES2024 §27.6）：
//   %AsyncIteratorPrototype%  —— @@asyncIterator 返回 this
//   %AsyncGeneratorPrototype% —— next/return/throw 返回 Promise；@@toStringTag
//
// 异步驱动负责：请求队列串行化、resume 协程、await yield 值、settle 请求 Promise。
// 必须在 Promise 内建之后调用（依赖 %Promise% 构造函数）。
//
// 关联 ADR: 020（标准库组织）、034（原型-构造函数）、043（Promise）、045（协程抽象）

#ifndef NEO_CORE_PRIVATE_ASYNC_GENERATOR_BUILTINS_H
#define NEO_CORE_PRIVATE_ASYNC_GENERATOR_BUILTINS_H

namespace neo::core {

class IContext;

bool registerAsyncGeneratorBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_ASYNC_GENERATOR_BUILTINS_H
