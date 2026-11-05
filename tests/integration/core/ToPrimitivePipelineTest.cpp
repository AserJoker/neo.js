// ToPrimitivePipelineTest.cpp - 类型转换链端到端集成测试
//
// 端到端验证"用户定义 valueOf/toString/@@toPrimitive → 运算符/内建触发转换 →
// 结果参与运算/键查找 → GC 回收"全链路（真实依赖不 mock）。
//
// 关联 ADR: 016（Type 体系）、006（异常即值）、017（Fatal 透传）、027（集成测试）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

namespace {

using neo::core::IContext;
using neo::core::NumberData;
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

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }

Value* globalOf(IContext* ctx, const char* name) { return ctx->global()->lookup(name); }

// 造一个"类似金额"的对象：valueOf 返回内部数值
Value* makeAmount(IContext* ctx, double n) {
    Value* obj = ctx->createObject();
    Value* vo = ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t, Value*[]) {
            return c->getProperty(self, "__n");
        }, 0);
    ctx->defineProperty(obj, "__n", ctx->createNumber(n));
    ctx->defineProperty(obj, "valueOf", vo);
    return obj;
}

}  // namespace

// ============ 场景 1：valueOf 参与算术 ============

TEST(ToPrimitivePipelineTest, ValueOfDrivesArithmetic) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* amount = makeAmount(ctx.get(), 100);
    EXPECT_DOUBLE_EQ(numOf(ctx->add(amount, ctx->createNumber(1))), 101.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(amount, ctx->createNumber(40))), 60.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->mul(amount, ctx->createNumber(2))), 200.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->div(amount, ctx->createNumber(4))), 25.0);
}

// ============ 场景 2：toString 参与拼接与键 ============

TEST(ToPrimitivePipelineTest, ToStringDrivesConcatAndKeys) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* key = ctx->createObject();
    Value* ts = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString("id"); }, 0);
    ctx->defineProperty(key, "toString", ts);

    // 键转换
    Value* map = ctx->createObject();
    ctx->setProperty(map, key, ctx->createNumber(7));
    EXPECT_EQ(numOf(ctx->getProperty(map, "id")), 7.0);

    // 拼接
    EXPECT_EQ(strOf(ctx->add(key, ctx->createString("!"))), "id!");
}

// ============ 场景 3：@@toPrimitive 覆盖默认行为 ============

TEST(ToPrimitivePipelineTest, ExoticToPrimitiveOverridesDefaults) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    // valueOf 返回 1，但 @@toPrimitive 返回 42 → 后者胜出
    Value* vo = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0);
    ctx->defineProperty(obj, "valueOf", vo);
    Value* exotic = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(42); }, 0);
    ctx->defineProperty(obj, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToPrimitive),
                        exotic, true, false, true);

    EXPECT_DOUBLE_EQ(numOf(ctx->sub(obj, ctx->createNumber(2))), 40.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->add(obj, ctx->createNumber(8))), 50.0);
}

// ============ 场景 4：包装对象与数组经 ToPrimitive 参与运算 ============

TEST(ToPrimitivePipelineTest, WrappersAndArraysUseToPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // new Number(5) 参与运算
    Value* numberFn = globalOf(ctx.get(), "Number");
    Value* args[1] = {ctx->createNumber(5)};
    Value* wrapper = ctx->construct(numberFn, 1, args);
    EXPECT_DOUBLE_EQ(numOf(ctx->add(wrapper, ctx->createNumber(3))), 8.0);
    EXPECT_EQ(strOf(ctx->add(wrapper, ctx->createString("x"))), "5x");

    // 数组 join 后参与运算
    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    EXPECT_EQ(strOf(ctx->add(arr, ctx->createNumber(3))), "1,23");
    // [1,2] - 1 → Number("1,2") = NaN（JS 语义）
    EXPECT_TRUE(std::isnan(numOf(ctx->sub(arr, ctx->createNumber(1)))));
    // 单元素数组可转数字：[5] - 1 === 4
    Value* single = ctx->createArray();
    ctx->setProperty(single, "0", ctx->createNumber(5));
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(single, ctx->createNumber(1))), 4.0);
}

// ============ 场景 5：异常与 Fatal 透传 ============

TEST(ToPrimitivePipelineTest, ExceptionsPropagateThroughConversions) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(obj, "valueOf", boom);
    ctx->defineProperty(obj, "toString", boom);

    EXPECT_EQ(ctx->add(obj, ctx->createNumber(1))->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->sub(obj, ctx->createNumber(1))->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->toString(obj)->type()->kind(), TypeKind::Exception);
    // 作为属性键同样透传
    EXPECT_EQ(ctx->getProperty(ctx->createObject(), obj)->type()->kind(),
              TypeKind::Exception);
}

// ============ 场景 6：大量转换后 GC 正常回收（无泄漏）============

TEST(ToPrimitivePipelineTest, ConversionsUnderGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* amount = makeAmount(ctx.get(), 7);
    for (int i = 0; i < 300; ++i) {
        Value* obj = ctx->createObject();
        Value* vo = ctx->createNativeFunction(
            [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0);
        ctx->defineProperty(obj, "valueOf", vo);
        ctx->add(obj, ctx->createNumber(i));
    }
    ctx->gc()->collect();
    EXPECT_DOUBLE_EQ(numOf(ctx->add(amount, ctx->createNumber(3))), 10.0);
    // Context 析构时 GC 回收全部 Value，ASan 验证无泄漏
}
