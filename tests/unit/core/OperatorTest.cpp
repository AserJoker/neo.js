// OperatorTest.cpp - Engine 转换/运算符原语单元测试
//
// 验证 VM 可调用的原语：
// - toBoolean / toNumber / toPrimitive / toPropertyKey
// - 严格/抽象相等、关系、位运算、取模/幂、一元、typeof、void、in
// - 错误经 Value* 通道传播（Symbol 转换、in 的原始值右操作数）

#include "core/IContext.h"
#include "core/IType.h"
#include "core/OperatorKind.h"
#include "core/Value.h"
#include "core/types/Boolean.h"
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

using neo::core::BinaryOperator;
using neo::core::IContext;
using neo::core::ToPrimitiveHint;
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
};

double num(Value* v) {
    return static_cast<neo::core::NumberData*>(v->data())->value;
}
bool flag(Value* v) {
    return static_cast<neo::core::BooleanData*>(v->data())->value;
}
std::string text(Value* v) {
    return static_cast<neo::core::StringData*>(v->data())->value;
}

int g_valueOfCalls = 0;
int g_toStringCalls = 0;

Value* valueOfFn(IContext* ctx, Value*, size_t, Value*[]) {
    ++g_valueOfCalls;
    return ctx->createNumber(7);
}

Value* toStringFn(IContext* ctx, Value*, size_t, Value*[]) {
    ++g_toStringCalls;
    return ctx->createString("obj");
}

Value* makeConvertibleObject(Fixture& f) {
    Value* obj = f.ctx->createObject();
    Value* valueOf = f.ctx->createNativeFunction(valueOfFn, 0);
    Value* toString = f.ctx->createNativeFunction(toStringFn, 0);
    f.ctx->defineProperty(obj, "valueOf", valueOf);
    f.ctx->defineProperty(obj, "toString", toString);
    return obj;
}

// —— ToBoolean ——

TEST(OperatorTest, ToBoolean) {
    Fixture f;
    EXPECT_FALSE(f.ctx->toBoolean(f.ctx->createUndefined()));
    EXPECT_FALSE(f.ctx->toBoolean(f.ctx->createNull()));
    EXPECT_FALSE(f.ctx->toBoolean(f.ctx->createBoolean(false)));
    EXPECT_FALSE(f.ctx->toBoolean(f.ctx->createNumber(0)));
    EXPECT_FALSE(f.ctx->toBoolean(f.ctx->createNumber(std::nan(""))));
    EXPECT_FALSE(f.ctx->toBoolean(f.ctx->createString("")));

    EXPECT_TRUE(f.ctx->toBoolean(f.ctx->createBoolean(true)));
    EXPECT_TRUE(f.ctx->toBoolean(f.ctx->createNumber(-1)));
    EXPECT_TRUE(f.ctx->toBoolean(f.ctx->createString("a")));
    EXPECT_TRUE(f.ctx->toBoolean(f.ctx->createSymbol("s")));
    EXPECT_TRUE(f.ctx->toBoolean(f.ctx->createObject()));
}

// —— ToNumber ——

TEST(OperatorTest, ToNumberPrimitives) {
    Fixture f;
    EXPECT_EQ(num(f.ctx->toNumber(f.ctx->createString("42"))), 42.0);
    EXPECT_EQ(num(f.ctx->toNumber(f.ctx->createString(""))), 0.0);
    EXPECT_EQ(num(f.ctx->toNumber(f.ctx->createBoolean(true))), 1.0);
    EXPECT_EQ(num(f.ctx->toNumber(f.ctx->createNull())), 0.0);
    EXPECT_TRUE(std::isnan(num(f.ctx->toNumber(f.ctx->createUndefined()))));
    EXPECT_TRUE(std::isnan(num(f.ctx->toNumber(f.ctx->createString("infinity")))));
}

TEST(OperatorTest, ToNumberUsesValueOf) {
    Fixture f;
    g_valueOfCalls = 0;
    Value* obj = makeConvertibleObject(f);
    EXPECT_EQ(num(f.ctx->toNumber(obj)), 7.0);
    EXPECT_EQ(g_valueOfCalls, 1);
}

TEST(OperatorTest, ToNumberSymbolThrows) {
    Fixture f;
    Value* r = f.ctx->toNumber(f.ctx->createSymbol("s"));
    EXPECT_TRUE(isErrorValue(r));
}

// —— ToPrimitive ——

