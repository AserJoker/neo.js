// TaskTest.cpp - 事件循环任务队列与默认执行器单元测试
//
// 验证（非阻塞事件循环）：
// - postTask 返回 id、hasTasks/hasRunnableTask 的到期判定
// - 到期顺序：按 (due, 入队顺序) 稳定排序
// - runOneTask 执行任务并返回结果；无到期任务返回 undefined
// - cancelTask 取消未执行任务，未知 id 返回 false
// - 循环任务执行后重新入队，取消后不再执行
// - 默认执行器：Exception 打印 "Uncaught <消息>" 后返回 undefined；Fatal 透传
// - 任务实体作为 GC 根持有，出队后释放
//
// 关联 ADR: 006（异常即值）、017 §4（Fatal 透传）、010（接口-实现-工厂）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Task.h"
#include "core/Value.h"
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

using neo::core::IContext;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::TaskRunner;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::infra::Console;
using neo::infra::IAllocator;
using neo::infra::IClock;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

// 记录所有输出的 sink（验证未捕获异常打印）
class CapturingSink : public IOutputSink {
public:
    std::vector<std::string> lines;
    void write(std::string_view text) override { lines.emplace_back(text); }
    void writeLine(std::string_view text) override { lines.emplace_back(text); }
    void flush() override {}
};

// 可控假时钟：测试显式推进时间
class FakeClock : public IClock {
public:
    uint64_t now = 1000;
    uint64_t nowMs() const override { return now; }
    void advance(uint64_t ms) { now += ms; }
};

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }

// native 函数无捕获，且任务执行器以 undefined 为 this，故测试数据经函数的
// 闭包作用域传递（setClosure 把 global 上的临时槽位绑进 fn 的 closureScope）。
// 每个函数有独立闭包槽位，同用例内多个函数互不干扰。
// 槽位：__calls（调用次数）、__result（返回值）、__msg（异常消息）。
Value* bindClosureSlot(IContext* ctx, Value* fn, const char* name, Value* value) {
    ctx->global()->bind(name, value);
    ctx->setClosure(fn, name);
    return fn;
}

Value* countingFn(IContext* ctx, double result) {
    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* n = c->current()->lookupChain("__calls");
            double calls = static_cast<NumberData*>(n->data())->value + 1.0;
            c->store("__calls", c->createNumber(calls));
            return c->current()->lookupChain("__result");
        },
        0);
    bindClosureSlot(ctx, fn, "__calls", ctx->createNumber(0));
    bindClosureSlot(ctx, fn, "__result", ctx->createNumber(result));
    return fn;
}

// 读函数闭包作用域里的 __calls（global 上的同名槽位不会被 store 更新）
double callCount(IContext* /*ctx*/, Value* fn) {
    auto* d = static_cast<neo::core::FunctionData*>(fn->data());
    return numOf(d->closureScope->lookup("__calls"));
}

// 造一个抛 Exception 的 native 函数
Value* throwingFn(IContext* ctx, const char* msg) {
    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->current()->lookupChain("__msg"));
        },
        0);
    return bindClosureSlot(ctx, fn, "__msg", ctx->createString(msg));
}

// 造一个返回 Fatal 的 native 函数
Value* fatalFn(IContext* ctx) {
    return ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createFatal("boom"); }, 0);
}

struct Fixture {
    std::unique_ptr<IAllocator> alloc = createMallocAllocator();
    CapturingSink sink;
    Console console{&sink};
    FakeClock clock;
    std::unique_ptr<IContext> ctx{createContext(alloc.get(), &console, &clock)};
};

}  // namespace

// ===== 入队与查询 =====

TEST(TaskTest, PostTaskReturnsPositiveIdAndIsPending) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    uint64_t id = f.ctx->postTask(fn);
    EXPECT_GT(id, 0u);
    EXPECT_TRUE(f.ctx->hasTasks());
}

TEST(TaskTest, ZeroTimeoutIsRunnableImmediately) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    f.ctx->postTask(fn, 0);
    EXPECT_TRUE(f.ctx->hasRunnableTask());
}

TEST(TaskTest, FutureTimeoutIsNotRunnableUntilClockAdvances) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    f.ctx->postTask(fn, 100);
    EXPECT_TRUE(f.ctx->hasTasks());
    EXPECT_FALSE(f.ctx->hasRunnableTask());

    f.clock.advance(99);
    EXPECT_FALSE(f.ctx->hasRunnableTask());

    f.clock.advance(1);
    EXPECT_TRUE(f.ctx->hasRunnableTask());
}

