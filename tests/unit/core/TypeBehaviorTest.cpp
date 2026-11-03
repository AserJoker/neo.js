// TypeBehaviorTest.cpp - IType 行为方法单元测试
//
// 验证 ADR-016（Type 体系行为方法）+ ADR-017 §4（Fatal 透传）+ ADR-006（异常即值）：
// - 算术运算：add/sub/mul/div（number/string/boolean/null/undefined 间）
// - toString：5 种基础类型
// - Exception/Fatal 透传：左/右操作数透传，Fatal 优先
// - createException/createFatal：错误值创建
//
// 关联 ADR: 016（§4/§5/§6 分派/调换/兜底）、017（透传）、006（异常即值）、030 阶段3

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/Undefined.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

using neo::core::IContext;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::ExceptionData;
using neo::core::FatalData;
using neo::core::NumberData;
using neo::core::StringData;
using neo::infra::Console;
using neo::infra::IAllocator;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

// 取 number Value 的 double 值
double numOf(Value* v) {
    return static_cast<NumberData*>(v->data())->value;
}

// 取 string Value 的内容
const std::string& strOf(Value* v) {
    return static_cast<StringData*>(v->data())->value;
}

}  // namespace

// ============ 算术运算：add ============

TEST(TypeBehaviorTest, NumberAddNumber) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* a = ctx->createNumber(1.0);
    auto* b = ctx->createNumber(2.0);
    auto* r = ctx->add(a, b);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(r), 3.0);
}

TEST(TypeBehaviorTest, NumberAddStringConcatenates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* a = ctx->createNumber(1.0);
    auto* b = ctx->createString("2");
    auto* r = ctx->add(a, b);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(r), "12");
}

TEST(TypeBehaviorTest, StringAddStringConcatenates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* a = ctx->createString("a");
    auto* b = ctx->createString("b");
    auto* r = ctx->add(a, b);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(r), "ab");
}

TEST(TypeBehaviorTest, StringAddNumberConcatenates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* a = ctx->createString("a");
    auto* b = ctx->createNumber(1.0);
    auto* r = ctx->add(a, b);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(r), "a1");
}

// ============ 算术运算：sub ============

TEST(TypeBehaviorTest, NumberSubNumber) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createNumber(5.0), ctx->createNumber(3.0));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(numOf(r), 2.0);
}

TEST(TypeBehaviorTest, NumberSubStringToNumber) {
    // 5 - "3" = 2（string 不主导 sub，ToNumber("3")=3）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createNumber(5.0), ctx->createString("3"));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(numOf(r), 2.0);
}

TEST(TypeBehaviorTest, StringSubNumber) {
    // "5" - 3 = 2（string 左操作数 sub → ToNumber("5")=5）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("5"), ctx->createNumber(3.0));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(numOf(r), 2.0);
}

TEST(TypeBehaviorTest, StringSubNonNumericIsNaN) {
    // "a" - 1 = NaN（ToNumber("a")=NaN）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("a"), ctx->createNumber(1.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isnan(numOf(r)));
}

// —— toNumber(string) 对 Infinity/NaN 字面量的精确大小写匹配 ——
// JS 只认 "Infinity"/"+Infinity"/"-Infinity" 精确大小写，
// "infinity"/"INF"/"Inf" 等应返回 NaN（strtod 大小写不敏感，需特判拦截）。

TEST(TypeBehaviorTest, StringInfinityLiteralToInfinity) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // "Infinity" - 0 = Infinity（ToNumber("Infinity")=Infinity）
    auto* r = ctx->sub(ctx->createString("Infinity"), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isinf(numOf(r)));
    EXPECT_GT(numOf(r), 0.0);
}

TEST(TypeBehaviorTest, StringPlusInfinityLiteralToInfinity) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("+Infinity"), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isinf(numOf(r)));
    EXPECT_GT(numOf(r), 0.0);
}

TEST(TypeBehaviorTest, StringMinusInfinityLiteralToMinusInfinity) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("-Infinity"), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isinf(numOf(r)));
    EXPECT_LT(numOf(r), 0.0);
}

