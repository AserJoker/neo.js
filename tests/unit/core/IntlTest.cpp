// IntlTest.cpp - Intl 命名空间与 Intl.NumberFormat（ECMA-402）
//
// 验证 core 的选项管线与宿主后端注入（C ABI）。

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"
#include "neojs.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {

using neo::core::ArrayData;
using neo::core::BooleanData;
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

    Value* intl() { return ctx->global()->lookup("Intl"); }

    Value* numberFormat(size_t argc = 0, Value* argv[] = nullptr) {
        Value* ctor = ctx->getProperty(intl(), "NumberFormat");
        return ctx->construct(ctor, argc, argv);
    }

    Value* dateTimeFormat(size_t argc = 0, Value* argv[] = nullptr) {
        Value* ctor = ctx->getProperty(intl(), "DateTimeFormat");
        return ctx->construct(ctor, argc, argv);
    }

    Value* collator(size_t argc = 0, Value* argv[] = nullptr) {
        Value* ctor = ctx->getProperty(intl(), "Collator");
        return ctx->construct(ctor, argc, argv);
    }

    Value* call(Value* self, const char* name, size_t argc = 0, Value* argv[] = nullptr) {
        Value* fn = ctx->getProperty(self, name);
        return ctx->call(fn, self, argc, argv);
    }

    Value* call1(Value* self, const char* name, Value* a) {
        Value* args[1] = {a};
        return call(self, name, 1, args);
    }
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

bool boolOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Boolean &&
           static_cast<BooleanData*>(v->data())->value;
}

std::string strOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::String
               ? static_cast<neo::core::StringData*>(v->data())->value
               : std::string();
}

Value* formatOf(Fixture& f, Value* formatter, double value) {
    Value* fn = f.ctx->getProperty(formatter, "format");
    Value* args[1] = {f.ctx->createNumber(value)};
    return f.ctx->call(fn, formatter, 1, args);
}

// —— 测试用宿主后端：所有数字格式化为 "FAKE" ——

struct FakeFormatter {
    int marker = 42;
};

NJS_IntlStatus fakeCanonicalize(void*, const char* tag, size_t len, char* out,
                                size_t* outLen) {
    if (out == nullptr || *outLen < len + 1) {
        *outLen = len + 1;
        return NJS_INTL_OK;
    }
    std::memcpy(out, tag, len);
    out[len] = '\0';
    *outLen = len + 1;
    return NJS_INTL_OK;
}

const char* const kFakeLocales[] = {"xx", nullptr};

const char* const* fakeAvailable(void*, size_t* count) {
    *count = 1;
    return kFakeLocales;
}

NJS_IntlStatus fakeDefaultLocale(void*, char* out, size_t* outLen) {
    return fakeCanonicalize(nullptr, "xx", 2, out, outLen);
}

NJS_IntlStatus fakeDefaultTimeZone(void*, char* out, size_t* outLen) {
    return fakeCanonicalize(nullptr, "UTC", 3, out, outLen);
}

NJS_IntlFormatter* fakeCreate(void*, const char* locale, const NJS_IntlOption*,
                              size_t, NJS_IntlResolved* resolved,
                              NJS_IntlStatus* status) {
    auto* formatter = new FakeFormatter();
    if (resolved != nullptr) {
        std::memset(resolved, 0, sizeof *resolved);
        std::snprintf(resolved->locale, sizeof resolved->locale, "%s", locale);
        std::snprintf(resolved->numbering_system,
                      sizeof resolved->numbering_system, "%s", "latn");
        std::snprintf(resolved->calendar, sizeof resolved->calendar, "%s", "gregory");
        std::snprintf(resolved->time_zone, sizeof resolved->time_zone, "%s", "UTC");
    }
    if (status != nullptr) *status = NJS_INTL_OK;
    return reinterpret_cast<NJS_IntlFormatter*>(formatter);
}

NJS_IntlStatus fakeFormat(NJS_IntlFormatter*, double, NJS_IntlPart* parts,
                          size_t* count) {
    if (parts == nullptr || *count < 1) {
        *count = 1;
        return NJS_INTL_OK;
    }
    parts[0].type = NJS_INTL_PART_LITERAL;
    parts[0].text = "FAKE";
    parts[0].length = 4;
    *count = 1;
    return NJS_INTL_OK;
}

