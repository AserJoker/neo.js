// Port.cpp - MessagePort 端点实现

#include "worker/Port.h"

#include "core/IContext.h"

#include <utility>

namespace neo::worker {

std::pair<std::shared_ptr<MessagePortEndpoint>,
          std::shared_ptr<MessagePortEndpoint>>
MessagePortEndpoint::createPair() {
    auto channel = std::make_shared<PortChannel>();
    auto p0 = std::shared_ptr<MessagePortEndpoint>(new MessagePortEndpoint());
    auto p1 = std::shared_ptr<MessagePortEndpoint>(new MessagePortEndpoint());
    p0->channel_ = channel;
    p0->side_ = 0;
    p1->channel_ = channel;
    p1->side_ = 1;
    channel->endpoints[0] = p0;
    channel->endpoints[1] = p1;
    return {std::move(p0), std::move(p1)};
}

bool MessagePortEndpoint::post(Message msg) {
    if (closed_.load(std::memory_order_acquire) || !channel_) return false;
    const int dst = 1 - side_;
    if (channel_->closed[dst].load(std::memory_order_acquire)) return false;
    if (!channel_->queues[dst].push(std::move(msg))) return false;

    auto other = channel_->endpoints[dst].lock();
    if (!other) return true;
    auto wake = other->wakeSnapshot();
    PortDispatch dispatch = other->dispatch();
    if (wake && wake->alive.load(std::memory_order_acquire) && dispatch) {
        wake->ctx->postExternalTask(
            [other, dispatch](core::IContext* ctx) { dispatch(ctx, other); });
    }
    return true;
}

void MessagePortEndpoint::close() {
    closed_.store(true, std::memory_order_release);
    if (!channel_) return;
    channel_->closed[side_].store(true, std::memory_order_release);
    channel_->queues[side_].close();
}

void MessagePortEndpoint::attach(core::IContext* ctx, core::Value* object,
                                 PortDispatch dispatch) {
    ownerCtx_.store(ctx, std::memory_order_release);
    portObject_.store(object, std::memory_order_release);
    dispatch_.store(dispatch, std::memory_order_release);

    auto wake = std::make_shared<PortWake>();
    wake->ctx = ctx;
    std::lock_guard<std::mutex> lk(wakeMutex_);
    wake_ = std::move(wake);
}

void MessagePortEndpoint::detachOwner() {
    std::shared_ptr<PortWake> old;
    {
        std::lock_guard<std::mutex> lk(wakeMutex_);
        old = std::move(wake_);
        wake_.reset();
    }
    if (old) old->alive.store(false, std::memory_order_release);
    ownerCtx_.store(nullptr, std::memory_order_release);
    portObject_.store(nullptr, std::memory_order_release);
    dispatch_.store(nullptr, std::memory_order_release);
}

std::shared_ptr<PortWake> MessagePortEndpoint::wakeSnapshot() const {
    std::lock_guard<std::mutex> lk(wakeMutex_);
    return wake_;
}

}  // namespace neo::worker