TEST(TypeBehaviorTest, StringLowercaseInfinityIsNaN) {
    // "infinity" - 0 = NaN（JS 不认小写，strtod 会误判，须拦截）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("infinity"), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isnan(numOf(r)));
}

TEST(TypeBehaviorTest, StringUppercaseINFIsNaN) {
    // "INF" - 0 = NaN（JS 不认大写缩写）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("INF"), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isnan(numOf(r)));
}

TEST(TypeBehaviorTest, StringMixedCaseInfIsNaN) {
    // "Inf" - 0 = NaN
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("Inf"), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isnan(numOf(r)));
}

TEST(TypeBehaviorTest, StringOverflowExponentToInfinity) {
    // "1e309" - 0 = Infinity（超出 double 上限，JS 与 strtod 一致）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("1e309"), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isinf(numOf(r)));
    EXPECT_GT(numOf(r), 0.0);
}

TEST(TypeBehaviorTest, StringInfinityWithTrailingSpacesToInfinity) {
    // "  Infinity  " - 0 = Infinity（trim 空白后精确匹配）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("  Infinity  "), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isinf(numOf(r)));
    EXPECT_GT(numOf(r), 0.0);
}

// ============ 算术运算：mul / div ============

TEST(TypeBehaviorTest, NumberMulNumber) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->mul(ctx->createNumber(2.0), ctx->createNumber(3.0));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(numOf(r), 6.0);
}

TEST(TypeBehaviorTest, StringMulNumber) {
    // "2" * 3 = 6（ToNumber("2")=2）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->mul(ctx->createString("2"), ctx->createNumber(3.0));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(numOf(r), 6.0);
}

TEST(TypeBehaviorTest, NumberDivNumber) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->div(ctx->createNumber(6.0), ctx->createNumber(2.0));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(numOf(r), 3.0);
}

TEST(TypeBehaviorTest, DivByZeroIsInfinity) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->div(ctx->createNumber(1.0), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isinf(numOf(r)));
    EXPECT_GT(numOf(r), 0.0);
}

TEST(TypeBehaviorTest, ZeroDivZeroIsNaN) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->div(ctx->createNumber(0.0), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isnan(numOf(r)));
}

// ============ boolean / null / undefined 运算 ============

TEST(TypeBehaviorTest, BooleanAddNumber) {
    // true + 1 = 2（ToNumber(true)=1）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->add(ctx->createBoolean(true), ctx->createNumber(1.0));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(numOf(r), 2.0);
}

TEST(TypeBehaviorTest, BooleanAddStringConcatenates) {
    // true + "" = "true"（boolean + string → string 主导 concat）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->add(ctx->createBoolean(true), ctx->createString(""));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(r), "true");
}

TEST(TypeBehaviorTest, NullAddNumber) {
    // null + 1 = 1（ToNumber(null)=0）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->add(ctx->createNull(), ctx->createNumber(1.0));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(numOf(r), 1.0);
}

TEST(TypeBehaviorTest, UndefinedAddNumberIsNaN) {
    // undefined + 1 = NaN（ToNumber(undefined)=NaN）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->add(ctx->createUndefined(), ctx->createNumber(1.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isnan(numOf(r)));
}

TEST(TypeBehaviorTest, NullAddNullIsZero) {
    // null + null = 0
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->add(ctx->createNull(), ctx->createNull());
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(numOf(r), 0.0);
}

TEST(TypeBehaviorTest, UndefinedAddUndefinedIsNaN) {
    // undefined + undefined = NaN
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->add(ctx->createUndefined(), ctx->createUndefined());
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isnan(numOf(r)));
}

// ============ toString ============

TEST(TypeBehaviorTest, ToStringNumber) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r1 = ctx->toString(ctx->createNumber(42.0));
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(strOf(r1), "42");

    auto* r2 = ctx->toString(ctx->createNumber(3.14));
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(strOf(r2), "3.14");
}

TEST(TypeBehaviorTest, ToStringNumberSpecial) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r1 = ctx->toString(ctx->createNumber(std::nan("")));
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(strOf(r1), "NaN");

    auto* r2 = ctx->toString(ctx->createNumber(std::numeric_limits<double>::infinity()));
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(strOf(r2), "Infinity");
}

