// Worker.cpp - 原生 Worker 实现（isolate-per-thread）
//
// 每个 Worker 一个线程：线程入口创建自己的 Context（线程亲和），拥有独立的
// 子分配器配额，然后循环从 inbox 取消息调用宿主处理器。
//
// 生命周期：start → run（创建 Context + 消息循环）→ requestStop 关闭 inbox
// → 消息循环退出 → join。析构自动 requestStop + join。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、028（infra 配额）

#include "worker/IWorker.h"

#include "infra/QuotaAllocator.h"

#include <atomic>
#include <thread>
#include <utility>

namespace neo::worker {

namespace {

class WorkerImpl final : public IWorker {
public:
    WorkerImpl(infra::IAllocator* parentAllocator, infra::Console* console,
               WorkerHandler handler, const WorkerOptions& options)
        : alloc_(infra::createQuotaAllocator(parentAllocator,
                                             options.memoryLimit)),
          console_(console),
          handler_(handler),
          clock_(options.clock),
          intl_(options.intl) {}

    ~WorkerImpl() override {
        requestStop();
        join();
    }

    bool start() override {
        if (thread_.joinable()) return false;
        running_.store(true, std::memory_order_release);
        thread_ = std::thread(&WorkerImpl::run, this);
        return true;
    }

    void post(Message msg) override {
        inbox_.push(std::move(msg));
    }

    bool tryReceive(Message& out) override {
        return outbox_.tryPop(out);
    }

    void requestStop() override {
        inbox_.close();
    }

    void join() override {
        if (thread_.joinable()) thread_.join();
        running_.store(false, std::memory_order_release);
    }

    bool running() const override {
        return running_.load(std::memory_order_acquire);
    }

    infra::IAllocator* allocator() const override {
        return alloc_.get();
    }

private:
    void run() {
        // Context 必须在本线程创建：ownerThread_ 在构造时捕获。
        std::unique_ptr<core::IContext> ctx =
            core::createContext(alloc_.get(), console_, clock_, intl_);

        Message msg;
        while (inbox_.waitPop(msg)) {
            handler_(ctx.get(), msg, outbox_);
        }
        running_.store(false, std::memory_order_release);
    }

    std::unique_ptr<infra::IAllocator> alloc_;
    infra::Console* console_;
    WorkerHandler handler_;
    infra::IClock* clock_;
    const NJS_IntlBackend* intl_;

    MessageQueue inbox_;   // 主线程 → worker
    MessageQueue outbox_;  // worker → 主线程

    std::thread thread_;
    std::atomic<bool> running_ {false};
};

}  // namespace

std::unique_ptr<IWorker> createWorker(infra::IAllocator* parentAllocator,
                                      infra::Console* console,
                                      WorkerHandler handler,
                                      WorkerOptions options) {
    return std::make_unique<WorkerImpl>(parentAllocator, console, handler,
                                        options);
}

}  // namespace neo::worker
