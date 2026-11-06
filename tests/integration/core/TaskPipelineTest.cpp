// TaskPipelineTest.cpp - 事件循环端到端集成测试
//
// 端到端验证"任务入队 → 到期调度 → 执行 → 异常处理 → GC 协作"全链路
// （真实依赖不 mock，仅注入可控时钟）。
//
// 关联 ADR: 006（异常即值）、017 §4（Fatal 透传）、027（集成测试）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Task.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/IClock.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using neo::core::FunctionData;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::infra::Console;
using neo::infra::IAllocator;
using neo::infra::IClock;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class CapturingSink : public IOutputSink {
public:
    std::vector<std::string> lines;
    void write(std::string_view text) override { lines.emplace_back(text); }
    void writeLine(std::string_view text) override { lines.emplace_back(text); }
    void flush() override {}
};

class FakeClock : public IClock {
public:
    uint64_t now = 0;
    uint64_t nowMs() const override { return now; }
    void advance(uint64_t ms) { now += ms; }
};

const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }

// 把 global 槽位绑进函数闭包作用域（native 无捕获，任务执行器 this=undefined）
Value* bindSlot(IContext* ctx, Value* fn, const char* name, Value* value) {
    ctx->global()->bind(name, value);
    ctx->setClosure(fn, name);
    return fn;
}

// 造一个"记录执行顺序"的函数：每次执行把 tag 追加到 order 数组
Value* orderFn(IContext* ctx, Value* order, const char* tag) {
    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* arr = c->current()->lookupChain("__order");
            Value* t = c->current()->lookupChain("__tag");
            auto* d = static_cast<neo::core::ArrayData*>(arr->data());
            d->setElement(d->length(), t);
            return c->createUndefined();
        },
        0);
    bindSlot(ctx, fn, "__order", order);
    return bindSlot(ctx, fn, "__tag", ctx->createString(tag));
}

std::string joinOrder(Value* arr) {
    auto* d = static_cast<neo::core::ArrayData*>(arr->data());
    std::string out;
    for (uint32_t i = 0; i < d->length(); ++i) {
        if (i > 0) out += ',';
        out += strOf(d->elementAt(i));
    }
    return out;
}

struct Fixture {
    std::unique_ptr<IAllocator> alloc = createMallocAllocator();
    CapturingSink sink;
    Console console{&sink};
    FakeClock clock;
    std::unique_ptr<IContext> ctx{createContext(alloc.get(), &console, &clock)};
};

}  // namespace

// ============ 场景 1：定时器语义（延迟到期 + 顺序） ============

TEST(TaskPipelineTest, TimersFireInDueOrderAcrossClockAdvances) {
    Fixture f;
    Value* order = f.ctx->createArray();
    f.ctx->global()->bind("__order", order);

    Value* slow = orderFn(f.ctx.get(), order, "slow");
    Value* fast = orderFn(f.ctx.get(), order, "fast");
    // 用各自的 __order（orderFn 内部已绑定同一个 order）
    f.ctx->postTask(slow, 30);
    f.ctx->postTask(fast, 10);

    // 10ms：只有 fast 到期
    f.clock.advance(10);
    EXPECT_TRUE(f.ctx->hasRunnableTask());
    f.ctx->runOneTask();
    EXPECT_EQ(joinOrder(order), "fast");

    // 20ms（累计 30）：slow 到期
    f.clock.advance(20);
    f.ctx->runOneTask();
    EXPECT_EQ(joinOrder(order), "fast,slow");
    EXPECT_FALSE(f.ctx->hasTasks());
}

// ============ 场景 2：任务内再投递任务（链式调度） ============

TEST(TaskPipelineTest, TaskCanPostAnotherTaskDuringExecution) {
    Fixture f;
    Value* order = f.ctx->createArray();
    f.ctx->global()->bind("__order", order);

    // 第一个任务执行时再投递第二个
    Value* second = orderFn(f.ctx.get(), order, "second");
    Value* first = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* order2 = c->current()->lookupChain("__order");
            auto* d = static_cast<neo::core::ArrayData*>(order2->data());
            d->setElement(d->length(), c->createString("first"));
            c->postTask(c->current()->lookupChain("__second"));
            return c->createUndefined();
        },
        0);
    bindSlot(f.ctx.get(), first, "__order", order);
    bindSlot(f.ctx.get(), first, "__second", second);

    f.ctx->postTask(first);
    f.ctx->runOneTask();
    EXPECT_EQ(joinOrder(order), "first");
    EXPECT_TRUE(f.ctx->hasRunnableTask());  // 新任务已入队

    f.ctx->runOneTask();
    EXPECT_EQ(joinOrder(order), "first,second");
    EXPECT_FALSE(f.ctx->hasTasks());
}

// ============ 场景 3：循环任务（周期执行 + 取消） ============

TEST(TaskPipelineTest, LoopTaskRunsPeriodicallyUntilCancelled) {
    Fixture f;
    Value* order = f.ctx->createArray();
    f.ctx->global()->bind("__order", order);

    Value* tick = orderFn(f.ctx.get(), order, "t");
    uint64_t id = f.ctx->postLoopTask(tick, 5);

    for (int i = 0; i < 3; ++i) {
        f.clock.advance(5);
        f.ctx->runOneTask();
    }
    EXPECT_EQ(joinOrder(order), "t,t,t");
    EXPECT_TRUE(f.ctx->hasTasks());

    EXPECT_TRUE(f.ctx->cancelTask(id));
    f.clock.advance(5);
    EXPECT_FALSE(f.ctx->hasRunnableTask());
    EXPECT_FALSE(f.ctx->hasTasks());
}

