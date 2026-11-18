// IntlBuiltins.h - Intl 命名空间与 Intl.NumberFormat（ECMA-402）
//
// 关联 ADR: 020（标准库组织）

#ifndef NEO_CORE_PRIVATE_INTL_BUILTINS_H
#define NEO_CORE_PRIVATE_INTL_BUILTINS_H

namespace neo::core {

class IContext;

// 注册 Intl 命名空间、Intl.getCanonicalLocales 与 Intl.NumberFormat。
bool registerIntlBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_INTL_BUILTINS_H
