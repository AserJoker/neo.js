// ProxyBuiltins.h (private) - Proxy 构造函数与 Proxy.revocable 注册
//
// 关联 ADR: 020（标准库组织）、048（Proxy 内部方法层）

#ifndef NEO_CORE_PRIVATE_PROXY_BUILTINS_H
#define NEO_CORE_PRIVATE_PROXY_BUILTINS_H

namespace neo::core {

class IContext;

// 建立 Proxy 构造函数并注册到 global。依赖 Object/Function 内建已就绪。
// 失败返回 false（调用方 abort）。
bool registerProxyBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_PROXY_BUILTINS_H