TEST(OperatorTest, ToPrimitiveHintOrder) {
    Fixture f;
    Value* obj = makeConvertibleObject(f);

    g_valueOfCalls = 0;
    g_toStringCalls = 0;
    Value* n = f.ctx->toPrimitive(obj, ToPrimitiveHint::Number);
    EXPECT_EQ(num(n), 7.0);
    EXPECT_EQ(g_valueOfCalls, 1);
    EXPECT_EQ(g_toStringCalls, 0);

    g_valueOfCalls = 0;
    g_toStringCalls = 0;
    Value* s = f.ctx->toPrimitive(obj, ToPrimitiveHint::String);
    EXPECT_EQ(text(s), "obj");
    EXPECT_EQ(g_valueOfCalls, 0);
    EXPECT_EQ(g_toStringCalls, 1);

    // Default 与 Number 等价
    g_valueOfCalls = 0;
    g_toStringCalls = 0;
    EXPECT_EQ(num(f.ctx->toPrimitive(obj)), 7.0);
    EXPECT_EQ(g_valueOfCalls, 1);
}

// —— ToPropertyKey ——

TEST(OperatorTest, ToPropertyKey) {
    Fixture f;
    EXPECT_EQ(text(f.ctx->toPropertyKey(f.ctx->createNumber(1))), "1");
    EXPECT_EQ(text(f.ctx->toPropertyKey(f.ctx->createBoolean(true))), "true");

    Value* sym = f.ctx->createSymbol("s");
    EXPECT_EQ(f.ctx->toPropertyKey(sym), sym);
}

// —— 相等 ——

TEST(OperatorTest, StrictEquals) {
    Fixture f;
    auto eq = [&](Value* a, Value* b) {
        return flag(f.ctx->binaryOp(BinaryOperator::StrictEqual, a, b));
    };
    EXPECT_TRUE(eq(f.ctx->createNumber(1), f.ctx->createNumber(1)));
    EXPECT_FALSE(eq(f.ctx->createNumber(1), f.ctx->createString("1")));
    EXPECT_FALSE(eq(f.ctx->createNumber(std::nan("")),
                    f.ctx->createNumber(std::nan(""))));
    EXPECT_TRUE(eq(f.ctx->createNumber(0), f.ctx->createNumber(-0.0)));

    Value* obj = f.ctx->createObject();
    EXPECT_TRUE(eq(obj, obj));
    EXPECT_FALSE(eq(obj, f.ctx->createObject()));
}

TEST(OperatorTest, AbstractEquals) {
    Fixture f;
    auto eq = [&](Value* a, Value* b) {
        return flag(f.ctx->binaryOp(BinaryOperator::AbstractEqual, a, b));
    };
    EXPECT_TRUE(eq(f.ctx->createNull(), f.ctx->createUndefined()));
    EXPECT_TRUE(eq(f.ctx->createNumber(1), f.ctx->createString("1")));
    EXPECT_TRUE(eq(f.ctx->createBoolean(true), f.ctx->createNumber(1)));
    EXPECT_TRUE(eq(f.ctx->createNumber(0), f.ctx->createBoolean(false)));
    EXPECT_TRUE(eq(f.ctx->createString(""), f.ctx->createNumber(0)));
    EXPECT_FALSE(eq(f.ctx->createSymbol("s"), f.ctx->createNumber(1)));

    // 对象与原始值：对象先 ToPrimitive
    Value* obj = makeConvertibleObject(f);
    EXPECT_TRUE(eq(obj, f.ctx->createNumber(7)));
}

// —— 关系 ——

TEST(OperatorTest, Relational) {
    Fixture f;
    auto cmp = [&](BinaryOperator op, Value* a, Value* b) {
        return flag(f.ctx->binaryOp(op, a, b));
    };
    EXPECT_TRUE(cmp(BinaryOperator::LessThan, f.ctx->createNumber(1),
                    f.ctx->createNumber(2)));
    EXPECT_TRUE(cmp(BinaryOperator::GreaterThan, f.ctx->createNumber(2),
                    f.ctx->createNumber(1)));
    EXPECT_TRUE(cmp(BinaryOperator::LessThanOrEqual, f.ctx->createNumber(2),
                    f.ctx->createNumber(2)));
    EXPECT_TRUE(cmp(BinaryOperator::GreaterThanOrEqual, f.ctx->createNumber(2),
                    f.ctx->createNumber(2)));

    // 字符串按字典序，不转数值
    EXPECT_TRUE(cmp(BinaryOperator::LessThan, f.ctx->createString("10"),
                    f.ctx->createString("9")));
    // 混合类型转数值
    EXPECT_TRUE(cmp(BinaryOperator::LessThan, f.ctx->createNumber(1),
                    f.ctx->createString("2")));

    // NaN 参与的关系运算恒 false
    Value* nan = f.ctx->createNumber(std::nan(""));
    EXPECT_FALSE(cmp(BinaryOperator::LessThan, nan, f.ctx->createNumber(1)));
    EXPECT_FALSE(cmp(BinaryOperator::LessThanOrEqual, nan, f.ctx->createNumber(1)));
}

// —— 位运算 ——

