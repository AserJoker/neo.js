// DateBuiltins.cpp - Date 构造函数与原型（ES2024 §21.4）
//
// 设计要点：
//   - [[DateValue]] 为毫秒时间值，NaN 表示 Invalid Date；
//   - 本实现没有时区数据库，本地时间与 UTC 相同（getTimezoneOffset 恒为 0）；
//   - Date.parse 只接受 ISO 8601 子集（日期、日期时间、可选 Z/±HH:mm），
//     其余格式返回 NaN；
//   - Date.now 使用宿主 system_clock（墙钟，非事件循环的单调时钟）。
//
// 关联 ADR: 020（标准库组织）、006（异常即值）

#include "core/DateBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Date.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace neo::core {

namespace {

constexpr double kMsPerDay = 86400000.0;
constexpr double kMsPerHour = 3600000.0;
constexpr double kMsPerMinute = 60000.0;
constexpr double kMsPerSecond = 1000.0;
constexpr double kMaxTime = 8.64e15;

// —— 公历 ↔ 天数（Howard Hinnant 算法）——

int64_t daysFromCivil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2 ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2 ? 1 : 0;
}

// TimeClip（§21.4.1.31）：非有限或越界 → NaN，否则截断到整数毫秒。
double timeClip(double t) {
    if (!std::isfinite(t) || std::fabs(t) > kMaxTime) return std::nan("");
    // +0.0 把 -0 归一为 +0（§21.4.1.31 步骤 3）。
    return std::trunc(t) + 0.0;
}

// 年份偏移：0..99 视为 1900+year（§21.4.1.1 / §21.4.2.1）。
double yearOffset(double y) {
    if (!std::isfinite(y)) return y;
    const double iy = std::trunc(y);
    if (iy >= 0.0 && iy <= 99.0) return 1900.0 + iy;
    return y;
}

int64_t floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

int64_t floorMod(int64_t a, int64_t b) {
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

double nowMs() {
    using namespace std::chrono;
    return static_cast<double>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

// —— 参数转换 ——

Value* argOrUndefined(IContext* ctx, size_t argc, Value* argv[], size_t i) {
    return i < argc ? argv[i] : ctx->createUndefined();
}

// ToNumber；失败返回错误值。
Value* toNumberArg(IContext* ctx, size_t argc, Value* argv[], size_t i,
                   double& out) {
    Value* nv = detail::toNumberValue(ctx, argOrUndefined(ctx, argc, argv, i));
    if (detail::isErrorValue(nv)) return nv;
    out = static_cast<NumberData*>(nv->data())->value;
    return nullptr;
}

// MakeDay（§21.4.1.12）：月份先归一化到 [0,11]，再按公历求天数。
double makeDay(double year, double month, double date) {
    if (!std::isfinite(year) || !std::isfinite(month) || !std::isfinite(date)) {
        return std::nan("");
    }
    const int64_t y = static_cast<int64_t>(std::trunc(year));
    const int64_t m = static_cast<int64_t>(std::trunc(month));
    const int64_t d = static_cast<int64_t>(std::trunc(date));
    const int64_t y2 = y + floorDiv(m, 12);
    const int64_t m2 = floorMod(m, 12);
    return static_cast<double>(daysFromCivil(y2, m2 + 1, d));
}

// MakeTime（§21.4.1.13）。
double makeTime(double hour, double min, double sec, double ms) {
    if (!std::isfinite(hour) || !std::isfinite(min) || !std::isfinite(sec) ||
        !std::isfinite(ms)) {
        return std::nan("");
    }
    return std::trunc(hour) * kMsPerHour + std::trunc(min) * kMsPerMinute +
           std::trunc(sec) * kMsPerSecond + std::trunc(ms);
}

double makeDate(double day, double time) {
    if (!std::isfinite(day) || !std::isfinite(time)) return std::nan("");
    return day * kMsPerDay + time;
}

// —— ISO 8601 解析（子集）——

bool readDigits(std::string_view s, size_t& i, size_t count, int& out) {
    if (i + count > s.size()) return false;
    int v = 0;
    for (size_t k = 0; k < count; ++k) {
        const char c = s[i + k];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    i += count;
    out = v;
    return true;
}

int daysInMonth(int64_t y, int64_t m) {
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) {
        const bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    return kDays[m - 1];
}

bool parseIsoDate(std::string_view text, double& out) {
    // 去掉首尾空白
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
                           text[begin] == '\n' || text[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\n' || text[end - 1] == '\r')) {
        --end;
    }
    std::string_view s = text.substr(begin, end - begin);
    size_t i = 0;
    int year = 0;
    int month = 1;
    int day = 1;
    // 扩展年份：±YYYYYY；普通年份：YYYY。
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        const bool negative = s[i] == '-';
        ++i;
        int magnitude = 0;
        if (!readDigits(s, i, 6, magnitude)) return false;
        if (negative && magnitude == 0) return false;  // -000000 非法
        year = negative ? -magnitude : magnitude;
    } else {
        if (!readDigits(s, i, 4, year)) return false;
    }
    if (i < s.size() && s[i] == '-') {
        ++i;
        if (!readDigits(s, i, 2, month)) return false;
        if (i < s.size() && s[i] == '-') {
            ++i;
            if (!readDigits(s, i, 2, day)) return false;
        }
    }
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > daysInMonth(year, month)) return false;

    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
    bool hasTime = false;
    if (i < s.size() && (s[i] == 'T' || s[i] == ' ')) {
        hasTime = true;
        ++i;
        if (!readDigits(s, i, 2, hour)) return false;
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        if (!readDigits(s, i, 2, minute)) return false;
        if (i < s.size() && s[i] == ':') {
            ++i;
            if (!readDigits(s, i, 2, second)) return false;
            if (i < s.size() && s[i] == '.') {
                ++i;
                size_t digits = 0;
                int frac = 0;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                    if (digits < 3) {
                        frac = frac * 10 + (s[i] - '0');
                        ++digits;
                    }
                    ++i;
                }
                if (digits == 0) return false;
                while (digits < 3) {
                    frac *= 10;
                    ++digits;
                }
                millisecond = frac;
            }
        }
        if (hour > 24 || minute > 59 || second > 59) return false;
        if (hour == 24 && (minute != 0 || second != 0 || millisecond != 0)) {
            return false;
        }
    }

    int64_t offsetMs = 0;
    if (hasTime && i < s.size()) {
        if (s[i] == 'Z') {
            ++i;
        } else if (s[i] == '+' || s[i] == '-') {
            const int sign = s[i] == '+' ? 1 : -1;
            ++i;
            int oh = 0;
            int om = 0;
            if (!readDigits(s, i, 2, oh)) return false;
            if (i < s.size() && s[i] == ':') ++i;
            if (!readDigits(s, i, 2, om)) return false;
            if (oh > 23 || om > 59) return false;
            offsetMs = sign * (static_cast<int64_t>(oh) * 3600000 +
                               static_cast<int64_t>(om) * 60000);
        } else {
            return false;
        }
    }
    if (i != s.size()) return false;

    const int64_t days = daysFromCivil(year, month, day);
    double t = static_cast<double>(days) * kMsPerDay +
               static_cast<double>(hour) * kMsPerHour +
               static_cast<double>(minute) * kMsPerMinute +
               static_cast<double>(second) * kMsPerSecond +
               static_cast<double>(millisecond) - static_cast<double>(offsetMs);
    out = timeClip(t);
    return true;
}

