// PromiseChainTest.cpp - Promise M2：resolving functions、thenable 兼容、then/catch/finally
//
// 验证（ES2024 §27.2，ADR-043）：
// - resolve/reject 的状态迁移与 alreadyResolved 幂等
// - then 的 handler 在微任务中执行；返回新 promise
// - handler 缺失时按方向透传；handler 抛错 → 派生 promise 拒绝
// - catch / finally 语义
// - **thenable 兼容**：判断依据是「可取到可调用的 then」，不是 instanceof Promise
//   （普通对象、then 不可调用、then 抛错、自引用、只生效一次）

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
#include "core/types/Promise.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using neo::core::IContext;
using neo::core::PromiseData;
using neo::core::PromiseState;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
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
};

std::string str(Value* v) {
    return static_cast<StringData*>(v->data())->value;
}

double num(Value* v) {
    return static_cast<neo::core::NumberData*>(v->data())->value;
}

void drain(IContext* ctx) {
    while (ctx->hasRunnableTask()) ctx->runOneTask();
}

// executor：把 resolve/reject 存到 global 槽位，便于测试手动触发
Value* captureExecutor(IContext* ctx) {
    return ctx->createNativeFunction(
        [](IContext* c, Value*, size_t argc, Value* argv[]) {
            c->global()->bind("__resolve", argc > 0 ? argv[0] : c->createUndefined());
            c->global()->bind("__reject", argc > 1 ? argv[1] : c->createUndefined());
            return c->createUndefined();
        }, 2);
}

Value* newPromise(IContext* ctx) {
    Value* ctor = ctx->global()->lookup("Promise");
    Value* args[] = {captureExecutor(ctx)};
    return ctx->construct(ctor, 1, args);
}

void resolveWith(IContext* ctx, Value* v) {
    Value* args[] = {v};
    ctx->call(ctx->global()->lookup("__resolve"), ctx->createUndefined(), 1, args);
}

void rejectWith(IContext* ctx, Value* v) {
    Value* args[] = {v};
    ctx->call(ctx->global()->lookup("__reject"), ctx->createUndefined(), 1, args);
}

Value* thenOf(IContext* ctx, Value* p, Value* onFulfilled, Value* onRejected) {
    Value* args[2] = {onFulfilled, onRejected};
    return ctx->call(ctx->getProperty(p, "then"), p, 2, args);
}

// 记录到全局数组的 handler。
// native 的 self 是 thisArg（Promise handler 以 undefined 调用），故 tag/ret 用
// bound args 携带：调用时前置 argv[0]=tag、argv[1]=ret。
Value* tagHandler(IContext* ctx, const char* tag, Value* ret) {
    Value* target = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t argc, Value* argv[]) {
            Value* arr = c->global()->lookup("__order");
            auto* ad = static_cast<neo::core::ArrayData*>(arr->data());
            ad->setElement(ad->length(), argc > 0 ? argv[0] : c->createUndefined());
            return argc > 1 ? argv[1] : c->createUndefined();
        }, 0);
    Value* bound[2] = {ctx->createString(tag),
                       ret != nullptr ? ret : ctx->createUndefined()};
    return ctx->createBoundFunction(target, ctx->createUndefined(), 2, bound);
}

}  // namespace

// ===== 状态迁移 =====

TEST(PromiseChainTest, ResolveSettlesFulfilled) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), f.ctx->createNumber(42));
    PromiseData* d = neo::core::asPromise(p);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 42.0);
}

TEST(PromiseChainTest, RejectSettlesRejected) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    Value* reason = f.ctx->createString("bad");
    rejectWith(f.ctx.get(), reason);
    PromiseData* d = neo::core::asPromise(p);
    EXPECT_EQ(d->state, PromiseState::Rejected);
    EXPECT_EQ(d->result, reason);
}

TEST(PromiseChainTest, AlreadyResolvedIgnoresLaterCalls) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), f.ctx->createNumber(1));
    resolveWith(f.ctx.get(), f.ctx->createNumber(2));
    rejectWith(f.ctx.get(), f.ctx->createString("no"));
    PromiseData* d = neo::core::asPromise(p);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 1.0);
}

// ===== then =====

TEST(PromiseChainTest, ThenHandlerRunsInMicrotask) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), f.ctx->createNumber(7));

    f.ctx->global()->bind("__seen", f.ctx->createUndefined());
    Value* seen = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t argc, Value* argv[]) {
            c->global()->bind("__seen", argc > 0 ? argv[0] : c->createUndefined());
            return c->createUndefined();
        }, 1);
    Value* derived = thenOf(f.ctx.get(), p, seen, f.ctx->createUndefined());
    ASSERT_EQ(derived->type()->kind(), TypeKind::Promise);

    // handler 尚未执行
    EXPECT_EQ(f.ctx->global()->lookup("__seen")->type()->kind(), TypeKind::Undefined);
    drain(f.ctx.get());
    Value* got = f.ctx->global()->lookup("__seen");
    ASSERT_EQ(got->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(num(got), 7.0);
}

