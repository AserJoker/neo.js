// SymbolBuiltins.h (private) - Symbol 包装类注册
//
// 最小包装类（ES2024 §20.4）：
//   Symbol(description)          —— 普通函数调用返回新 symbol（不是构造函数）
//   Symbol.prototype.toString()  —— 返回 "Symbol(description)"
//   Symbol.<well-known>          —— 15 个 well-known symbols 作为构造函数属性
//
// 注册到 global scope（ADR-020 §2 标准库是全局变量）。
//
// 关联 ADR: 020（标准库组织）、016（Type 体系）

#ifndef NEO_CORE_PRIVATE_SYMBOL_BUILTINS_H
#define NEO_CORE_PRIVATE_SYMBOL_BUILTINS_H

namespace neo::core {

class IContext;

// 在 global scope 注册 Symbol 包装类（构造函数 + prototype.toString + well-known 属性）。
// Context 构造末尾调用一次。OOM 时返回 false（调用方 abort）。
bool registerSymbolBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_SYMBOL_BUILTINS_H
