// MathBuiltinsTest.cpp - Math 命名空间单元测试（ES2024 §21.3）
//
// 验证：常量、一元方法、round 的 -0/半值语义、max/min 的 NaN 与 ±0、
// clz32/imul/fround/sign、random 值域。

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

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

struct Fixture {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    NullSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx = createContext(alloc.get(), &console);

    Value* math() { return ctx->global()->lookup("Math"); }

    Value* call(const char* name, size_t argc = 0, Value* argv[] = nullptr) {
        Value* fn = ctx->getProperty(math(), name);
        return ctx->call(fn, math(), argc, argv);
    }

    Value* num(double v) { return ctx->createNumber(v); }

    Value* call1(const char* name, double a) {
        Value* args[1] = {num(a)};
        return call(name, 1, args);
    }

    Value* call2(const char* name, double a, double b) {
        Value* args[2] = {num(a), num(b)};
        return call(name, 2, args);
    }
};

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }

bool isError(Value* v) {
    return v != nullptr && (v->type()->kind() == TypeKind::Exception ||
                            v->type()->kind() == TypeKind::Fatal);
}

}  // namespace

TEST(MathBuiltinsTest, RegisteredOnGlobalWithConstants) {
    Fixture f;
    Value* math = f.math();
    ASSERT_NE(math, nullptr);
    ASSERT_EQ(math->type()->kind(), TypeKind::Object);
    EXPECT_DOUBLE_EQ(numOf(f.ctx->getProperty(math, "PI")), 3.14159265358979323846);
    EXPECT_DOUBLE_EQ(numOf(f.ctx->getProperty(math, "E")), 2.7182818284590452354);
    EXPECT_DOUBLE_EQ(numOf(f.ctx->getProperty(math, "SQRT2")), 1.4142135623730951);
}

TEST(MathBuiltinsTest, UnaryMethods) {
    Fixture f;
    Value* args[1] = {f.num(-2.5)};
    EXPECT_DOUBLE_EQ(numOf(f.call("abs", 1, args)), 2.5);
    EXPECT_DOUBLE_EQ(numOf(f.call("floor", 1, args)), -3.0);
    EXPECT_DOUBLE_EQ(numOf(f.call("ceil", 1, args)), -2.0);
    EXPECT_DOUBLE_EQ(numOf(f.call("trunc", 1, args)), -2.0);
    EXPECT_DOUBLE_EQ(numOf(f.call1("sqrt", 9.0)), 3.0);
    EXPECT_DOUBLE_EQ(numOf(f.call1("cbrt", 27.0)), 3.0);
    EXPECT_DOUBLE_EQ(numOf(f.call2("pow", 2.0, 10.0)), 1024.0);
}

TEST(MathBuiltinsTest, MissingArgumentIsNaN) {
    Fixture f;
    Value* r = f.call("abs");
    ASSERT_FALSE(isError(r));
    EXPECT_TRUE(std::isnan(numOf(r)));
}

TEST(MathBuiltinsTest, RoundHalfTowardPositiveInfinityAndNegativeZero) {
    Fixture f;
    EXPECT_DOUBLE_EQ(numOf(f.call1("round", 2.5)), 3.0);
    EXPECT_DOUBLE_EQ(numOf(f.call1("round", -2.5)), -2.0);
    Value* r = f.call1("round", -0.5);
    EXPECT_DOUBLE_EQ(numOf(r), 0.0);
    EXPECT_TRUE(std::signbit(numOf(r)));
}

TEST(MathBuiltinsTest, MaxMinNaNPropagationAndSignedZero) {
    Fixture f;
    Value* nanArgs[2] = {f.num(1.0), f.num(std::numeric_limits<double>::quiet_NaN())};
    EXPECT_TRUE(std::isnan(numOf(f.call("max", 2, nanArgs))));
    EXPECT_TRUE(std::isnan(numOf(f.call("min", 2, nanArgs))));

    Value* zeroArgs[2] = {f.num(0.0), f.num(-0.0)};
    Value* maxZero = f.call("max", 2, zeroArgs);
    EXPECT_DOUBLE_EQ(numOf(maxZero), 0.0);
    EXPECT_FALSE(std::signbit(numOf(maxZero)));
    Value* minZero = f.call("min", 2, zeroArgs);
    EXPECT_DOUBLE_EQ(numOf(minZero), 0.0);
    EXPECT_TRUE(std::signbit(numOf(minZero)));
}

TEST(MathBuiltinsTest, VariadicMethodsAllowNoArguments) {
    Fixture f;
    EXPECT_DOUBLE_EQ(numOf(f.call("max")),
                     -std::numeric_limits<double>::infinity());
    EXPECT_DOUBLE_EQ(numOf(f.call("min")),
                     std::numeric_limits<double>::infinity());
    EXPECT_DOUBLE_EQ(numOf(f.call("hypot")), 0.0);
    EXPECT_TRUE(std::isnan(numOf(f.call("pow"))));
}

TEST(MathBuiltinsTest, SignPreservesZeroAndNaN) {
    Fixture f;
    Value* negZero = f.call1("sign", -0.0);
    EXPECT_TRUE(std::signbit(numOf(negZero)));
    EXPECT_DOUBLE_EQ(numOf(f.call1("sign", -3.0)), -1.0);
    EXPECT_DOUBLE_EQ(numOf(f.call1("sign", 3.0)), 1.0);
    EXPECT_TRUE(std::isnan(
        numOf(f.call1("sign", std::numeric_limits<double>::quiet_NaN()))));
}

TEST(MathBuiltinsTest, Clz32ImulFround) {
    Fixture f;
    EXPECT_DOUBLE_EQ(numOf(f.call1("clz32", 1.0)), 31.0);
    EXPECT_DOUBLE_EQ(numOf(f.call1("clz32", 0.0)), 32.0);
    EXPECT_DOUBLE_EQ(numOf(f.call1("clz32", 2147483648.0)), 0.0);

    EXPECT_DOUBLE_EQ(numOf(f.call2("imul", 3.0, 4.0)), 12.0);
    EXPECT_DOUBLE_EQ(numOf(f.call2("imul", 4294967295.0, 5.0)),
                     static_cast<double>(static_cast<int32_t>(0xFFFFFFFFu * 5u)));

    EXPECT_DOUBLE_EQ(numOf(f.call1("fround", 1.337)),
                     static_cast<double>(static_cast<float>(1.337)));
}

TEST(MathBuiltinsTest, RandomInUnitInterval) {
    Fixture f;
    for (int i = 0; i < 100; ++i) {
        double v = numOf(f.call("random"));
        EXPECT_GE(v, 0.0);
        EXPECT_LT(v, 1.0);
    }
}

TEST(MathBuiltinsTest, SymbolArgumentPropagatesTypeError) {
    Fixture f;
    Value* sym = f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator);
    Value* r = f.call("abs", 1, &sym);
    ASSERT_TRUE(isError(r));
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}
