// Port.h (private) - MessagePort 端点与共享通道
//
// 一对 entangled 端点共享一个 PortChannel（两条线程安全队列）。post 把消息
// 放入对端队列，并通过对端注册的 wake（owner Context）投递外部任务唤醒其
// 事件循环。
//
// 端点本身与 Context/GC 无关：JS 侧用 MessagePortData 持有 shared_ptr，
// 转移时把同一端点交给接收方的 Context，并安装新的 wake。

#ifndef NEO_WORKER_PRIVATE_PORT_H
#define NEO_WORKER_PRIVATE_PORT_H

#include "worker/MessageQueue.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace neo::core {
class IContext;
class Value;
}  // namespace neo::core

namespace neo::worker {

class MessagePortEndpoint;

// 端点的 owner 唤醒信息。alive=false 后不再向该 Context 投递任务。
struct PortWake {
    core::IContext* ctx = nullptr;
    std::atomic<bool> alive {true};
};

using PortDispatch = void (*)(core::IContext*,
                              const std::shared_ptr<MessagePortEndpoint>&);

struct PortChannel {
    MessageQueue queues[2];
    std::atomic<bool> closed[2] {{false}, {false}};
    // 弱引用避免 channel ↔ endpoint 循环。
    std::weak_ptr<MessagePortEndpoint> endpoints[2];
};

class MessagePortEndpoint {
public:
    static std::pair<std::shared_ptr<MessagePortEndpoint>,
                     std::shared_ptr<MessagePortEndpoint>>
    createPair();

    // 发送到对端。返回 false 表示端口已关闭或队列拒绝。
    bool post(Message msg);
    void close();

    // 安装 owner（创建端口对象 / 接收转移端口时）。
    void attach(core::IContext* ctx, core::Value* object, PortDispatch dispatch);
    // 源对象被转移：使旧 owner 的唤醒失效。
    void detachOwner();

    std::shared_ptr<PortWake> wakeSnapshot() const;

    int side() const { return side_; }
    bool closed() const { return closed_.load(std::memory_order_acquire); }
    core::IContext* ownerCtx() const {
        return ownerCtx_.load(std::memory_order_acquire);
    }
    core::Value* portObject() const {
        return portObject_.load(std::memory_order_acquire);
    }
    PortDispatch dispatch() const {
        return dispatch_.load(std::memory_order_acquire);
    }
    std::shared_ptr<PortChannel> channel() const { return channel_; }

private:
    std::shared_ptr<PortChannel> channel_;
    int side_ = 0;
    std::atomic<bool> closed_ {false};
    std::atomic<core::IContext*> ownerCtx_ {nullptr};
    std::atomic<core::Value*> portObject_ {nullptr};
    std::atomic<PortDispatch> dispatch_ {nullptr};

    mutable std::mutex wakeMutex_;
    std::shared_ptr<PortWake> wake_;
};

}  // namespace neo::worker

#endif  // NEO_WORKER_PRIVATE_PORT_H
