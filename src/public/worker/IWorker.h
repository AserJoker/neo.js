// IWorker.h - 原生 Worker 门面（isolate-per-thread）
//
// 多线程模型（W1）：每个 Worker 独占一个线程，在该线程上创建自己的 Context
// （GC/作用域/事件循环全部线程亲和），并拥有独立的子分配器配额。主线程与
// Worker 之间只通过 MessageQueue 传递消息，绝不共享 Value*。
//
// W1 只提供 C++ 层骨架（无 JS API）：宿主用 createWorker 指定消息处理器，
// 处理器在 worker 线程上执行。W2 起增加 JS Worker/MessagePort 门面与
// MessageValue 结构化克隆。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、028（infra 配额）

#ifndef NEO_WORKER_IWORKER_H
#define NEO_WORKER_IWORKER_H

#include "core/IContext.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/IClock.h"
#include "neojs.h"
#include "worker/MessageQueue.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace neo::worker {

// 初始化回调：worker 线程创建 Context 后、消息循环开始前执行一次。
// 用于注册 worker 专属全局（postMessage/onmessage）并运行入口脚本。
using WorkerSetup = void (*)(core::IContext* ctx, MessageQueue& outbox,
                             void* userdata);

// 消息处理器：在 worker 线程上执行。ctx 是该线程独占的 Context；
// outbox 可随时 push，主线程用 tryReceive 取回。
using WorkerHandler = void (*)(core::IContext* ctx, const Message& in,
                               MessageQueue& outbox, void* userdata);

struct WorkerCallbacks {
    WorkerSetup setup = nullptr;
    WorkerHandler handler = nullptr;
    // 调用方持有；必须活到 worker 线程退出（IWorker 析构会 join）。
    void* userdata = nullptr;
};

struct WorkerOptions {
    // 该 Worker 的独立内存配额（SIZE_MAX = 不限额）。
    // 实际内存仍由 parentAllocator 分配，parent->used() 汇总统计。
    size_t memoryLimit = SIZE_MAX;
    // nullptr 时使用进程级默认时钟。
    infra::IClock* clock = nullptr;
    // 宿主 Intl 后端；nullptr 或 ABI 不兼容时使用内置最小后端。
    const NJS_IntlBackend* intl = nullptr;
};

// Worker 句柄。析构时自动 requestStop + join。
class IWorker {
public:
    virtual ~IWorker() = default;

    // 启动 worker 线程（在该线程上创建 Context）。重复启动返回 false。
    virtual bool start() = 0;

    // 主线程向 worker 投递消息；worker 已停止时丢弃。
    virtual void post(Message msg) = 0;

    // 主线程非阻塞取回 worker 发出的消息。
    virtual bool tryReceive(Message& out) = 0;

    // 请求停止：关闭 inbox，唤醒等待中的 worker。已排队的消息仍会处理完。
    virtual void requestStop() = 0;

    // 等待 worker 线程退出（未启动时无操作）。
    virtual void join() = 0;

    // worker 线程是否仍在运行。
    virtual bool running() const = 0;

    // 该 Worker 的子分配器（借用；生命周期与 Worker 一致）。
    virtual infra::IAllocator* allocator() const = 0;
};

// 工厂：创建 Worker。parentAllocator 必须比返回的 Worker 活得久。
// console 由调用方持有，所有 Worker 共享；默认 StdoutSink 已线程安全。
std::unique_ptr<IWorker> createWorker(infra::IAllocator* parentAllocator,
                                      infra::Console* console,
                                      WorkerCallbacks callbacks,
                                      WorkerOptions options = {});

}  // namespace neo::worker

#endif  // NEO_WORKER_IWORKER_H