TEST(TypeBehaviorTest, ToStringBoolean) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(strOf(ctx->toString(ctx->createBoolean(true))), "true");
    EXPECT_EQ(strOf(ctx->toString(ctx->createBoolean(false))), "false");
}

TEST(TypeBehaviorTest, ToStringUndefinedAndNull) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(strOf(ctx->toString(ctx->createUndefined())), "undefined");
    EXPECT_EQ(strOf(ctx->toString(ctx->createNull())), "null");
}

TEST(TypeBehaviorTest, ToStringStringIsSelf) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->toString(ctx->createString("hello"));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(strOf(r), "hello");
}

// ============ Exception/Fatal 透传 ============

TEST(TypeBehaviorTest, ExceptionLeftOperandPassthrough) {
    // Exception + 1 = Exception（左操作数透传）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* exc = ctx->createException(ctx->createUndefined());
    auto* one = ctx->createNumber(1.0);
    auto* r = ctx->add(exc, one);
    EXPECT_EQ(r, exc);  // 透传同一个 Exception Value
}

TEST(TypeBehaviorTest, ExceptionRightOperandPassthrough) {
    // 1 + Exception = Exception（右操作数透传）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* exc = ctx->createException(ctx->createUndefined());
    auto* one = ctx->createNumber(1.0);
    auto* r = ctx->add(one, exc);
    EXPECT_EQ(r, exc);
}

TEST(TypeBehaviorTest, FatalLeftOperandPassthrough) {
    // Fatal + 1 = Fatal
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* fatal = ctx->createFatal("OOM");
    auto* one = ctx->createNumber(1.0);
    auto* r = ctx->add(fatal, one);
    EXPECT_EQ(r, fatal);
}

TEST(TypeBehaviorTest, FatalRightOperandPassthrough) {
    // 1 + Fatal = Fatal
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* fatal = ctx->createFatal("OOM");
    auto* one = ctx->createNumber(1.0);
    auto* r = ctx->add(one, fatal);
    EXPECT_EQ(r, fatal);
}

TEST(TypeBehaviorTest, FatalPrecedenceOverException) {
    // Fatal + Exception = Fatal（Fatal 优先）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* fatal = ctx->createFatal("OOM");
    auto* exc = ctx->createException(ctx->createUndefined());
    auto* r = ctx->add(fatal, exc);
    EXPECT_EQ(r, fatal);
}

TEST(TypeBehaviorTest, ExceptionPrecedenceWhenNoFatal) {
    // Exception + Fatal = Fatal（Fatal 优先，右操作数 Fatal 透传）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* exc = ctx->createException(ctx->createUndefined());
    auto* fatal = ctx->createFatal("OOM");
    auto* r = ctx->add(exc, fatal);
    EXPECT_EQ(r, fatal);
}

TEST(TypeBehaviorTest, ExceptionToStringPassthrough) {
    // toString(Exception) = Exception（透传）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* exc = ctx->createException(ctx->createUndefined());
    auto* r = ctx->toString(exc);
    EXPECT_EQ(r, exc);
}

TEST(TypeBehaviorTest, FatalToStringPassthrough) {
    // toString(Fatal) = Fatal（透传）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* fatal = ctx->createFatal("OOM");
    auto* r = ctx->toString(fatal);
    EXPECT_EQ(r, fatal);
}

// ============ createException / createFatal ============

TEST(TypeBehaviorTest, CreateExceptionHasExceptionType) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* child = ctx->createUndefined();
    auto* exc = ctx->createException(child);
    ASSERT_NE(exc, nullptr);
    EXPECT_EQ(exc->type()->kind(), TypeKind::Exception);
}

TEST(TypeBehaviorTest, CreateFatalHasFatalType) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* fatal = ctx->createFatal("OOM");
    ASSERT_NE(fatal, nullptr);
    EXPECT_EQ(fatal->type()->kind(), TypeKind::Fatal);
    EXPECT_STREQ(static_cast<FatalData*>(fatal->data())->message, "OOM");
}

// ============ sub/mul/div 各类型全覆盖（补覆盖率缺口） ============

