// PromiseStaticTest.cpp - Promise M3：静态方法 + @@species
//
// 验证（ES2024 §27.2.4，ADR-043）：
// - Promise.resolve / Promise.reject
// - Promise.all / allSettled / any / race（含空 iterable、非 iterable、元素 thenable）
// - 结果顺序按输入顺序（不是 settle 顺序）
// - @@species：then 派生 promise 使用 SpeciesConstructor；getter 返回 this
// - 静态方法以非构造函数为 this → TypeError

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/WellKnownSymbol.h"
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

Value* promiseCtor(IContext* ctx) { return ctx->global()->lookup("Promise"); }

Value* callStatic(IContext* ctx, const char* name, Value* arg) {
    Value* fn = ctx->getProperty(promiseCtor(ctx), name);
    Value* args[1] = {arg};
    return ctx->call(fn, promiseCtor(ctx), arg != nullptr ? 1 : 0, args);
}

// executor：把 resolve/reject 存到指定 global 槽位。
// 槽位名用 bound args 前置携带（调用时 argv = [resolveName, rejectName, resolve, reject]）。
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

// 数组元素
Value* arrayOf(IContext* ctx, const std::vector<Value*>& items) {
    Value* arr = ctx->createArray();
    auto* d = static_cast<neo::core::ArrayData*>(arr->data());
    for (size_t i = 0; i < items.size(); ++i) {
        d->setElement(static_cast<uint32_t>(i), items[i]);
    }
    return arr;
}

Value* elementAt(Value* arr, uint32_t i) {
    return static_cast<neo::core::ArrayData*>(arr->data())->elementAt(i);
}

uint32_t arrayLength(Value* arr) {
    return static_cast<neo::core::ArrayData*>(arr->data())->length();
}

}  // namespace

// ===== Promise.resolve / Promise.reject =====

TEST(PromiseStaticTest, ResolveReturnsSamePromiseWhenConstructorMatches) {
    Fixture f;
    Value* p = newPromise(f.ctx.get());
    Value* r = callStatic(f.ctx.get(), "resolve", p);
    EXPECT_EQ(r, p);
}

TEST(PromiseStaticTest, ResolveWrapsPlainValue) {
    Fixture f;
    Value* r = callStatic(f.ctx.get(), "resolve", f.ctx->createNumber(7));
    ASSERT_EQ(r->type()->kind(), TypeKind::Promise);
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(r);
    EXPECT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 7.0);
}

TEST(PromiseStaticTest, RejectCreatesRejectedPromise) {
    Fixture f;
    Value* reason = f.ctx->createString("nope");
    Value* r = callStatic(f.ctx.get(), "reject", reason);
    ASSERT_EQ(r->type()->kind(), TypeKind::Promise);
    PromiseData* d = neo::core::asPromise(r);
    EXPECT_EQ(d->state, PromiseState::Rejected);
    EXPECT_EQ(d->result, reason);
}

TEST(PromiseStaticTest, ResolveOnNonConstructorIsTypeError) {
    Fixture f;
    Value* fn = f.ctx->getProperty(promiseCtor(f.ctx.get()), "resolve");
    Value* args[1] = {f.ctx->createNumber(1)};
    Value* r = f.ctx->call(fn, f.ctx->createNumber(1), 1, args);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    Value* err = static_cast<neo::core::ExceptionData*>(r->data())->child;
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "TypeError");
}

// ===== Promise.all =====

TEST(PromiseStaticTest, AllResolvesInInputOrder) {
    Fixture f;
    Value* p1 = newPromise(f.ctx.get(), "__r1", "__j1");
    Value* p2 = newPromise(f.ctx.get(), "__r2", "__j2");
    Value* p3 = newPromise(f.ctx.get(), "__r3", "__j3");

    Value* result = callStatic(f.ctx.get(), "all", arrayOf(f.ctx.get(), {p1, p2, p3}));
    ASSERT_EQ(result->type()->kind(), TypeKind::Promise);

    // 乱序 settle：2 → 3 → 1
    settle(f.ctx.get(), "__r2", f.ctx->createNumber(20));
    settle(f.ctx.get(), "__r3", f.ctx->createNumber(30));
    settle(f.ctx.get(), "__r1", f.ctx->createNumber(10));
    drain(f.ctx.get());

    PromiseData* d = neo::core::asPromise(result);
    ASSERT_EQ(d->state, PromiseState::Fulfilled);
    ASSERT_EQ(d->result->type()->kind(), TypeKind::Array);
    ASSERT_EQ(arrayLength(d->result), 3u);
    EXPECT_DOUBLE_EQ(num(elementAt(d->result, 0)), 10.0);
    EXPECT_DOUBLE_EQ(num(elementAt(d->result, 1)), 20.0);
    EXPECT_DOUBLE_EQ(num(elementAt(d->result, 2)), 30.0);
}

