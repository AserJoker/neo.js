// AsyncGeneratorTest.cpp - 异步生成器单元测试（ES2024 §27.6 / ADR-045）
//
// 验证：
// - async function* 调用返回 AsyncGenerator（SuspendedStart，不执行函数体）
// - next() 返回 Promise，微任务排空后 resolve { value, done }
// - 并发 next 串行化（请求队列）
// - yield 的值被 await（Promise.resolve）
// - yield 的 Promise 拒绝时把异常抛回生成器
// - return/throw 注入；未启动时直接完成/拒绝
// - 完成后 next/return/throw 语义
// - 原型链：实例 → fn.prototype → %AsyncGeneratorPrototype% → %AsyncIteratorPrototype% → Object.prototype
// - @@asyncIterator 返回 this、@@toStringTag = "AsyncGenerator"、不可构造
// - GC：不可达且无挂起请求的生成器可回收

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/InterruptKind.h"
#include "core/Value.h"
#include "core/types/AsyncGenerator.h"
#include "core/types/Coroutine.h"
#include "core/types/Interrupt.h"
#include "core/types/Number.h"
#include "core/types/Promise.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using neo::core::AsyncGeneratorData;
using neo::core::AsyncGeneratorState;
using neo::core::CoroutineData;
using neo::core::IContext;
using neo::core::IGCEngine;
using neo::core::InterruptData;
using neo::core::InterruptKind;
using neo::core::NumberData;
using neo::core::PromiseData;
using neo::core::PromiseState;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::asAsyncGenerator;
using neo::core::asCoroutine;
using neo::core::asInterrupt;
using neo::core::asPromise;
using neo::core::createContext;
using neo::core::detail::isInterruptValue;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

struct Fixture {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    NullSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx = createContext(alloc.get(), &console);

    Value* asyncGenFn(neo::core::NativeFn body, const char* name) {
        Value* fn = ctx->createNativeFunction(body, 0);
        ctx->setFunctionName(fn, name);
        ctx->setFunctionAsyncGenerator(fn, true);
        return fn;
    }

    Value* make(neo::core::NativeFn body, const char* name = "g") {
        return ctx->call(asyncGenFn(body, name), ctx->createUndefined(), 0, nullptr);
    }

    Value* prop(Value* obj, const char* key) {
        return ctx->getProperty(obj, ctx->createString(key));
    }

    Value* call(Value* fn, Value* self, size_t argc = 0, Value* argv[] = nullptr) {
        return ctx->call(fn, self, argc, argv);
    }

    void drain() {
        while (ctx->hasRunnableTask()) ctx->runOneTask();
    }
};

double num(Value* v) { return static_cast<NumberData*>(v->data())->value; }
bool flag(Value* v) { return neo::core::detail::toBoolean(v); }

bool handleInjected(CoroutineData* co, Value** out) {
    Value* sent = co->sentValue;
    // await 拒绝以 Exception 注入；未捕获则沿 native 错误通道原样返回。
    if (neo::core::detail::isErrorValue(sent)) {
        *out = sent;
        return true;
    }
    if (!isInterruptValue(sent)) return false;
    InterruptData* ir = asInterrupt(sent);
    if (ir->kind == InterruptKind::Return || ir->kind == InterruptKind::Throw) {
        *out = ir->value;
        return true;
    }
    return false;
}

// 三阶段：yield 1、yield 2、返回 3
Value* body3(IContext* ctx, Value*, size_t, Value*[]) {
    CoroutineData* co = asCoroutine(ctx->getCurrentCoroutine());
    Value* injected = nullptr;
    if (handleInjected(co, &injected)) return injected;
    switch (co->resumePoint) {
        case 0: return ctx->createInterrupt(InterruptKind::Yield, 1, ctx->createNumber(1));
        case 1: return ctx->createInterrupt(InterruptKind::Yield, 2, ctx->createNumber(2));
        case 2: return ctx->createNumber(3);
        default: return ctx->createUndefined();
    }
}

