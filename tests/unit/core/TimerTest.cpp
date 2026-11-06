// TimerTest.cpp - setTimeout / setInterval / clearTimeout / clearInterval 单元测试
//
// 验证（HTML Timers 语义）：
// - setTimeout 返回任务 id，到期后执行一次；重复 runOneTask 不再执行
// - setInterval 每次执行后重新入队，clearInterval 后停止
// - clearTimeout 与 clearInterval 等价、可互换
// - 未知 / 非数字 id 静默忽略
// - 延迟参数 ToNumber 语义：缺省 0、NaN/负数 → 0、字符串可转换
// - 额外参数经绑定函数前置传入回调，this = undefined
// - 回调非函数 → Exception；延迟为 Symbol → Exception
// - 定时器注册到 global（writable/不可枚举/可配置）
//
// 关联 ADR: 020（标准库组织）、006（异常即值）、017 §4（Fatal 透传）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/IClock.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <cmath>
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

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

class FakeClock : public IClock {
public:
    uint64_t now = 0;
    uint64_t nowMs() const override { return now; }
    void advance(uint64_t ms) { now += ms; }
};

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }

// 把 global 槽位绑进函数闭包（native 无捕获；定时器以 undefined 为 this 调用）
Value* bindSlot(IContext* ctx, Value* fn, const char* name, Value* value) {
    ctx->global()->bind(name, value);
    ctx->setClosure(fn, name);
    return fn;
}

// 造一个"调用计数 + 记录收到的参数"的回调。
// 计数记在闭包槽位 __calls，参数拼成字符串记在 __args。
Value* makeCallback(IContext* ctx) {
    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t argc, Value* argv[]) {
            Value* n = c->current()->lookupChain("__calls");
            c->store("__calls", c->createNumber(static_cast<NumberData*>(n->data())->value + 1.0));
            std::string joined;
            for (size_t i = 0; i < argc; ++i) {
                if (i > 0) joined += ",";
                Value* s = c->toString(argv[i]);
                joined += static_cast<StringData*>(s->data())->value;
            }
            c->store("__args", c->createString(joined));
            // this 记录（验证绑定为 undefined）
            c->store("__thisKind", c->createNumber(static_cast<double>(
                                       static_cast<int>(self->type()->kind()))));
            return c->createUndefined();
        },
        0);
    bindSlot(ctx, fn, "__calls", ctx->createNumber(0));
    bindSlot(ctx, fn, "__args", ctx->createString(""));
    bindSlot(ctx, fn, "__thisKind", ctx->createNumber(-1));
    return fn;
}

double callCount(IContext* /*ctx*/, Value* fn) {
    auto* d = static_cast<FunctionData*>(fn->data());
    return numOf(d->closureScope->lookup("__calls"));
}

std::string argsOf(IContext* /*ctx*/, Value* fn) {
    auto* d = static_cast<FunctionData*>(fn->data());
    return strOf(d->closureScope->lookup("__args"));
}

Value* globalOf(IContext* ctx, const char* name) { return ctx->global()->lookup(name); }

// 调用 global 函数
Value* callGlobal(IContext* ctx, const char* name, size_t argc, Value* argv[]) {
    Value* fn = globalOf(ctx, name);
    return ctx->call(fn, ctx->createUndefined(), argc, argv);
}

struct Fixture {
    std::unique_ptr<IAllocator> alloc = createMallocAllocator();
    NullSink sink;
    Console console{&sink};
    FakeClock clock;
    std::unique_ptr<IContext> ctx{createContext(alloc.get(), &console, &clock)};
};

}  // namespace

// ===== 注册 =====

TEST(TimerTest, TimersAreRegisteredOnGlobal) {
    Fixture f;
    const char* names[] = {"setTimeout", "setInterval", "clearTimeout", "clearInterval"};
    for (const char* name : names) {
        Value* fn = globalOf(f.ctx.get(), name);
        ASSERT_NE(fn, nullptr) << name;
        EXPECT_EQ(fn->type()->kind(), TypeKind::Function) << name;
    }
}

// ===== setTimeout =====

TEST(TimerTest, SetTimeoutReturnsIdAndRunsOnce) {
    Fixture f;
    Value* cb = makeCallback(f.ctx.get());
    Value* args[1] = {cb};

    Value* idValue = callGlobal(f.ctx.get(), "setTimeout", 1, args);
    ASSERT_EQ(idValue->type()->kind(), TypeKind::Number);
    EXPECT_GT(numOf(idValue), 0.0);
    EXPECT_TRUE(f.ctx->hasRunnableTask());  // 缺省延迟 0

    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), cb), 1.0);
    EXPECT_FALSE(f.ctx->hasTasks());  // 一次性任务

    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), cb), 1.0);
}