TEST(TypeBehaviorTest, NullSubMulDivNumber) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // null - 1 = -1（ToNumber(null)=0）
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createNull(), ctx->createNumber(1.0))), -1.0);
    // null * 5 = 0
    EXPECT_DOUBLE_EQ(numOf(ctx->mul(ctx->createNull(), ctx->createNumber(5.0))), 0.0);
    // null / 2 = 0
    EXPECT_DOUBLE_EQ(numOf(ctx->div(ctx->createNull(), ctx->createNumber(2.0))), 0.0);
}

TEST(TypeBehaviorTest, UndefinedSubMulDivNumberAreNaN) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_TRUE(std::isnan(numOf(ctx->sub(ctx->createUndefined(), ctx->createNumber(1.0)))));
    EXPECT_TRUE(std::isnan(numOf(ctx->mul(ctx->createUndefined(), ctx->createNumber(5.0)))));
    EXPECT_TRUE(std::isnan(numOf(ctx->div(ctx->createUndefined(), ctx->createNumber(2.0)))));
}

TEST(TypeBehaviorTest, BooleanSubMulDivNumber) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // true - 1 = 0, true * 3 = 3, true / 2 = 0.5
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createBoolean(true), ctx->createNumber(1.0))), 0.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->mul(ctx->createBoolean(true), ctx->createNumber(3.0))), 3.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->div(ctx->createBoolean(true), ctx->createNumber(2.0))), 0.5);
    // false - 0 = 0, false * 5 = 0, false / 1 = 0
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createBoolean(false), ctx->createNumber(0.0))), 0.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->mul(ctx->createBoolean(false), ctx->createNumber(5.0))), 0.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->div(ctx->createBoolean(false), ctx->createNumber(1.0))), 0.0);
}

TEST(TypeBehaviorTest, StringSubMulDivNumber) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // "10" - 3 = 7, "10" * 2 = 20, "10" / 4 = 2.5
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString("10"), ctx->createNumber(3.0))), 7.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->mul(ctx->createString("10"), ctx->createNumber(2.0))), 20.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->div(ctx->createString("10"), ctx->createNumber(4.0))), 2.5);
}

TEST(TypeBehaviorTest, StringDivByZeroIsInfinity) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->div(ctx->createString("5"), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isinf(numOf(r)));
}

TEST(TypeBehaviorTest, BooleanAddStringConcatenatesFalse) {
    // false + "x" = "falsex"（boolean + string → concat 保持顺序）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->add(ctx->createBoolean(false), ctx->createString("x"));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(strOf(r), "falsex");
}

TEST(TypeBehaviorTest, NullAddStringConcatenates) {
    // null + "x" = "nullx"
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->add(ctx->createNull(), ctx->createString("x"));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(strOf(r), "nullx");
}

TEST(TypeBehaviorTest, UndefinedAddStringConcatenates) {
    // undefined + "x" = "undefinedx"
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->add(ctx->createUndefined(), ctx->createString("x"));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(strOf(r), "undefinedx");
}

// ============ Exception/Fatal sub/mul/div 透传 ============

TEST(TypeBehaviorTest, ExceptionSubMulDivPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* exc = ctx->createException(ctx->createUndefined());
    auto* one = ctx->createNumber(1.0);
    EXPECT_EQ(ctx->sub(exc, one), exc);
    EXPECT_EQ(ctx->mul(exc, one), exc);
    EXPECT_EQ(ctx->div(exc, one), exc);
}

TEST(TypeBehaviorTest, FatalSubMulDivPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* fatal = ctx->createFatal("OOM");
    auto* one = ctx->createNumber(1.0);
    EXPECT_EQ(ctx->sub(fatal, one), fatal);
    EXPECT_EQ(ctx->mul(fatal, one), fatal);
    EXPECT_EQ(ctx->div(fatal, one), fatal);
}

TEST(TypeBehaviorTest, ExceptionRightOperandSubMulDivPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* exc = ctx->createException(ctx->createUndefined());
    auto* one = ctx->createNumber(1.0);
    EXPECT_EQ(ctx->sub(one, exc), exc);
    EXPECT_EQ(ctx->mul(one, exc), exc);
    EXPECT_EQ(ctx->div(one, exc), exc);
}

