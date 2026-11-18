// ConsoleBuiltins.h - 宿主侧 console 与 queueMicrotask
//
// 关联 ADR: 020（标准库组织）、006（异常即值）

#ifndef NEO_CORE_PRIVATE_CONSOLE_BUILTINS_H
#define NEO_CORE_PRIVATE_CONSOLE_BUILTINS_H

namespace neo::core {

class IContext;

// 注册 global.console 与 queueMicrotask。
bool registerConsoleBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_CONSOLE_BUILTINS_H
