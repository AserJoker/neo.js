// MessageQueue.h - 跨线程消息队列
//
// Worker 与主线程之间只传消息，不共享 Value/Context。队列本身是线程安全的：
// 生产者可在任意线程 push，消费者在 worker 线程 waitPop。
//
// 消息载荷是与 Context 无关的 MessageValue（JSON-like 深拷贝）。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装）、028（infra）

#ifndef NEO_WORKER_MESSAGEQUEUE_H
#define NEO_WORKER_MESSAGEQUEUE_H

#include "worker/MessageValue.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace neo::worker {

// 跨线程消息：载荷是深拷贝后的结构化值。
struct Message {
    MessageValue value;
};

// 线程安全 FIFO。close 后不再接受新消息，等待者被唤醒并返回 false。
class MessageQueue {
public:
    // 投递消息；队列已关闭时丢弃（返回 false）。
    bool push(Message msg) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (closed_) return false;
            queue_.push_back(std::move(msg));
        }
        cv_.notify_one();
        return true;
    }

    // 非阻塞取出一条消息；空或已关闭且空时返回 false。
    bool tryPop(Message& out) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    // 阻塞等待一条消息。队列关闭时返回 false（已排队的消息先取完）。
    bool waitPop(Message& out) {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    // 关闭队列：唤醒所有等待者，此后 push 被丢弃。
    void close() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Message> queue_;
    bool closed_ = false;
};

}  // namespace neo::worker

#endif  // NEO_WORKER_MESSAGEQUEUE_H