TEST(TypeBehaviorTest, FatalRightOperandSubMulDivPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* fatal = ctx->createFatal("OOM");
    auto* one = ctx->createNumber(1.0);
    EXPECT_EQ(ctx->sub(one, fatal), fatal);
    EXPECT_EQ(ctx->mul(one, fatal), fatal);
    EXPECT_EQ(ctx->div(one, fatal), fatal);
}

TEST(TypeBehaviorTest, ExceptionSubMulDivWithFatalRightPrioritizesFatal) {
    // Exception - Fatal = Fatal（Fatal 优先）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* exc = ctx->createException(ctx->createUndefined());
    auto* fatal = ctx->createFatal("OOM");
    EXPECT_EQ(ctx->sub(exc, fatal), fatal);
    EXPECT_EQ(ctx->mul(exc, fatal), fatal);
    EXPECT_EQ(ctx->div(exc, fatal), fatal);
}

// ============ numberToString 小数裁剪分支 ============

TEST(TypeBehaviorTest, ToStringNumberDecimalTrimZeros) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 3.14 → "3.14"（小数保留有效位）
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(3.14))), "3.14");
    // 3.10 → "3.1"（裁剪 trailing zero）
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(3.10))), "3.1");
    // 3.0 → "3"（整数化的 double）
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(3.0))), "3");
    // -3.14 → "-3.14"
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(-3.14))), "-3.14");
}

TEST(TypeBehaviorTest, ToStringNumberMinusInfinity) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->toString(ctx->createNumber(-std::numeric_limits<double>::infinity()));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(strOf(r), "-Infinity");
}

TEST(TypeBehaviorTest, ToStringNumberZero) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(0.0))), "0");
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(-0.0))), "0");
}

// ============ toNumber 边界分支 ============

TEST(TypeBehaviorTest, NumberAddBoolean) {
    // 1 + true = 2, 1 + false = 1（ToNumber(boolean)）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createNumber(1.0), ctx->createBoolean(true))), 2.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createNumber(1.0), ctx->createBoolean(false))), 1.0);
}

TEST(TypeBehaviorTest, NumberSubNull) {
    // 5 - null = 5（ToNumber(null)=0）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createNumber(5.0), ctx->createNull())), 5.0);
}

TEST(TypeBehaviorTest, NumberSubUndefinedIsNaN) {
    // 5 - undefined = NaN（ToNumber(undefined)=NaN）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_TRUE(std::isnan(numOf(ctx->sub(ctx->createNumber(5.0), ctx->createUndefined()))));
}

TEST(TypeBehaviorTest, StringEmptyToNumberIsZero) {
    // "" - 0 = 0（ToNumber("")=0）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString(""), ctx->createNumber(0.0))), 0.0);
}

TEST(TypeBehaviorTest, StringWhitespaceToNumberIsZero) {
    // "   " - 0 = 0（全空白 → 0）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString("   "), ctx->createNumber(0.0))), 0.0);
}

TEST(TypeBehaviorTest, StringNumericWithSpacesToNumber) {
    // "  42  " - 0 = 42（前后空白 trim）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString("  42  "), ctx->createNumber(0.0))), 42.0);
}

TEST(TypeBehaviorTest, StringFloatToNumber) {
    // "3.14" * 1 = 3.14
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->mul(ctx->createString("3.14"), ctx->createNumber(1.0))), 3.14);
}

TEST(TypeBehaviorTest, StringPartialParseIsNaN) {
    // "42x" - 0 = NaN（未完全消费）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_TRUE(std::isnan(numOf(ctx->sub(ctx->createString("42x"), ctx->createNumber(0.0)))));
}

TEST(TypeBehaviorTest, NumberMulStringToNumber) {
    // 3 * "4" = 12（ToNumber("4")=4）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->mul(ctx->createNumber(3.0), ctx->createString("4"))), 12.0);
}

TEST(TypeBehaviorTest, NumberDivStringToNumber) {
    // 12 / "4" = 3
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->div(ctx->createNumber(12.0), ctx->createString("4"))), 3.0);
}

TEST(TypeBehaviorTest, NumberAddNull) {
    // 1 + null = 1（ToNumber(null)=0）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createNumber(1.0), ctx->createNull())), 1.0);
}