TEST(PromiseStaticTest, AllRejectsOnFirstRejection) {
    Fixture f;
    Value* p1 = newPromise(f.ctx.get(), "__r1", "__j1");
    Value* p2 = newPromise(f.ctx.get(), "__r2", "__j2");
    Value* result = callStatic(f.ctx.get(), "all", arrayOf(f.ctx.get(), {p1, p2}));

    settle(f.ctx.get(), "__j2", f.ctx->createString("bad"));
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(result);
    EXPECT_EQ(d->state, PromiseState::Rejected);
    EXPECT_EQ(str(d->result), "bad");
}

TEST(PromiseStaticTest, AllWithEmptyIterableResolvesEmptyArray) {
    Fixture f;
    Value* result = callStatic(f.ctx.get(), "all", f.ctx->createArray());
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(result);
    ASSERT_EQ(d->state, PromiseState::Fulfilled);
    ASSERT_EQ(d->result->type()->kind(), TypeKind::Array);
    EXPECT_EQ(arrayLength(d->result), 0u);
}

TEST(PromiseStaticTest, AllWithNonIterableRejects) {
    Fixture f;
    Value* result = callStatic(f.ctx.get(), "all", f.ctx->createNumber(1));
    PromiseData* d = neo::core::asPromise(result);
    EXPECT_EQ(d->state, PromiseState::Rejected);
}

TEST(PromiseStaticTest, AllAcceptsThenables) {
    Fixture f;
    Value* thenable = f.ctx->createObject();
    Value* then = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value* argv[]) {
            Value* args[1] = {c->createNumber(42)};
            return c->call(argv[0], c->createUndefined(), 1, args);
        }, 2);
    f.ctx->defineProperty(thenable, "then", then);

    Value* result = callStatic(f.ctx.get(), "all",
                               arrayOf(f.ctx.get(), {f.ctx->createNumber(1), thenable}));
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(result);
    ASSERT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(elementAt(d->result, 1)), 42.0);
}

// ===== Promise.allSettled =====

TEST(PromiseStaticTest, AllSettledReportsBothOutcomes) {
    Fixture f;
    Value* p1 = newPromise(f.ctx.get(), "__r1", "__j1");
    Value* p2 = newPromise(f.ctx.get(), "__r2", "__j2");
    Value* result =
        callStatic(f.ctx.get(), "allSettled", arrayOf(f.ctx.get(), {p1, p2}));

    settle(f.ctx.get(), "__r1", f.ctx->createNumber(1));
    settle(f.ctx.get(), "__j2", f.ctx->createString("err"));
    drain(f.ctx.get());

    PromiseData* d = neo::core::asPromise(result);
    ASSERT_EQ(d->state, PromiseState::Fulfilled);
    ASSERT_EQ(arrayLength(d->result), 2u);
    Value* first = elementAt(d->result, 0);
    Value* second = elementAt(d->result, 1);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(first->type()->kind(), TypeKind::Object);
    ASSERT_EQ(second->type()->kind(), TypeKind::Object);
    Value* st = f.ctx->getProperty(first, "status");
    EXPECT_EQ(str(st), "fulfilled");
    EXPECT_DOUBLE_EQ(num(f.ctx->getProperty(first, "value")), 1.0);
    EXPECT_EQ(str(f.ctx->getProperty(second, "status")), "rejected");
    EXPECT_EQ(str(f.ctx->getProperty(second, "reason")), "err");
}

TEST(PromiseStaticTest, AllSettledEmptyResolvesEmpty) {
    Fixture f;
    Value* result = callStatic(f.ctx.get(), "allSettled", f.ctx->createArray());
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(result);
    ASSERT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_EQ(arrayLength(d->result), 0u);
}

// ===== Promise.any =====

TEST(PromiseStaticTest, AnyResolvesWithFirstFulfilled) {
    Fixture f;
    Value* p1 = newPromise(f.ctx.get(), "__r1", "__j1");
    Value* p2 = newPromise(f.ctx.get(), "__r2", "__j2");
    Value* result = callStatic(f.ctx.get(), "any", arrayOf(f.ctx.get(), {p1, p2}));

    settle(f.ctx.get(), "__j1", f.ctx->createString("no1"));
    settle(f.ctx.get(), "__r2", f.ctx->createNumber(9));
    drain(f.ctx.get());

    PromiseData* d = neo::core::asPromise(result);
    ASSERT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 9.0);
}