TEST(TaskTest, EmptyQueueHasNoTasksAndRunReturnsUndefined) {
    Fixture f;
    EXPECT_FALSE(f.ctx->hasTasks());
    EXPECT_FALSE(f.ctx->hasRunnableTask());
    Value* r = f.ctx->runOneTask();
    EXPECT_EQ(r->type()->kind(), TypeKind::Undefined);
}

// ===== 微任务队列（优先于宏任务） =====

TEST(TaskTest, MicrotaskIsRunnableAndHasTask) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    uint64_t id = f.ctx->postMicrotask(fn);
    EXPECT_GT(id, 0u);
    EXPECT_TRUE(f.ctx->hasTasks());
    EXPECT_TRUE(f.ctx->hasRunnableTask());
}

TEST(TaskTest, MicrotaskRunsBeforeReadyMacrotask) {
    Fixture f;
    Value* macro = countingFn(f.ctx.get(), 2);
    Value* micro = countingFn(f.ctx.get(), 1);
    // 宏任务先入队且已到期，微任务后入队——微任务仍先执行
    f.ctx->postTask(macro, 0);
    f.ctx->postMicrotask(micro);

    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), micro), 1.0);
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), macro), 0.0);

    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), macro), 1.0);
}

TEST(TaskTest, MicrotasksDrainBeforeNextMacrotask) {
    Fixture f;
    Value* macro = countingFn(f.ctx.get(), 3);
    Value* m1 = countingFn(f.ctx.get(), 1);
    Value* m2 = countingFn(f.ctx.get(), 2);
    f.ctx->postTask(macro, 0);
    f.ctx->postMicrotask(m1);
    f.ctx->postMicrotask(m2);

    // 排空微任务（FIFO）
    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), m1), 1.0);
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), macro), 0.0);
    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), m2), 1.0);
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), macro), 0.0);

    // 微任务排空后才轮到宏任务
    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), macro), 1.0);
    EXPECT_FALSE(f.ctx->hasTasks());
}

TEST(TaskTest, MicrotaskRunsEvenWhenOnlyFutureMacrotaskExists) {
    Fixture f;
    Value* micro = countingFn(f.ctx.get(), 1);
    Value* macro = countingFn(f.ctx.get(), 2);
    f.ctx->postTask(macro, 1000);  // 未到期
    f.ctx->postMicrotask(micro);

    EXPECT_TRUE(f.ctx->hasRunnableTask());
    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), micro), 1.0);
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), macro), 0.0);
    EXPECT_TRUE(f.ctx->hasTasks());          // 宏任务仍在队列
    EXPECT_FALSE(f.ctx->hasRunnableTask());  // 但未到期
}

TEST(TaskTest, MicrotaskPostedDuringMicrotaskRunsInSameDrain) {
    Fixture f;
    Value* nested = countingFn(f.ctx.get(), 2);
    Value* first = countingFn(f.ctx.get(), 1);
    f.ctx->postMicrotask(first);

    f.ctx->runOneTask();  // first
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), first), 1.0);
    // 手动再投递一个微任务，模拟回调内 queueMicrotask
    f.ctx->postMicrotask(nested);
    EXPECT_TRUE(f.ctx->hasRunnableTask());
    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), nested), 1.0);
}

TEST(TaskTest, CancelMicrotask) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    uint64_t id = f.ctx->postMicrotask(fn);
    EXPECT_TRUE(f.ctx->cancelTask(id));
    EXPECT_FALSE(f.ctx->hasTasks());
    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), fn), 0.0);
}

TEST(TaskTest, MicrotaskEntityIsRootedAndReleased) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    int before = fn->scopeRef();
    f.ctx->postMicrotask(fn);
    EXPECT_EQ(fn->scopeRef(), before + 1);
    f.ctx->runOneTask();
    EXPECT_EQ(fn->scopeRef(), before);
}

TEST(TaskTest, MicrotaskWithNullEntityReturnsZero) {
    Fixture f;
    EXPECT_EQ(f.ctx->postMicrotask(nullptr), 0u);
    EXPECT_FALSE(f.ctx->hasTasks());
}

// ===== 执行 =====

TEST(TaskTest, RunOneTaskInvokesEntityAndReturnsResult) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 42);
    f.ctx->postTask(fn);

    Value* r = f.ctx->runOneTask();
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(r), 42.0);
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), fn), 1.0);
    EXPECT_FALSE(f.ctx->hasTasks());
}