// 只 yield 一次传入的 value，然后返回
Value* bodyYieldValue(IContext* ctx, Value*, size_t, Value*[]) {
    CoroutineData* co = asCoroutine(ctx->getCurrentCoroutine());
    Value* injected = nullptr;
    if (handleInjected(co, &injected)) return injected;
    if (co->resumePoint == 0) {
        return ctx->createInterrupt(InterruptKind::Yield, 1, co->initialArgs[0]);
    }
    return ctx->createNumber(0);
}

// 捕获 yield 值的拒绝后继续 yield 99
Value* bodyCatchYield(IContext* ctx, Value*, size_t, Value*[]) {
    CoroutineData* co = asCoroutine(ctx->getCurrentCoroutine());
    Value* sent = co->sentValue;
    // await 拒绝（Exception）可被函数体捕获。
    if (neo::core::detail::isErrorValue(sent)) {
        return ctx->createInterrupt(InterruptKind::Yield, 2, ctx->createNumber(99));
    }
    if (isInterruptValue(sent)) {
        InterruptData* ir = asInterrupt(sent);
        if (ir->kind == InterruptKind::Throw) {
            return ctx->createInterrupt(InterruptKind::Yield, 2, ctx->createNumber(99));
        }
        if (ir->kind == InterruptKind::Return) return ir->value;
    }
    if (co->resumePoint == 0) {
        return ctx->createInterrupt(InterruptKind::Yield, 1, co->initialArgs[0]);
    }
    return ctx->createNumber(0);
}

}  // namespace

// ===== 调用语义 =====

TEST(AsyncGeneratorTest, CallReturnsSuspendedStartWithoutRunningBody) {
    Fixture f;
    Value* gen = f.make(body3);
    EXPECT_EQ(gen->type()->kind(), TypeKind::AsyncGenerator);
    AsyncGeneratorData* ag = asAsyncGenerator(gen);
    ASSERT_NE(ag, nullptr);
    EXPECT_EQ(ag->asyncState, AsyncGeneratorState::SuspendedStart);
    EXPECT_EQ(ag->execScope, nullptr);
}

