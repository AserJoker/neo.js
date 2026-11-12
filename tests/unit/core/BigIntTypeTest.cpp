// BigIntTypeTest.cpp - bigint 原始值（引擎接入第一步）
//
// 覆盖：类型/typeof/ToString/ToBoolean、严格相等、四则与混用错误、
// 位运算与移位、关系比较（含 BigInt/Number 精确比较）、抽象相等、
// ToNumber/ToPropertyKey、字符串拼接。

#include "bigint/BigInt.h"
#include "core/ErrorKind.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/OperatorKind.h"
#include "core/Value.h"
#include "core/types/BigInt.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>

namespace {

using neo::bigint::BigInt;
using neo::core::BinaryOperator;
using neo::core::IContext;
using neo::core::TypeKind;
using neo::core::UnaryOperator;
using neo::core::Value;
using neo::core::createContext;
using neo::core::detail::isErrorValue;
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

    Value* big(const BigInt& v) { return ctx->createBigInt(v); }
    Value* big(int64_t v) { return ctx->createBigInt(BigInt(v)); }
    Value* num(double v) { return ctx->createNumber(v); }
    Value* str(const std::string& v) { return ctx->createString(v); }
    Value* boolean(bool v) { return ctx->createBoolean(v); }
};

const BigInt& valueOf(Value* v) {
    return static_cast<neo::core::BigIntData*>(v->data())->value;
}

std::string text(Value* v) {
    return static_cast<neo::core::StringData*>(v->data())->value;
}

bool flag(Value* v) {
    return static_cast<neo::core::BooleanData*>(v->data())->value;
}

bool isException(Value* v) {
    return v->type()->kind() == TypeKind::Exception;
}

Value* bigValueOf(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createBigInt(BigInt(static_cast<int64_t>(2)));
}

}  // namespace

TEST(BigIntTypeTest, TypeAndConversions) {
    Fixture f;
    Value* one = f.big(1);
    EXPECT_EQ(one->type()->kind(), TypeKind::BigInt);
    EXPECT_STREQ(one->type()->name(), "bigint");

    EXPECT_EQ(text(f.ctx->unaryOp(UnaryOperator::TypeOf, one)), "bigint");
    EXPECT_EQ(text(f.ctx->toString(one)), "1");
    EXPECT_EQ(text(f.ctx->toString(f.big(BigInt(int64_t(-42))))), "-42");
    EXPECT_TRUE(f.ctx->toBoolean(one));
    EXPECT_FALSE(f.ctx->toBoolean(f.big(0)));

    // ToNumber 对 BigInt 抛 TypeError。
    EXPECT_TRUE(isException(f.ctx->toNumber(one)));
    // ToPropertyKey 走 ToString。
    EXPECT_EQ(text(f.ctx->toPropertyKey(f.big(42))), "42");
}

TEST(BigIntTypeTest, StrictEquality) {
    Fixture f;
    EXPECT_TRUE(flag(f.ctx->binaryOp(BinaryOperator::StrictEqual, f.big(1), f.big(1))));
    EXPECT_FALSE(flag(f.ctx->binaryOp(BinaryOperator::StrictEqual, f.big(1), f.big(2))));
    EXPECT_FALSE(flag(f.ctx->binaryOp(BinaryOperator::StrictEqual, f.big(1), f.num(1))));
    EXPECT_TRUE(flag(f.ctx->binaryOp(
        BinaryOperator::StrictEqual, f.big(BigInt(1) << 100), f.big(BigInt(1) << 100))));
}