// ============ 场景 4：未捕获异常不终止循环 ============

TEST(TaskPipelineTest, UncaughtExceptionDoesNotStopEventLoop) {
    Fixture f;
    Value* order = f.ctx->createArray();
    f.ctx->global()->bind("__order", order);

    Value* bad = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("bad task"));
        },
        0);
    Value* good = orderFn(f.ctx.get(), order, "good");

    f.ctx->postTask(bad);
    f.ctx->postTask(good);

    Value* r1 = f.ctx->runOneTask();
    EXPECT_EQ(r1->type()->kind(), TypeKind::Undefined);
    ASSERT_EQ(f.sink.lines.size(), 1u);
    EXPECT_EQ(f.sink.lines[0], "Uncaught bad task");

    // 循环继续：后续任务照常执行
    f.ctx->runOneTask();
    EXPECT_EQ(joinOrder(order), "good");
    EXPECT_FALSE(f.ctx->hasTasks());
}

// ============ 场景 5：GC 与任务根 ============

TEST(TaskPipelineTest, QueuedTasksSurviveGcAndAreCollectedAfterRun) {
    Fixture f;
    Value* order = f.ctx->createArray();
    f.ctx->global()->bind("__order", order);

    Value* fn = orderFn(f.ctx.get(), order, "x");
    size_t valuesBefore = f.ctx->gc()->valueCount();
    f.ctx->postTask(fn, 10);

    // 多轮 GC：任务实体由队列持有，不应被回收
    f.ctx->gc()->collect();
    f.ctx->gc()->collect();
    EXPECT_GE(f.ctx->gc()->valueCount(), valuesBefore);

    f.clock.advance(10);
    f.ctx->runOneTask();
    EXPECT_EQ(joinOrder(order), "x");

    // 出队后解除根引用，GC 可回收（值数量下降）
    size_t afterRun = f.ctx->gc()->valueCount();
    f.ctx->gc()->collect();
    EXPECT_LE(f.ctx->gc()->valueCount(), afterRun);
}

// ============ 场景 6：微任务优先（Promise 回调语义） ============

TEST(TaskPipelineTest, MicrotasksDrainBeforeNextMacrotask) {
    Fixture f;
    Value* order = f.ctx->createArray();
    f.ctx->global()->bind("__order", order);

    Value* macro1 = orderFn(f.ctx.get(), order, "macro1");
    Value* macro2 = orderFn(f.ctx.get(), order, "macro2");
    Value* micro1 = orderFn(f.ctx.get(), order, "micro1");
    Value* micro2 = orderFn(f.ctx.get(), order, "micro2");

    // 宏任务先入队、微任务后入队：微任务队列整体优先，先排空再执行宏任务
    f.ctx->postTask(macro1, 0);
    f.ctx->postTask(macro2, 0);
    f.ctx->postMicrotask(micro1);
    f.ctx->postMicrotask(micro2);

    while (f.ctx->hasRunnableTask()) f.ctx->runOneTask();
    EXPECT_EQ(joinOrder(order), "micro1,micro2,macro1,macro2");
}

// ============ 场景 7：微任务内再投递（Promise 链） ============

TEST(TaskPipelineTest, MicrotaskChainDrainsFullyBeforeMacrotask) {
    Fixture f;
    Value* order = f.ctx->createArray();
    f.ctx->global()->bind("__order", order);

    Value* macro = orderFn(f.ctx.get(), order, "macro");
    Value* step2 = orderFn(f.ctx.get(), order, "step2");
    // step1 执行时投递 step2（模拟 then 链）
    Value* step1 = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* arr = c->current()->lookupChain("__order");
            auto* d = static_cast<neo::core::ArrayData*>(arr->data());
            d->setElement(d->length(), c->createString("step1"));
            c->postMicrotask(c->current()->lookupChain("__step2"));
            return c->createUndefined();
        },
        0);
    bindSlot(f.ctx.get(), step1, "__order", order);
    bindSlot(f.ctx.get(), step1, "__step2", step2);

    f.ctx->postTask(macro, 0);
    f.ctx->postMicrotask(step1);

    while (f.ctx->hasRunnableTask()) f.ctx->runOneTask();
    EXPECT_EQ(joinOrder(order), "step1,step2,macro");
}

// ============ 场景 8：Fatal 终止循环并透传给宿主 ============

TEST(TaskPipelineTest, FatalPropagatesAndStopsLoopTask) {
    Fixture f;
    Value* fatal = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createFatal("oom"); }, 0);
    f.ctx->postLoopTask(fatal);

    Value* r = f.ctx->runOneTask();
    ASSERT_EQ(r->type()->kind(), TypeKind::Fatal);
    // 循环任务不再重新入队，宿主可安全退出
    EXPECT_FALSE(f.ctx->hasTasks());
    EXPECT_TRUE(f.sink.lines.empty());
}