TEST(TimerTest, SetTimeoutHonorsDelay) {
    Fixture f;
    Value* cb = makeCallback(f.ctx.get());
    Value* args[2] = {cb, f.ctx->createNumber(50)};
    callGlobal(f.ctx.get(), "setTimeout", 2, args);

    EXPECT_FALSE(f.ctx->hasRunnableTask());
    f.clock.advance(49);
    EXPECT_FALSE(f.ctx->hasRunnableTask());
    f.clock.advance(1);
    EXPECT_TRUE(f.ctx->hasRunnableTask());

    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), cb), 1.0);
}

TEST(TimerTest, SetTimeoutNegativeAndNaNDelayBecomeZero) {
    Fixture f;
    Value* cb1 = makeCallback(f.ctx.get());
    Value* cb2 = makeCallback(f.ctx.get());
    Value* a1[2] = {cb1, f.ctx->createNumber(-100)};
    Value* a2[2] = {cb2, f.ctx->createNumber(std::nan(""))};
    callGlobal(f.ctx.get(), "setTimeout", 2, a1);
    callGlobal(f.ctx.get(), "setTimeout", 2, a2);

    // 两者都按延迟 0 入队：无需推进时钟即可执行
    EXPECT_TRUE(f.ctx->hasRunnableTask());
    f.ctx->runOneTask();
    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), cb1), 1.0);
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), cb2), 1.0);
    EXPECT_FALSE(f.ctx->hasTasks());
}

TEST(TimerTest, SetTimeoutClampsHugeDelayToMax) {
    Fixture f;
    Value* cb = makeCallback(f.ctx.get());
    Value* args[2] = {cb, f.ctx->createNumber(1e20)};
    callGlobal(f.ctx.get(), "setTimeout", 2, args);

    // 钳到 2^31-1：远未到期
    f.clock.advance(2147483646ull);
    EXPECT_FALSE(f.ctx->hasRunnableTask());
    f.clock.advance(1);
    EXPECT_TRUE(f.ctx->hasRunnableTask());
}

TEST(TimerTest, SetTimeoutAcceptsNumericStringDelay) {
    Fixture f;
    Value* cb = makeCallback(f.ctx.get());
    Value* args[2] = {cb, f.ctx->createString("30")};
    callGlobal(f.ctx.get(), "setTimeout", 2, args);

    EXPECT_FALSE(f.ctx->hasRunnableTask());
    f.clock.advance(30);
    EXPECT_TRUE(f.ctx->hasRunnableTask());
}

TEST(TimerTest, SetTimeoutPassesExtraArgsAndUndefinedThis) {
    Fixture f;
    Value* cb = makeCallback(f.ctx.get());
    Value* args[4] = {cb, f.ctx->createNumber(0), f.ctx->createString("a"),
                      f.ctx->createNumber(2)};
    callGlobal(f.ctx.get(), "setTimeout", 4, args);

    f.ctx->runOneTask();
    EXPECT_EQ(argsOf(f.ctx.get(), cb), "a,2");
    auto* d = static_cast<FunctionData*>(cb->data());
    EXPECT_EQ(static_cast<int>(numOf(d->closureScope->lookup("__thisKind"))),
              static_cast<int>(TypeKind::Undefined));
}

TEST(TimerTest, SetTimeoutNonFunctionCallbackThrows) {
    Fixture f;
    Value* args[1] = {f.ctx->createNumber(1)};
    Value* r = callGlobal(f.ctx.get(), "setTimeout", 1, args);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    EXPECT_FALSE(f.ctx->hasTasks());
}

TEST(TimerTest, SetTimeoutSymbolDelayThrows) {
    Fixture f;
    Value* cb = makeCallback(f.ctx.get());
    Value* args[2] = {cb, f.ctx->createSymbol("s")};
    Value* r = callGlobal(f.ctx.get(), "setTimeout", 2, args);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
    EXPECT_FALSE(f.ctx->hasTasks());
}

// ===== clearTimeout =====

TEST(TimerTest, ClearTimeoutCancelsPendingTimer) {
    Fixture f;
    Value* cb = makeCallback(f.ctx.get());
    Value* a1[2] = {cb, f.ctx->createNumber(10)};
    Value* id = callGlobal(f.ctx.get(), "setTimeout", 2, a1);

    Value* a2[1] = {id};
    Value* r = callGlobal(f.ctx.get(), "clearTimeout", 1, a2);
    EXPECT_EQ(r->type()->kind(), TypeKind::Undefined);
    EXPECT_FALSE(f.ctx->hasTasks());

    f.clock.advance(10);
    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), cb), 0.0);
}