TEST(TypeBehaviorTest, NumberAddUndefinedIsNaN) {
    // 1 + undefined = NaN
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_TRUE(std::isnan(numOf(ctx->add(ctx->createNumber(1.0), ctx->createUndefined()))));
}

TEST(TypeBehaviorTest, NumberAddNumberProducesNumber) {
    // 覆盖 NumberType::add 的数值加法分支（非 string）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createNumber(2.5), ctx->createNumber(3.5))), 6.0);
    EXPECT_TRUE(std::isnan(numOf(ctx->add(ctx->createNumber(1.0), ctx->createNumber(std::nan(""))))));
    EXPECT_TRUE(std::isinf(numOf(ctx->add(ctx->createNumber(std::numeric_limits<double>::infinity()),
                                              ctx->createNumber(1.0)))));
}

// ============ toNumber(string) 空白字符与符号边界分支 ============

TEST(TypeBehaviorTest, StringTabNewlineWhitespaceToNumber) {
    // "\t\n42\r" - 0 = 42（tab/newline/CR 空白 trim）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString("\t\n42\r"), ctx->createNumber(0.0))), 42.0);
}

TEST(TypeBehaviorTest, StringTrailingWhitespaceAfterNumber) {
    // "42 \t" - 0 = 42（尾部空白跳过分支）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString("42 \t"), ctx->createNumber(0.0))), 42.0);
}

TEST(TypeBehaviorTest, StringPlusSignOverflowToInfinity) {
    // "+1e309" - 0 = Infinity（+ 符号后数字溢出 → 合法 inf）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("+1e309"), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isinf(numOf(r)));
    EXPECT_GT(numOf(r), 0.0);
}

TEST(TypeBehaviorTest, StringMinusSignOverflowToMinusInfinity) {
    // "-1e309" - 0 = -Infinity（- 符号后数字溢出 → 合法 -inf）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("-1e309"), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isinf(numOf(r)));
    EXPECT_LT(numOf(r), 0.0);
}

TEST(TypeBehaviorTest, StringPlusInfinityWithSpacesToInfinity) {
    // "  +Infinity  " - 0 = Infinity
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("  +Infinity  "), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isinf(numOf(r)));
    EXPECT_GT(numOf(r), 0.0);
}

TEST(TypeBehaviorTest, StringMinusInfinityWithSpacesToMinusInfinity) {
    // "  -Infinity  " - 0 = -Infinity
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* r = ctx->sub(ctx->createString("  -Infinity  "), ctx->createNumber(0.0));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(std::isinf(numOf(r)));
    EXPECT_LT(numOf(r), 0.0);
}

// ============ numberToString 整数边界 ============

TEST(TypeBehaviorTest, ToStringNumberLargeInteger) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 大整数（< 1e16 走 long long 分支）
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(1234567890.0))), "1234567890");
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(-42.0))), "-42");
}

TEST(TypeBehaviorTest, ToStringNumberSmallDecimal) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 小数裁剪 trailing zeros 分支
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(1.5))), "1.5");
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(0.25))), "0.25");
}

TEST(TypeBehaviorTest, ToStringNumberHugeIntegerFallsToDecimalPath) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 1e16：floor==n 但 abs(n) < 1e16 为 false → 落到 to_string(double) 小数路径
    // std::to_string(1e16) = "10000000000000000.000000" → 裁剪 trailing zeros
    //   find_last_not_of('0') 命中 '.' → erase → "10000000000000000"（覆盖 line 109 true）
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(1e16))), "10000000000000000");
}

// ============ valueToString 各类型（通过 concat 间接覆盖） ============

TEST(TypeBehaviorTest, StringConcatWithAllTypes) {
    // "" + undefined = "undefined", "" + null = "null", "" + true = "true"
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(strOf(ctx->add(ctx->createString(""), ctx->createUndefined())), "undefined");
    EXPECT_EQ(strOf(ctx->add(ctx->createString(""), ctx->createNull())), "null");
    EXPECT_EQ(strOf(ctx->add(ctx->createString(""), ctx->createBoolean(true))), "true");
    EXPECT_EQ(strOf(ctx->add(ctx->createString(""), ctx->createBoolean(false))), "false");
    EXPECT_EQ(strOf(ctx->add(ctx->createString(""), ctx->createNumber(42.0))), "42");
}

