// StringMethodsTest.cpp - String.prototype 常用方法
//
// 验证（ES2024 §22.1.3）：
// - charAt/charCodeAt/codePointAt/at（UTF-16 码元索引）
// - includes/startsWith/endsWith/indexOf/lastIndexOf
// - slice/substring/substr
// - trim/trimStart/trimEnd、padStart/padEnd、repeat、concat、localeCompare
// - toLowerCase/toUpperCase（含非 ASCII 简单映射）
// - null/undefined this 抛 TypeError

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

    Value* str(std::string_view s) { return ctx->createString(s); }

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
};

bool isError(Value* v) {
    return v != nullptr && (v->type()->kind() == TypeKind::Exception ||
                            v->type()->kind() == TypeKind::Fatal);
}

std::string strOf(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::String) return "<not string>";
    return static_cast<StringData*>(v->data())->value;
}

double numOf(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::Number) return -999;
    return static_cast<NumberData*>(v->data())->value;
}

bool boolOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Boolean &&
           static_cast<BooleanData*>(v->data())->value;
}

bool isUndefined(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Undefined;
}

}  // namespace

TEST(StringMethodsTest, CharAccess) {
    Fixture f;
    Value* s = f.str("abc");
    EXPECT_EQ(strOf(f.call1(s, "charAt", f.ctx->createNumber(1))), "b");
    EXPECT_EQ(strOf(f.call1(s, "charAt", f.ctx->createNumber(9))), "");
    EXPECT_EQ(strOf(f.call1(s, "charAt", f.ctx->createNumber(-1))), "");
    EXPECT_EQ(numOf(f.call1(s, "charCodeAt", f.ctx->createNumber(2))), 99.0);
    EXPECT_TRUE(std::isnan(numOf(f.call1(s, "charCodeAt", f.ctx->createNumber(5)))));
    EXPECT_EQ(numOf(f.call1(s, "codePointAt", f.ctx->createNumber(0))), 97.0);
    EXPECT_TRUE(isUndefined(f.call1(s, "codePointAt", f.ctx->createNumber(5))));
    EXPECT_EQ(strOf(f.call1(s, "at", f.ctx->createNumber(-1))), "c");
    EXPECT_TRUE(isUndefined(f.call1(s, "at", f.ctx->createNumber(5))));
}

TEST(StringMethodsTest, SurrogatePairUsesUtf16Units) {
    Fixture f;
    // U+1F600 在 UTF-16 中占两个码元。
    Value* s = f.str("\xF0\x9F\x98\x80");
    EXPECT_EQ(numOf(f.call1(s, "charCodeAt", f.ctx->createNumber(0))), 0xD83D);
    EXPECT_EQ(numOf(f.call1(s, "charCodeAt", f.ctx->createNumber(1))), 0xDE00);
    EXPECT_EQ(numOf(f.call1(s, "codePointAt", f.ctx->createNumber(0))), 0x1F600);
    EXPECT_EQ(numOf(f.call1(s, "codePointAt", f.ctx->createNumber(1))), 0xDE00);
    EXPECT_EQ(strOf(f.call1(s, "charAt", f.ctx->createNumber(0))), "\xED\xA0\xBD");
}

TEST(StringMethodsTest, SearchPredicates) {
    Fixture f;
    Value* s = f.str("hello world");
    EXPECT_TRUE(boolOf(f.call1(s, "includes", f.str("lo w"))));
    EXPECT_FALSE(boolOf(f.call1(s, "includes", f.str("LO W"))));
    EXPECT_TRUE(boolOf(f.call2(s, "includes", f.str("hello"), f.ctx->createNumber(-5))));
    EXPECT_TRUE(boolOf(f.call1(s, "startsWith", f.str("hello"))));
    EXPECT_FALSE(boolOf(f.call1(s, "startsWith", f.str("world"))));
    EXPECT_TRUE(boolOf(f.call1(s, "endsWith", f.str("world"))));
    EXPECT_FALSE(boolOf(f.call1(s, "endsWith", f.str("hello"))));
    EXPECT_EQ(numOf(f.call1(s, "indexOf", f.str("o"))), 4.0);
    EXPECT_EQ(numOf(f.call2(s, "indexOf", f.str("o"), f.ctx->createNumber(5))), 7.0);
    EXPECT_EQ(numOf(f.call1(s, "indexOf", f.str("z"))), -1.0);
    EXPECT_EQ(numOf(f.call1(s, "lastIndexOf", f.str("o"))), 7.0);
    EXPECT_EQ(numOf(f.call2(s, "lastIndexOf", f.str("o"), f.ctx->createNumber(4))), 4.0);
    EXPECT_EQ(numOf(f.call2(s, "lastIndexOf", f.str(""), f.ctx->createNumber(3))), 3.0);
}

