// IteratorProtocolTest.cpp - 迭代协议原语与 ToLength 单元测试
//
// 验证 TypeBehavior.h 中的通用原语（ES2024 §7.4 / §7.1.20）：
// - getIterator：取 @@iterator → 可调用校验 → 调用 → 结果须为对象
// - iteratorNext：取 next → 可调用校验 → 调用 → 结果须为对象
// - iteratorComplete / iteratorValue：读 done / value（含 getter 抛错）
// - iteratorClose：无 return 忽略、return 非函数报错、return 抛错透传、
//   completion 优先于 return 的错误
// - toLengthValue / lengthOf：NaN/负数 → 0，超上限截断，异常透传
//
// 这些原语是 for-of / spread / 解构 / Array.from / Map/Set 的共同底座，
// 本阶段虽只有数组迭代器，仍按通用协议验证。
//
// 关联 ADR: 016（Type 体系）、006（异常即值）、017（Fatal 透传）

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

namespace {

using neo::core::IContext;
using neo::core::NumberData;
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

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }

// 造 [1, 2, 3]
Value* makeArray(IContext* ctx, size_t n) {
    Value* arr = ctx->createArray();
    auto* d = static_cast<neo::core::ArrayData*>(arr->data());
    for (size_t i = 0; i < n; ++i) {
        d->setElement(static_cast<uint32_t>(i),
                      ctx->createNumber(static_cast<double>(i + 1)));
    }
    return arr;
}

// 造一个带 next 的对象（next 由调用方提供）
Value* makeIteratorLike(IContext* ctx, Value* next) {
    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "next", next, true, true, true);
    return obj;
}

// native 函数无捕获，统一抛固定消息的 Exception（各用例只关心是否透传）
Value* throwingFn(IContext* ctx) {
    return ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        },
        0);
}

}  // namespace

// ===== getIterator =====

TEST(IteratorProtocolTest, GetIteratorFromArray) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 2);
    Value* iter = neo::core::detail::getIterator(ctx.get(), arr);
    ASSERT_EQ(iter->type()->kind(), TypeKind::ArrayIterator);
}

TEST(IteratorProtocolTest, GetIteratorNonCallableThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj,
                        ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator),
                        ctx->createNumber(1), true, true, true);
    Value* r = neo::core::detail::getIterator(ctx.get(), obj);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(IteratorProtocolTest, GetIteratorMethodReturnsNonObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0);
    ctx->defineProperty(obj,
                        ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator),
                        fn, true, true, true);
    Value* r = neo::core::detail::getIterator(ctx.get(), obj);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(IteratorProtocolTest, GetIteratorMethodThrowsPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj,
                        ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator),
                        throwingFn(ctx.get()), true, true, true);
    Value* r = neo::core::detail::getIterator(ctx.get(), obj);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
}

// ===== iteratorNext =====

TEST(IteratorProtocolTest, IteratorNextReturnsResultObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 2);
    Value* iter = neo::core::detail::getIterator(ctx.get(), arr);
    Value* r = neo::core::detail::iteratorNext(ctx.get(), iter);
    ASSERT_EQ(r->type()->kind(), TypeKind::Object);
    EXPECT_DOUBLE_EQ(numOf(neo::core::detail::iteratorValue(ctx.get(), r)), 1.0);
}

TEST(IteratorProtocolTest, IteratorNextNotCallableThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = makeIteratorLike(ctx.get(), ctx->createNumber(1));
    Value* r = neo::core::detail::iteratorNext(ctx.get(), obj);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(IteratorProtocolTest, IteratorNextResultNonObjectThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0);
    Value* obj = makeIteratorLike(ctx.get(), fn);
    Value* r = neo::core::detail::iteratorNext(ctx.get(), obj);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(IteratorProtocolTest, IteratorNextThrowsPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = makeIteratorLike(ctx.get(), throwingFn(ctx.get()));
    Value* r = neo::core::detail::iteratorNext(ctx.get(), obj);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
}

// ===== iteratorComplete / iteratorValue =====

TEST(IteratorProtocolTest, IteratorCompleteReadsDone) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* done = ctx->createObject();
    ctx->defineProperty(done, "done", ctx->createBoolean(true), true, true, true);
    Value* err = nullptr;
    EXPECT_TRUE(neo::core::detail::iteratorComplete(ctx.get(), done, err));
    EXPECT_EQ(err, nullptr);

    Value* notDone = ctx->createObject();
    ctx->defineProperty(notDone, "done", ctx->createBoolean(false), true, true, true);
    EXPECT_FALSE(neo::core::detail::iteratorComplete(ctx.get(), notDone, err));
    EXPECT_EQ(err, nullptr);
}

TEST(IteratorProtocolTest, IteratorCompleteGetterErrorPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "done", throwingFn(ctx.get()), nullptr);
    Value* err = nullptr;
    EXPECT_TRUE(neo::core::detail::iteratorComplete(ctx.get(), obj, err));
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(err->type()->kind(), TypeKind::Exception);
}