// ============ 各 Type 左操作数遇 Fatal/Exception 的 passthrough 分支 ============
// 覆盖 Null/Undefined/Boolean/String 的 sub/mul/div 入口 passthrough(a,b) 4 个 if 分支

TEST(TypeBehaviorTest, NullLeftWithFatalExceptionPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* n = ctx->createNull();
    auto* fatal = ctx->createFatal("OOM");
    auto* exc = ctx->createException(ctx->createUndefined());
    // Null + Fatal = Fatal（a=Null, b=Fatal → passthrough 返回 b=Fatal）
    EXPECT_EQ(ctx->add(n, fatal), fatal);
    // Null + Exception = Exception
    EXPECT_EQ(ctx->add(n, exc), exc);
    // Null sub/mul/div Fatal/Exception
    EXPECT_EQ(ctx->sub(n, fatal), fatal);
    EXPECT_EQ(ctx->sub(n, exc), exc);
    EXPECT_EQ(ctx->mul(n, fatal), fatal);
    EXPECT_EQ(ctx->mul(n, exc), exc);
    EXPECT_EQ(ctx->div(n, fatal), fatal);
    EXPECT_EQ(ctx->div(n, exc), exc);
    // 反向：Fatal + Null = Fatal（a=Fatal → passthrough 返回 a）
    EXPECT_EQ(ctx->add(fatal, n), fatal);
    EXPECT_EQ(ctx->add(exc, n), exc);
}

TEST(TypeBehaviorTest, UndefinedLeftWithFatalExceptionPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* u = ctx->createUndefined();
    auto* fatal = ctx->createFatal("OOM");
    auto* exc = ctx->createException(ctx->createUndefined());
    EXPECT_EQ(ctx->add(u, fatal), fatal);
    EXPECT_EQ(ctx->add(u, exc), exc);
    EXPECT_EQ(ctx->sub(u, fatal), fatal);
    EXPECT_EQ(ctx->sub(u, exc), exc);
    EXPECT_EQ(ctx->mul(u, fatal), fatal);
    EXPECT_EQ(ctx->mul(u, exc), exc);
    EXPECT_EQ(ctx->div(u, fatal), fatal);
    EXPECT_EQ(ctx->div(u, exc), exc);
    EXPECT_EQ(ctx->add(fatal, u), fatal);
    EXPECT_EQ(ctx->add(exc, u), exc);
}

TEST(TypeBehaviorTest, BooleanLeftWithFatalExceptionPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* b = ctx->createBoolean(true);
    auto* fatal = ctx->createFatal("OOM");
    auto* exc = ctx->createException(ctx->createUndefined());
    EXPECT_EQ(ctx->add(b, fatal), fatal);
    EXPECT_EQ(ctx->add(b, exc), exc);
    EXPECT_EQ(ctx->sub(b, fatal), fatal);
    EXPECT_EQ(ctx->sub(b, exc), exc);
    EXPECT_EQ(ctx->mul(b, fatal), fatal);
    EXPECT_EQ(ctx->mul(b, exc), exc);
    EXPECT_EQ(ctx->div(b, fatal), fatal);
    EXPECT_EQ(ctx->div(b, exc), exc);
    EXPECT_EQ(ctx->add(fatal, b), fatal);
    EXPECT_EQ(ctx->add(exc, b), exc);
}

TEST(TypeBehaviorTest, StringLeftWithFatalExceptionPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* s = ctx->createString("x");
    auto* fatal = ctx->createFatal("OOM");
    auto* exc = ctx->createException(ctx->createUndefined());
    EXPECT_EQ(ctx->add(s, fatal), fatal);
    EXPECT_EQ(ctx->add(s, exc), exc);
    EXPECT_EQ(ctx->sub(s, fatal), fatal);
    EXPECT_EQ(ctx->sub(s, exc), exc);
    EXPECT_EQ(ctx->mul(s, fatal), fatal);
    EXPECT_EQ(ctx->mul(s, exc), exc);
    EXPECT_EQ(ctx->div(s, fatal), fatal);
    EXPECT_EQ(ctx->div(s, exc), exc);
    EXPECT_EQ(ctx->add(fatal, s), fatal);
    EXPECT_EQ(ctx->add(exc, s), exc);
}