void fakeDestroy(NJS_IntlFormatter* formatter) {
    delete reinterpret_cast<FakeFormatter*>(formatter);
}

const char* fakeLastError(void*) { return nullptr; }

const NJS_IntlBackend kFakeBackend = {
    NJS_INTL_ABI_VERSION,
    sizeof(NJS_IntlBackend),
    NJS_INTL_CAP_NUMBER | NJS_INTL_CAP_FORMAT_TO_PARTS,
    nullptr,
    fakeCanonicalize,
    fakeAvailable,
    fakeDefaultLocale,
    fakeDefaultTimeZone,
    fakeCreate,
    fakeFormat,
    fakeDestroy,
    fakeLastError,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

}  // namespace

TEST(IntlTest, NamespaceAndCanonicalLocales) {
    Fixture f;
    Value* intl = f.intl();
    ASSERT_FALSE(isError(intl));
    EXPECT_EQ(intl->type()->kind(), TypeKind::Object);

    Value* fn = f.ctx->getProperty(intl, "getCanonicalLocales");
    ASSERT_FALSE(isError(fn));

    Value* one[1] = {f.ctx->createString("EN-us")};
    Value* result = f.ctx->call(fn, intl, 1, one);
    ASSERT_FALSE(isError(result));
    EXPECT_EQ(numOf(f.ctx->getProperty(result, "length")), 1.0);
    EXPECT_EQ(strOf(f.ctx->getProperty(result, "0")), "en-US");

    Value* array = f.ctx->createArray();
    static_cast<ArrayData*>(array->data())
        ->setElement(0, f.ctx->createString("zh-hans-cn"));
    Value* list[1] = {array};
    Value* result2 = f.ctx->call(fn, intl, 1, list);
    ASSERT_FALSE(isError(result2));
    EXPECT_EQ(strOf(f.ctx->getProperty(result2, "0")), "zh-Hans-CN");

    Value* bad[1] = {f.ctx->createString("en_US")};
    EXPECT_TRUE(isError(f.ctx->call(fn, intl, 1, bad)));
}

TEST(IntlTest, DecimalFormattingAndGrouping) {
    Fixture f;
    Value* formatter = f.numberFormat();
    ASSERT_FALSE(isError(formatter));

    EXPECT_EQ(strOf(formatOf(f, formatter, 1234.5)), "1,234.5");
    EXPECT_EQ(strOf(formatOf(f, formatter, 1234.0)), "1,234");
    EXPECT_EQ(strOf(formatOf(f, formatter, -5.0)), "-5");

    Value* options = f.ctx->createObject();
    f.ctx->defineProperty(options, "useGrouping", f.ctx->createBoolean(false));
    f.ctx->defineProperty(options, "minimumFractionDigits", f.ctx->createNumber(2));
    Value* args[2] = {f.ctx->createUndefined(), options};
    Value* formatter2 = f.numberFormat(2, args);
    ASSERT_FALSE(isError(formatter2));
    EXPECT_EQ(strOf(formatOf(f, formatter2, 1234.5)), "1234.50");

    Value* opts3 = f.ctx->createObject();
    f.ctx->defineProperty(opts3, "signDisplay", f.ctx->createString("always"));
    Value* args3[2] = {f.ctx->createUndefined(), opts3};
    Value* formatter3 = f.numberFormat(2, args3);
    ASSERT_FALSE(isError(formatter3));
    EXPECT_EQ(strOf(formatOf(f, formatter3, 5.0)), "+5");
}