TEST(AsyncGeneratorTest, NotConstructible) {
    Fixture f;
    Value* fn = f.asyncGenFn(body3, "g");
    Value* r = f.ctx->construct(fn, 0, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

// ===== next =====

TEST(AsyncGeneratorTest, NextReturnsPromiseAndYields) {
    Fixture f;
    Value* gen = f.make(body3);
    Value* next = f.prop(gen, "next");

    Value* p1 = f.call(next, gen);
    ASSERT_EQ(p1->type()->kind(), TypeKind::Promise);
    // 未排空微任务前仍 pending
    EXPECT_EQ(asPromise(p1)->state, PromiseState::Pending);
    f.drain();
    ASSERT_EQ(asPromise(p1)->state, PromiseState::Fulfilled);
    Value* r1 = asPromise(p1)->result;
    EXPECT_DOUBLE_EQ(num(f.prop(r1, "value")), 1.0);
    EXPECT_FALSE(flag(f.prop(r1, "done")));

    Value* p2 = f.call(next, gen);
    f.drain();
    Value* r2 = asPromise(p2)->result;
    EXPECT_DOUBLE_EQ(num(f.prop(r2, "value")), 2.0);
    EXPECT_FALSE(flag(f.prop(r2, "done")));

    Value* p3 = f.call(next, gen);
    f.drain();
    Value* r3 = asPromise(p3)->result;
    EXPECT_DOUBLE_EQ(num(f.prop(r3, "value")), 3.0);
    EXPECT_TRUE(flag(f.prop(r3, "done")));
    EXPECT_EQ(asAsyncGenerator(gen)->asyncState, AsyncGeneratorState::Completed);

    Value* p4 = f.call(next, gen);
    f.drain();
    Value* r4 = asPromise(p4)->result;
    EXPECT_EQ(f.prop(r4, "value")->type()->kind(), TypeKind::Undefined);
    EXPECT_TRUE(flag(f.prop(r4, "done")));
}

TEST(AsyncGeneratorTest, ConcurrentNextRequestsAreSerialized) {
    Fixture f;
    Value* gen = f.make(body3);
    Value* next = f.prop(gen, "next");

    Value* p1 = f.call(next, gen);
    Value* p2 = f.call(next, gen);  // 第一个请求 await 期间入队
    ASSERT_EQ(asAsyncGenerator(gen)->queue.size(), 1u);

    f.drain();
    ASSERT_EQ(asPromise(p1)->state, PromiseState::Fulfilled);
    ASSERT_EQ(asPromise(p2)->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(f.prop(asPromise(p1)->result, "value")), 1.0);
    EXPECT_DOUBLE_EQ(num(f.prop(asPromise(p2)->result, "value")), 2.0);
}

TEST(AsyncGeneratorTest, YieldedPromiseIsAwaited) {
    Fixture f;
    // 用 initialArgs 注入 Promise.resolve(5)
    Value* fn = f.asyncGenFn(bodyYieldValue, "g");
    Value* resolveFn = f.prop(f.ctx->promiseCtor(), "resolve");
    Value* five = f.ctx->createNumber(5);
    Value* promise = f.call(resolveFn, f.ctx->promiseCtor(), 1, &five);
    Value* gen2 = f.call(fn, f.ctx->createUndefined(), 1, &promise);

    Value* next = f.prop(gen2, "next");
    Value* p = f.call(next, gen2);
    f.drain();
    ASSERT_EQ(asPromise(p)->state, PromiseState::Fulfilled);
    Value* r = asPromise(p)->result;
    EXPECT_DOUBLE_EQ(num(f.prop(r, "value")), 5.0);
    EXPECT_FALSE(flag(f.prop(r, "done")));
}

TEST(AsyncGeneratorTest, RejectedYieldValueThrowsIntoGenerator) {
    Fixture f;
    Value* fn = f.asyncGenFn(bodyYieldValue, "g");
    Value* rejectFn = f.prop(f.ctx->promiseCtor(), "reject");
    Value* reason = f.ctx->createString("boom");
    Value* promise = f.call(rejectFn, f.ctx->promiseCtor(), 1, &reason);
    Value* gen = f.call(fn, f.ctx->createUndefined(), 1, &promise);

    Value* next = f.prop(gen, "next");
    Value* p = f.call(next, gen);
    f.drain();
    // body 未捕获：请求被拒绝，生成器完成
    ASSERT_EQ(asPromise(p)->state, PromiseState::Rejected);
    EXPECT_EQ(asPromise(p)->result, reason);
    EXPECT_EQ(asAsyncGenerator(gen)->asyncState, AsyncGeneratorState::Completed);
}

TEST(AsyncGeneratorTest, BodyCanCatchRejectedYieldValue) {
    Fixture f;
    Value* fn = f.asyncGenFn(bodyCatchYield, "g");
    Value* rejectFn = f.prop(f.ctx->promiseCtor(), "reject");
    Value* reason = f.ctx->createString("boom");
    Value* promise = f.call(rejectFn, f.ctx->promiseCtor(), 1, &reason);
    Value* gen = f.call(fn, f.ctx->createUndefined(), 1, &promise);

    Value* next = f.prop(gen, "next");
    Value* p = f.call(next, gen);
    f.drain();
    ASSERT_EQ(asPromise(p)->state, PromiseState::Fulfilled);
    Value* r = asPromise(p)->result;
    EXPECT_DOUBLE_EQ(num(f.prop(r, "value")), 99.0);
    EXPECT_FALSE(flag(f.prop(r, "done")));
    EXPECT_EQ(asAsyncGenerator(gen)->asyncState, AsyncGeneratorState::SuspendedYield);
}

// ===== return / throw =====

TEST(AsyncGeneratorTest, ReturnBeforeStartCompletesWithoutRunning) {
    Fixture f;
    Value* gen = f.make(body3);
    Value* ret = f.prop(gen, "return");
    Value* v = f.ctx->createNumber(42);
    Value* p = f.call(ret, gen, 1, &v);
    f.drain();
    ASSERT_EQ(asPromise(p)->state, PromiseState::Fulfilled);
    Value* r = asPromise(p)->result;
    EXPECT_DOUBLE_EQ(num(f.prop(r, "value")), 42.0);
    EXPECT_TRUE(flag(f.prop(r, "done")));
    EXPECT_EQ(asAsyncGenerator(gen)->execScope, nullptr);
}

TEST(AsyncGeneratorTest, ReturnInjectsIntoBody) {
    Fixture f;
    Value* gen = f.make(body3);
    Value* next = f.prop(gen, "next");
    Value* p1 = f.call(next, gen);
    f.drain();
    (void)p1;

    Value* ret = f.prop(gen, "return");
    Value* v = f.ctx->createNumber(7);
    Value* p = f.call(ret, gen, 1, &v);
    f.drain();
    ASSERT_EQ(asPromise(p)->state, PromiseState::Fulfilled);
    Value* r = asPromise(p)->result;
    EXPECT_DOUBLE_EQ(num(f.prop(r, "value")), 7.0);
    EXPECT_TRUE(flag(f.prop(r, "done")));
}

TEST(AsyncGeneratorTest, ThrowBeforeStartRejects) {
    Fixture f;
    Value* gen = f.make(body3);
    Value* thr = f.prop(gen, "throw");
    Value* e = f.ctx->createString("boom");
    Value* p = f.call(thr, gen, 1, &e);
    f.drain();
    ASSERT_EQ(asPromise(p)->state, PromiseState::Rejected);
    EXPECT_EQ(asPromise(p)->result, e);
    EXPECT_EQ(asAsyncGenerator(gen)->asyncState, AsyncGeneratorState::Completed);
}

TEST(AsyncGeneratorTest, ThrowInjectsIntoBody) {
    Fixture f;
    Value* gen = f.make(body3);
    Value* next = f.prop(gen, "next");
    (void)f.call(next, gen);
    f.drain();

    Value* thr = f.prop(gen, "throw");
    Value* e = f.ctx->createString("boom");
    Value* p = f.call(thr, gen, 1, &e);
    f.drain();
    ASSERT_EQ(asPromise(p)->state, PromiseState::Rejected);
    EXPECT_EQ(asPromise(p)->result, e);
}

// ===== 原型与协议 =====

TEST(AsyncGeneratorTest, PrototypeChain) {
    Fixture f;
    Value* fn = f.asyncGenFn(body3, "g");
    Value* gen = f.call(fn, f.ctx->createUndefined());

    Value* asyncGenProto = f.ctx->asyncGeneratorPrototype();
    Value* asyncIterProto = f.ctx->asyncIteratorPrototype();
    ASSERT_NE(asyncGenProto, nullptr);
    ASSERT_NE(asyncIterProto, nullptr);

    Value* fnProto = f.ctx->getPrototype(gen);
    EXPECT_EQ(fnProto, f.prop(fn, "prototype"));
    EXPECT_EQ(f.ctx->getPrototype(fnProto), asyncGenProto);
    EXPECT_EQ(f.ctx->getPrototype(asyncGenProto), asyncIterProto);
    EXPECT_EQ(f.ctx->getPrototype(asyncIterProto), f.ctx->objectPrototype());
}

TEST(AsyncGeneratorTest, AsyncIteratorSymbolReturnsThis) {
    Fixture f;
    Value* gen = f.make(body3);
    Value* sym = f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::AsyncIterator);
    Value* method = f.ctx->getProperty(gen, sym);
    ASSERT_EQ(method->type()->kind(), TypeKind::Function);
    EXPECT_EQ(f.call(method, gen), gen);
}

TEST(AsyncGeneratorTest, ToStringTagIsAsyncGenerator) {
    Fixture f;
    Value* gen = f.make(body3);
    Value* sym = f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToStringTag);
    Value* tag = f.ctx->getProperty(gen, sym);
    ASSERT_EQ(tag->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<neo::core::StringData*>(tag->data())->value, "AsyncGenerator");
}

// ===== GC =====

TEST(AsyncGeneratorTest, UnreachableUnstartedGeneratorIsCollected) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();
    size_t baseline = gc->valueCount();

    {
        f.ctx->pushNewScope(f.ctx->current());
        Value* fn = f.asyncGenFn(body3, "g");
        (void)f.ctx->call(fn, f.ctx->createUndefined(), 0, nullptr);
        f.ctx->popScope();
    }

    gc->collect();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), baseline);
}
