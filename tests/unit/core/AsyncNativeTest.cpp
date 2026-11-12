// AsyncNativeTest.cpp - native async 函数（协程阶段机 + await 驱动）
//
// 验证：
// - async native 调用返回 Promise，不可构造、无 prototype 属性
// - 同步完成 / await 非 Promise / 连续 await
// - await 拒绝以 Exception 注入 sentValue；函数体可透传或捕获
// - 同步抛错（直接返回 Exception）→ rejected Promise
// - 业务临时变量经 Scope-Value-Slot 跨 stage 持久

#include "core/AsyncNative.h"
#include "core/IContext.h"
#include "core/PromiseCapability.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/InterruptKind.h"
#include "core/types/Coroutine.h"
#include "core/types/Exception.h"
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

using neo::core::CoroutineData;
using neo::core::IContext;
using neo::core::InterruptKind;
using neo::core::NumberData;
using neo::core::PromiseCapability;
using neo::core::PromiseData;
using neo::core::PromiseState;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::asCoroutine;
using neo::core::asPromise;
using neo::core::createContext;
using neo::core::createPromiseCapability;
using neo::core::detail::isErrorValue;
using neo::core::loadNativeLocal;
using neo::core::storeNativeLocal;
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

    Value* makeAsync(neo::core::NativeFn body, const char* name = "f") {
        neo::core::FunctionCreateOptions opts;
        opts.asyncNative = true;
        Value* fn = ctx->createNativeFunction(body, 0, {}, opts);
        ctx->setFunctionName(fn, name);
        return fn;
    }

    Value* call(Value* fn, Value* self = nullptr) {
        return ctx->call(fn, self != nullptr ? self : ctx->createUndefined(), 0, nullptr);
    }

    void drain() {
        while (ctx->hasRunnableTask()) ctx->runOneTask();
    }
};

double num(Value* v) { return static_cast<NumberData*>(v->data())->value; }

Value* rejectedPromise(IContext* ctx, Value* reason) {
    PromiseCapability cap;
    if (Value* err = createPromiseCapability(ctx, ctx->promiseCtor(), cap)) return err;
    Value* args[1] = {reason};
    (void)ctx->call(cap.reject, ctx->createUndefined(), 1, args);
    return cap.promise;
}

// 同步完成。
Value* immediate(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createNumber(42);
}

// await 非 Promise：stage 0 存业务变量 __x，stage 1 读回并加 sentValue。
Value* awaitValueBody(IContext* ctx, Value*, size_t, Value*[]) {
    CoroutineData* co = asCoroutine(ctx->getCurrentCoroutine());
    switch (co->resumePoint) {
        case 0:
            storeNativeLocal(ctx, "__x", ctx->createNumber(1));
            return ctx->createInterrupt(InterruptKind::Await, 1, ctx->createNumber(5));
        case 1: {
            if (isErrorValue(co->sentValue)) return co->sentValue;
            Value* x = loadNativeLocal(ctx, "__x");
            return ctx->createNumber(num(x) + num(co->sentValue));
        }
        default:
            return ctx->createUndefined();
    }
}

// 连续 await，业务变量跨多个 stage。
Value* sequentialBody(IContext* ctx, Value*, size_t, Value*[]) {
    CoroutineData* co = asCoroutine(ctx->getCurrentCoroutine());
    switch (co->resumePoint) {
        case 0:
            storeNativeLocal(ctx, "__a", ctx->createNumber(1));
            return ctx->createInterrupt(InterruptKind::Await, 1, ctx->createNumber(10));
        case 1: {
            if (isErrorValue(co->sentValue)) return co->sentValue;
            Value* a = loadNativeLocal(ctx, "__a");
            storeNativeLocal(ctx, "__sum",
                             ctx->createNumber(num(a) + num(co->sentValue)));
            return ctx->createInterrupt(InterruptKind::Await, 2, ctx->createNumber(20));
        }
        case 2: {
            if (isErrorValue(co->sentValue)) return co->sentValue;
            Value* sum = loadNativeLocal(ctx, "__sum");
            return ctx->createNumber(num(sum) + num(co->sentValue));
        }
        default:
            return ctx->createUndefined();
    }
}