TEST(IntlTest, PercentAndCurrency) {
    Fixture f;
    Value* percentOpts = f.ctx->createObject();
    f.ctx->defineProperty(percentOpts, "style", f.ctx->createString("percent"));
    Value* percentArgs[2] = {f.ctx->createUndefined(), percentOpts};
    Value* percent = f.numberFormat(2, percentArgs);
    ASSERT_FALSE(isError(percent));
    EXPECT_EQ(strOf(formatOf(f, percent, 0.25)), "25%");

    Value* currencyOpts = f.ctx->createObject();
    f.ctx->defineProperty(currencyOpts, "style", f.ctx->createString("currency"));
    f.ctx->defineProperty(currencyOpts, "currency", f.ctx->createString("USD"));
    Value* currencyArgs[2] = {f.ctx->createUndefined(), currencyOpts};
    Value* currency = f.numberFormat(2, currencyArgs);
    ASSERT_FALSE(isError(currency));
    EXPECT_EQ(strOf(formatOf(f, currency, 1234.5)), "$1,234.50");

    Value* badOpts = f.ctx->createObject();
    f.ctx->defineProperty(badOpts, "style", f.ctx->createString("currency"));
    Value* badArgs[2] = {f.ctx->createUndefined(), badOpts};
    EXPECT_TRUE(isError(f.numberFormat(2, badArgs)));
}

TEST(IntlTest, FormatToPartsAndResolvedOptions) {
    Fixture f;
    Value* formatter = f.numberFormat();
    ASSERT_FALSE(isError(formatter));

    Value* parts = f.call1(formatter, "formatToParts", f.ctx->createNumber(1234.5));
    ASSERT_FALSE(isError(parts));
    const double length = numOf(f.ctx->getProperty(parts, "length"));
    ASSERT_GT(length, 3.0);
    Value* first = f.ctx->getProperty(parts, "0");
    EXPECT_EQ(strOf(f.ctx->getProperty(first, "type")), "integer");
    EXPECT_EQ(strOf(f.ctx->getProperty(first, "value")), "1");

    Value* resolved = f.call(formatter, "resolvedOptions");
    ASSERT_FALSE(isError(resolved));
    EXPECT_EQ(strOf(f.ctx->getProperty(resolved, "locale")), "en");
    EXPECT_EQ(strOf(f.ctx->getProperty(resolved, "numberingSystem")), "latn");
    EXPECT_EQ(strOf(f.ctx->getProperty(resolved, "style")), "decimal");
    EXPECT_TRUE(boolOf(f.ctx->getProperty(resolved, "useGrouping")));
    EXPECT_EQ(numOf(f.ctx->getProperty(resolved, "minimumIntegerDigits")), 1.0);
}

TEST(IntlTest, SupportedLocalesAndFallback) {
    Fixture f;
    Value* ctor = f.ctx->getProperty(f.intl(), "NumberFormat");
    Value* supported = f.ctx->getProperty(ctor, "supportedLocalesOf");
    ASSERT_FALSE(isError(supported));

    Value* args[1] = {f.ctx->createString("en-US")};
    Value* result = f.ctx->call(supported, ctor, 1, args);
    ASSERT_FALSE(isError(result));
    EXPECT_EQ(numOf(f.ctx->getProperty(result, "length")), 1.0);
    EXPECT_EQ(strOf(f.ctx->getProperty(result, "0")), "en");

    Value* zhArgs[1] = {f.ctx->createString("zh")};
    Value* zh = f.ctx->call(supported, ctor, 1, zhArgs);
    ASSERT_FALSE(isError(zh));
    EXPECT_EQ(numOf(f.ctx->getProperty(zh, "length")), 0.0);

    Value* localeArgs[1] = {f.ctx->createString("zh")};
    Value* formatter = f.numberFormat(1, localeArgs);
    ASSERT_FALSE(isError(formatter));
    Value* resolved = f.call(formatter, "resolvedOptions");
    EXPECT_EQ(strOf(f.ctx->getProperty(resolved, "locale")), "en");
}