constexpr const char* kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// 解析 Date.prototype.toString / toUTCString 产生的旧式格式（本地时区 = UTC）：
//   "Thu Jan 01 1970 00:00:00 GMT+0000 (Coordinated Universal Time)"
//   "Thu, 01 Jan 1970 00:00:00 GMT"
bool parseLegacyDate(std::string_view s, double& out) {
    size_t i = 0;
    while (i < s.size() && s[i] != ' ') ++i;  // 星期几
    if (i >= s.size()) return false;
    while (i < s.size() && s[i] == ' ') ++i;
    if (i < s.size() && s[i] == ',') {
        ++i;
        while (i < s.size() && s[i] == ' ') ++i;
    }
    int day = 0;
    int month = 0;
    const auto readMonth = [&]() {
        for (int m = 0; m < 12; ++m) {
            if (s.compare(i, 3, kMonthNames[m]) == 0) {
                month = m + 1;
                i += 3;
                return true;
            }
        }
        return false;
    };
    if (i < s.size() && s[i] >= 'A' && s[i] <= 'Z') {
        // toString: "Www Mmm DD YYYY ..."
        if (!readMonth()) return false;
        if (i >= s.size() || s[i] != ' ') return false;
        ++i;
        if (!readDigits(s, i, 2, day)) return false;
    } else {
        // toUTCString: "Www, DD Mmm YYYY ..."
        if (!readDigits(s, i, 2, day)) return false;
        if (i >= s.size() || s[i] != ' ') return false;
        ++i;
        if (!readMonth()) return false;
    }
    if (i >= s.size() || s[i] != ' ') return false;
    ++i;
    int year = 0;
    if (!readDigits(s, i, 4, year)) return false;
    if (i >= s.size() || s[i] != ' ') return false;
    ++i;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!readDigits(s, i, 2, hour)) return false;
    if (i >= s.size() || s[i] != ':') return false;
    ++i;
    if (!readDigits(s, i, 2, minute)) return false;
    if (i >= s.size() || s[i] != ':') return false;
    ++i;
    if (!readDigits(s, i, 2, second)) return false;
    if (i >= s.size() || s[i] != ' ') return false;
    ++i;
    if (s.compare(i, 3, "GMT") != 0) return false;
    const int64_t days = daysFromCivil(year, month, day);
    out = timeClip(static_cast<double>(days) * kMsPerDay +
                   static_cast<double>(hour) * kMsPerHour +
                   static_cast<double>(minute) * kMsPerMinute +
                   static_cast<double>(second) * kMsPerSecond);
    return true;
}

