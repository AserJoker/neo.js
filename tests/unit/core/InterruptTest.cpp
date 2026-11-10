// InterruptTest.cpp - Interrupt 控制信号值单元测试（ADR-046）
//
// 验证：
// - Interrupt 是非对象类型（TypeKind::Interrupt，hasObjectLayout=false）
// - kind / offset / value 三部分可读，nullptr 载荷归一为 undefined
// - 行为方法全部透传 self（不参与运算/属性/调用）
// - GC 遍历 value 载荷（pin 住 Interrupt 即保活载荷）
// - isInterruptValue 判定

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/InterruptKind.h"
#include "core/Value.h"
#include "core/types/Interrupt.h"
#include "core/types/Number.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using neo::core::IContext;
using neo::core::IGCEngine;
using neo::core::InterruptData;
using neo::core::InterruptKind;
using neo::core::TypeKind;
using neo::core::Value;
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
};

}  // namespace

// ===== 类型与字段 =====

TEST(InterruptTest, KindAndNonObjectLayout) {
    Fixture f;
    Value* v = f.ctx->createInterrupt(InterruptKind::Yield, 0, nullptr);
    EXPECT_EQ(v->type()->kind(), TypeKind::Interrupt);
    EXPECT_FALSE(v->type()->hasObjectLayout());
    EXPECT_FALSE(neo::core::kindHasObjectLayout(TypeKind::Interrupt));
    EXPECT_STREQ(v->type()->name(), "interrupt");
    EXPECT_TRUE(isInterruptValue(v));
}

TEST(InterruptTest, FieldsAreReadable) {
    Fixture f;
    Value* payload = f.ctx->createNumber(7);
    Value* v = f.ctx->createInterrupt(InterruptKind::Return, 42, payload);

    InterruptData* d = asInterrupt(v);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->kind, InterruptKind::Return);
    EXPECT_EQ(d->offset, 42u);
    EXPECT_EQ(d->value, payload);
}

TEST(InterruptTest, NullPayloadBecomesUndefined) {
    Fixture f;
    Value* v = f.ctx->createInterrupt(InterruptKind::Yield, 3, nullptr);
    InterruptData* d = asInterrupt(v);
    ASSERT_NE(d, nullptr);
    ASSERT_NE(d->value, nullptr);
    EXPECT_EQ(d->value->type()->kind(), TypeKind::Undefined);
}

TEST(InterruptTest, AsInterruptRejectsNonInterrupt) {
    Fixture f;
    Value* n = f.ctx->createNumber(1);
    EXPECT_EQ(asInterrupt(n), nullptr);
    EXPECT_EQ(asInterrupt(nullptr), nullptr);
    EXPECT_FALSE(isInterruptValue(n));
    EXPECT_FALSE(isInterruptValue(nullptr));
}

// ===== 行为透传 =====

TEST(InterruptTest, ArithmeticPassthrough) {
    Fixture f;
    Value* v = f.ctx->createInterrupt(InterruptKind::Yield, 0, nullptr);
    Value* n = f.ctx->createNumber(2);

    EXPECT_EQ(f.ctx->add(v, n), v);
    EXPECT_EQ(f.ctx->sub(v, n), v);
    EXPECT_EQ(f.ctx->mul(v, n), v);
    EXPECT_EQ(f.ctx->div(v, n), v);
    EXPECT_EQ(f.ctx->toString(v), v);
}

TEST(InterruptTest, PropertyAndCallPassthrough) {
    Fixture f;
    Value* v = f.ctx->createInterrupt(InterruptKind::Yield, 0, nullptr);
    Value* key = f.ctx->createString("x");

    EXPECT_EQ(f.ctx->getProperty(v, key), v);
    EXPECT_EQ(f.ctx->setProperty(v, key, v), v);
    EXPECT_FALSE(f.ctx->hasProperty(v, key));
    EXPECT_EQ(f.ctx->deleteProperty(v, key), v);
    EXPECT_EQ(f.ctx->call(v, v, 0, nullptr), v);
    EXPECT_EQ(f.ctx->construct(v, 0, nullptr), v);
}

// ===== GC =====

TEST(InterruptTest, PayloadKeptAliveWithPinnedInterrupt) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();
    size_t baseline = gc->valueCount();

    Value* payload = nullptr;
    Value* v = nullptr;
    {
        // 临时作用域创建后弹出：两者都不再从 global 树可达
        f.ctx->pushNewScope(f.ctx->current());
        payload = f.ctx->createNumber(9);
        v = f.ctx->createInterrupt(InterruptKind::Yield, 1, payload);
        f.ctx->popScope();
    }
    ASSERT_EQ(gc->valueCount(), baseline + 2);

    v->pinRefInc();
    gc->collect();
    // Interrupt 与载荷都存活
    EXPECT_EQ(gc->valueCount(), baseline + 2);
    EXPECT_EQ(asInterrupt(v)->value, payload);
    EXPECT_DOUBLE_EQ(static_cast<neo::core::NumberData*>(payload->data())->value, 9.0);

    v->pinRefDec();
    gc->collect();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), baseline);
}

TEST(InterruptTest, UnreachableInterruptCollectedWithPayload) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();
    size_t baseline = gc->valueCount();

    {
        f.ctx->pushNewScope(f.ctx->current());
        Value* payload = f.ctx->createNumber(1);
        (void)f.ctx->createInterrupt(InterruptKind::Throw, 0, payload);
        f.ctx->popScope();
    }

    gc->collect();
    EXPECT_EQ(gc->valueCount(), baseline);
}
