// PromiseRejectionTest.cpp - Promise M4：未处理拒绝上报
//
// 验证（ES2024 §27.2.1.9，ADR-043）：
// - 被 reject 且无人接管 → 微任务排空后打印 "Unhandled promise rejection: ..."
// - 有 then/catch 接管 → 不打印（且已登记后仍能被移出）
// - 上报后清空，不重复打印
// - Context 析构兜底上报（宿主未排空事件循环）
// - 上报不泄漏（promise 作为动态根被归还）

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
#include <vector>

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

// 记录所有输出的 sink
class RecordingSink : public IOutputSink {
public:
    void write(std::string_view text) override { lines.emplace_back(text); }
    void writeLine(std::string_view text) override { lines.emplace_back(text); }
    void flush() override {}
    std::vector<std::string> lines;
};

struct Fixture {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    RecordingSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx = createContext(alloc.get(), &console);
};

std::string str(Value* v) {
    return static_cast<StringData*>(v->data())->value;
}

// 跑完当前这一轮：排空任务队列，然后做一次微任务检查点（宿主职责）。
// runOneTask 在微任务排空时会自动上报；同步 reject（无任务入队）需要宿主
// 在轮次末尾显式调用 reportUnhandledRejections，这里统一模拟。
void drain(IContext* ctx) {
    while (ctx->hasRunnableTask()) ctx->runOneTask();
    ctx->reportUnhandledRejections();
}

Value* promiseCtor(IContext* ctx) { return ctx->global()->lookup("Promise"); }

// executor：把 resolve/reject 存到指定 global 槽位
Value* captureExecutor(IContext* ctx, const char* resolveName, const char* rejectName) {
    Value* target = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t argc, Value* argv[]) {
            Value* resolveName = argc > 0 ? argv[0] : c->createUndefined();
            Value* rejectName = argc > 1 ? argv[1] : c->createUndefined();
            Value* resolve = argc > 2 ? argv[2] : c->createUndefined();
            Value* reject = argc > 3 ? argv[3] : c->createUndefined();
            c->global()->bind(str(resolveName), resolve);
            c->global()->bind(str(rejectName), reject);
            return c->createUndefined();
        }, 0);
    Value* names[2] = {ctx->createString(resolveName), ctx->createString(rejectName)};
    return ctx->createBoundFunction(target, ctx->createUndefined(), 2, names);
}

Value* newPromise(IContext* ctx, const char* resolveName = "__resolve",
                  const char* rejectName = "__reject") {
    Value* executor = captureExecutor(ctx, resolveName, rejectName);
    Value* args[1] = {executor};
    return ctx->construct(promiseCtor(ctx), 1, args);
}

void settle(IContext* ctx, const char* name, Value* v) {
    Value* fn = ctx->global()->lookup(name);
    Value* args[1] = {v};
    ctx->call(fn, ctx->createUndefined(), 1, args);
}

// 统计包含给定子串的输出行数
size_t countLines(const RecordingSink& sink, std::string_view needle) {
    size_t n = 0;
    for (const auto& line : sink.lines) {
        if (line.find(needle) != std::string::npos) ++n;
    }
    return n;
}

}  // namespace

TEST(PromiseRejectionTest, UnhandledRejectionIsReported) {
    Fixture f;
    newPromise(f.ctx.get());
    settle(f.ctx.get(), "__reject", f.ctx->createString("boom"));
    drain(f.ctx.get());
    EXPECT_EQ(countLines(f.sink, "Unhandled promise rejection: boom"), 1u);
}

TEST(PromiseRejectionTest, HandledRejectionIsNotReported) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    // 先注册 catch，再 reject
    Value* onRejected = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 1);
    Value* args[1] = {onRejected};
    f.ctx->call(f.ctx->getProperty(p, "catch"), p, 1, args);
    settle(f.ctx.get(), "__reject", f.ctx->createString("handled"));
    drain(f.ctx.get());
    EXPECT_EQ(countLines(f.sink, "Unhandled promise rejection"), 0u);
}

TEST(PromiseRejectionTest, RejectionHandledAfterSettleIsNotReported) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    // reject 先发生（此时未处理，登记），随后 then 接管（应移出）
    settle(f.ctx.get(), "__reject", f.ctx->createString("late"));
    Value* onRejected = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 1);
    Value* args[1] = {onRejected};
    f.ctx->call(f.ctx->getProperty(p, "catch"), p, 1, args);
    drain(f.ctx.get());
    EXPECT_EQ(countLines(f.sink, "Unhandled promise rejection"), 0u);
}

TEST(PromiseRejectionTest, ReportedOnlyOnce) {
    Fixture f;
    newPromise(f.ctx.get());
    settle(f.ctx.get(), "__reject", f.ctx->createString("once"));
    drain(f.ctx.get());
    // 再排空一次（无任务）不应重复上报
    drain(f.ctx.get());
    f.ctx->reportUnhandledRejections();
    EXPECT_EQ(countLines(f.sink, "Unhandled promise rejection: once"), 1u);
}

TEST(PromiseRejectionTest, ErrorReasonIsRenderedWithName) {
    Fixture f;
    newPromise(f.ctx.get());
    Value* err = f.ctx->createErrorException(neo::core::ErrorKind::TypeError, "bad type");
    Value* reason = static_cast<neo::core::ExceptionData*>(err->data())->child;
    settle(f.ctx.get(), "__reject", reason);
    drain(f.ctx.get());
    EXPECT_EQ(countLines(f.sink, "Unhandled promise rejection: TypeError: bad type"), 1u);
}

TEST(PromiseRejectionTest, DestructorReportsPendingRejection) {
    RecordingSink sink;
    {
        auto alloc = createMallocAllocator();
        Console console{&sink};
        auto ctx = createContext(alloc.get(), &console);
        newPromise(ctx.get());
        settle(ctx.get(), "__reject", ctx->createString("never drained"));
        // 不排空事件循环，直接析构
    }
    EXPECT_EQ(countLines(sink, "Unhandled promise rejection: never drained"), 1u);
}

TEST(PromiseRejectionTest, FulfilledPromiseIsNotReported) {
    Fixture f;
    newPromise(f.ctx.get());
    settle(f.ctx.get(), "__resolve", f.ctx->createNumber(1));
    drain(f.ctx.get());
    EXPECT_EQ(countLines(f.sink, "Unhandled promise rejection"), 0u);
}

TEST(PromiseRejectionTest, PendingPromiseIsNotReported) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    drain(f.ctx.get());
    EXPECT_EQ(countLines(f.sink, "Unhandled promise rejection"), 0u);
    EXPECT_EQ(neo::core::asPromise(p)->state, PromiseState::Pending);
}

TEST(PromiseRejectionTest, ChainedRejectionReportsDerivedOnly) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    // p.then(onFulfilled) 无 onRejected → p 已处理，派生 promise 承接拒绝
    Value* onFulfilled = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 1);
    Value* args[2] = {onFulfilled, f.ctx->createUndefined()};
    f.ctx->call(f.ctx->getProperty(p, "then"), p, 2, args);
    settle(f.ctx.get(), "__reject", f.ctx->createString("propagated"));
    drain(f.ctx.get());
    // 派生 promise 无人接管 → 上报一次
    EXPECT_EQ(countLines(f.sink, "Unhandled promise rejection: propagated"), 1u);
}