// —— 组件读取 ——

double dateValueOf(Value* self) {
    DateData* d = asDate(self);
    return d == nullptr ? std::nan("") : d->timeValue;
}

// 取本地（= UTC）组件；t 为 NaN 时返回 false。
struct Components {
    int64_t year = 0;
    int64_t month = 0;  // 1-12
    int64_t day = 0;
    int64_t hour = 0;
    int64_t minute = 0;
    int64_t second = 0;
    int64_t ms = 0;
    int64_t weekday = 0;  // 0=Sunday
};

bool componentsOf(double t, Components& c) {
    if (!std::isfinite(t)) return false;
    const int64_t ms = static_cast<int64_t>(t);
    const int64_t days = floorDiv(ms, 86400000);
    int64_t rem = floorMod(ms, 86400000);
    civilFromDays(days, c.year, c.month, c.day);
    c.hour = rem / 3600000;
    rem %= 3600000;
    c.minute = rem / 60000;
    rem %= 60000;
    c.second = rem / 1000;
    c.ms = rem % 1000;
    c.weekday = floorMod(days + 4, 7);
    return true;
}

Value* numberOrNaN(IContext* ctx, double v) { return ctx->createNumber(v); }

// —— 构造函数 ——

Value* dateCall(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    // Date() 忽略参数，返回当前时间的字符串形式。
    Value* date = ctx->createDate(nowMs());
    if (detail::isErrorValue(date)) return date;
    return ctx->toString(date);
}

Value* dateConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createDate(timeClip(nowMs()));
    if (argc == 1) {
        Value* v = argv[0];
        if (asDate(v) != nullptr) return ctx->createDate(dateValueOf(v));
        Value* prim = detail::toPrimitive(ctx, v, detail::PreferredType::Default);
        if (detail::isErrorValue(prim)) return prim;
        if (prim->type()->kind() == TypeKind::String) {
            const std::string& text = static_cast<StringData*>(prim->data())->value;
            double parsed = std::nan("");
            if (!parseIsoDate(text, parsed)) parseLegacyDate(text, parsed);
            return ctx->createDate(parsed);
        }
        Value* nv = detail::toNumberValue(ctx, prim);
        if (detail::isErrorValue(nv)) return nv;
        return ctx->createDate(
            timeClip(static_cast<NumberData*>(nv->data())->value));
    }

    double year = 0;
    double month = 0;
    double day = 1;
    double hour = 0;
    double minute = 0;
    double second = 0;
    double ms = 0;
    if (Value* err = toNumberArg(ctx, argc, argv, 0, year)) return err;
    if (argc > 1) {
        if (Value* err = toNumberArg(ctx, argc, argv, 1, month)) return err;
    }
    if (argc > 2) {
        if (Value* err = toNumberArg(ctx, argc, argv, 2, day)) return err;
    }
    if (argc > 3) {
        if (Value* err = toNumberArg(ctx, argc, argv, 3, hour)) return err;
    }
    if (argc > 4) {
        if (Value* err = toNumberArg(ctx, argc, argv, 4, minute)) return err;
    }
    if (argc > 5) {
        if (Value* err = toNumberArg(ctx, argc, argv, 5, second)) return err;
    }
    if (argc > 6) {
        if (Value* err = toNumberArg(ctx, argc, argv, 6, ms)) return err;
    }
    const double dayNumber = makeDay(yearOffset(year), month, day);
    const double timeNumber = makeTime(hour, minute, second, ms);
    return ctx->createDate(timeClip(makeDate(dayNumber, timeNumber)));
}

// —— 静态方法 ——

Value* dateNow(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createNumber(timeClip(nowMs()));
}

Value* dateParse(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* s = detail::toStringValue(ctx, argOrUndefined(ctx, argc, argv, 0));
    if (detail::isErrorValue(s)) return s;
    const std::string& text = static_cast<StringData*>(s->data())->value;
    double parsed = std::nan("");
    if (!parseIsoDate(text, parsed)) parseLegacyDate(text, parsed);
    return ctx->createNumber(parsed);
}