TEST(PromiseStaticTest, AnyAllRejectedGivesAggregateError) {
    Fixture f;
    Value* p1 = newPromise(f.ctx.get(), "__r1", "__j1");
    Value* p2 = newPromise(f.ctx.get(), "__r2", "__j2");
    Value* result = callStatic(f.ctx.get(), "any", arrayOf(f.ctx.get(), {p1, p2}));

    settle(f.ctx.get(), "__j1", f.ctx->createString("a"));
    settle(f.ctx.get(), "__j2", f.ctx->createString("b"));
    drain(f.ctx.get());

    PromiseData* d = neo::core::asPromise(result);
    ASSERT_EQ(d->state, PromiseState::Rejected);
    Value* err = d->result;
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "AggregateError");
    Value* errors = f.ctx->getProperty(err, "errors");
    ASSERT_EQ(errors->type()->kind(), TypeKind::Array);
    ASSERT_EQ(arrayLength(errors), 2u);
    EXPECT_EQ(str(elementAt(errors, 0)), "a");
    EXPECT_EQ(str(elementAt(errors, 1)), "b");
}

TEST(PromiseStaticTest, AnyEmptyIterableRejectsAggregateError) {
    Fixture f;
    Value* result = callStatic(f.ctx.get(), "any", f.ctx->createArray());
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(result);
    ASSERT_EQ(d->state, PromiseState::Rejected);
    EXPECT_EQ(str(f.ctx->getProperty(d->result, "name")), "AggregateError");
}

// ===== Promise.race =====

TEST(PromiseStaticTest, RaceResolvesWithFirstSettled) {
    Fixture f;
    Value* p1 = newPromise(f.ctx.get(), "__r1", "__j1");
    Value* p2 = newPromise(f.ctx.get(), "__r2", "__j2");
    Value* result = callStatic(f.ctx.get(), "race", arrayOf(f.ctx.get(), {p1, p2}));

    settle(f.ctx.get(), "__r2", f.ctx->createNumber(2));
    settle(f.ctx.get(), "__r1", f.ctx->createNumber(1));
    drain(f.ctx.get());

    PromiseData* d = neo::core::asPromise(result);
    ASSERT_EQ(d->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(d->result), 2.0);
}

TEST(PromiseStaticTest, RaceRejectsWhenFirstSettledIsRejection) {
    Fixture f;
    Value* p1 = newPromise(f.ctx.get(), "__r1", "__j1");
    Value* result = callStatic(f.ctx.get(), "race", arrayOf(f.ctx.get(), {p1}));
    settle(f.ctx.get(), "__j1", f.ctx->createString("fast"));
    drain(f.ctx.get());
    PromiseData* d = neo::core::asPromise(result);
    EXPECT_EQ(d->state, PromiseState::Rejected);
    EXPECT_EQ(str(d->result), "fast");
}

TEST(PromiseStaticTest, RaceEmptyIterableStaysPending) {
    Fixture f;
    Value* result = callStatic(f.ctx.get(), "race", f.ctx->createArray());
    drain(f.ctx.get());
    EXPECT_EQ(neo::core::asPromise(result)->state, PromiseState::Pending);
}

TEST(PromiseStaticTest, RaceWithNonIterableRejects) {
    Fixture f;
    Value* result = callStatic(f.ctx.get(), "race", f.ctx->createNumber(1));
    EXPECT_EQ(neo::core::asPromise(result)->state, PromiseState::Rejected);
}

// ===== @@species =====

TEST(PromiseStaticTest, SpeciesGetterReturnsThis) {
    Fixture f;
    Value* species = f.ctx->getProperty(
        promiseCtor(f.ctx.get()),
        f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Species));
    EXPECT_EQ(species, promiseCtor(f.ctx.get()));
}

