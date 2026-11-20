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
#include <mutex>
#include <thread>
#include <utility>

namespace neo::worker {

namespace {

class WorkerImpl final : public IWorker {
public:
    WorkerImpl(infra::IAllocator* parentAllocator, infra::Console* console,
               const WorkerCallbacks& callbacks, const WorkerOptions& options)
        : alloc_(infra::createQuotaAllocator(parentAllocator,
                                             options.memoryLimit)),
          console_(console),
          callbacks_(callbacks),
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
        stopRequested_.store(true, std::memory_order_release);
        inbox_.close();
        // 若 worker 已在执行脚本，置 Context 停止标记；VM 每条指令后检查。
        // mutex 保证不会与 run() 末尾的 Context 销毁竞争。
        std::lock_guard<std::mutex> lock(ctxMutex_);
        if (ctx_ != nullptr) ctx_->requestStop();
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
        // 标记为 worker 上下文：Atomics.wait 等仅 worker 可阻塞。
        ctx->setWorkerContext(true);
        {
            std::lock_guard<std::mutex> lock(ctxMutex_);
            ctx_ = ctx.get();
        }
        // start 后立即 terminate：Context 尚未创建时已收到停止请求。
        if (stopRequested_.load(std::memory_order_acquire)) ctx->requestStop();

        // 外部任务（如 MessagePort 消息）入队时唤醒阻塞的 inbox 等待。
        ctx->setExternalTaskWake([this] {
            Message wake;
            wake.internal = true;
            inbox_.push(std::move(wake));
        });

        if (callbacks_.setup != nullptr) {
            callbacks_.setup(ctx.get(), outbox_, callbacks_.userdata);
        }

        auto drainTasks = [&ctx] {
            while (ctx->hasRunnableTask()) (void)ctx->runOneTask();
        };
        drainTasks();

        Message msg;
        while (inbox_.waitPop(msg)) {
            if (msg.internal) {
                drainTasks();
                continue;
            }
            if (callbacks_.handler != nullptr) {
                callbacks_.handler(ctx.get(), msg, outbox_, callbacks_.userdata);
            }
            drainTasks();
        }
        {
            std::lock_guard<std::mutex> lock(ctxMutex_);
            ctx_ = nullptr;
        }
        running_.store(false, std::memory_order_release);
    }

    std::unique_ptr<infra::IAllocator> alloc_;
    infra::Console* console_;
    WorkerCallbacks callbacks_;
    infra::IClock* clock_;
    const NJS_IntlBackend* intl_;

    MessageQueue inbox_;   // 主线程 → worker
    MessageQueue outbox_;  // worker → 主线程

    std::thread thread_;
    std::atomic<bool> running_ {false};
    std::atomic<bool> stopRequested_ {false};
    // worker 线程创建 Context 后写入；requestStop 跨线程读取并置停止标记。
    // mutex 保护 ctx_ 的读写与 Context 销毁的先后关系。
    std::mutex ctxMutex_;
    core::IContext* ctx_ = nullptr;
};

}  // namespace

std::unique_ptr<IWorker> createWorker(infra::IAllocator* parentAllocator,
                                      infra::Console* console,
                                      WorkerCallbacks callbacks,
                                      WorkerOptions options) {
    return std::make_unique<WorkerImpl>(parentAllocator, console, callbacks,
                                        options);
}

}  // namespace neo::worker