TEST(IteratorProtocolTest, IteratorValueReadsValue) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* r = ctx->createObject();
    ctx->defineProperty(r, "value", ctx->createString("v"), true, true, true);
    Value* v = neo::core::detail::iteratorValue(ctx.get(), r);
    ASSERT_EQ(v->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<neo::core::StringData*>(v->data())->value, "v");
}

// ===== iteratorClose =====

TEST(IteratorProtocolTest, IteratorCloseWithoutReturnIsNoop) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    EXPECT_EQ(neo::core::detail::iteratorClose(ctx.get(), obj, nullptr), nullptr);
}

TEST(IteratorProtocolTest, IteratorCloseNullReturnIsNoop) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "return", ctx->createNull(), true, true, true);
    EXPECT_EQ(neo::core::detail::iteratorClose(ctx.get(), obj, nullptr), nullptr);
}

TEST(IteratorProtocolTest, IteratorCloseNonCallableReturnThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "return", ctx->createNumber(1), true, true, true);
    Value* r = neo::core::detail::iteratorClose(ctx.get(), obj, nullptr);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(IteratorProtocolTest, IteratorCloseReturnThrowsPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "return", throwingFn(ctx.get()), true, true, true);
    Value* r = neo::core::detail::iteratorClose(ctx.get(), obj, nullptr);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(IteratorProtocolTest, IteratorCloseReturnNonObjectThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0);
    ctx->defineProperty(obj, "return", fn, true, true, true);
    Value* r = neo::core::detail::iteratorClose(ctx.get(), obj, nullptr);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(IteratorProtocolTest, IteratorCloseSuccessReturnsCompletion) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createObject(); }, 0);
    ctx->defineProperty(obj, "return", fn, true, true, true);
    EXPECT_EQ(neo::core::detail::iteratorClose(ctx.get(), obj, nullptr), nullptr);
}

TEST(IteratorProtocolTest, IteratorCloseCompletionWinsOverReturnError) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "return", throwingFn(ctx.get()), true, true, true);
    Value* completion = ctx->createException(ctx->createString("original"));
    Value* r = neo::core::detail::iteratorClose(ctx.get(), obj, completion);
    EXPECT_EQ(r, completion);
}

TEST(IteratorProtocolTest, IteratorCloseCompletionWinsOverNonCallableReturn) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "return", ctx->createNumber(1), true, true, true);
    Value* completion = ctx->createException(ctx->createString("original"));
    EXPECT_EQ(neo::core::detail::iteratorClose(ctx.get(), obj, completion), completion);
}

TEST(IteratorProtocolTest, IteratorCloseCompletionWinsOverNonObjectReturn) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0);
    ctx->defineProperty(obj, "return", fn, true, true, true);
    Value* completion = ctx->createException(ctx->createString("original"));
    EXPECT_EQ(neo::core::detail::iteratorClose(ctx.get(), obj, completion), completion);
}

// ===== toLengthValue / lengthOf =====

TEST(IteratorProtocolTest, ToLengthNormalizesValues) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    uint32_t out = 0;
    EXPECT_EQ(neo::core::detail::toLengthValue(ctx.get(), ctx->createNumber(5.7), out),
              nullptr);
    EXPECT_EQ(out, 5u);

    // 字符串走 ToNumber
    EXPECT_EQ(neo::core::detail::toLengthValue(ctx.get(), ctx->createString("3"), out),
              nullptr);
    EXPECT_EQ(out, 3u);

    // NaN → 0
    EXPECT_EQ(neo::core::detail::toLengthValue(
                  ctx.get(), ctx->createNumber(std::nan("")), out), nullptr);
    EXPECT_EQ(out, 0u);

    // 负数 → 0
    EXPECT_EQ(neo::core::detail::toLengthValue(ctx.get(), ctx->createNumber(-7.0), out),
              nullptr);
    EXPECT_EQ(out, 0u);

    // 超上限 → 2^32-1
    EXPECT_EQ(neo::core::detail::toLengthValue(ctx.get(), ctx->createNumber(1e20), out),
              nullptr);
    EXPECT_EQ(out, 4294967295u);
}

TEST(IteratorProtocolTest, ToLengthSymbolThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    uint32_t out = 0;
    Value* r = neo::core::detail::toLengthValue(ctx.get(), ctx->createSymbol("s"), out);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(IteratorProtocolTest, ToLengthObjectUsesToPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* vo = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(4); }, 0);
    ctx->defineProperty(obj, "valueOf", vo, true, true, true);

    uint32_t out = 0;
    EXPECT_EQ(neo::core::detail::toLengthValue(ctx.get(), obj, out), nullptr);
    EXPECT_EQ(out, 4u);
}

TEST(IteratorProtocolTest, LengthOfReadsProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "length", ctx->createNumber(2), true, true, true);
    uint32_t out = 0;
    EXPECT_EQ(neo::core::detail::lengthOf(ctx.get(), obj, out), nullptr);
    EXPECT_EQ(out, 2u);
}

TEST(IteratorProtocolTest, LengthOfGetterErrorPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "length", throwingFn(ctx.get()), nullptr);
    uint32_t out = 0;
    Value* r = neo::core::detail::lengthOf(ctx.get(), obj, out);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}