TEST(PromiseStaticTest, ThenUsesSpeciesConstructor) {
    Fixture f;
    // 自定义构造函数：记录被调用次数，仍返回真正的 Promise
    f.ctx->global()->bind("__speciesCalls", f.ctx->createNumber(0));
    Value* custom = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t argc, Value* argv[]) {
            Value* n = c->global()->lookup("__speciesCalls");
            c->global()->bind("__speciesCalls",
                              c->createNumber(num(n) + 1.0));
            Value* args[1] = {argc > 0 ? argv[0] : c->createUndefined()};
            return c->construct(c->global()->lookup("Promise"), 1, args);
        }, 1);
    // SpeciesConstructor：C = p.constructor（对象）→ S = C[@@species]
    Value* species = f.ctx->createObject();
    f.ctx->defineProperty(species,
                          f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Species),
                          custom);

    Value* p = newPromise(f.ctx.get());
    f.ctx->setProperty(p, "constructor", species);
    settle(f.ctx.get(), "__resolve", f.ctx->createNumber(1));

    Value* derived = f.ctx->call(f.ctx->getProperty(p, "then"), p, 0, nullptr);
    drain(f.ctx.get());
    EXPECT_DOUBLE_EQ(num(f.ctx->global()->lookup("__speciesCalls")), 1.0);
    EXPECT_EQ(neo::core::asPromise(derived)->state, PromiseState::Fulfilled);
}

TEST(PromiseStaticTest, SpeciesNullFallsBackToDefaultPromise) {
    Fixture f;
    // C[@@species] = null → 回退默认 %Promise%
    Value* species = f.ctx->createObject();
    f.ctx->defineProperty(species,
                          f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Species),
                          f.ctx->createNull());
    Value* p = newPromise(f.ctx.get());
    f.ctx->setProperty(p, "constructor", species);
    settle(f.ctx.get(), "__resolve", f.ctx->createNumber(3));
    Value* derived = f.ctx->call(f.ctx->getProperty(p, "then"), p, 0, nullptr);
    drain(f.ctx.get());
    ASSERT_EQ(derived->type()->kind(), TypeKind::Promise);
    EXPECT_EQ(neo::core::asPromise(derived)->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(neo::core::asPromise(derived)->result), 3.0);
}

// ===== Promise.withResolvers（ES2024 §27.2.4.9）=====

TEST(PromiseStaticTest, WithResolversReturnsCapabilityObject) {
    Fixture f;
    Value* fn = f.ctx->getProperty(promiseCtor(f.ctx.get()), "withResolvers");
    Value* obj = f.ctx->call(fn, promiseCtor(f.ctx.get()), 0, nullptr);
    ASSERT_EQ(obj->type()->kind(), TypeKind::Object);

    Value* promise = f.ctx->getProperty(obj, "promise");
    Value* resolve = f.ctx->getProperty(obj, "resolve");
    Value* reject = f.ctx->getProperty(obj, "reject");
    ASSERT_EQ(promise->type()->kind(), TypeKind::Promise);
    EXPECT_TRUE(neo::core::detail::isCallable(resolve));
    EXPECT_TRUE(neo::core::detail::isCallable(reject));
    EXPECT_EQ(neo::core::asPromise(promise)->state, PromiseState::Pending);

    Value* args[1] = {f.ctx->createNumber(5)};
    f.ctx->call(resolve, f.ctx->createUndefined(), 1, args);
    EXPECT_EQ(neo::core::asPromise(promise)->state, PromiseState::Fulfilled);
    EXPECT_DOUBLE_EQ(num(neo::core::asPromise(promise)->result), 5.0);
}

TEST(PromiseStaticTest, WithResolversRejectWorks) {
    Fixture f;
    Value* fn = f.ctx->getProperty(promiseCtor(f.ctx.get()), "withResolvers");
    Value* obj = f.ctx->call(fn, promiseCtor(f.ctx.get()), 0, nullptr);
    Value* promise = f.ctx->getProperty(obj, "promise");
    Value* reject = f.ctx->getProperty(obj, "reject");
    Value* args[1] = {f.ctx->createString("nope")};
    f.ctx->call(reject, f.ctx->createUndefined(), 1, args);
    EXPECT_EQ(neo::core::asPromise(promise)->state, PromiseState::Rejected);
    EXPECT_EQ(str(neo::core::asPromise(promise)->result), "nope");
}

TEST(PromiseStaticTest, WithResolversOnNonConstructorIsTypeError) {
    Fixture f;
    Value* fn = f.ctx->getProperty(promiseCtor(f.ctx.get()), "withResolvers");
    Value* r = f.ctx->call(fn, f.ctx->createNumber(1), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    Value* err = static_cast<neo::core::ExceptionData*>(r->data())->child;
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "TypeError");
}

// ===== 方法 length 属性（§27.2.5.1/.3/.4）=====

TEST(PromiseStaticTest, ThenCatchFinallyLengthIsOne) {
    Fixture f;
    Value* proto = f.ctx->promisePrototype();
    for (const char* name : {"then", "catch", "finally"}) {
        Value* fn = f.ctx->getProperty(proto, name);
        Value* len = f.ctx->getProperty(fn, "length");
        ASSERT_EQ(len->type()->kind(), TypeKind::Number) << name;
        EXPECT_DOUBLE_EQ(num(len), 1.0) << name;
    }
}