TEST(OperatorTest, Bitwise) {
    Fixture f;
    auto op = [&](BinaryOperator o, double a, double b) {
        return num(f.ctx->binaryOp(o, f.ctx->createNumber(a), f.ctx->createNumber(b)));
    };
    EXPECT_EQ(op(BinaryOperator::BitAnd, 5, 3), 1.0);
    EXPECT_EQ(op(BinaryOperator::BitOr, 5, 3), 7.0);
    EXPECT_EQ(op(BinaryOperator::BitXor, 5, 3), 6.0);
    EXPECT_EQ(op(BinaryOperator::ShiftLeft, 1, 31), -2147483648.0);
    EXPECT_EQ(op(BinaryOperator::ShiftLeft, 1, 32), 1.0);  // 移位量 mod 32
    EXPECT_EQ(op(BinaryOperator::ShiftRight, -8, 1), -4.0);
    EXPECT_EQ(op(BinaryOperator::UnsignedShiftRight, -1, 1), 2147483647.0);
}

TEST(OperatorTest, BitNot) {
    Fixture f;
    Value* r = f.ctx->unaryOp(UnaryOperator::BitNot, f.ctx->createNumber(5));
    EXPECT_EQ(num(r), -6.0);
}

// —— 取模/幂 ——

TEST(OperatorTest, ModuloAndExponent) {
    Fixture f;
    auto op = [&](BinaryOperator o, double a, double b) {
        return num(f.ctx->binaryOp(o, f.ctx->createNumber(a), f.ctx->createNumber(b)));
    };
    EXPECT_EQ(op(BinaryOperator::Mod, 5, 2), 1.0);
    EXPECT_EQ(op(BinaryOperator::Mod, -5, 2), -1.0);
    EXPECT_EQ(op(BinaryOperator::Mod, 5, -2), 1.0);
    EXPECT_EQ(op(BinaryOperator::Exp, 2, 3), 8.0);
    EXPECT_EQ(op(BinaryOperator::Exp, 2, -1), 0.5);
}

// —— 一元 ——

TEST(OperatorTest, Unary) {
    Fixture f;
    EXPECT_EQ(num(f.ctx->unaryOp(UnaryOperator::Plus, f.ctx->createString("3"))), 3.0);
    EXPECT_EQ(num(f.ctx->unaryOp(UnaryOperator::Minus, f.ctx->createBoolean(true))), -1.0);
    EXPECT_TRUE(flag(f.ctx->unaryOp(UnaryOperator::LogicalNot, f.ctx->createNumber(0))));
    EXPECT_FALSE(flag(f.ctx->unaryOp(UnaryOperator::LogicalNot, f.ctx->createString("a"))));
    EXPECT_EQ(f.ctx->unaryOp(UnaryOperator::Void, f.ctx->createNumber(1))->type()->kind(),
              TypeKind::Undefined);
}

TEST(OperatorTest, TypeOf) {
    Fixture f;
    auto typeOf = [&](Value* v) {
        return text(f.ctx->unaryOp(UnaryOperator::TypeOf, v));
    };
    EXPECT_EQ(typeOf(f.ctx->createUndefined()), "undefined");
    EXPECT_EQ(typeOf(f.ctx->createNull()), "object");
    EXPECT_EQ(typeOf(f.ctx->createBoolean(true)), "boolean");
    EXPECT_EQ(typeOf(f.ctx->createNumber(1)), "number");
    EXPECT_EQ(typeOf(f.ctx->createString("s")), "string");
    EXPECT_EQ(typeOf(f.ctx->createSymbol("s")), "symbol");
    EXPECT_EQ(typeOf(f.ctx->createObject()), "object");
    EXPECT_EQ(typeOf(f.ctx->createArray()), "object");
    EXPECT_EQ(typeOf(f.ctx->createNativeFunction(valueOfFn, 0)), "function");
}

// —— in ——

TEST(OperatorTest, InOperator) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    f.ctx->defineProperty(obj, "a", f.ctx->createNumber(1));

    EXPECT_TRUE(flag(f.ctx->binaryOp(BinaryOperator::In, f.ctx->createString("a"), obj)));
    EXPECT_FALSE(flag(f.ctx->binaryOp(BinaryOperator::In, f.ctx->createString("b"), obj)));

    Value* err = f.ctx->binaryOp(BinaryOperator::In, f.ctx->createString("a"),
                                 f.ctx->createNumber(1));
    EXPECT_TRUE(isErrorValue(err));
}

// —— 算术委托 ——

TEST(OperatorTest, ArithmeticDelegatesToExistingOps) {
    Fixture f;
    Value* sum = f.ctx->binaryOp(BinaryOperator::Add, f.ctx->createNumber(1),
                                 f.ctx->createNumber(2));
    EXPECT_EQ(num(sum), 3.0);
    Value* cat = f.ctx->binaryOp(BinaryOperator::Add, f.ctx->createString("a"),
                                 f.ctx->createNumber(1));
    EXPECT_EQ(text(cat), "a1");
}

}  // namespace
