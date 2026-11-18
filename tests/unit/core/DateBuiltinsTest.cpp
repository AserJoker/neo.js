// DateBuiltinsTest.cpp - Date 构造函数、组件读取与格式化
//
// 验证（ES2024 §21.4，本地时间按 UTC 处理）。

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
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

    Value* dateCtor() { return ctx->global()->lookup("Date"); }

    Value* make(double t) {
        Value* args[1] = {ctx->createNumber(t)};
        return ctx->construct(dateCtor(), 1, args);
    }

    Value* call(Value* self, const char* name, size_t argc = 0, Value* argv[] = nullptr) {
        Value* fn = ctx->getProperty(self, name);
        return ctx->call(fn, self, argc, argv);
    }

    Value* call1(Value* self, const char* name, Value* a) {
        Value* args[1] = {a};
        return call(self, name, 1, args);
    }

    Value* statik(const char* name) { return ctx->getProperty(dateCtor(), name); }
};

bool isError(Value* v) {
    return v != nullptr && (v->type()->kind() == TypeKind::Exception ||
                            v->type()->kind() == TypeKind::Fatal);
}

double numOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Number
               ? static_cast<NumberData*>(v->data())->value
               : -999.0;
}

std::string strOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::String
               ? static_cast<StringData*>(v->data())->value
               : "<not string>";
}

}  // namespace

TEST(DateBuiltinsTest, EpochAndComponents) {
    Fixture f;
    Value* d = f.make(0.0);
    ASSERT_FALSE(isError(d));
    EXPECT_EQ(numOf(f.call(d, "getTime")), 0.0);
    EXPECT_EQ(numOf(f.call(d, "getFullYear")), 1970.0);
    EXPECT_EQ(numOf(f.call(d, "getMonth")), 0.0);
    EXPECT_EQ(numOf(f.call(d, "getDate")), 1.0);
    EXPECT_EQ(numOf(f.call(d, "getDay")), 4.0);  // Thursday
    EXPECT_EQ(numOf(f.call(d, "getHours")), 0.0);
    EXPECT_EQ(numOf(f.call(d, "getTimezoneOffset")), 0.0);
    EXPECT_EQ(strOf(f.call(d, "toISOString")), "1970-01-01T00:00:00.000Z");
    EXPECT_EQ(strOf(f.call(d, "toUTCString")), "Thu, 01 Jan 1970 00:00:00 GMT");
}

TEST(DateBuiltinsTest, UtcAndParse) {
    Fixture f;
    Value* utc = f.statik("UTC");
    Value* args[3] = {f.ctx->createNumber(2020.0), f.ctx->createNumber(0.0),
                      f.ctx->createNumber(1.0)};
    EXPECT_EQ(numOf(f.ctx->call(utc, f.ctx->createUndefined(), 3, args)),
              1577836800000.0);

    Value* parse = f.statik("parse");
    Value* text[1] = {f.ctx->createString("2020-01-01T00:00:00.000Z")};
    EXPECT_EQ(numOf(f.ctx->call(parse, f.ctx->createUndefined(), 1, text)),
              1577836800000.0);
    Value* dateOnly[1] = {f.ctx->createString("2020-01-01")};
    EXPECT_EQ(numOf(f.ctx->call(parse, f.ctx->createUndefined(), 1, dateOnly)),
              1577836800000.0);
    Value* bad[1] = {f.ctx->createString("not a date")};
    EXPECT_TRUE(std::isnan(numOf(f.ctx->call(parse, f.ctx->createUndefined(), 1, bad))));
}

TEST(DateBuiltinsTest, ConstructFromStringAndFields) {
    Fixture f;
    Value* args[1] = {f.ctx->createString("2020-01-01T00:00:00Z")};
    Value* fromString = f.ctx->construct(f.dateCtor(), 1, args);
    EXPECT_EQ(numOf(f.call(fromString, "getTime")), 1577836800000.0);

    Value* fields[3] = {f.ctx->createNumber(2020.0), f.ctx->createNumber(0.0),
                        f.ctx->createNumber(2.0)};
    Value* fromFields = f.ctx->construct(f.dateCtor(), 3, fields);
    EXPECT_EQ(strOf(f.call(fromFields, "toISOString")), "2020-01-02T00:00:00.000Z");
}

TEST(DateBuiltinsTest, Setters) {
    Fixture f;
    Value* d = f.make(0.0);
    EXPECT_EQ(numOf(f.call1(d, "setUTCFullYear", f.ctx->createNumber(2000.0))),
              946684800000.0);
    EXPECT_EQ(numOf(f.call(d, "getUTCFullYear")), 2000.0);
    EXPECT_EQ(numOf(f.call1(d, "setUTCMonth", f.ctx->createNumber(5.0))),
              959817600000.0);
    EXPECT_EQ(numOf(f.call(d, "getUTCMonth")), 5.0);
    EXPECT_EQ(numOf(f.call1(d, "setTime", f.ctx->createNumber(1000.0))), 1000.0);
    EXPECT_EQ(numOf(f.call(d, "getUTCSeconds")), 1.0);
}

TEST(DateBuiltinsTest, InvalidDate) {
    Fixture f;
    Value* d = f.make(std::nan(""));
    EXPECT_TRUE(std::isnan(numOf(f.call(d, "getTime"))));
    EXPECT_EQ(strOf(f.call(d, "toString")), "Invalid Date");
    EXPECT_TRUE(isError(f.call(d, "toISOString")));
    EXPECT_EQ(f.call(d, "toJSON")->type()->kind(), TypeKind::Null);
}

TEST(DateBuiltinsTest, CallWithoutNewReturnsString) {
    Fixture f;
    Value* r = f.ctx->call(f.dateCtor(), f.ctx->createUndefined(), 0, nullptr);
    ASSERT_FALSE(isError(r));
    EXPECT_EQ(r->type()->kind(), TypeKind::String);
    EXPECT_FALSE(strOf(r).empty());
}