TEST(TaskTest, RunOneTaskWithoutRunnableTaskDoesNotConsumeFutureTask) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    f.ctx->postTask(fn, 50);

    Value* r = f.ctx->runOneTask();
    EXPECT_EQ(r->type()->kind(), TypeKind::Undefined);
    EXPECT_TRUE(f.ctx->hasTasks());
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), fn), 0.0);
}

TEST(TaskTest, TasksRunInDueOrder) {
    Fixture f;
    Value* late = countingFn(f.ctx.get(), 2);
    Value* early = countingFn(f.ctx.get(), 1);
    f.ctx->postTask(late, 100);
    f.ctx->postTask(early, 10);

    f.clock.advance(100);
    f.ctx->runOneTask();  // early（due 更早）
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), early), 1.0);
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), late), 0.0);

    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), late), 1.0);
}

TEST(TaskTest, SameDueRunsInInsertionOrder) {
    Fixture f;
    Value* first = countingFn(f.ctx.get(), 1);
    Value* second = countingFn(f.ctx.get(), 2);
    f.ctx->postTask(first, 10);
    f.ctx->postTask(second, 10);

    f.clock.advance(10);
    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), first), 1.0);
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), second), 0.0);
}

TEST(TaskTest, CustomRunnerReturningNullYieldsUndefined) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    f.ctx->postTask(fn, 0, [](IContext*, Value*) -> Value* { return nullptr; });
    Value* r = f.ctx->runOneTask();
    EXPECT_EQ(r->type()->kind(), TypeKind::Undefined);
}

TEST(TaskTest, CustomRunnerIsUsed) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    f.ctx->postTask(fn, 0, [](IContext* c, Value*) {
        return c->createString("custom");
    });
    Value* r = f.ctx->runOneTask();
    ASSERT_EQ(r->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(r), "custom");
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), fn), 0.0);  // 实体未被调用
}

// ===== 取消 =====

TEST(TaskTest, CancelTaskRemovesPendingTask) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    uint64_t id = f.ctx->postTask(fn);
    EXPECT_TRUE(f.ctx->cancelTask(id));
    EXPECT_FALSE(f.ctx->hasTasks());
    EXPECT_FALSE(f.ctx->hasRunnableTask());

    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), fn), 0.0);
}

TEST(TaskTest, CancelUnknownIdReturnsFalse) {
    Fixture f;
    EXPECT_FALSE(f.ctx->cancelTask(12345));
}

TEST(TaskTest, CancelOnlyRemovesTargetTask) {
    Fixture f;
    Value* a = countingFn(f.ctx.get(), 1);
    Value* b = countingFn(f.ctx.get(), 2);
    uint64_t idA = f.ctx->postTask(a);
    f.ctx->postTask(b);

    EXPECT_TRUE(f.ctx->cancelTask(idA));
    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), a), 0.0);
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), b), 1.0);
}

// ===== 循环任务 =====

TEST(TaskTest, LoopTaskRequeuesAfterRun) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    uint64_t id = f.ctx->postLoopTask(fn, 0);

    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), fn), 1.0);
    EXPECT_TRUE(f.ctx->hasTasks());       // 已重新入队
    EXPECT_TRUE(f.ctx->hasRunnableTask());  // 以当前时刻为起点，立即到期

    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), fn), 2.0);

    EXPECT_TRUE(f.ctx->cancelTask(id));
    EXPECT_FALSE(f.ctx->hasTasks());
}

TEST(TaskTest, LoopTaskHonorsTimeoutBetweenRuns) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    f.ctx->postLoopTask(fn, 20);

    f.clock.advance(20);
    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), fn), 1.0);
    // 重新入队后起点是执行时刻，需再等 20ms
    EXPECT_FALSE(f.ctx->hasRunnableTask());
    f.clock.advance(20);
    EXPECT_TRUE(f.ctx->hasRunnableTask());
}

// ===== 默认执行器：异常与 Fatal =====

TEST(TaskTest, ExceptionIsPrintedAsUncaughtAndReturnsUndefined) {
    Fixture f;
    Value* fn = throwingFn(f.ctx.get(), "kaboom");
    f.ctx->postTask(fn);

    Value* r = f.ctx->runOneTask();
    EXPECT_EQ(r->type()->kind(), TypeKind::Undefined);
    ASSERT_EQ(f.sink.lines.size(), 1u);
    EXPECT_EQ(f.sink.lines[0], "Uncaught kaboom");
    EXPECT_FALSE(f.ctx->hasTasks());
}

