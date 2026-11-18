// GlobalFunctionTest.cpp - 全局函数单元测试（ES2024 §19.2）
//
// 验证：parseInt/parseFloat 文法、isNaN/isFinite、URI 编解码与畸形输入。

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
#include "core/types/String.h"
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

    Value* call(const char* name, size_t argc, Value* argv[]) {
        Value* fn = ctx->global()->lookup(name);
        return ctx->call(fn, ctx->createUndefined(), argc, argv);
    }

    Value* callStr(const char* name, const char* text) {
        Value* args[1] = {ctx->createString(text)};
        return call(name, 1, args);
    }

    Value* callStrRadix(const char* text, double radix) {
        Value* args[2] = {ctx->createString(text), ctx->createNumber(radix)};
        return call("parseInt", 2, args);
    }
};

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
bool boolOf(Value* v) { return static_cast<BooleanData*>(v->data())->value; }
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }

bool isError(Value* v) {
    return v != nullptr && (v->type()->kind() == TypeKind::Exception ||
                            v->type()->kind() == TypeKind::Fatal);
}

}  // namespace

TEST(GlobalFunctionTest, ParseIntBasics) {
    Fixture f;
    EXPECT_DOUBLE_EQ(numOf(f.callStr("parseInt", "42")), 42.0);
    EXPECT_DOUBLE_EQ(numOf(f.callStr("parseInt", "  -12px")), -12.0);
    EXPECT_DOUBLE_EQ(numOf(f.callStr("parseInt", "0x1f")), 31.0);
    EXPECT_DOUBLE_EQ(numOf(f.callStr("parseInt", "+7")), 7.0);
    EXPECT_TRUE(std::isnan(numOf(f.callStr("parseInt", "abc"))));
    EXPECT_TRUE(std::isnan(numOf(f.callStr("parseInt", ""))));
}

TEST(GlobalFunctionTest, ParseIntRadix) {
    Fixture f;
    EXPECT_DOUBLE_EQ(numOf(f.callStrRadix("101", 2)), 5.0);
    EXPECT_DOUBLE_EQ(numOf(f.callStrRadix("z", 36)), 35.0);
    EXPECT_DOUBLE_EQ(numOf(f.callStrRadix("0x10", 16)), 16.0);
    EXPECT_TRUE(std::isnan(numOf(f.callStrRadix("10", 1))));
    EXPECT_TRUE(std::isnan(numOf(f.callStrRadix("10", 37))));
}

TEST(GlobalFunctionTest, ParseFloatGrammar) {
    Fixture f;
    EXPECT_DOUBLE_EQ(numOf(f.callStr("parseFloat", "3.14abc")), 3.14);
    EXPECT_DOUBLE_EQ(numOf(f.callStr("parseFloat", ".5e2x")), 50.0);
    EXPECT_DOUBLE_EQ(numOf(f.callStr("parseFloat", "1e")), 1.0);
    EXPECT_DOUBLE_EQ(numOf(f.callStr("parseFloat", "  -0.25")), -0.25);
    EXPECT_DOUBLE_EQ(numOf(f.callStr("parseFloat", "Infinity")),
                     std::numeric_limits<double>::infinity());
    EXPECT_DOUBLE_EQ(numOf(f.callStr("parseFloat", "-Infinity")),
                     -std::numeric_limits<double>::infinity());
    // 大小写敏感：infinity 不是合法前缀。
    EXPECT_TRUE(std::isnan(numOf(f.callStr("parseFloat", "infinity"))));
    EXPECT_TRUE(std::isnan(numOf(f.callStr("parseFloat", "abc"))));
}

TEST(GlobalFunctionTest, IsNaNAndIsFinite) {
    Fixture f;
    Value* nan = f.ctx->createNumber(std::numeric_limits<double>::quiet_NaN());
    Value* inf = f.ctx->createNumber(std::numeric_limits<double>::infinity());
    EXPECT_TRUE(boolOf(f.call("isNaN", 1, &nan)));
    EXPECT_TRUE(boolOf(f.callStr("isNaN", "abc")));
    EXPECT_FALSE(boolOf(f.callStr("isNaN", "12")));
    EXPECT_TRUE(boolOf(f.callStr("isFinite", "12")));
    EXPECT_FALSE(boolOf(f.call("isFinite", 1, &inf)));
    EXPECT_FALSE(boolOf(f.call("isFinite", 1, &nan)));
}

TEST(GlobalFunctionTest, EncodeUriVsComponent) {
    Fixture f;
    EXPECT_EQ(strOf(f.callStr("encodeURI", "a b?x=1&y=中#f")),
              "a%20b?x=1&y=%E4%B8%AD#f");
    EXPECT_EQ(strOf(f.callStr("encodeURIComponent", "a b?x=1&y=中")),
              "a%20b%3Fx%3D1%26y%3D%E4%B8%AD");
    EXPECT_EQ(strOf(f.callStr("encodeURIComponent", "-_.!~*'()")), "-_.!~*'()");
}

TEST(GlobalFunctionTest, DecodeUriPreservesReserved) {
    Fixture f;
    EXPECT_EQ(strOf(f.callStr("decodeURI", "%E4%B8%AD%20%23")), "中 %23");
    EXPECT_EQ(strOf(f.callStr("decodeURIComponent", "%E4%B8%AD%20%23")), "中 #");
    EXPECT_EQ(strOf(f.callStr("decodeURIComponent", "a%2Fb")), "a/b");
}

TEST(GlobalFunctionTest, MalformedUriThrows) {
    Fixture f;
    EXPECT_TRUE(isError(f.callStr("decodeURIComponent", "%E4")));
    EXPECT_TRUE(isError(f.callStr("decodeURIComponent", "%ZZ")));
    EXPECT_TRUE(isError(f.callStr("decodeURIComponent", "%")));
    // WTF-8 孤立代理不是合法 Unicode 标量，编码时抛 URIError。
    EXPECT_TRUE(isError(f.callStr("encodeURIComponent", "\xED\xA0\x80")));
}