TEST(BigIntTypeTest, Arithmetic) {
    Fixture f;
    EXPECT_EQ(valueOf(f.ctx->add(f.big(2), f.big(3))).toString(), "5");
    EXPECT_EQ(valueOf(f.ctx->sub(f.big(2), f.big(3))).toString(), "-1");
    EXPECT_EQ(valueOf(f.ctx->mul(f.big(123456789), f.big(987654321))).toString(),
              "121932631112635269");
    EXPECT_EQ(valueOf(f.ctx->div(f.big(10), f.big(3))).toString(), "3");
    EXPECT_EQ(valueOf(f.ctx->binaryOp(BinaryOperator::Mod, f.big(10), f.big(3))).toString(),
              "1");
    EXPECT_EQ(valueOf(f.ctx->binaryOp(BinaryOperator::Exp, f.big(2), f.big(100))).toString(),
              "1267650600228229401496703205376");

    // 大数跨 limb。
    Value* p = f.big(BigInt(1) << 64);
    EXPECT_EQ(valueOf(f.ctx->add(p, f.big(1))).toString(), "18446744073709551617");

    // 混用 BigInt/Number → TypeError。
    EXPECT_TRUE(isException(f.ctx->add(f.big(1), f.num(1))));
    EXPECT_TRUE(isException(f.ctx->sub(f.num(1), f.big(1))));
    EXPECT_TRUE(isException(f.ctx->mul(f.big(1), f.num(2))));
    EXPECT_TRUE(isException(f.ctx->binaryOp(BinaryOperator::Mod, f.big(5), f.num(2))));
    EXPECT_TRUE(isException(f.ctx->binaryOp(BinaryOperator::Exp, f.big(2), f.num(3))));

    // 除零 / 负指数 → RangeError。
    Value* divZero = f.ctx->div(f.big(1), f.big(0));
    ASSERT_TRUE(isException(divZero));
    auto* exception = static_cast<neo::core::ExceptionData*>(divZero->data());
    EXPECT_EQ(text(f.ctx->getProperty(exception->child, "name")), "RangeError");
    EXPECT_TRUE(isException(
        f.ctx->binaryOp(BinaryOperator::Exp, f.big(2), f.big(-1))));
}

TEST(BigIntTypeTest, UnaryAndBitwise) {
    Fixture f;
    EXPECT_EQ(valueOf(f.ctx->unaryOp(UnaryOperator::Minus, f.big(5))).toString(), "-5");
    EXPECT_EQ(valueOf(f.ctx->unaryOp(UnaryOperator::BitNot, f.big(5))).toString(), "-6");
    EXPECT_TRUE(flag(f.ctx->unaryOp(UnaryOperator::LogicalNot, f.big(0))));
    EXPECT_TRUE(isException(f.ctx->unaryOp(UnaryOperator::Plus, f.big(1))));

    EXPECT_EQ(valueOf(f.ctx->binaryOp(BinaryOperator::BitAnd, f.big(0xF0), f.big(0x0F)))
                  .toString(),
              "0");
    EXPECT_EQ(valueOf(f.ctx->binaryOp(BinaryOperator::BitOr, f.big(0xF0), f.big(0x0F)))
                  .toString(),
              "255");
    EXPECT_EQ(valueOf(f.ctx->binaryOp(BinaryOperator::BitXor, f.big(0xFF), f.big(0x0F)))
                  .toString(),
              "240");
    EXPECT_EQ(valueOf(f.ctx->binaryOp(BinaryOperator::BitAnd, f.big(-1), f.big(5)))
                  .toString(),
              "5");

    EXPECT_EQ(valueOf(f.ctx->binaryOp(BinaryOperator::ShiftLeft, f.big(1), f.big(100)))
                  .toString(),
              "1267650600228229401496703205376");
    EXPECT_EQ(valueOf(f.ctx->binaryOp(BinaryOperator::ShiftRight,
                                      f.big(BigInt(1) << 100), f.big(100)))
                  .toString(),
              "1");
    // 负移位反向。
    EXPECT_EQ(valueOf(f.ctx->binaryOp(BinaryOperator::ShiftLeft, f.big(1), f.big(-1)))
                  .toString(),
              "0");
    EXPECT_EQ(valueOf(f.ctx->binaryOp(BinaryOperator::ShiftRight, f.big(-5), f.big(1)))
                  .toString(),
              "-3");
    // >>> 对 BigInt 不合法。
    EXPECT_TRUE(isException(f.ctx->binaryOp(
        BinaryOperator::UnsignedShiftRight, f.big(1), f.big(1))));
    // 混用 → TypeError。
    EXPECT_TRUE(isException(
        f.ctx->binaryOp(BinaryOperator::BitAnd, f.big(1), f.num(1))));
}