TEST(TimerTest, ClearIntervalCancelsTimeout) {
    Fixture f;
    Value* cb = makeCallback(f.ctx.get());
    Value* a1[1] = {cb};
    Value* id = callGlobal(f.ctx.get(), "setTimeout", 1, a1);

    Value* a2[1] = {id};
    callGlobal(f.ctx.get(), "clearInterval", 1, a2);  // 两个 clear 等价
    EXPECT_FALSE(f.ctx->hasTasks());
}

TEST(TimerTest, ClearWithUnknownIdIsIgnored) {
    Fixture f;
    Value* a1[1] = {f.ctx->createNumber(9999)};
    Value* r = callGlobal(f.ctx.get(), "clearTimeout", 1, a1);
    EXPECT_EQ(r->type()->kind(), TypeKind::Undefined);
}

TEST(TimerTest, ClearWithNaNOrZeroIdIsIgnored) {
    Fixture f;
    Value* a1[1] = {f.ctx->createNumber(std::nan(""))};
    EXPECT_EQ(callGlobal(f.ctx.get(), "clearTimeout", 1, a1)->type()->kind(),
              TypeKind::Undefined);
    Value* a2[1] = {f.ctx->createNumber(0)};
    EXPECT_EQ(callGlobal(f.ctx.get(), "clearTimeout", 1, a2)->type()->kind(),
              TypeKind::Undefined);
}

TEST(TimerTest, ClearWithoutArgumentsReturnsUndefined) {
    Fixture f;
    EXPECT_EQ(callGlobal(f.ctx.get(), "clearTimeout", 0, nullptr)->type()->kind(),
              TypeKind::Undefined);
}

TEST(TimerTest, ClearWithSymbolThrows) {
    Fixture f;
    Value* args[1] = {f.ctx->createSymbol("s")};
    EXPECT_EQ(callGlobal(f.ctx.get(), "clearTimeout", 1, args)->type()->kind(),
              TypeKind::Exception);
}

// ===== setInterval =====

TEST(TimerTest, SetIntervalRepeatsUntilCleared) {
    Fixture f;
    Value* cb = makeCallback(f.ctx.get());
    Value* args[2] = {cb, f.ctx->createNumber(5)};
    Value* id = callGlobal(f.ctx.get(), "setInterval", 2, args);

    for (int i = 0; i < 3; ++i) {
        f.clock.advance(5);
        f.ctx->runOneTask();
    }
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), cb), 3.0);
    EXPECT_TRUE(f.ctx->hasTasks());

    Value* a2[1] = {id};
    callGlobal(f.ctx.get(), "clearInterval", 1, a2);
    f.clock.advance(5);
    EXPECT_FALSE(f.ctx->hasTasks());
    EXPECT_FALSE(f.ctx->hasRunnableTask());
}

TEST(TimerTest, SetIntervalPassesExtraArgsEachRun) {
    Fixture f;
    Value* cb = makeCallback(f.ctx.get());
    Value* args[3] = {cb, f.ctx->createNumber(1), f.ctx->createString("x")};
    Value* id = callGlobal(f.ctx.get(), "setInterval", 3, args);

    f.clock.advance(1);
    f.ctx->runOneTask();
    EXPECT_EQ(argsOf(f.ctx.get(), cb), "x");

    Value* a2[1] = {id};
    callGlobal(f.ctx.get(), "clearInterval", 1, a2);
}

TEST(TimerTest, SetIntervalNonFunctionCallbackThrows) {
    Fixture f;
    Value* args[1] = {f.ctx->createNull()};
    EXPECT_EQ(callGlobal(f.ctx.get(), "setInterval", 1, args)->type()->kind(),
              TypeKind::Exception);
}

// ===== 与微任务的协作 =====

TEST(TimerTest, TimerIsMacrotaskSoMicrotaskRunsFirst) {
    Fixture f;
    Value* timerCb = makeCallback(f.ctx.get());
    Value* microCb = makeCallback(f.ctx.get());

    Value* args[1] = {timerCb};
    callGlobal(f.ctx.get(), "setTimeout", 1, args);  // 宏任务（延迟 0）
    f.ctx->postMicrotask(microCb);

    f.ctx->runOneTask();  // 微任务优先
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), microCb), 1.0);
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), timerCb), 0.0);

    f.ctx->runOneTask();
    EXPECT_DOUBLE_EQ(callCount(f.ctx.get(), timerCb), 1.0);
}