TEST(IntlTest, HostBackendInjection) {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    NullSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx =
        createContext(alloc.get(), &console, nullptr, &kFakeBackend);

    Value* intl = ctx->global()->lookup("Intl");
    ASSERT_FALSE(isError(intl));
    Value* ctor = ctx->getProperty(intl, "NumberFormat");
    Value* formatter = ctx->construct(ctor, 0, nullptr);
    ASSERT_FALSE(isError(formatter));

    Value* options = ctx->call(ctx->getProperty(formatter, "resolvedOptions"),
                               formatter, 0, nullptr);
    ASSERT_FALSE(isError(options));
    EXPECT_EQ(strOf(ctx->getProperty(options, "locale")), "xx");

    Value* format = ctx->getProperty(formatter, "format");
    Value* args[1] = {ctx->createNumber(1.0)};
    Value* text = ctx->call(format, formatter, 1, args);
    ASSERT_FALSE(isError(text));
    EXPECT_EQ(strOf(text), "FAKE");
}

TEST(IntlTest, NumberToLocaleStringDelegatesToIntl) {
    Fixture f;
    Value* number = f.ctx->createNumber(1234.5);
    Value* fn = f.ctx->getProperty(number, "toLocaleString");
    ASSERT_FALSE(isError(fn));
    Value* text = f.ctx->call(fn, number, 0, nullptr);
    ASSERT_FALSE(isError(text));
    EXPECT_EQ(strOf(text), "1,234.5");

    // Intl.NumberFormat 可无 new 调用（ECMA-402 §15.2.2）。
    Value* ctor = f.ctx->getProperty(f.intl(), "NumberFormat");
    Value* formatter = f.ctx->call(ctor, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_FALSE(isError(formatter));
    EXPECT_EQ(strOf(formatOf(f, formatter, 1234.5)), "1,234.5");
}

TEST(IntlTest, DateTimeFormatBasics) {
    Fixture f;
    Value* formatter = f.dateTimeFormat();
    ASSERT_FALSE(isError(formatter));
    // 默认 year/month/day 为 numeric（最小后端使用 ISO 风格分隔符）。
    EXPECT_EQ(strOf(formatOf(f, formatter, 0.0)), "1970-1-1");

    Value* opts = f.ctx->createObject();
    f.ctx->defineProperty(opts, "year", f.ctx->createString("numeric"));
    f.ctx->defineProperty(opts, "month", f.ctx->createString("2-digit"));
    f.ctx->defineProperty(opts, "day", f.ctx->createString("2-digit"));
    f.ctx->defineProperty(opts, "hour", f.ctx->createString("2-digit"));
    f.ctx->defineProperty(opts, "minute", f.ctx->createString("2-digit"));
    f.ctx->defineProperty(opts, "second", f.ctx->createString("2-digit"));
    Value* args[2] = {f.ctx->createUndefined(), opts};
    Value* formatter2 = f.dateTimeFormat(2, args);
    ASSERT_FALSE(isError(formatter2));
    EXPECT_EQ(strOf(formatOf(f, formatter2, 0.0)), "1970-01-01 00:00:00");

    Value* hour12Opts = f.ctx->createObject();
    f.ctx->defineProperty(hour12Opts, "hour", f.ctx->createString("2-digit"));
    f.ctx->defineProperty(hour12Opts, "minute", f.ctx->createString("2-digit"));
    f.ctx->defineProperty(hour12Opts, "second", f.ctx->createString("2-digit"));
    f.ctx->defineProperty(hour12Opts, "hour12", f.ctx->createBoolean(true));
    Value* hour12Args[2] = {f.ctx->createUndefined(), hour12Opts};
    Value* formatter3 = f.dateTimeFormat(2, hour12Args);
    ASSERT_FALSE(isError(formatter3));
    EXPECT_EQ(strOf(formatOf(f, formatter3, 0.0)), "12:00:00 AM");
}

TEST(IntlTest, DateTimeFormatStylesAndParts) {
    Fixture f;
    Value* dateOpts = f.ctx->createObject();
    f.ctx->defineProperty(dateOpts, "dateStyle", f.ctx->createString("short"));
    Value* dateArgs[2] = {f.ctx->createUndefined(), dateOpts};
    Value* dateFormatter = f.dateTimeFormat(2, dateArgs);
    ASSERT_FALSE(isError(dateFormatter));
    EXPECT_EQ(strOf(formatOf(f, dateFormatter, 0.0)), "1970-01-01");

    Value* timeOpts = f.ctx->createObject();
    f.ctx->defineProperty(timeOpts, "timeStyle", f.ctx->createString("medium"));
    Value* timeArgs[2] = {f.ctx->createUndefined(), timeOpts};
    Value* timeFormatter = f.dateTimeFormat(2, timeArgs);
    ASSERT_FALSE(isError(timeFormatter));
    EXPECT_EQ(strOf(formatOf(f, timeFormatter, 0.0)), "00:00:00");

    Value* parts = f.call1(dateFormatter, "formatToParts", f.ctx->createNumber(0));
    ASSERT_FALSE(isError(parts));
    ASSERT_GT(numOf(f.ctx->getProperty(parts, "length")), 0.0);
    Value* first = f.ctx->getProperty(parts, "0");
    EXPECT_EQ(strOf(f.ctx->getProperty(first, "type")), "year");
    EXPECT_EQ(strOf(f.ctx->getProperty(first, "value")), "1970");

    Value* resolved = f.call(dateFormatter, "resolvedOptions");
    ASSERT_FALSE(isError(resolved));
    EXPECT_EQ(strOf(f.ctx->getProperty(resolved, "locale")), "en");
    EXPECT_EQ(strOf(f.ctx->getProperty(resolved, "calendar")), "gregory");
    EXPECT_EQ(strOf(f.ctx->getProperty(resolved, "timeZone")), "UTC");
    EXPECT_EQ(strOf(f.ctx->getProperty(resolved, "dateStyle")), "short");
}

TEST(IntlTest, DateTimeFormatErrors) {
    Fixture f;
    Value* formatter = f.dateTimeFormat();
    ASSERT_FALSE(isError(formatter));
    EXPECT_TRUE(isError(formatOf(f, formatter, std::nan(""))));

    Value* bothOpts = f.ctx->createObject();
    f.ctx->defineProperty(bothOpts, "dateStyle", f.ctx->createString("short"));
    f.ctx->defineProperty(bothOpts, "year", f.ctx->createString("numeric"));
    Value* bothArgs[2] = {f.ctx->createUndefined(), bothOpts};
    EXPECT_TRUE(isError(f.dateTimeFormat(2, bothArgs)));

    Value* tzOpts = f.ctx->createObject();
    f.ctx->defineProperty(tzOpts, "timeZone",
                          f.ctx->createString("America/New_York"));
    Value* tzArgs[2] = {f.ctx->createUndefined(), tzOpts};
    EXPECT_TRUE(isError(f.dateTimeFormat(2, tzArgs)));
}

TEST(IntlTest, DateToLocaleStringDelegatesToIntl) {
    Fixture f;
    Value* dateCtor = f.ctx->global()->lookup("Date");
    Value* args[1] = {f.ctx->createNumber(0.0)};
    Value* date = f.ctx->construct(dateCtor, 1, args);
    ASSERT_FALSE(isError(date));

    Value* fn = f.ctx->getProperty(date, "toLocaleString");
    ASSERT_FALSE(isError(fn));
    Value* text = f.ctx->call(fn, date, 0, nullptr);
    ASSERT_FALSE(isError(text));
    EXPECT_EQ(strOf(text), "1970-1-1 0:00:00");

    Value* dateFn = f.ctx->getProperty(date, "toLocaleDateString");
    Value* dateText = f.ctx->call(dateFn, date, 0, nullptr);
    ASSERT_FALSE(isError(dateText));
    EXPECT_EQ(strOf(dateText), "1970-1-1");

    Value* timeFn = f.ctx->getProperty(date, "toLocaleTimeString");
    Value* timeText = f.ctx->call(timeFn, date, 0, nullptr);
    ASSERT_FALSE(isError(timeText));
    EXPECT_EQ(strOf(timeText), "0:00:00");
}

TEST(IntlTest, CollatorCompareAndOptions) {
    Fixture f;
    Value* collator = f.collator();
    ASSERT_FALSE(isError(collator));
    Value* compare = f.ctx->getProperty(collator, "compare");
    ASSERT_FALSE(isError(compare));

    auto compareStrings = [&](const char* a, const char* b) -> double {
        Value* args[2] = {f.ctx->createString(a), f.ctx->createString(b)};
        return numOf(f.ctx->call(compare, collator, 2, args));
    };
    EXPECT_EQ(compareStrings("a", "b"), -1.0);
    EXPECT_EQ(compareStrings("b", "a"), 1.0);
    EXPECT_EQ(compareStrings("a", "a"), 0.0);
    EXPECT_EQ(compareStrings("A", "a"), -1.0);

    Value* baseOpts = f.ctx->createObject();
    f.ctx->defineProperty(baseOpts, "sensitivity", f.ctx->createString("base"));
    Value* baseArgs[2] = {f.ctx->createUndefined(), baseOpts};
    Value* base = f.collator(2, baseArgs);
    ASSERT_FALSE(isError(base));
    Value* baseCompare = f.ctx->getProperty(base, "compare");
    Value* basePair[2] = {f.ctx->createString("A"), f.ctx->createString("a")};
    EXPECT_EQ(numOf(f.ctx->call(baseCompare, base, 2, basePair)), 0.0);

    Value* numericOpts = f.ctx->createObject();
    f.ctx->defineProperty(numericOpts, "numeric", f.ctx->createBoolean(true));
    Value* numericArgs[2] = {f.ctx->createUndefined(), numericOpts};
    Value* numeric = f.collator(2, numericArgs);
    ASSERT_FALSE(isError(numeric));
    Value* numericCompare = f.ctx->getProperty(numeric, "compare");
    Value* numericPair[2] = {f.ctx->createString("10"), f.ctx->createString("2")};
    EXPECT_EQ(numOf(f.ctx->call(numericCompare, numeric, 2, numericPair)), 1.0);

    Value* resolved = f.call(collator, "resolvedOptions");
    ASSERT_FALSE(isError(resolved));
    EXPECT_EQ(strOf(f.ctx->getProperty(resolved, "locale")), "en");
    EXPECT_EQ(strOf(f.ctx->getProperty(resolved, "usage")), "sort");
    EXPECT_EQ(strOf(f.ctx->getProperty(resolved, "sensitivity")), "variant");
    EXPECT_FALSE(boolOf(f.ctx->getProperty(resolved, "numeric")));
}

TEST(IntlTest, StringLocaleCompareDelegatesToCollator) {
    Fixture f;
    Value* a = f.ctx->createString("a");
    Value* fn = f.ctx->getProperty(a, "localeCompare");
    ASSERT_FALSE(isError(fn));

    Value* args[1] = {f.ctx->createString("b")};
    EXPECT_EQ(numOf(f.ctx->call(fn, a, 1, args)), -1.0);

    Value* ten = f.ctx->createString("10");
    Value* tenFn = f.ctx->getProperty(ten, "localeCompare");
    Value* opts = f.ctx->createObject();
    f.ctx->defineProperty(opts, "numeric", f.ctx->createBoolean(true));
    Value* numericArgs[3] = {f.ctx->createString("2"), f.ctx->createUndefined(), opts};
    EXPECT_EQ(numOf(f.ctx->call(tenFn, ten, 3, numericArgs)), 1.0);
}

TEST(IntlTest, ErrorCases) {
    Fixture f;
    Value* ctor = f.ctx->getProperty(f.intl(), "NumberFormat");

    Value* badStyleOpts = f.ctx->createObject();
    f.ctx->defineProperty(badStyleOpts, "style", f.ctx->createString("bogus"));
    Value* badStyleArgs[2] = {f.ctx->createUndefined(), badStyleOpts};
    EXPECT_TRUE(isError(f.ctx->construct(ctor, 2, badStyleArgs)));

    Value* badLocaleArgs[1] = {f.ctx->createString("en_US")};
    EXPECT_TRUE(isError(f.ctx->construct(ctor, 1, badLocaleArgs)));

    Value* formatter = f.numberFormat();
    ASSERT_FALSE(isError(formatter));
    Value* obj = f.ctx->createObject();
    EXPECT_TRUE(isError(f.call1(obj, "formatToParts", f.ctx->createNumber(1))));
}