TEST(PromiseChainTest, ThenFulfillsDerivedWithHandlerResult) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), f.ctx->createNumber(1));
    Value* handler = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(99); }, 1);
    Value* derived = thenOf(f.ctx.get(), p, handler, f.ctx->createUndefined());
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(derived);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 99.0);
}

TEST(PromiseChainTest, HandlerThrowRejectsDerived) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), f.ctx->createNumber(1));
    Value* handler = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 1);
    Value* derived = thenOf(f.ctx.get(), p, handler, f.ctx->createUndefined());
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(derived);
    EXPECT_EQ(d->state, PromiseState::Rejected);
    EXPECT_EQ(str(d->result), "boom");
}

TEST(PromiseChainTest, MissingHandlerPassesThrough) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), f.ctx->createNumber(5));
    Value* derived = f.ctx->call(f.ctx->getProperty(p, "then"), p, 0, nullptr);
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(derived);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 5.0);
}

TEST(PromiseChainTest, MissingRejectHandlerPassesReasonThrough) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    rejectWith(f.ctx.get(), f.ctx->createString("why"));
    Value* derived = f.ctx->call(f.ctx->getProperty(p, "then"), p, 0, nullptr);
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(derived);
    EXPECT_EQ(d->state, PromiseState::Rejected);
    EXPECT_EQ(str(d->result), "why");
}

TEST(PromiseChainTest, ThenOnPendingWaitsForSettle) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    Value* handler = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(3); }, 1);
    Value* derived = thenOf(f.ctx.get(), p, handler, f.ctx->createUndefined());
    drain(f.ctx.get());
    EXPECT_EQ(neo::core::asPromise(derived)->state, PromiseState::Pending);

    resolveWith(f.ctx.get(), f.ctx->createNumber(1));
    drain(f.ctx.get());
    EXPECT_EQ(neo::core::asPromise(derived)->state, PromiseState::Fulfilled);
}

TEST(PromiseChainTest, ChainedThenRunsInOrder) {
    Fixture f;
    Value* order = f.ctx->createArray();
    f.ctx->global()->bind("__order", order);

    Value* p = newPromise(f.ctx.get());
    Value* h1 = tagHandler(f.ctx.get(), "h1", f.ctx->createNumber(1));
    Value* h2 = tagHandler(f.ctx.get(), "h2", f.ctx->createNumber(2));
    Value* d1 = thenOf(f.ctx.get(), p, h1, f.ctx->createUndefined());
    thenOf(f.ctx.get(), d1, h2, f.ctx->createUndefined());

    resolveWith(f.ctx.get(), f.ctx->createNumber(0));
    drain(f.ctx.get());

    auto* od = static_cast<neo::core::ArrayData*>(order->data());
    ASSERT_EQ(od->length(), 2u);
    EXPECT_EQ(str(od->elementAt(0)), "h1");
    EXPECT_EQ(str(od->elementAt(1)), "h2");
}

TEST(PromiseChainTest, CatchHandlesRejection) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    rejectWith(f.ctx.get(), f.ctx->createString("oops"));
    Value* onRejected = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString("recovered"); },
        1);
    Value* args[] = {onRejected};
    Value* derived = f.ctx->call(f.ctx->getProperty(p, "catch"), p, 1, args);
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(derived);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_EQ(str(d->result), "recovered");
}

TEST(PromiseChainTest, ThenOnNonPromiseIsTypeError) {
    Fixture f;
    Value* r = f.ctx->call(f.ctx->getProperty(f.ctx->promisePrototype(), "then"),
                          f.ctx->createObject(), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    Value* err = static_cast<neo::core::ExceptionData*>(r->data())->child;
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "TypeError");
}

// ===== thenable 兼容 =====

TEST(PromiseChainTest, ResolveAdoptsThenable) {
    Fixture f;
    Value* thenable = f.ctx->createObject();
    Value* then = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value* argv[]) {
            Value* args[] = {c->createString("adopted")};
            return c->call(argv[0], c->createUndefined(), 1, args);
        }, 2);
    f.ctx->defineProperty(thenable, "then", then);

    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), thenable);
    // then 在微任务中调用，尚未 settle
    EXPECT_EQ(neo::core::asPromise(p)->state, PromiseState::Pending);
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(p);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_EQ(str(d->result), "adopted");
}

TEST(PromiseChainTest, ResolveNonCallableThenFulfillsDirectly) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    f.ctx->defineProperty(obj, "then", f.ctx->createNumber(1));  // 不可调用
    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), obj);
    PromiseData* d = neo::core::asPromise(p);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_EQ(d->result, obj);
}

TEST(PromiseChainTest, ThenableThenThrowsRejects) {
    Fixture f;
    Value* thenable = f.ctx->createObject();
    Value* then = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("then failed"));
        }, 2);
    f.ctx->defineProperty(thenable, "then", then);
    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), thenable);
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(p);
    EXPECT_EQ(d->state, PromiseState::Rejected);
    EXPECT_EQ(str(d->result), "then failed");
}