TEST(StringMethodsTest, SliceSubstringSubstr) {
    Fixture f;
    Value* s = f.str("abcdef");
    EXPECT_EQ(strOf(f.call2(s, "slice", f.ctx->createNumber(1), f.ctx->createNumber(4))), "bcd");
    EXPECT_EQ(strOf(f.call2(s, "slice", f.ctx->createNumber(-2), f.ctx->createNumber(6))), "ef");
    EXPECT_EQ(strOf(f.call2(s, "slice", f.ctx->createNumber(4), f.ctx->createNumber(1))), "");
    EXPECT_EQ(strOf(f.call2(s, "substring", f.ctx->createNumber(4), f.ctx->createNumber(1))), "bcd");
    EXPECT_EQ(strOf(f.call2(s, "substring", f.ctx->createNumber(-3), f.ctx->createNumber(2))), "ab");
    EXPECT_EQ(strOf(f.call2(s, "substr", f.ctx->createNumber(2), f.ctx->createNumber(3))), "cde");
    EXPECT_EQ(strOf(f.call2(s, "substr", f.ctx->createNumber(-2), f.ctx->createNumber(5))), "ef");
}

TEST(StringMethodsTest, TrimFamily) {
    Fixture f;
    Value* s = f.str(" \t\nabc \r\n");
    EXPECT_EQ(strOf(f.call(s, "trim")), "abc");
    EXPECT_EQ(strOf(f.call(s, "trimStart")), "abc \r\n");
    EXPECT_EQ(strOf(f.call(s, "trimEnd")), " \t\nabc");
}

TEST(StringMethodsTest, PadRepeatConcat) {
    Fixture f;
    Value* s = f.str("ab");
    EXPECT_EQ(strOf(f.call2(s, "padStart", f.ctx->createNumber(5), f.str("xy"))), "xyxab");
    EXPECT_EQ(strOf(f.call2(s, "padEnd", f.ctx->createNumber(5), f.str("xy"))), "abxyx");
    EXPECT_EQ(strOf(f.call1(s, "padStart", f.ctx->createNumber(1))), "ab");
    EXPECT_EQ(strOf(f.call1(s, "repeat", f.ctx->createNumber(3))), "ababab");
    EXPECT_EQ(strOf(f.call1(s, "repeat", f.ctx->createNumber(0))), "");
    EXPECT_TRUE(isError(f.call1(s, "repeat", f.ctx->createNumber(-1))));
    EXPECT_EQ(strOf(f.call2(s, "concat", f.str("-"), f.str("cd"))), "ab-cd");
}

TEST(StringMethodsTest, LocaleCompare) {
    Fixture f;
    Value* a = f.str("apple");
    EXPECT_EQ(numOf(f.call1(a, "localeCompare", f.str("banana"))), -1.0);
    EXPECT_EQ(numOf(f.call1(a, "localeCompare", f.str("apple"))), 0.0);
    EXPECT_EQ(numOf(f.call1(a, "localeCompare", f.str("Apple"))), 1.0);
}

TEST(StringMethodsTest, CaseMapping) {
    Fixture f;
    Value* s = f.str("AbC");
    EXPECT_EQ(strOf(f.call(s, "toLowerCase")), "abc");
    EXPECT_EQ(strOf(f.call(s, "toUpperCase")), "ABC");
    EXPECT_EQ(strOf(f.call(s, "toLocaleLowerCase")), "abc");
    EXPECT_EQ(strOf(f.call(s, "toLocaleUpperCase")), "ABC");

    // 非 ASCII：Ä (U+00C4) 与 ä (U+00E4)。
    Value* de = f.str("\xC3\x84");
    EXPECT_EQ(strOf(f.call(de, "toLowerCase")), "\xC3\xA4");
    Value* lower = f.str("\xC3\xA4");
    EXPECT_EQ(strOf(f.call(lower, "toUpperCase")), "\xC3\x84");

    // 希腊字母：Σ → σ。
    Value* greek = f.str("\xCE\xA3");
    EXPECT_EQ(strOf(f.call(greek, "toLowerCase")), "\xCF\x83");
}

TEST(StringMethodsTest, NullUndefinedThisThrows) {
    Fixture f;
    Value* fn = f.ctx->getProperty(f.str("x"), "charAt");
    Value* undef = f.ctx->createUndefined();
    Value* r = f.ctx->call(fn, undef, 0, nullptr);
    EXPECT_TRUE(isError(r));

    Value* fn2 = f.ctx->getProperty(f.str("x"), "toUpperCase");
    Value* nullValue = f.ctx->createNull();
    Value* r2 = f.ctx->call(fn2, nullValue, 0, nullptr);
    EXPECT_TRUE(isError(r2));
}
