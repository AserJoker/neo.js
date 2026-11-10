// GeneratorTest.cpp - Generator 封装单元测试（ADR-045 §8）
//
// 验证：
// - 生成器函数调用不执行函数体，返回 Suspended 协程
// - next() 按 stage 推进，返回 { value, done }
// - 首次 next 的实参被丢弃
// - return(v) 注入 Interrupt(Return)：已挂起走函数体，未启动直接完成
// - throw(e) 注入 Interrupt(Throw)：未启动直接抛出
// - 完成后 next 返回 { undefined, true }
// - 原型链：实例 → fn.prototype → %GeneratorPrototype% → %Iterator.prototype% → Object.prototype
// - @@iterator 返回 this、@@toStringTag = "Generator"
// - 生成器函数不可构造

#include "core/IContext.h"
#include "core/IType.h"
#include "core/InterruptKind.h"
#include "core/Value.h"
#include "core/types/Coroutine.h"
#include "core/types/Interrupt.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using neo::core::CoroutineData;
using neo::core::CoroutineState;
using neo::core::IContext;
using neo::core::InterruptData;
using neo::core::InterruptKind;
using neo::core::NumberData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::asCoroutine;
using neo::core::asInterrupt;
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

    Value* genFn(neo::core::NativeFn body, const char* name) {
        Value* fn = ctx->createNativeFunction(body, 0);
        ctx->setFunctionName(fn, name);
        ctx->setFunctionGenerator(fn, true);
        return fn;
    }

    Value* call(Value* fn) {
        return ctx->call(fn, ctx->createUndefined(), 0, nullptr);
    }

    Value* prop(Value* obj, const char* key) {
        return ctx->getProperty(obj, ctx->createString(key));
    }
};

double num(Value* v) { return static_cast<NumberData*>(v->data())->value; }
bool flag(Value* v) { return neo::core::detail::toBoolean(v); }

// 每个 case 开头检查注入信号（ADR-045 §8.1）。
bool handleInjected(CoroutineData* co, Value** out) {
    Value* sent = co->sentValue;
    if (!isInterruptValue(sent)) return false;
    InterruptData* ir = asInterrupt(sent);
    if (ir->kind == InterruptKind::Return || ir->kind == InterruptKind::Throw) {
        *out = ir->value;  // Throw 时是 Exception 通道值
        return true;
    }
    return false;
}

// 三阶段生成器：yield 1、yield 2、返回 3。
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

// 首次 next 读取 sentValue 并 yield 它，用于验证首参被丢弃。
Value* bodyEchoSent(IContext* ctx, Value*, size_t, Value*[]) {
    CoroutineData* co = asCoroutine(ctx->getCurrentCoroutine());
    return ctx->createInterrupt(InterruptKind::Yield, 1, co->sentValue);
}

}  // namespace

// ===== 调用语义 =====

TEST(GeneratorTest, CallReturnsSuspendedCoroutineWithoutRunningBody) {
    Fixture f;
    Value* fn = f.genFn(body3, "g");
    Value* gen = f.call(fn);

    EXPECT_EQ(gen->type()->kind(), TypeKind::Coroutine);
    CoroutineData* co = asCoroutine(gen);
    ASSERT_NE(co, nullptr);
    EXPECT_EQ(co->state, CoroutineState::Suspended);
    EXPECT_EQ(co->execScope, nullptr);  // 函数体未执行
}