TEST(PromiseStaticTest, StaticMethodLengths) {
    Fixture f;
    Value* ctor = promiseCtor(f.ctx.get());
    struct Expect { const char* name; double len; };
    const Expect expects[] = {{"resolve", 1}, {"reject", 1}, {"all", 1}, {"race", 1},
                              {"allSettled", 1}, {"any", 1}, {"withResolvers", 0}};
    for (const auto& e : expects) {
        Value* fn = f.ctx->getProperty(ctor, e.name);
        Value* len = f.ctx->getProperty(fn, "length");
        ASSERT_EQ(len->type()->kind(), TypeKind::Number) << e.name;
        EXPECT_DOUBLE_EQ(num(len), e.len) << e.name;
    }
}

// ===== 组合器走 GetPromiseResolve（§27.2.4.1.1 step 2）=====

TEST(PromiseStaticTest, AllUsesOverriddenResolve) {
    Fixture f;
    // 覆写 C.resolve：记录调用次数，转调内建
    f.ctx->global()->bind("__resolveCalls", f.ctx->createNumber(0));
    Value* custom = f.ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t argc, Value* argv[]) {
            Value* n = c->global()->lookup("__resolveCalls");
            c->global()->bind("__resolveCalls",
                              c->createNumber(num(n) + 1.0));
            Value* args[1] = {argc > 0 ? argv[0] : c->createUndefined()};
            return c->call(c->getProperty(self, "__realResolve"), self, 1, args);
        }, 1);
    Value* ctor = promiseCtor(f.ctx.get());

    // 用自定义构造函数：构造时转调 %Promise%
    Value* speciesCtor = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t argc, Value* argv[]) {
            Value* args[1] = {argc > 0 ? argv[0] : c->createUndefined()};
            return c->construct(c->global()->lookup("Promise"), 1, args);
        }, 1);
    f.ctx->setProperty(speciesCtor, "resolve", custom);
    // custom 以 self = speciesCtor 调用，故 __realResolve 挂在 speciesCtor 上
    f.ctx->setProperty(speciesCtor, "__realResolve",
                       f.ctx->getProperty(ctor, "resolve"));

    Value* allFn = f.ctx->getProperty(ctor, "all");
    Value* arr = arrayOf(f.ctx.get(), {f.ctx->createNumber(1), f.ctx->createNumber(2)});
    Value* args[1] = {arr};
    Value* result = f.ctx->call(allFn, speciesCtor, 1, args);
    drain(f.ctx.get());

    EXPECT_DOUBLE_EQ(num(f.ctx->global()->lookup("__resolveCalls")), 2.0);
    EXPECT_EQ(neo::core::asPromise(result)->state, PromiseState::Fulfilled);
}

TEST(PromiseStaticTest, AllRejectsWhenResolveIsNotCallable) {
    Fixture f;
    Value* ctor = promiseCtor(f.ctx.get());
    Value* fakeCtor = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t argc, Value* argv[]) {
            Value* args[1] = {argc > 0 ? argv[0] : c->createUndefined()};
            return c->construct(c->global()->lookup("Promise"), 1, args);
        }, 1);
    f.ctx->setProperty(fakeCtor, "resolve", f.ctx->createNumber(1));  // 不可调用

    Value* allFn = f.ctx->getProperty(ctor, "all");
    Value* args[1] = {f.ctx->createArray()};
    Value* result = f.ctx->call(allFn, fakeCtor, 1, args);
    ASSERT_EQ(result->type()->kind(), TypeKind::Promise);
    EXPECT_EQ(neo::core::asPromise(result)->state, PromiseState::Rejected);
}

// ===== catch 走 Invoke(self, "then")（§27.2.5.1）=====

TEST(PromiseStaticTest, CatchUsesOverriddenThen) {
    Fixture f;
    f.ctx->global()->bind("__thenCalls", f.ctx->createNumber(0));
    Value* p = newPromise(f.ctx.get());
    Value* customThen = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* n = c->global()->lookup("__thenCalls");
            c->global()->bind("__thenCalls", c->createNumber(num(n) + 1.0));
            return c->createUndefined();
        }, 2);
    f.ctx->setProperty(p, "then", customThen);

    Value* args[1] = {f.ctx->createUndefined()};
    f.ctx->call(f.ctx->getProperty(p, "catch"), p, 1, args);
    EXPECT_DOUBLE_EQ(num(f.ctx->global()->lookup("__thenCalls")), 1.0);
}
