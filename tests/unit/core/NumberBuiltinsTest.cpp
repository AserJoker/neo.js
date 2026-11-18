// NumberBuiltinsTest.cpp - Number 静态成员与数值格式化
//
// 验证（ES2024 §21.1.2 / §21.1.3）：
// - isFinite/isNaN/isInteger/isSafeInteger 与常量
// - toFixed/toExponential/toPrecision 的精确十进制舍入
// - toString(radix) 的整数与小数换基

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
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

using neo::core::BooleanData;
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

struct Fixture {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    NullSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx = createContext(alloc.get(), &console);

    Value* num(double n) { return ctx->createNumber(n); }

    Value* numberCtor() { return ctx->global()->lookup("Number"); }

    Value* statik(const char* name) { return ctx->getProperty(numberCtor(), name); }

    Value* call(Value* self, const char* name, size_t argc = 0, Value* argv[] = nullptr) {
        Value* fn = ctx->getProperty(self, name);
        return ctx->call(fn, self, argc, argv);
    }

    Value* call1(Value* self, const char* name, Value* a) {
        Value* args[1] = {a};
        return call(self, name, 1, args);
    }

    Value* call2(Value* self, const char* name, Value* a, Value* b) {
        Value* args[2] = {a, b};
        return call(self, name, 2, args);
    }

    Value* callStatic(const char* name, Value* a) {
        Value* args[1] = {a};
        return ctx->call(statik(name), ctx->createUndefined(), 1, args);
    }
};

bool isError(Value* v) {
    return v != nullptr && (v->type()->kind() == TypeKind::Exception ||
                            v->type()->kind() == TypeKind::Fatal);
}

std::string strOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::String
               ? static_cast<StringData*>(v->data())->value
               : "<not string>";
}

double numOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Number
               ? static_cast<NumberData*>(v->data())->value
               : -999.0;
}

bool boolOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Boolean &&
           static_cast<BooleanData*>(v->data())->value;
}

}  // namespace

TEST(NumberBuiltinsTest, StaticPredicates) {
    Fixture f;
    EXPECT_TRUE(boolOf(f.callStatic("isInteger", f.num(3.0))));
    EXPECT_FALSE(boolOf(f.callStatic("isInteger", f.num(3.5))));
    EXPECT_FALSE(boolOf(f.callStatic("isInteger", f.ctx->createString("3"))));

    EXPECT_TRUE(boolOf(f.callStatic("isSafeInteger", f.num(9007199254740991.0))));
    EXPECT_FALSE(boolOf(f.callStatic("isSafeInteger", f.num(9007199254740992.0))));

    EXPECT_TRUE(boolOf(f.callStatic("isNaN", f.num(std::nan("")))));
    EXPECT_FALSE(boolOf(f.callStatic("isNaN", f.ctx->createString("NaN"))));

    EXPECT_TRUE(boolOf(f.callStatic("isFinite", f.num(1.0))));
    EXPECT_FALSE(boolOf(f.callStatic("isFinite", f.num(std::nan("")))));
}

TEST(NumberBuiltinsTest, Constants) {
    Fixture f;
    EXPECT_EQ(numOf(f.statik("MAX_SAFE_INTEGER")), 9007199254740991.0);
    EXPECT_EQ(numOf(f.statik("MIN_SAFE_INTEGER")), -9007199254740991.0);
    EXPECT_EQ(numOf(f.statik("EPSILON")), 2.220446049250313e-16);
    EXPECT_TRUE(std::isinf(numOf(f.statik("POSITIVE_INFINITY"))));
    EXPECT_TRUE(std::isnan(numOf(f.statik("NaN"))));
}

TEST(NumberBuiltinsTest, ToFixed) {
    Fixture f;
    EXPECT_EQ(strOf(f.call1(f.num(123.456), "toFixed", f.num(2))), "123.46");
    EXPECT_EQ(strOf(f.call1(f.num(1.005), "toFixed", f.num(2))), "1.00");
    EXPECT_EQ(strOf(f.call1(f.num(2.5), "toFixed", f.num(0))), "3");
    EXPECT_EQ(strOf(f.call1(f.num(0.05), "toFixed", f.num(1))), "0.1");
    EXPECT_EQ(strOf(f.call1(f.num(-1.5), "toFixed", f.num(0))), "-2");
    EXPECT_EQ(strOf(f.call1(f.num(0.0), "toFixed", f.num(2))), "0.00");
    EXPECT_TRUE(isError(f.call1(f.num(1.0), "toFixed", f.num(101))));
}

TEST(NumberBuiltinsTest, ToExponentialAndPrecision) {
    Fixture f;
    EXPECT_EQ(strOf(f.call1(f.num(123.456), "toExponential", f.num(2))), "1.23e+2");
    EXPECT_EQ(strOf(f.call1(f.num(0.0), "toExponential", f.num(2))), "0.00e+0");
    EXPECT_EQ(strOf(f.call(f.num(123.456), "toExponential")), "1.23456e+2");
    EXPECT_EQ(strOf(f.call1(f.num(123.456), "toPrecision", f.num(4))), "123.5");
    EXPECT_EQ(strOf(f.call1(f.num(0.000123), "toPrecision", f.num(2))), "0.00012");
    EXPECT_EQ(strOf(f.call1(f.num(123456.0), "toPrecision", f.num(3))), "1.23e+5");
    EXPECT_TRUE(isError(f.call1(f.num(1.0), "toPrecision", f.num(0))));
}

TEST(NumberBuiltinsTest, ToStringRadix) {
    Fixture f;
    EXPECT_EQ(strOf(f.call1(f.num(255.0), "toString", f.num(16))), "ff");
    EXPECT_EQ(strOf(f.call1(f.num(-255.0), "toString", f.num(16))), "-ff");
    EXPECT_EQ(strOf(f.call1(f.num(10.0), "toString", f.num(2))), "1010");
    EXPECT_EQ(strOf(f.call1(f.num(0.5), "toString", f.num(2))), "0.1");
    EXPECT_EQ(strOf(f.call(f.num(42.0), "toString")), "42");
    EXPECT_TRUE(isError(f.call1(f.num(1.0), "toString", f.num(1))));
    EXPECT_TRUE(isError(f.call1(f.num(1.0), "toString", f.num(37))));
}