TEST(GeneratorTest, NotConstructible) {
    Fixture f;
    Value* fn = f.genFn(body3, "g");
    Value* r = f.ctx->construct(fn, 0, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

// ===== next =====

TEST(GeneratorTest, NextYieldsThenCompletes) {
    Fixture f;
    Value* gen = f.call(f.genFn(body3, "g"));

    Value* next = f.prop(gen, "next");
    Value* r1 = f.ctx->call(next, gen, 0, nullptr);
    EXPECT_DOUBLE_EQ(num(f.prop(r1, "value")), 1.0);
    EXPECT_FALSE(flag(f.prop(r1, "done")));

    Value* r2 = f.ctx->call(next, gen, 0, nullptr);
    EXPECT_DOUBLE_EQ(num(f.prop(r2, "value")), 2.0);
    EXPECT_FALSE(flag(f.prop(r2, "done")));

    Value* r3 = f.ctx->call(next, gen, 0, nullptr);
    EXPECT_DOUBLE_EQ(num(f.prop(r3, "value")), 3.0);
    EXPECT_TRUE(flag(f.prop(r3, "done")));
    EXPECT_EQ(asCoroutine(gen)->state, CoroutineState::Completed);

    // 完成后 next 返回 { undefined, true }
    Value* r4 = f.ctx->call(next, gen, 0, nullptr);
    EXPECT_EQ(f.prop(r4, "value")->type()->kind(), TypeKind::Undefined);
    EXPECT_TRUE(flag(f.prop(r4, "done")));
}

TEST(GeneratorTest, FirstNextArgumentIsDiscarded) {
    Fixture f;
    Value* gen = f.call(f.genFn(bodyEchoSent, "g"));
    Value* next = f.prop(gen, "next");
    Value* arg = f.ctx->createNumber(99);
    Value* r = f.ctx->call(next, gen, 1, &arg);
    EXPECT_EQ(f.prop(r, "value")->type()->kind(), TypeKind::Undefined);
    EXPECT_FALSE(flag(f.prop(r, "done")));
}

TEST(GeneratorTest, NextOnNonGeneratorThrows) {
    Fixture f;
    Value* genProto = f.ctx->generatorPrototype();
    Value* nextFn = f.prop(genProto, "next");
    Value* r = f.ctx->call(nextFn, f.ctx->createObject(), 0, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

// ===== return / throw 注入 =====

TEST(GeneratorTest, ReturnBeforeStartCompletesWithoutRunning) {
    Fixture f;
    Value* gen = f.call(f.genFn(body3, "g"));
    Value* ret = f.prop(gen, "return");
    Value* v = f.ctx->createNumber(42);
    Value* r = f.ctx->call(ret, gen, 1, &v);
    EXPECT_DOUBLE_EQ(num(f.prop(r, "value")), 42.0);
    EXPECT_TRUE(flag(f.prop(r, "done")));
    EXPECT_EQ(asCoroutine(gen)->state, CoroutineState::Completed);
    EXPECT_EQ(asCoroutine(gen)->execScope, nullptr);
}

TEST(GeneratorTest, ReturnInjectsIntoBody) {
    Fixture f;
    Value* gen = f.call(f.genFn(body3, "g"));
    Value* next = f.prop(gen, "next");
    (void)f.ctx->call(next, gen, 0, nullptr);  // 执行到 stage 1

    Value* ret = f.prop(gen, "return");
    Value* v = f.ctx->createNumber(7);
    Value* r = f.ctx->call(ret, gen, 1, &v);
    EXPECT_DOUBLE_EQ(num(f.prop(r, "value")), 7.0);
    EXPECT_TRUE(flag(f.prop(r, "done")));
    EXPECT_EQ(asCoroutine(gen)->state, CoroutineState::Completed);
}

TEST(GeneratorTest, ThrowBeforeStartPropagates) {
    Fixture f;
    Value* gen = f.call(f.genFn(body3, "g"));
    Value* thr = f.prop(gen, "throw");
    Value* e = f.ctx->createString("boom");
    Value* r = f.ctx->call(thr, gen, 1, &e);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(asCoroutine(gen)->state, CoroutineState::Completed);
}

TEST(GeneratorTest, ThrowInjectsIntoBody) {
    Fixture f;
    Value* gen = f.call(f.genFn(body3, "g"));
    Value* next = f.prop(gen, "next");
    (void)f.ctx->call(next, gen, 0, nullptr);

    Value* thr = f.prop(gen, "throw");
    Value* e = f.ctx->createString("boom");
    Value* r = f.ctx->call(thr, gen, 1, &e);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(asCoroutine(gen)->state, CoroutineState::Completed);
}

// ===== 原型链与协议 =====

TEST(GeneratorTest, PrototypeChain) {
    Fixture f;
    Value* fn = f.genFn(body3, "g");
    Value* gen = f.call(fn);

    Value* genProto = f.ctx->generatorPrototype();
    ASSERT_NE(genProto, nullptr);

    // 实例 → fn.prototype
    Value* fnProto = f.ctx->getPrototype(gen);
    EXPECT_EQ(fnProto, f.prop(fn, "prototype"));
    // fn.prototype → %GeneratorPrototype%
    EXPECT_EQ(f.ctx->getPrototype(fnProto), genProto);
    // %GeneratorPrototype% → %Iterator.prototype%
    EXPECT_EQ(f.ctx->getPrototype(genProto), f.ctx->iteratorPrototype());
    // %Iterator.prototype% → Object.prototype
    EXPECT_EQ(f.ctx->getPrototype(f.ctx->iteratorPrototype()), f.ctx->objectPrototype());
}

TEST(GeneratorTest, IteratorSymbolReturnsThis) {
    Fixture f;
    Value* gen = f.call(f.genFn(body3, "g"));
    Value* sym = f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator);
    Value* method = f.ctx->getProperty(gen, sym);
    ASSERT_EQ(method->type()->kind(), TypeKind::Function);
    Value* r = f.ctx->call(method, gen, 0, nullptr);
    EXPECT_EQ(r, gen);
}

TEST(GeneratorTest, ToStringTagIsGenerator) {
    Fixture f;
    Value* gen = f.call(f.genFn(body3, "g"));
    Value* sym = f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToStringTag);
    Value* tag = f.ctx->getProperty(gen, sym);
    ASSERT_EQ(tag->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<neo::core::StringData*>(tag->data())->value, "Generator");
}

TEST(GeneratorTest, GeneratorMethodLengthIsOne) {
    Fixture f;
    Value* genProto = f.ctx->generatorPrototype();
    for (const char* name : {"next", "return", "throw"}) {
        Value* m = f.prop(genProto, name);
        ASSERT_EQ(m->type()->kind(), TypeKind::Function) << name;
        Value* len = f.prop(m, "length");
        EXPECT_DOUBLE_EQ(num(len), 1.0) << name;
    }
}
