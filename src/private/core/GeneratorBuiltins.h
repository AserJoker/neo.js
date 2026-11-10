// GeneratorBuiltins.h (private) - %GeneratorPrototype% 与 next/return/throw（ADR-045 §8）
//
// 建立 %GeneratorPrototype%（原型 = %Iterator.prototype%）：
//   next(v)    —— 恢复协程；返回 { value, done }
//   return(v)  —— 注入 Interrupt(Return, resumePoint, v)；未启动时直接完成
//   throw(e)   —— 注入 Interrupt(Throw, resumePoint, createException(e))；未启动时直接抛出
//   @@iterator —— 返回 this
//   @@toStringTag —— "Generator"
//
// 生成器函数本身由 setFunctionGenerator 标记；生成器实例是 Coroutine 对象，
// 其原型为 fn.prototype（fn.prototype 已挂到 %GeneratorPrototype% 下）。
//
// 关联 ADR: 020（标准库组织）、034（原型-构造函数）、045（协程抽象）、046（Interrupt）

#ifndef NEO_CORE_PRIVATE_GENERATOR_BUILTINS_H
#define NEO_CORE_PRIVATE_GENERATOR_BUILTINS_H

namespace neo::core {

class IContext;

// 建立 %GeneratorPrototype% 并设置到 Context。
// 必须在 Iterator 内建之后调用（依赖 %Iterator.prototype%）。失败返回 false。
bool registerGeneratorBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_GENERATOR_BUILTINS_H