Value* dateUtc(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    double year = 0;
    double month = 0;
    double day = 1;
    double hour = 0;
    double minute = 0;
    double second = 0;
    double ms = 0;
    if (Value* err = toNumberArg(ctx, argc, argv, 0, year)) return err;
    if (argc > 1) {
        if (Value* err = toNumberArg(ctx, argc, argv, 1, month)) return err;
    }
    if (argc > 2) {
        if (Value* err = toNumberArg(ctx, argc, argv, 2, day)) return err;
    }
    if (argc > 3) {
        if (Value* err = toNumberArg(ctx, argc, argv, 3, hour)) return err;
    }
    if (argc > 4) {
        if (Value* err = toNumberArg(ctx, argc, argv, 4, minute)) return err;
    }
    if (argc > 5) {
        if (Value* err = toNumberArg(ctx, argc, argv, 5, second)) return err;
    }
    if (argc > 6) {
        if (Value* err = toNumberArg(ctx, argc, argv, 6, ms)) return err;
    }
    return ctx->createNumber(
        timeClip(makeDate(makeDay(yearOffset(year), month, day),
                          makeTime(hour, minute, second, ms))));
}

// —— 原型读取方法 ——

Value* dateGetTime(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    if (asDate(self) == nullptr) {
        return detail::typeError(ctx, "this is not a Date object");
    }
    return ctx->createNumber(dateValueOf(self));
}

Value* dateGetTimezoneOffset(IContext* ctx, Value* self, size_t /*argc*/,
                             Value* /*argv*/[]) {
    if (asDate(self) == nullptr) {
        return detail::typeError(ctx, "this is not a Date object");
    }
    if (!std::isfinite(dateValueOf(self))) return ctx->createNumber(std::nan(""));
    return ctx->createNumber(0.0);
}

enum class DateField {
    FullYear,
    Month,
    Date,
    Day,
    Hours,
    Minutes,
    Seconds,
    Milliseconds,
};

Value* dateGetField(IContext* ctx, Value* self, DateField field) {
    if (asDate(self) == nullptr) {
        return detail::typeError(ctx, "this is not a Date object");
    }
    Components c;
    if (!componentsOf(dateValueOf(self), c)) return ctx->createNumber(std::nan(""));
    switch (field) {
        case DateField::FullYear: return numberOrNaN(ctx, static_cast<double>(c.year));
        case DateField::Month: return numberOrNaN(ctx, static_cast<double>(c.month - 1));
        case DateField::Date: return numberOrNaN(ctx, static_cast<double>(c.day));
        case DateField::Day: return numberOrNaN(ctx, static_cast<double>(c.weekday));
        case DateField::Hours: return numberOrNaN(ctx, static_cast<double>(c.hour));
        case DateField::Minutes: return numberOrNaN(ctx, static_cast<double>(c.minute));
        case DateField::Seconds: return numberOrNaN(ctx, static_cast<double>(c.second));
        case DateField::Milliseconds: return numberOrNaN(ctx, static_cast<double>(c.ms));
    }
    return ctx->createNumber(std::nan(""));
}

#define NEO_DATE_GETTER(name, field)                                         \
    Value* name(IContext* ctx, Value* self, size_t, Value*[]) {              \
        return dateGetField(ctx, self, field);                               \
    }

NEO_DATE_GETTER(dateGetFullYear, DateField::FullYear)
NEO_DATE_GETTER(dateGetMonth, DateField::Month)
NEO_DATE_GETTER(dateGetDate, DateField::Date)
NEO_DATE_GETTER(dateGetDay, DateField::Day)
NEO_DATE_GETTER(dateGetHours, DateField::Hours)
NEO_DATE_GETTER(dateGetMinutes, DateField::Minutes)
NEO_DATE_GETTER(dateGetSeconds, DateField::Seconds)
NEO_DATE_GETTER(dateGetMilliseconds, DateField::Milliseconds)

#undef NEO_DATE_GETTER

// —— 原型写入方法 ——

Value* dateSetTime(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (asDate(self) == nullptr) {
        return detail::typeError(ctx, "this is not a Date object");
    }
    double t = 0;
    if (Value* err = toNumberArg(ctx, argc, argv, 0, t)) return err;
    asDate(self)->timeValue = timeClip(t);
    return ctx->createNumber(asDate(self)->timeValue);
}

// 通用 setter：读取当前组件（NaN 时按 base 处理），替换指定字段后重算时间值。
struct SetterArgs {
    DateField first = DateField::Milliseconds;
    size_t count = 0;
    double values[7] = {0, 0, 0, 0, 0, 0, 0};
};