TEST(TaskTest, ExceptionWithNonStringChildIsStringified) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createNumber(42));
        },
        0);
    f.ctx->postTask(fn);

    Value* r = f.ctx->runOneTask();
    EXPECT_EQ(r->type()->kind(), TypeKind::Undefined);
    ASSERT_EQ(f.sink.lines.size(), 1u);
    EXPECT_EQ(f.sink.lines[0], "Uncaught 42");
}

TEST(TaskTest, FatalIsPropagatedAndLoopStops) {
    Fixture f;
    Value* fn = fatalFn(f.ctx.get());
    f.ctx->postLoopTask(fn);

    Value* r = f.ctx->runOneTask();
    ASSERT_EQ(r->type()->kind(), TypeKind::Fatal);
    // Fatal 终止循环：不再重新入队
    EXPECT_FALSE(f.ctx->hasTasks());
    EXPECT_TRUE(f.sink.lines.empty());  // Fatal 不打印
}

TEST(TaskTest, NonFunctionEntityIsReportedAsUncaught) {
    Fixture f;
    Value* notFn = f.ctx->createNumber(1);
    f.ctx->postTask(notFn);

    Value* r = f.ctx->runOneTask();
    EXPECT_EQ(r->type()->kind(), TypeKind::Undefined);
    ASSERT_EQ(f.sink.lines.size(), 1u);
    // 引擎报错现在携带规范 Error 对象，按 toString 渲染为 "Name: message"（ADR-039）
    EXPECT_EQ(f.sink.lines[0], "Uncaught TypeError: Task entity is not a function");
}

TEST(TaskTest, DefaultRunnerWithNullEntityIsReported) {
    Fixture f;
    Value* r = neo::core::defaultTaskRunner(f.ctx.get(), nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::Undefined);
    ASSERT_EQ(f.sink.lines.size(), 1u);
}

TEST(TaskTest, DefaultRunnerReturnsNormalValue) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 7);
    Value* r = neo::core::defaultTaskRunner(f.ctx.get(), fn);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(r), 7.0);
}

// ===== 任务实体作为 GC 根 =====

TEST(TaskTest, QueuedEntityIsRootedAndReleasedAfterRun) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    int before = fn->scopeRef();

    f.ctx->postTask(fn);
    EXPECT_EQ(fn->scopeRef(), before + 1);

    f.ctx->runOneTask();
    EXPECT_EQ(fn->scopeRef(), before);
}

TEST(TaskTest, CancelReleasesEntityRoot) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    int before = fn->scopeRef();

    uint64_t id = f.ctx->postTask(fn);
    EXPECT_EQ(fn->scopeRef(), before + 1);
    f.ctx->cancelTask(id);
    EXPECT_EQ(fn->scopeRef(), before);
}

TEST(TaskTest, SameEntityInMultipleTasksIsRootedOnce) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 1);
    int before = fn->scopeRef();

    uint64_t a = f.ctx->postTask(fn);
    uint64_t b = f.ctx->postTask(fn);
    EXPECT_EQ(fn->scopeRef(), before + 1);  // 只加一次根

    f.ctx->cancelTask(a);
    EXPECT_EQ(fn->scopeRef(), before + 1);  // 仍有 b 持有

    f.ctx->cancelTask(b);
    EXPECT_EQ(fn->scopeRef(), before);
}

TEST(TaskTest, EntitySurvivesGcWhileQueued) {
    Fixture f;
    Value* fn = countingFn(f.ctx.get(), 5);
    f.ctx->postTask(fn, 10);
    // 实体由任务队列作为根持有，GC 不应回收
    f.ctx->gc()->collect();
    f.clock.advance(10);
    Value* r = f.ctx->runOneTask();
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(r), 5.0);
}

// ===== 默认时钟 =====

TEST(TaskTest, DefaultClockIsMonotonic) {
    uint64_t a = neo::infra::defaultClock()->nowMs();
    uint64_t b = neo::infra::defaultClock()->nowMs();
    EXPECT_LE(a, b);
}

TEST(TaskTest, ContextWithoutClockUsesDefaultClock) {
    auto alloc = createMallocAllocator();
    CapturingSink sink;
    Console console{&sink};
    auto ctx = createContext(alloc.get(), &console);  // 不注入时钟

    Value* fn = countingFn(ctx.get(), 1);
    ctx->postTask(fn, 0);
    EXPECT_TRUE(ctx->hasRunnableTask());
    Value* r = ctx->runOneTask();
    EXPECT_EQ(r->type()->kind(), TypeKind::Number);
}