TEST(TypeBehaviorTest, NumberLeftWithFatalExceptionPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* n = ctx->createNumber(1.0);
    auto* fatal = ctx->createFatal("OOM");
    auto* exc = ctx->createException(ctx->createUndefined());
    EXPECT_EQ(ctx->add(n, fatal), fatal);
    EXPECT_EQ(ctx->add(n, exc), exc);
    EXPECT_EQ(ctx->sub(n, fatal), fatal);
    EXPECT_EQ(ctx->sub(n, exc), exc);
    EXPECT_EQ(ctx->mul(n, fatal), fatal);
    EXPECT_EQ(ctx->mul(n, exc), exc);
    EXPECT_EQ(ctx->div(n, fatal), fatal);
    EXPECT_EQ(ctx->div(n, exc), exc);
    EXPECT_EQ(ctx->add(fatal, n), fatal);
    EXPECT_EQ(ctx->add(exc, n), exc);
}

// toString 对 Fatal/Exception 的单目透传分支
TEST(TypeBehaviorTest, ToStringExceptionAllTypesPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* exc = ctx->createException(ctx->createUndefined());
    // Exception.toString() 透传（passthrough(self) 单目版）
    EXPECT_EQ(ctx->toString(exc), exc);
}

// ============ toNumber(string) 空白字符短路分支补全 ============

TEST(TypeBehaviorTest, StringLeadingCRWhitespaceToNumber) {
    // "\r42" - 0 = 42（前导 CR 空白 trim，覆盖 line 45 的 \r 分支）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString("\r42"), ctx->createNumber(0.0))), 42.0);
}

TEST(TypeBehaviorTest, StringTrailingCRWhitespaceToNumber) {
    // "42\r" - 0 = 42（尾部 CR 空白 trim，覆盖 line 49 的 \r 分支）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString("42\r"), ctx->createNumber(0.0))), 42.0);
}

TEST(TypeBehaviorTest, StringTrailingNewlineWhitespaceToNumber) {
    // "42\n" - 0 = 42（尾部 newline 空白 trim）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString("42\n"), ctx->createNumber(0.0))), 42.0);
}

TEST(TypeBehaviorTest, StringStrtodTrailingTabIsFullyConsumed) {
    // "42\t" - 0 = 42：strtod 在 "42" 停止，end 指向 '\t'，
    // line 72-73 while 跳过 '\t' 后 *end=='\0' → 完全消费 → 42
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString("42\t"), ctx->createNumber(0.0))), 42.0);
}

TEST(TypeBehaviorTest, StringStrtodTrailingNewlineAndCR) {
    // "42\n\r" - 0 = 42：strtod 后尾部 \n\r 跳过（覆盖 line 73 的 \n/\r 分支）
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString("42\n\r"), ctx->createNumber(0.0))), 42.0);
}

TEST(TypeBehaviorTest, StringNineCharNonInfinityToNumber) {
    // "123456789"（9 字符数字，len==9 但首字符非 +/-）：
    //   line 56 `s[i]=='+'` false、line 59 `s[i]=='-'` false → 走 strtod
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createString("123456789"), ctx->createNumber(0.0))), 123456789.0);
}

TEST(TypeBehaviorTest, StringNineCharPlusNonInfinityIsNaN) {
    // "+nnnnnnnn"（len==9，首字符 '+' 但后续非 "Infinity"）：
    //   line 56 `s[i]=='+'` true 但 compare != 0 → false → 走 strtod → 未完全消费 → NaN
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    EXPECT_TRUE(std::isnan(numOf(ctx->sub(ctx->createString("+nnnnnnnn"), ctx->createNumber(0.0)))));
}

TEST(TypeBehaviorTest, StringNineCharMinusNonInfinityIsNaN) {
    // "-nnnnnnnn"（len==9，首字符 '-' 但后续非 "Infinity"）：
    //   line 59 `s[i]=='-'` true 但 compare != 0 → false → 走 strtod → NaN
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    EXPECT_TRUE(std::isnan(numOf(ctx->sub(ctx->createString("-nnnnnnnn"), ctx->createNumber(0.0)))));
}