TEST(BigIntTypeTest, RelationalComparisons) {
    Fixture f;
    EXPECT_TRUE(flag(f.ctx->binaryOp(BinaryOperator::LessThan, f.big(1), f.big(2))));
    EXPECT_FALSE(flag(f.ctx->binaryOp(BinaryOperator::LessThan, f.big(2), f.big(1))));

    // BigInt/Number 精确比较。
    EXPECT_TRUE(flag(f.ctx->binaryOp(BinaryOperator::LessThan, f.big(1), f.num(1.5))));
    EXPECT_FALSE(flag(f.ctx->binaryOp(BinaryOperator::LessThan, f.big(2), f.num(1.5))));
    EXPECT_TRUE(flag(f.ctx->binaryOp(BinaryOperator::GreaterThan, f.big(2), f.num(1.5))));
    EXPECT_TRUE(flag(f.ctx->binaryOp(BinaryOperator::LessThanOrEqual, f.big(1), f.num(1))));
    EXPECT_TRUE(flag(f.ctx->binaryOp(BinaryOperator::LessThan, f.big(1), f.num(2.0))));

    // 大整数不能被 double 精度吞掉。
    Value* huge = f.big((BigInt(1) << 60) + BigInt(1));
    EXPECT_TRUE(flag(f.ctx->binaryOp(BinaryOperator::GreaterThan, huge, f.num(1.0e18))));

    // NaN → 所有关系比较为 false；Infinity 正常。
    EXPECT_FALSE(flag(f.ctx->binaryOp(BinaryOperator::LessThan, f.big(1),
                                      f.num(std::nan("")))));
    EXPECT_TRUE(flag(f.ctx->binaryOp(BinaryOperator::LessThan, f.big(1),
                                     f.num(INFINITY))));
    EXPECT_TRUE(flag(f.ctx->binaryOp(BinaryOperator::GreaterThan, f.big(-1),
                                     f.num(-INFINITY))));

    // 字符串走 ToNumber（不是 StringToBigInt）。
    EXPECT_TRUE(flag(f.ctx->binaryOp(BinaryOperator::LessThan, f.big(1), f.str("2"))));
}

TEST(BigIntTypeTest, AbstractEquality) {
    Fixture f;
    auto eq = [&](Value* a, Value* b) {
        return flag(f.ctx->binaryOp(BinaryOperator::AbstractEqual, a, b));
    };
    EXPECT_TRUE(eq(f.big(1), f.num(1)));
    EXPECT_FALSE(eq(f.big(1), f.num(1.5)));
    EXPECT_FALSE(eq(f.big(1), f.num(std::nan(""))));
    EXPECT_TRUE(eq(f.big(0), f.num(-0.0)));
    EXPECT_FALSE(eq(f.big(1), f.num(INFINITY)));

    EXPECT_TRUE(eq(f.big(1), f.str("1")));
    EXPECT_TRUE(eq(f.big(1), f.str(" 1 ")));
    EXPECT_TRUE(eq(f.big(1), f.str("0x1")));
    EXPECT_FALSE(eq(f.big(1), f.str("1.0")));
    EXPECT_FALSE(eq(f.big(1), f.str("x")));
    EXPECT_TRUE(eq(f.big(0), f.str("")));

    EXPECT_TRUE(eq(f.big(1), f.boolean(true)));
    EXPECT_FALSE(eq(f.big(1), f.boolean(false)));
    EXPECT_FALSE(eq(f.big(1), f.ctx->createNull()));
    EXPECT_FALSE(eq(f.big(1), f.ctx->createUndefined()));
}

TEST(BigIntTypeTest, ObjectToNumeric) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    f.ctx->defineProperty(obj, "valueOf",
                          f.ctx->createNativeFunction(bigValueOf, 0), true, true, true);
    EXPECT_EQ(valueOf(f.ctx->add(f.big(1), obj)).toString(), "3");
    EXPECT_EQ(valueOf(f.ctx->binaryOp(BinaryOperator::BitAnd, f.big(3), obj)).toString(),
              "2");
}

TEST(BigIntTypeTest, StringConcatenation) {
    Fixture f;
    EXPECT_EQ(text(f.ctx->add(f.big(1), f.str("x"))), "1x");
    EXPECT_EQ(text(f.ctx->add(f.str("x"), f.big(1))), "x1");
    EXPECT_EQ(text(f.ctx->add(f.big(BigInt(1) << 100), f.str(""))),
              "1267650600228229401496703205376");
}
