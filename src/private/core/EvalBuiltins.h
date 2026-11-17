// EvalBuiltins.h - 全局 eval 内建（ES2024 §19.2.1）
//
// eval 的运行时入口：源码 → Parser → Sema → Compiler → VM。
// 直接 eval 由 Compiler 在调用点发射 DirectEval 指令，VM 校验被调函数
// 是否为内建 %eval% 后把调用方作用域交给 Context::evalSource；间接 eval
// 走普通调用，在 global 作用域执行。
//
// 关联 ADR: 006（异常即值）、033（eval 与隔离函数）、058（Sema）、
//          060（VM 框架）、061（Compiler 框架）

#ifndef NEO_CORE_PRIVATE_EVAL_BUILTINS_H
#define NEO_CORE_PRIVATE_EVAL_BUILTINS_H

namespace neo::core {

class IContext;

// 注册全局 eval 并记录 %eval% 函数对象。失败返回 false（bootstrap abort）。
bool registerEvalBuiltin(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_EVAL_BUILTINS_H