// await 拒绝：不捕获，原样返回 Exception。
Value* awaitRejectedBody(IContext* ctx, Value*, size_t, Value*[]) {
    CoroutineData* co = asCoroutine(ctx->getCurrentCoroutine());
    if (co->resumePoint == 0) {
        Value* p = rejectedPromise(ctx, ctx->createString("boom"));
        return ctx->createInterrupt(InterruptKind::Await, 1, p);
    }
    return co->sentValue;
}

// await 拒绝：捕获并返回兜底值。
Value* awaitCatchBody(IContext* ctx, Value*, size_t, Value*[]) {
    CoroutineData* co = asCoroutine(ctx->getCurrentCoroutine());
    if (co->resumePoint == 0) {
        Value* p = rejectedPromise(ctx, ctx->createString("boom"));
        return ctx->createInterrupt(InterruptKind::Await, 1, p);
    }
    if (isErrorValue(co->sentValue)) return ctx->createNumber(99);
    return co->sentValue;
}

// 同步抛错：直接返回 Exception。
Value* throwImmediate(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createException(ctx->createString("bad"));
}

}  // namespace

TEST(AsyncNativeTest, ReturnsPromiseAndResolvesSynchronously) {
    Fixture f;
    Value* fn = f.makeAsync(immediate);
    Value* p = f.call(fn);
    ASSERT_EQ(p->type()->kind(), TypeKind::Promise);

    f.drain();
    PromiseData* d = asPromise(p);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 42.0);
}

TEST(AsyncNativeTest, AwaitNonPromiseAndKeepsBusinessLocal) {
    Fixture f;
    Value* fn = f.makeAsync(awaitValueBody);
    Value* p = f.call(fn);
    ASSERT_EQ(p->type()->kind(), TypeKind::Promise);

    f.drain();
    PromiseData* d = asPromise(p);
    ASSERT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 6.0);  // __x(1) + awaited(5)
}

TEST(AsyncNativeTest, SequentialAwaitsKeepState) {
    Fixture f;
    Value* fn = f.makeAsync(sequentialBody);
    Value* p = f.call(fn);

    f.drain();
    PromiseData* d = asPromise(p);
    ASSERT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 31.0);  // 1 + 10 + 20
}

TEST(AsyncNativeTest, AwaitRejectionPropagatesAsException) {
    Fixture f;
    Value* fn = f.makeAsync(awaitRejectedBody);
    Value* p = f.call(fn);

    f.drain();
    PromiseData* d = asPromise(p);
    ASSERT_EQ(d->state, PromiseState::Rejected);
    ASSERT_EQ(d->result->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<neo::core::StringData*>(d->result->data())->value, "boom");
}

TEST(AsyncNativeTest, AwaitRejectionCanBeCaughtByBody) {
    Fixture f;
    Value* fn = f.makeAsync(awaitCatchBody);
    Value* p = f.call(fn);

    f.drain();
    PromiseData* d = asPromise(p);
    ASSERT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 99.0);
}

TEST(AsyncNativeTest, SynchronousThrowRejectsPromise) {
    Fixture f;
    Value* fn = f.makeAsync(throwImmediate);
    Value* p = f.call(fn);

    f.drain();
    PromiseData* d = asPromise(p);
    ASSERT_EQ(d->state, PromiseState::Rejected);
    ASSERT_EQ(d->result->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<neo::core::StringData*>(d->result->data())->value, "bad");
}

TEST(AsyncNativeTest, NotConstructibleAndNoPrototypeProperty) {
    Fixture f;
    Value* fn = f.makeAsync(immediate);

    Value* ctor = f.ctx->construct(fn, 0, nullptr);
    EXPECT_EQ(ctor->type()->kind(), TypeKind::Exception);

    Value* hasProto = f.ctx->hasProperty(fn, f.ctx->createString("prototype"));
    EXPECT_FALSE(neo::core::detail::toBoolean(hasProto));
}
