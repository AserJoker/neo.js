// PortBuiltins.h - MessagePort / MessageChannel 注册与转移端口创建

#ifndef NEO_CORE_PRIVATE_PORT_BUILTINS_H
#define NEO_CORE_PRIVATE_PORT_BUILTINS_H

#include <memory>

namespace neo::worker {
class MessagePortEndpoint;
}  // namespace neo::worker

namespace neo::core {

class IContext;
class Value;

// 注册全局 MessagePort / MessageChannel。
bool registerMessagePortBuiltins(IContext* ctx);

// 为已转移的端点创建接收方端口对象（MessageValue 还原用）。
Value* createTransferredPort(
    IContext* ctx, std::shared_ptr<worker::MessagePortEndpoint> endpoint);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_PORT_BUILTINS_H