TEST(PromiseChainTest, ResolveWithSelfRejectsTypeError) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), p);
    PromiseData* d = neo::core::asPromise(p);
    EXPECT_EQ(d->state, PromiseState::Rejected);
    ASSERT_EQ(d->result->type()->kind(), TypeKind::Error);
    EXPECT_EQ(str(f.ctx->getProperty(d->result, "name")), "TypeError");
}

TEST(PromiseChainTest, ThenableOnlyResolvesOnce) {
    Fixture f;
    Value* thenable = f.ctx->createObject();
    Value* then = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value* argv[]) {
            Value* a[] = {c->createString("first")};
            c->call(argv[0], c->createUndefined(), 1, a);
            Value* b[] = {c->createString("second")};
            c->call(argv[0], c->createUndefined(), 1, b);
            Value* e[] = {c->createString("err")};
            c->call(argv[1], c->createUndefined(), 1, e);
            return c->createUndefined();
        }, 2);
    f.ctx->defineProperty(thenable, "then", then);
    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), thenable);
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(p);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_EQ(str(d->result), "first");
}

TEST(PromiseChainTest, PromiseResolvedWithAnotherPromiseAdoptsIt) {
    Fixture f;
    // 两个 promise 的 resolve 需分别保存（newPromise 会覆盖 __resolve 槽位）
    Value* inner = newPromise(f.ctx.get());
    Value* innerResolve = f.ctx->global()->lookup("__resolve");
    Value* outer = newPromise(f.ctx.get());
    Value* outerResolve = f.ctx->global()->lookup("__resolve");

    Value* innerArg[] = {inner};
    f.ctx->call(outerResolve, f.ctx->createUndefined(), 1, innerArg);
    drain(f.ctx.get());
    EXPECT_EQ(neo::core::asPromise(outer)->state, PromiseState::Pending);

    // 现在让 inner 完成
    Value* args[] = {f.ctx->createNumber(11)};
    f.ctx->call(innerResolve, f.ctx->createUndefined(), 1, args);
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(outer);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 11.0);
}

// ===== finally =====

TEST(PromiseChainTest, FinallyRunsAndPassesThroughValue) {
    Fixture f;
    f.ctx->global()->bind("__ran", f.ctx->createBoolean(false));
    Value* onFinally = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            c->global()->bind("__ran", c->createBoolean(true));
            return c->createUndefined();
        }, 0);

    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), f.ctx->createNumber(8));
    Value* args[] = {onFinally};
    Value* derived = f.ctx->call(f.ctx->getProperty(p, "finally"), p, 1, args);
    drain(f.ctx.get());

    EXPECT_TRUE(neo::core::detail::toBoolean(f.ctx->global()->lookup("__ran")));
    PromiseData* d = neo::core::asPromise(derived);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 8.0);
}

TEST(PromiseChainTest, FinallyRethrowsRejection) {
    Fixture f;
    Value* onFinally = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 0);
    Value* p = newPromise(f.ctx.get());
    rejectWith(f.ctx.get(), f.ctx->createString("orig"));
    Value* args[] = {onFinally};
    Value* derived = f.ctx->call(f.ctx->getProperty(p, "finally"), p, 1, args);
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(derived);
    EXPECT_EQ(d->state, PromiseState::Rejected);
    EXPECT_EQ(str(d->result), "orig");
}

TEST(PromiseChainTest, FinallyNonCallablePassesThrough) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), f.ctx->createNumber(4));
    Value* args[] = {f.ctx->createNumber(1)};
    Value* derived = f.ctx->call(f.ctx->getProperty(p, "finally"), p, 1, args);
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(derived);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 4.0);
}

TEST(PromiseChainTest, FinallyReturningPromiseIsAwaited) {
    Fixture f;
    Value* inner = newPromise(f.ctx.get());
    Value* innerResolve = f.ctx->global()->lookup("__resolve");  // 后续 newPromise 会覆盖槽位
    Value* target = f.ctx->createNativeFunction(
        [](IContext* /*ctx*/, Value*, size_t argc, Value* argv[]) {
            return argc > 0 ? argv[0] : nullptr;
        }, 0);
    Value* bound[1] = {inner};
    Value* onFinally =
        f.ctx->createBoundFunction(target, f.ctx->createUndefined(), 1, bound);

    Value* p = newPromise(f.ctx.get());
    resolveWith(f.ctx.get(), f.ctx->createNumber(1));
    Value* args[] = {onFinally};
    Value* derived = f.ctx->call(f.ctx->getProperty(p, "finally"), p, 1, args);
    drain(f.ctx.get());
    // inner 尚未完成 → derived 仍 Pending
    EXPECT_EQ(neo::core::asPromise(derived)->state, PromiseState::Pending);

    Value* a[] = {f.ctx->createUndefined()};
    f.ctx->call(innerResolve, f.ctx->createUndefined(), 1, a);
    drain(f.ctx.get());
    EXPECT_EQ(neo::core::asPromise(derived)->state, PromiseState::Fulfilled);
}