// DateField → fields[7] 槽位（Day 不是可写字段）。
size_t fieldSlot(DateField field) {
    switch (field) {
        case DateField::FullYear: return 0;
        case DateField::Month: return 1;
        case DateField::Date: return 2;
        case DateField::Hours: return 3;
        case DateField::Minutes: return 4;
        case DateField::Seconds: return 5;
        case DateField::Milliseconds: return 6;
        case DateField::Day: break;
    }
    return 0;
}

Value* dateSetFields(IContext* ctx, Value* self, size_t argc, Value* argv[],
                     const SetterArgs& spec) {
    if (asDate(self) == nullptr) {
        return detail::typeError(ctx, "this is not a Date object");
    }
    // 先读 [[DateValue]]，再按顺序 ToNumber 全部提供的实参（可观察顺序），
    // 最后才做 NaN 判定（§21.4.4.x）。
    double t = dateValueOf(self);
    double provided[7] = {0, 0, 0, 0, 0, 0, 0};
    bool has[7] = {false, false, false, false, false, false, false};
    for (size_t i = 0; i < spec.count; ++i) {
        // 第一个实参是必填（缺失按 undefined → NaN）；其余为可选。
        if (i != 0 && i >= argc) continue;
        double value = 0;
        if (Value* err = toNumberArg(ctx, argc, argv, i, value)) return err;
        provided[i] = value;
        has[i] = true;
    }
    if (!std::isfinite(t)) {
        if (spec.first != DateField::FullYear) return ctx->createNumber(std::nan(""));
        t = 0.0;
    }
    Components c;
    componentsOf(t, c);

    double fields[7] = {static_cast<double>(c.year), static_cast<double>(c.month - 1),
                        static_cast<double>(c.day),  static_cast<double>(c.hour),
                        static_cast<double>(c.minute), static_cast<double>(c.second),
                        static_cast<double>(c.ms)};
    const size_t firstIndex = fieldSlot(spec.first);
    // 缺省参数保留当前组件值（§21.4.4.x 的可选参数语义）。
    for (size_t i = 0; i < spec.count; ++i) {
        if (has[i]) fields[firstIndex + i] = provided[i];
    }
    const double dayNumber = makeDay(fields[0], fields[1], fields[2]);
    const double timeNumber = makeTime(fields[3], fields[4], fields[5], fields[6]);
    asDate(self)->timeValue = timeClip(makeDate(dayNumber, timeNumber));
    return ctx->createNumber(asDate(self)->timeValue);
}

#define NEO_DATE_SETTER(name, field, n)                                      \
    Value* name(IContext* ctx, Value* self, size_t argc, Value* argv[]) {    \
        SetterArgs spec;                                                     \
        spec.first = field;                                                  \
        spec.count = n;                                                      \
        return dateSetFields(ctx, self, argc, argv, spec);                   \
    }

NEO_DATE_SETTER(dateSetFullYear, DateField::FullYear, 3)
NEO_DATE_SETTER(dateSetMonth, DateField::Month, 2)
NEO_DATE_SETTER(dateSetDate, DateField::Date, 1)
NEO_DATE_SETTER(dateSetHours, DateField::Hours, 4)
NEO_DATE_SETTER(dateSetMinutes, DateField::Minutes, 3)
NEO_DATE_SETTER(dateSetSeconds, DateField::Seconds, 2)
NEO_DATE_SETTER(dateSetMilliseconds, DateField::Milliseconds, 1)

#undef NEO_DATE_SETTER

// —— 格式化 ——

std::string pad(int64_t value, size_t width) {
    std::string s = std::to_string(value < 0 ? -value : value);
    if (s.size() < width) s.insert(0, width - s.size(), '0');
    return s;
}

// ToDateString 的年份：负号 + 至少 4 位（§21.4.4.41.4）。
std::string formatYear(int64_t year) {
    if (year < 0) return "-" + pad(-year, 4);
    return pad(year, 4);
}

std::string formatIso(double t) {
    Components c;
    if (!componentsOf(t, c)) return "";
    std::string year;
    if (c.year >= 0 && c.year <= 9999) {
        year = pad(c.year, 4);
    } else {
        year = (c.year < 0 ? "-" : "+") + pad(c.year < 0 ? -c.year : c.year, 6);
    }
    return year + "-" + pad(c.month, 2) + "-" + pad(c.day, 2) + "T" +
           pad(c.hour, 2) + ":" + pad(c.minute, 2) + ":" + pad(c.second, 2) + "." +
           pad(c.ms, 3) + "Z";
}

const char* const kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

