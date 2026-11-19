// WorkerTest.cpp - 原生 Worker 骨架与子分配器单元测试
//
// 验证：
//   - QuotaAllocator 独立记账/限额，parent->used() 汇总；
//   - Worker 线程上创建 Context、消息往返、优雅停止；
//   - 每 Worker 独立子分配器，Context 销毁后配额归零。
//
// W1 无 JS API，消息载荷为字符串占位（W2 换 MessageValue）。
//
// 关联 ADR: 008（IAllocator）、010（接口-实现-工厂）、028（infra 配额）

#include "worker/IWorker.h"

#include "core/IContext.h"
#include "core/Value.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/IOutputSink.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace {

using neo::core::IContext;
using neo::core::StringData;
using neo::infra::Console;
using neo::infra::IAllocator;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;
using neo::infra::createQuotaAllocator;
using neo::worker::IWorker;
using neo::worker::Message;
using neo::worker::MessageQueue;
using neo::worker::createWorker;

// 测试用空输出 sink：避免 worker 线程写 stdout，且线程安全。
class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

// 回显处理器：同时在 worker 线程上创建/读取一个字符串，验证 Context 可用。
void echoHandler(IContext* ctx, const Message& in, MessageQueue& outbox) {
    neo::core::Value* v = ctx->createString("ok");
    auto* s = static_cast<StringData*>(v->data());
    Message reply;
    reply.value = neo::worker::MessageValue::stringValue(
        "echo:" + in.value.string + ":" + s->value);
    outbox.push(std::move(reply));
}

bool receiveWithTimeout(IWorker& w, Message& out, int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (w.tryReceive(out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return w.tryReceive(out);
}

TEST(QuotaAllocatorTest, TracksIndependentlyAndReportsToParent) {
    auto parent = createMallocAllocator();
    auto child = createQuotaAllocator(parent.get(), 64);

    void* p = child->alloc(32);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(child->used(), 32u);
    EXPECT_EQ(parent->used(), 32u);

    child->free(p);
    EXPECT_EQ(child->used(), 0u);
    EXPECT_EQ(parent->used(), 0u);
}

TEST(QuotaAllocatorTest, EnforcesOwnLimit) {
    auto parent = createMallocAllocator();
    auto child = createQuotaAllocator(parent.get(), 64);

    void* p = child->alloc(65);
    EXPECT_EQ(p, nullptr);
    EXPECT_EQ(child->used(), 0u);
    EXPECT_EQ(parent->used(), 0u);

    // parent 仍可继续分配（子分配器超限不影响 parent）
    void* q = parent->alloc(65);
    ASSERT_NE(q, nullptr);
    parent->free(q);
}

TEST(QuotaAllocatorTest, AlignedAllocationAccounting) {
    auto parent = createMallocAllocator();
    auto child = createQuotaAllocator(parent.get(), 64);

    void* p = child->allocAligned(17, 16);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(child->used(), 32u);  // 对齐后 32 字节
    EXPECT_EQ(parent->used(), 32u);

    child->freeAligned(p);
    EXPECT_EQ(child->used(), 0u);
    EXPECT_EQ(parent->used(), 0u);
}

TEST(WorkerTest, EchoRoundTrip) {
    auto parent = createMallocAllocator();
    NullSink sink;
    Console console(&sink);

    auto w = createWorker(parent.get(), &console, &echoHandler);
    ASSERT_TRUE(w->start());
    EXPECT_TRUE(w->running());

    Message msg;
    msg.value = neo::worker::MessageValue::stringValue("ping");
    w->post(std::move(msg));

    Message reply;
    ASSERT_TRUE(receiveWithTimeout(*w, reply, 10000));
    EXPECT_EQ(reply.value.string, "echo:ping:ok");

    w->requestStop();
    w->join();
    EXPECT_FALSE(w->running());
}

TEST(WorkerTest, StopDrainsQueuedMessages) {
    auto parent = createMallocAllocator();
    NullSink sink;
    Console console(&sink);

    auto w = createWorker(parent.get(), &console, &echoHandler);
    ASSERT_TRUE(w->start());

    for (int i = 0; i < 3; ++i) {
        Message msg;
        msg.value = neo::worker::MessageValue::stringValue(std::to_string(i));
        w->post(std::move(msg));
    }
    w->requestStop();
    w->join();

    // 关闭后已排队的消息仍处理完（waitPop 先排空再退出）。
    for (int i = 0; i < 3; ++i) {
        Message reply;
        ASSERT_TRUE(w->tryReceive(reply)) << "missing reply " << i;
        EXPECT_EQ(reply.value.string, "echo:" + std::to_string(i) + ":ok");
    }
    Message extra;
    EXPECT_FALSE(w->tryReceive(extra));
}

TEST(WorkerTest, ChildAllocatorQuotaReleasedOnJoin) {
    auto parent = createMallocAllocator();
    NullSink sink;
    Console console(&sink);

    auto w = createWorker(parent.get(), &console, &echoHandler);
    ASSERT_TRUE(w->start());

    Message msg;
    msg.value = neo::worker::MessageValue::stringValue("x");
    w->post(std::move(msg));
    Message reply;
    ASSERT_TRUE(receiveWithTimeout(*w, reply, 10000));

    // Context 存活期间，子分配器有占用，且 parent 汇总包含它。
    EXPECT_GT(w->allocator()->used(), 0u);
    EXPECT_GE(parent->used(), w->allocator()->used());

    w->requestStop();
    w->join();

    // Context 析构后子分配器记账归零（无泄漏）。
    EXPECT_EQ(w->allocator()->used(), 0u);
}

TEST(WorkerTest, IndependentWorkers) {
    auto parent = createMallocAllocator();
    NullSink sink;
    Console console(&sink);

    auto a = createWorker(parent.get(), &console, &echoHandler);
    auto b = createWorker(parent.get(), &console, &echoHandler);
    ASSERT_TRUE(a->start());
    ASSERT_TRUE(b->start());
    EXPECT_NE(a->allocator(), b->allocator());

    Message ma;
    ma.value = neo::worker::MessageValue::stringValue("a");
    a->post(std::move(ma));
    Message mb;
    mb.value = neo::worker::MessageValue::stringValue("b");
    b->post(std::move(mb));

    Message ra;
    Message rb;
    ASSERT_TRUE(receiveWithTimeout(*a, ra, 10000));
    ASSERT_TRUE(receiveWithTimeout(*b, rb, 10000));
    EXPECT_EQ(ra.value.string, "echo:a:ok");
    EXPECT_EQ(rb.value.string, "echo:b:ok");

    a->requestStop();
    b->requestStop();
    a->join();
    b->join();
}

}  // namespace