Value* dateToString(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    if (asDate(self) == nullptr) {
        return detail::typeError(ctx, "this is not a Date object");
    }
    Components c;
    if (!componentsOf(dateValueOf(self), c)) {
        return ctx->createString("Invalid Date");
    }
    const std::string text =
        std::string(kWeekdays[c.weekday]) + " " + kMonths[c.month - 1] + " " +
        pad(c.day, 2) + " " + formatYear(c.year) + " " + pad(c.hour, 2) + ":" +
        pad(c.minute, 2) + ":" + pad(c.second, 2) +
        " GMT+0000 (Coordinated Universal Time)";
    return ctx->createString(text);
}

Value* dateToDateString(IContext* ctx, Value* self, size_t argc, Value* argv[]);
Value* dateToTimeString(IContext* ctx, Value* self, size_t argc, Value* argv[]);

// locale 形式：有 Intl 时委托给 %DateTimeFormat%（ECMA-402 §21.4.4.38 等），
// 否则退化为对应的非 locale 形式。
Value* dateLocaleFormat(IContext* ctx, Value* self, size_t argc, Value* argv[],
                        const char* const* defaultKeys, size_t defaultCount,
                        NativeFn fallback) {
    if (asDate(self) == nullptr) {
        return detail::typeError(ctx, "this is not a Date object");
    }
    Value* intl = ctx->global()->lookup("Intl");
    if (intl != nullptr && detail::isObjectKind(intl->type()->kind())) {
        Value* ctor = ctx->getProperty(intl, "DateTimeFormat");
        if (!detail::isErrorValue(ctor) && detail::isCallable(ctor)) {
            Value* locales = argc > 0 ? argv[0] : ctx->createUndefined();
            Value* options = argc > 1 ? argv[1] : ctx->createUndefined();
            if (options->type()->kind() == TypeKind::Undefined) {
                Value* defaults = ctx->createObject();
                if (detail::isErrorValue(defaults)) return defaults;
                for (size_t i = 0; i < defaultCount; ++i) {
                    Value* r = ctx->defineProperty(defaults, defaultKeys[i],
                                                   ctx->createString("numeric"),
                                                   true, true, true);
                    if (detail::isErrorValue(r)) return r;
                }
                options = defaults;
            }
            Value* args[2] = {locales, options};
            Value* formatter = ctx->construct(ctor, 2, args);
            if (detail::isErrorValue(formatter)) return formatter;
            Value* format = ctx->getProperty(formatter, "format");
            if (detail::isErrorValue(format)) return format;
            Value* timeValue = ctx->createNumber(dateValueOf(self));
            Value* formatArgs[1] = {timeValue};
            return ctx->call(format, formatter, 1, formatArgs);
        }
    }
    return fallback(ctx, self, argc, argv);
}

const char* const kLocaleDateTimeKeys[] = {"year", "month", "day",
                                           "hour", "minute", "second"};
const char* const kLocaleDateKeys[] = {"year", "month", "day"};
const char* const kLocaleTimeKeys[] = {"hour", "minute", "second"};

Value* dateToLocaleString(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return dateLocaleFormat(ctx, self, argc, argv, kLocaleDateTimeKeys, 6,
                            dateToString);
}

Value* dateToLocaleDateString(IContext* ctx, Value* self, size_t argc,
                              Value* argv[]) {
    return dateLocaleFormat(ctx, self, argc, argv, kLocaleDateKeys, 3,
                            dateToDateString);
}

Value* dateToLocaleTimeString(IContext* ctx, Value* self, size_t argc,
                              Value* argv[]) {
    return dateLocaleFormat(ctx, self, argc, argv, kLocaleTimeKeys, 3,
                            dateToTimeString);
}

Value* dateToIsoString(IContext* ctx, Value* self, size_t /*argc*/,
                       Value* /*argv*/[]) {
    if (asDate(self) == nullptr) {
        return detail::typeError(ctx, "this is not a Date object");
    }
    const double t = dateValueOf(self);
    if (!std::isfinite(t)) {
        return detail::rangeError(ctx, "Invalid time value");
    }
    return ctx->createString(formatIso(t));
}

// Date.prototype.toJSON（§21.4.4.37）：泛型，不要求 this 是 Date。
Value* dateToJson(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    TypeKind k = self->type()->kind();
    if (k == TypeKind::Undefined || k == TypeKind::Null) {
        return detail::typeError(ctx, "Date.prototype.toJSON called on null or undefined");
    }
    Value* obj = detail::isObjectKind(k) ? self : ctx->createPrimitiveWrapper(self);
    if (detail::isErrorValue(obj)) return obj;
    Value* tv = detail::toPrimitive(ctx, obj, detail::PreferredType::Number);
    if (detail::isErrorValue(tv)) return tv;
    if (tv->type()->kind() == TypeKind::Number &&
        !std::isfinite(static_cast<NumberData*>(tv->data())->value)) {
        return ctx->createNull();
    }
    Value* fn = ctx->getProperty(obj, "toISOString");
    if (detail::isErrorValue(fn)) return fn;
    return ctx->call(fn, obj, 0, nullptr);
}

Value* dateToUtcString(IContext* ctx, Value* self, size_t /*argc*/,
                       Value* /*argv*/[]) {
    if (asDate(self) == nullptr) {
        return detail::typeError(ctx, "this is not a Date object");
    }
    Components c;
    if (!componentsOf(dateValueOf(self), c)) {
        return ctx->createString("Invalid Date");
    }
    const std::string text =
        std::string(kWeekdays[c.weekday]) + ", " + pad(c.day, 2) + " " +
        kMonths[c.month - 1] + " " + formatYear(c.year) + " " + pad(c.hour, 2) + ":" +
        pad(c.minute, 2) + ":" + pad(c.second, 2) + " GMT";
    return ctx->createString(text);
}

Value* dateToDateString(IContext* ctx, Value* self, size_t /*argc*/,
                        Value* /*argv*/[]) {
    if (asDate(self) == nullptr) {
        return detail::typeError(ctx, "this is not a Date object");
    }
    Components c;
    if (!componentsOf(dateValueOf(self), c)) {
        return ctx->createString("Invalid Date");
    }
    return ctx->createString(std::string(kWeekdays[c.weekday]) + " " +
                             kMonths[c.month - 1] + " " + pad(c.day, 2) + " " +
                             formatYear(c.year));
}

Value* dateToTimeString(IContext* ctx, Value* self, size_t /*argc*/,
                        Value* /*argv*/[]) {
    if (asDate(self) == nullptr) {
        return detail::typeError(ctx, "this is not a Date object");
    }
    Components c;
    if (!componentsOf(dateValueOf(self), c)) {
        return ctx->createString("Invalid Date");
    }
    return ctx->createString(pad(c.hour, 2) + ":" + pad(c.minute, 2) + ":" +
                             pad(c.second, 2) +
                             " GMT+0000 (Coordinated Universal Time)");
}

// Date.prototype[@@toPrimitive]（§21.4.4.9）
Value* dateToPrimitive(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (!detail::isObjectKind(self->type()->kind())) {
        return detail::typeError(ctx, "Date.prototype[@@toPrimitive] called on non-object");
    }
    Value* hint = argOrUndefined(ctx, argc, argv, 0);
    if (hint->type()->kind() != TypeKind::String) {
        return detail::typeError(ctx, "invalid hint");
    }
    const std::string& hintText = static_cast<StringData*>(hint->data())->value;
    bool stringFirst;
    if (hintText == "default" || hintText == "string") {
        stringFirst = true;
    } else if (hintText == "number") {
        stringFirst = false;
    } else {
        return detail::typeError(ctx, "invalid hint");
    }
    const char* names[2] = {stringFirst ? "toString" : "valueOf",
                            stringFirst ? "valueOf" : "toString"};
    for (const char* name : names) {
        Value* fn = ctx->getProperty(self, name);
        if (detail::isErrorValue(fn)) return fn;
        if (!detail::isCallable(fn)) continue;
        Value* r = ctx->call(fn, self, 0, nullptr);
        if (detail::isErrorValue(r)) return r;
        if (!detail::isObjectKind(r->type()->kind())) return r;
    }
    return detail::typeError(ctx, "cannot convert object to primitive value");
}

// —— 注册辅助 ——

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t length) {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    // 最小实参数为 0（Date 方法均允许缺参），length 单独定义。
    Value* f = ctx->createNativeFunction(fn, 0, {}, opts);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    if (ctx->defineProperty(f, "length", ctx->createNumber(static_cast<double>(length)),
                            false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

bool defineSymbolMethod(IContext* ctx, Value* target, WellKnownSymbol which,
                        const char* name, NativeFn fn, size_t length,
                        bool writable = true) {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* f = ctx->createNativeFunction(fn, 0, {}, opts);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    if (ctx->defineProperty(f, "length", ctx->createNumber(static_cast<double>(length)),
                            false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    return ctx->defineProperty(target, ctx->getWellKnownSymbol(which), f, writable,
                               false, true)
               ->type()->kind() != TypeKind::Exception;
}

}  // namespace

bool registerDateBuiltins(IContext* ctx) {
    // —— 1. %Date.prototype%（原型 = Object.prototype）——
    Value* proto = ctx->createObject();
    if (proto == ctx->oomValue()) return false;
    if (ctx->setPrototype(proto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setDatePrototype(proto);

    // —— 2. Date 构造函数 ——
    Value* dateFn = ctx->createNativeFunction(dateCall, 0);
    if (dateFn == ctx->oomValue()) return false;
    ctx->setFunctionName(dateFn, "Date");
    if (ctx->defineProperty(dateFn, "length", ctx->createNumber(7.0), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    ctx->setFunctionConstructor(dateFn, dateConstruct);
    if (ctx->defineProperty(dateFn, "prototype", proto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(proto, "constructor", dateFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 3. 静态方法 ——
    if (!defineMethod(ctx, dateFn, "now", dateNow, 0)) return false;
    if (!defineMethod(ctx, dateFn, "parse", dateParse, 1)) return false;
    if (!defineMethod(ctx, dateFn, "UTC", dateUtc, 7)) return false;

    // —— 4. 原型读取方法（本地时间与 UTC 相同）——
    if (!defineMethod(ctx, proto, "getTime", dateGetTime, 0)) return false;
    if (!defineMethod(ctx, proto, "valueOf", dateGetTime, 0)) return false;
    if (!defineMethod(ctx, proto, "getTimezoneOffset", dateGetTimezoneOffset, 0))
        return false;
    const struct {
        const char* name;
        NativeFn fn;
    } getters[] = {
        {"getFullYear", dateGetFullYear},   {"getUTCFullYear", dateGetFullYear},
        {"getMonth", dateGetMonth},         {"getUTCMonth", dateGetMonth},
        {"getDate", dateGetDate},           {"getUTCDate", dateGetDate},
        {"getDay", dateGetDay},             {"getUTCDay", dateGetDay},
        {"getHours", dateGetHours},         {"getUTCHours", dateGetHours},
        {"getMinutes", dateGetMinutes},     {"getUTCMinutes", dateGetMinutes},
        {"getSeconds", dateGetSeconds},     {"getUTCSeconds", dateGetSeconds},
        {"getMilliseconds", dateGetMilliseconds},
        {"getUTCMilliseconds", dateGetMilliseconds},
    };
    for (const auto& g : getters) {
        if (!defineMethod(ctx, proto, g.name, g.fn, 0)) return false;
    }

    // —— 5. 原型写入方法 ——
    if (!defineMethod(ctx, proto, "setTime", dateSetTime, 1)) return false;
    const struct {
        const char* name;
        NativeFn fn;
        size_t length;
    } setters[] = {
        {"setFullYear", dateSetFullYear, 3},   {"setUTCFullYear", dateSetFullYear, 3},
        {"setMonth", dateSetMonth, 2},         {"setUTCMonth", dateSetMonth, 2},
        {"setDate", dateSetDate, 1},           {"setUTCDate", dateSetDate, 1},
        {"setHours", dateSetHours, 4},         {"setUTCHours", dateSetHours, 4},
        {"setMinutes", dateSetMinutes, 3},     {"setUTCMinutes", dateSetMinutes, 3},
        {"setSeconds", dateSetSeconds, 2},     {"setUTCSeconds", dateSetSeconds, 2},
        {"setMilliseconds", dateSetMilliseconds, 1},
        {"setUTCMilliseconds", dateSetMilliseconds, 1},
    };
    for (const auto& s : setters) {
        if (!defineMethod(ctx, proto, s.name, s.fn, s.length)) return false;
    }

    // —— 6. 格式化 ——
    if (!defineMethod(ctx, proto, "toString", dateToString, 0)) return false;
    if (!defineMethod(ctx, proto, "toISOString", dateToIsoString, 0)) return false;
    if (!defineMethod(ctx, proto, "toJSON", dateToJson, 1)) return false;
    if (!defineMethod(ctx, proto, "toUTCString", dateToUtcString, 0)) return false;
    if (!defineMethod(ctx, proto, "toGMTString", dateToUtcString, 0)) return false;
    if (!defineMethod(ctx, proto, "toDateString", dateToDateString, 0)) return false;
    if (!defineMethod(ctx, proto, "toTimeString", dateToTimeString, 0)) return false;
    // locale 形式：有 Intl 时委托给 %DateTimeFormat%，否则退化为非 locale 形式。
    if (!defineMethod(ctx, proto, "toLocaleString", dateToLocaleString, 0)) return false;
    if (!defineMethod(ctx, proto, "toLocaleDateString", dateToLocaleDateString, 0))
        return false;
    if (!defineMethod(ctx, proto, "toLocaleTimeString", dateToLocaleTimeString, 0))
        return false;
    if (!defineSymbolMethod(ctx, proto, WellKnownSymbol::ToPrimitive,
                            "[Symbol.toPrimitive]", dateToPrimitive, 1, false)) {
        return false;
    }
    // 注：%Date.prototype% 规范上**没有** @@toStringTag，
    // "Date" 内建 tag 由 Object.prototype.toString 按 [[DateValue]] 给出。

    ctx->defineGlobal("Date", dateFn, true, false, true);
    return true;
}

}  // namespace neo::core
