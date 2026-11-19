// intl/MinimalIntlBackend.cpp - core 内置的最小 Intl 后端
//
// 设计要点：
//   - 只支持 en / latn / gregory / UTC，输出确定，便于单测；
//   - 数字格式化覆盖 decimal / percent / currency 的基本路径；
//   - formatToParts 返回结构化字段，供 core 组装 JS 对象；
//   - 不支持的选项返回 NJS_INTL_UNSUPPORTED / NJS_INTL_INVALID_OPTION，
//     由 core 映射为 RangeError / TypeError。
//
// 关联 ADR: 020（标准库组织）

#include "core/intl/MinimalIntlBackend.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr size_t kBufferSize = 512;

bool equals(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isAlnum(char c) { return isAlpha(c) || isDigit(c); }

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

char upper(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// BCP 47 子集 canonicalize：language[-script][-region][-variant...][-extension...]。
bool canonicalizeTag(const std::string& input, std::string& out) {
    if (input.empty()) return false;
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t dash = input.find('-', start);
        const std::string part = input.substr(
            start, dash == std::string::npos ? std::string::npos : dash - start);
        if (part.empty() || part.size() > 8) return false;
        for (char c : part) {
            if (!isAlnum(c)) return false;
        }
        parts.push_back(part);
        if (dash == std::string::npos) break;
        start = dash + 1;
    }

    size_t i = 0;
    const std::string& language = parts[i];
    if (language.size() < 2 || language.size() > 8 ||
        (language.size() == 4)) {
        return false;
    }
    for (char c : language) {
        if (!isAlpha(c)) return false;
    }
    out.clear();
    for (char c : language) out.push_back(lower(c));
    ++i;

    if (i < parts.size() && parts[i].size() == 4) {
        bool alpha = true;
        for (char c : parts[i]) alpha = alpha && isAlpha(c);
        if (alpha) {
            out.push_back('-');
            out.push_back(upper(parts[i][0]));
            for (size_t k = 1; k < parts[i].size(); ++k) {
                out.push_back(lower(parts[i][k]));
            }
            ++i;
        }
    }

    if (i < parts.size()) {
        const std::string& region = parts[i];
        bool ok = (region.size() == 2 && isAlpha(region[0]) && isAlpha(region[1])) ||
                  (region.size() == 3 && isDigit(region[0]) && isDigit(region[1]) &&
                   isDigit(region[2]));
        if (ok) {
            out.push_back('-');
            for (char c : region) out.push_back(upper(c));
            ++i;
        }
    }

    // 其余 subtag（variant / extension / private use）原样小写保留。
    for (; i < parts.size(); ++i) {
        out.push_back('-');
        for (char c : parts[i]) out.push_back(lower(c));
    }
    return true;
}

NJS_IntlStatus copyOut(const std::string& value, char* out, size_t* out_len) {
    if (out_len == nullptr) return NJS_INTL_INTERNAL;
    const size_t required = value.size() + 1;
    if (out == nullptr || *out_len < required) {
        *out_len = required;
        return NJS_INTL_OK;
    }
    std::memcpy(out, value.c_str(), required);
    *out_len = required;
    return NJS_INTL_OK;
}

const char* const kAvailableLocales[] = {"en", nullptr};

NJS_IntlStatus canonicalizeLocale(void* /*ud*/, const char* tag, size_t tag_len,
                                  char* out, size_t* out_len) {
    if (tag == nullptr) return NJS_INTL_INVALID_OPTION;
    std::string canonical;
    if (!canonicalizeTag(std::string(tag, tag_len), canonical)) {
        return NJS_INTL_INVALID_OPTION;
    }
    return copyOut(canonical, out, out_len);
}

const char* const* availableLocales(void* /*ud*/, size_t* count) {
    if (count != nullptr) *count = 1;
    return kAvailableLocales;
}

NJS_IntlStatus defaultLocale(void* /*ud*/, char* out, size_t* out_len) {
    return copyOut("en", out, out_len);
}

NJS_IntlStatus defaultTimeZone(void* /*ud*/, char* out, size_t* out_len) {
    return copyOut("UTC", out, out_len);
}

struct Part {
    NJS_IntlPartType type;
    std::string text;
};

}  // namespace

// C ABI 的不透明格式化器。定义在全局作用域以匹配 C 的 struct tag。
struct NJS_IntlFormatter {
    enum class Kind { Number, DateTime };

    Kind kind = Kind::Number;
    std::string locale;
    std::string numbering_system = "latn";
    std::vector<Part> last_parts;

    // —— Number ——
    std::string style = "decimal";
    std::string currency;
    std::string currency_display = "symbol";
    std::string sign_display = "auto";
    bool use_grouping = true;
    int min_int = 1;
    int min_frac = 0;
    int max_frac = 3;

    // —— DateTime ——
    std::string date_style;
    std::string time_style;
    std::string time_zone = "UTC";
    std::string hour_cycle;
    std::string time_zone_name;
    bool hour12 = false;
    bool has_year = false;
    bool has_month = false;
    bool has_day = false;
    bool has_hour = false;
    bool has_minute = false;
    bool has_second = false;
    bool year_2digit = false;
    bool month_2digit = false;
    bool day_2digit = false;
    bool hour_2digit = false;
    bool minute_2digit = false;
    bool second_2digit = false;
    int fractional_second_digits = -1;
};

struct NJS_IntlCollator {
    std::string locale;
    std::string usage = "sort";
    std::string sensitivity = "variant";
    std::string case_first = "false";
    std::string collation = "default";
    bool ignore_punctuation = false;
    bool numeric = false;
};

namespace {

const char* findOption(const NJS_IntlOption* opts, size_t nopts, const char* key) {
    for (size_t i = 0; i < nopts; ++i) {
        if (opts[i].key != nullptr && equals(opts[i].key, key)) return opts[i].value;
    }
    return nullptr;
}

bool parseBool(const char* value, bool& out) {
    if (value == nullptr) return false;
    if (equals(value, "true") || equals(value, "always") || equals(value, "auto")) {
        out = true;
        return true;
    }
    if (equals(value, "false") || equals(value, "never")) {
        out = false;
        return true;
    }
    return false;
}

bool parseInt(const char* value, int min, int max, int& out) {
    if (value == nullptr) return false;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0') return false;
    if (parsed < min || parsed > max) return false;
    out = static_cast<int>(parsed);
    return true;
}

bool isCurrencyCode(const std::string& code) {
    if (code.size() != 3) return false;
    for (char c : code) {
        if (!isAlpha(c)) return false;
    }
    return true;
}

const char* currencySymbol(const std::string& code) {
    if (code == "USD") return "$";
    if (code == "EUR") return "\xE2\x82\xAC";  // €
    if (code == "GBP") return "\xC2\xA3";      // £
    if (code == "JPY") return "\xC2\xA5";      // ¥
    if (code == "CNY") return "\xC2\xA5";      // ¥
    if (code == "KRW") return "\xE2\x82\xA9";  // ₩
    return nullptr;
}

std::string trimFraction(std::string fraction, int min_frac) {
    while (static_cast<int>(fraction.size()) > min_frac && !fraction.empty() &&
           fraction.back() == '0') {
        fraction.pop_back();
    }
    return fraction;
}

void appendGrouped(std::vector<Part>& parts, const std::string& digits) {
    const size_t n = digits.size();
    for (size_t i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) {
            parts.push_back(Part{NJS_INTL_PART_GROUP, ","});
        }
        parts.push_back(Part{NJS_INTL_PART_INTEGER, std::string(1, digits[i])});
    }
}

NJS_IntlStatus formatNumber(NJS_IntlFormatter* f, double value,
                            std::vector<Part>& parts) {
    parts.clear();

    const bool negative = std::signbit(value) && !std::isnan(value);
    const bool is_nan = std::isnan(value);
    const bool is_inf = std::isinf(value);

    bool show_sign = false;
    bool sign_is_negative = negative;
    if (f->sign_display == "always") {
        show_sign = true;
    } else if (f->sign_display == "never") {
        show_sign = false;
    } else if (f->sign_display == "exceptZero") {
        show_sign = value != 0.0 || is_nan || is_inf;
    } else if (f->sign_display == "negative") {
        show_sign = negative;
    } else {  // auto
        show_sign = negative;
    }

    if (show_sign) {
        parts.push_back(Part{sign_is_negative ? NJS_INTL_PART_MINUS_SIGN
                                              : NJS_INTL_PART_PLUS_SIGN,
                             sign_is_negative ? "-" : "+"});
    }

    if (is_nan) {
        parts.push_back(Part{NJS_INTL_PART_LITERAL, "NaN"});
        return NJS_INTL_OK;
    }

    if (f->style == "currency") {
        if (f->currency_display == "code" || f->currency_display == "name") {
            parts.push_back(Part{NJS_INTL_PART_CURRENCY, f->currency + " "});
        } else {
            const char* symbol = currencySymbol(f->currency);
            if (symbol != nullptr) {
                parts.push_back(Part{NJS_INTL_PART_CURRENCY, symbol});
            } else {
                parts.push_back(Part{NJS_INTL_PART_CURRENCY, f->currency + " "});
            }
        }
    }

    if (is_inf) {
        parts.push_back(Part{NJS_INTL_PART_LITERAL, "\xE2\x88\x9E"});  // ∞
    } else {
        double abs = std::fabs(value);
        if (f->style == "percent") abs *= 100.0;

        char buffer[kBufferSize];
        auto result = std::to_chars(buffer, buffer + sizeof buffer, abs,
                                    std::chars_format::fixed, f->max_frac);
        std::string text;
        if (result.ec == std::errc()) {
            text.assign(buffer, result.ptr);
        } else {
            text = "0";
        }

        std::string integer = text;
        std::string fraction;
        const size_t dot = text.find('.');
        if (dot != std::string::npos) {
            integer = text.substr(0, dot);
            fraction = trimFraction(text.substr(dot + 1), f->min_frac);
        }

        if (static_cast<int>(integer.size()) < f->min_int) {
            integer.insert(0, static_cast<size_t>(f->min_int) - integer.size(), '0');
        }

        if (f->use_grouping) {
            appendGrouped(parts, integer);
        } else {
            parts.push_back(Part{NJS_INTL_PART_INTEGER, integer});
        }

        if (!fraction.empty()) {
            parts.push_back(Part{NJS_INTL_PART_DECIMAL, "."});
            parts.push_back(Part{NJS_INTL_PART_FRACTION, fraction});
        }
    }

    if (f->style == "percent") {
        parts.push_back(Part{NJS_INTL_PART_PERCENT, "%"});
    }
    return NJS_INTL_OK;
}

NJS_IntlFormatter* numberFormatCreate(void* /*ud*/, const char* locale,
                                      const NJS_IntlOption* opts, size_t nopts,
                                      NJS_IntlResolved* resolved,
                                      NJS_IntlStatus* status) {
    auto fail = [status](NJS_IntlStatus s) -> NJS_IntlFormatter* {
        if (status != nullptr) *status = s;
        return nullptr;
    };

    if (locale == nullptr) return fail(NJS_INTL_INVALID_OPTION);

    auto* f = new (std::nothrow) NJS_IntlFormatter();
    if (f == nullptr) return fail(NJS_INTL_INTERNAL);
    f->locale = locale;

    const char* style = findOption(opts, nopts, "style");
    if (style != nullptr) {
        if (!equals(style, "decimal") && !equals(style, "percent") &&
            !equals(style, "currency")) {
            delete f;
            return fail(NJS_INTL_INVALID_OPTION);
        }
        f->style = style;
    }

    const char* currency = findOption(opts, nopts, "currency");
    if (f->style == "currency") {
        if (currency == nullptr) {
            delete f;
            return fail(NJS_INTL_INVALID_OPTION);
        }
        f->currency = currency;
        for (char& c : f->currency) c = upper(c);
        if (!isCurrencyCode(f->currency)) {
            delete f;
            return fail(NJS_INTL_INVALID_OPTION);
        }
    }

    const char* currency_display = findOption(opts, nopts, "currencyDisplay");
    if (currency_display != nullptr) {
        if (!equals(currency_display, "symbol") && !equals(currency_display, "code") &&
            !equals(currency_display, "name")) {
            delete f;
            return fail(NJS_INTL_INVALID_OPTION);
        }
        f->currency_display = currency_display;
    }

    const char* notation = findOption(opts, nopts, "notation");
    if (notation != nullptr && !equals(notation, "standard")) {
        delete f;
        return fail(NJS_INTL_UNSUPPORTED);
    }

    const char* numbering = findOption(opts, nopts, "numberingSystem");
    if (numbering != nullptr) {
        if (!equals(numbering, "latn")) {
            delete f;
            return fail(NJS_INTL_UNSUPPORTED);
        }
        f->numbering_system = numbering;
    }

    const char* sign_display = findOption(opts, nopts, "signDisplay");
    if (sign_display != nullptr) {
        if (!equals(sign_display, "auto") && !equals(sign_display, "always") &&
            !equals(sign_display, "never") && !equals(sign_display, "exceptZero") &&
            !equals(sign_display, "negative")) {
            delete f;
            return fail(NJS_INTL_INVALID_OPTION);
        }
        f->sign_display = sign_display;
    }

    const char* grouping = findOption(opts, nopts, "useGrouping");
    if (grouping != nullptr && !parseBool(grouping, f->use_grouping)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }

    const char* min_int = findOption(opts, nopts, "minimumIntegerDigits");
    if (min_int != nullptr && !parseInt(min_int, 1, 21, f->min_int)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }

    const int default_currency_digits = f->style == "currency" ? 2 : 0;
    const int default_max_frac = f->style == "currency" ? 2 : 3;

    const char* min_frac = findOption(opts, nopts, "minimumFractionDigits");
    if (min_frac != nullptr && !parseInt(min_frac, 0, 100, f->min_frac)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    } else if (min_frac == nullptr) {
        f->min_frac = default_currency_digits;
    }

    const char* max_frac = findOption(opts, nopts, "maximumFractionDigits");
    if (max_frac != nullptr && !parseInt(max_frac, 0, 100, f->max_frac)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    } else if (max_frac == nullptr) {
        f->max_frac = std::max(f->min_frac, default_max_frac);
    }
    if (f->max_frac < f->min_frac) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }

    // 明确不支持但规范存在的选项：显式报 UNSUPPORTED，避免静默错误。
    static const char* kUnsupported[] = {
        "minimumSignificantDigits", "maximumSignificantDigits", "compactDisplay",
        "unit",                     "unitDisplay",               "currencySign",
        "roundingMode",             "trailingZeroDisplay",       "roundingPriority",
        "roundingIncrement",
    };
    for (const char* key : kUnsupported) {
        if (findOption(opts, nopts, key) != nullptr) {
            delete f;
            return fail(NJS_INTL_UNSUPPORTED);
        }
    }

    if (resolved != nullptr) {
        std::memset(resolved, 0, sizeof *resolved);
        auto copyFixed = [](char* dst, size_t size, const char* src) {
            std::snprintf(dst, size, "%s", src);
        };
        copyFixed(resolved->locale, sizeof resolved->locale, f->locale.c_str());
        copyFixed(resolved->numbering_system, sizeof resolved->numbering_system,
                  f->numbering_system.c_str());
        copyFixed(resolved->calendar, sizeof resolved->calendar, "gregory");
        copyFixed(resolved->time_zone, sizeof resolved->time_zone, "UTC");
    }
    if (status != nullptr) *status = NJS_INTL_OK;
    return f;
}

NJS_IntlStatus numberFormatFormat(NJS_IntlFormatter* formatter, double value,
                                  NJS_IntlPart* parts, size_t* count) {
    if (formatter == nullptr || count == nullptr) return NJS_INTL_INTERNAL;
    std::vector<Part> built;
    NJS_IntlStatus status = formatNumber(formatter, value, built);
    if (status != NJS_INTL_OK) return status;
    formatter->last_parts = std::move(built);

    const size_t required = formatter->last_parts.size();
    if (parts == nullptr || *count < required) {
        *count = required;
        return NJS_INTL_OK;
    }
    for (size_t i = 0; i < required; ++i) {
        parts[i].type = formatter->last_parts[i].type;
        parts[i].text = formatter->last_parts[i].text.c_str();
        parts[i].length = formatter->last_parts[i].text.size();
    }
    *count = required;
    return NJS_INTL_OK;
}

// —— DateTimeFormat ——

// Howard Hinnant civil-from-days（proleptic Gregorian）。
void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe =
        (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    d = doy - (153u * mp + 2u) / 5u + 1u;
    m = mp < 10u ? mp + 3u : mp - 9u;
    y += m <= 2 ? 1 : 0;
}

std::string pad(int value, int width) {
    std::string text = std::to_string(value);
    if (static_cast<int>(text.size()) < width) {
        text.insert(0, static_cast<size_t>(width) - text.size(), '0');
    }
    return text;
}

bool parseDateStyle(const char* value, std::string& out) {
    if (value == nullptr) return true;
    if (equals(value, "full") || equals(value, "long") || equals(value, "medium") ||
        equals(value, "short")) {
        out = value;
        return true;
    }
    return false;
}

NJS_IntlFormatter* dateTimeFormatCreate(void* /*ud*/, const char* locale,
                                        const NJS_IntlOption* opts, size_t nopts,
                                        NJS_IntlResolved* resolved,
                                        NJS_IntlStatus* status) {
    auto fail = [status](NJS_IntlStatus s) -> NJS_IntlFormatter* {
        if (status != nullptr) *status = s;
        return nullptr;
    };
    if (locale == nullptr) return fail(NJS_INTL_INVALID_OPTION);

    auto* f = new (std::nothrow) NJS_IntlFormatter();
    if (f == nullptr) return fail(NJS_INTL_INTERNAL);
    f->kind = NJS_IntlFormatter::Kind::DateTime;
    f->locale = locale;

    if (!parseDateStyle(findOption(opts, nopts, "dateStyle"), f->date_style)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }
    if (!parseDateStyle(findOption(opts, nopts, "timeStyle"), f->time_style)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }

    const char* time_zone = findOption(opts, nopts, "timeZone");
    if (time_zone != nullptr) {
        if (!equals(time_zone, "UTC") && !equals(time_zone, "Etc/UTC") &&
            !equals(time_zone, "Etc/GMT")) {
            delete f;
            return fail(NJS_INTL_UNSUPPORTED);
        }
        f->time_zone = "UTC";
    }

    const char* calendar = findOption(opts, nopts, "calendar");
    if (calendar != nullptr && !equals(calendar, "gregory")) {
        delete f;
        return fail(NJS_INTL_UNSUPPORTED);
    }
    const char* numbering = findOption(opts, nopts, "numberingSystem");
    if (numbering != nullptr) {
        if (!equals(numbering, "latn")) {
            delete f;
            return fail(NJS_INTL_UNSUPPORTED);
        }
        f->numbering_system = numbering;
    }

    const char* hour12 = findOption(opts, nopts, "hour12");
    if (hour12 != nullptr && !parseBool(hour12, f->hour12)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }
    const char* hour_cycle = findOption(opts, nopts, "hourCycle");
    if (hour_cycle != nullptr) {
        if (!equals(hour_cycle, "h11") && !equals(hour_cycle, "h12") &&
            !equals(hour_cycle, "h23") && !equals(hour_cycle, "h24")) {
            delete f;
            return fail(NJS_INTL_INVALID_OPTION);
        }
        f->hour_cycle = hour_cycle;
        f->hour12 = equals(hour_cycle, "h11") || equals(hour_cycle, "h12");
    }

    auto component = [&](const char* key, bool& present, bool& two_digit) -> bool {
        const char* value = findOption(opts, nopts, key);
        if (value == nullptr) return true;
        if (equals(value, "numeric")) {
            present = true;
            two_digit = false;
            return true;
        }
        if (equals(value, "2-digit")) {
            present = true;
            two_digit = true;
            return true;
        }
        return false;
    };

    bool any_component = false;
    auto mark = [&](bool present) { any_component = any_component || present; };
    if (!component("year", f->has_year, f->year_2digit)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }
    if (!component("month", f->has_month, f->month_2digit)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }
    if (!component("day", f->has_day, f->day_2digit)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }
    if (!component("hour", f->has_hour, f->hour_2digit)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }
    if (!component("minute", f->has_minute, f->minute_2digit)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }
    if (!component("second", f->has_second, f->second_2digit)) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }
    mark(f->has_year);
    mark(f->has_month);
    mark(f->has_day);
    mark(f->has_hour);
    mark(f->has_minute);
    mark(f->has_second);

    const char* fractional = findOption(opts, nopts, "fractionalSecondDigits");
    if (fractional != nullptr) {
        if (!parseInt(fractional, 0, 3, f->fractional_second_digits)) {
            delete f;
            return fail(NJS_INTL_INVALID_OPTION);
        }
        any_component = true;
    }

    const char* tz_name = findOption(opts, nopts, "timeZoneName");
    if (tz_name != nullptr) {
        if (!equals(tz_name, "short") && !equals(tz_name, "long")) {
            delete f;
            return fail(NJS_INTL_UNSUPPORTED);
        }
        f->time_zone_name = tz_name;
        any_component = true;
    }

    // 最小后端不支持 weekday / era / 长月份名等；显式报错。
    static const char* kUnsupported[] = {"weekday", "era"};
    for (const char* key : kUnsupported) {
        if (findOption(opts, nopts, key) != nullptr) {
            delete f;
            return fail(NJS_INTL_UNSUPPORTED);
        }
    }
    const char* month = findOption(opts, nopts, "month");
    if (month != nullptr && (equals(month, "long") || equals(month, "short") ||
                             equals(month, "narrow"))) {
        delete f;
        return fail(NJS_INTL_UNSUPPORTED);
    }

    const bool has_style = !f->date_style.empty() || !f->time_style.empty();
    if (has_style && any_component) {
        delete f;
        return fail(NJS_INTL_INVALID_OPTION);
    }
    if (!has_style && !any_component) {
        f->has_year = true;
        f->has_month = true;
        f->has_day = true;
    }
    if (!f->date_style.empty()) {
        f->has_year = true;
        f->has_month = true;
        f->has_day = true;
        f->month_2digit = true;
        f->day_2digit = true;
    }
    if (!f->time_style.empty()) {
        f->has_hour = true;
        f->has_minute = true;
        f->has_second = true;
        f->hour_2digit = true;
        f->minute_2digit = true;
        f->second_2digit = true;
    }

    if (resolved != nullptr) {
        std::memset(resolved, 0, sizeof *resolved);
        auto copyFixed = [](char* dst, size_t size, const char* src) {
            std::snprintf(dst, size, "%s", src);
        };
        copyFixed(resolved->locale, sizeof resolved->locale, f->locale.c_str());
        copyFixed(resolved->numbering_system, sizeof resolved->numbering_system,
                  f->numbering_system.c_str());
        copyFixed(resolved->calendar, sizeof resolved->calendar, "gregory");
        copyFixed(resolved->time_zone, sizeof resolved->time_zone, f->time_zone.c_str());
    }
    if (status != nullptr) *status = NJS_INTL_OK;
    return f;
}

NJS_IntlStatus dateTimeFormatFormat(NJS_IntlFormatter* f, double time,
                                    NJS_IntlPart* parts, size_t* count) {
    if (f == nullptr || count == nullptr) return NJS_INTL_INTERNAL;
    if (std::isnan(time)) return NJS_INTL_INVALID_VALUE;

    std::vector<Part> built;
    const double day_ms = 86400000.0;
    const double days_d = std::floor(time / day_ms);
    double ms_of_day = time - days_d * day_ms;
    if (ms_of_day < 0.0) ms_of_day += day_ms;
    int year = 1970;
    unsigned month = 1;
    unsigned day = 1;
    civilFromDays(static_cast<int64_t>(days_d), year, month, day);

    const int ms = static_cast<int>(ms_of_day);
    int hour = ms / 3600000;
    const int minute = (ms / 60000) % 60;
    const int second = (ms / 1000) % 60;
    const int millis = ms % 1000;

    const bool has_date = f->has_year || f->has_month || f->has_day;
    const bool has_time = f->has_hour || f->has_minute || f->has_second ||
                          f->fractional_second_digits >= 0;

    if (has_date) {
        bool first = true;
        if (f->has_year) {
            const int display_year = f->year_2digit ? year % 100 : year;
            built.push_back(Part{NJS_INTL_PART_YEAR,
                                 pad(display_year, f->year_2digit ? 2 : 4)});
            first = false;
        }
        if (f->has_month) {
            if (!first) built.push_back(Part{NJS_INTL_PART_LITERAL, "-"});
            built.push_back(Part{NJS_INTL_PART_MONTH,
                                 pad(static_cast<int>(month),
                                     f->month_2digit ? 2 : 1)});
            first = false;
        }
        if (f->has_day) {
            if (!first) built.push_back(Part{NJS_INTL_PART_LITERAL, "-"});
            built.push_back(Part{NJS_INTL_PART_DAY,
                                 pad(static_cast<int>(day), f->day_2digit ? 2 : 1)});
        }
    }

    if (has_date && has_time) {
        built.push_back(Part{NJS_INTL_PART_LITERAL, " "});
    }

    if (has_time) {
        bool first = true;
        if (f->has_hour) {
            int display_hour = hour;
            if (f->hour12) {
                display_hour = hour % 12;
                if (display_hour == 0) display_hour = 12;
            }
            built.push_back(Part{NJS_INTL_PART_HOUR,
                                 pad(display_hour, f->hour_2digit ? 2 : 1)});
            first = false;
        }
        if (f->has_minute) {
            if (!first) built.push_back(Part{NJS_INTL_PART_LITERAL, ":"});
            built.push_back(Part{NJS_INTL_PART_MINUTE, pad(minute, 2)});
            first = false;
        }
        if (f->has_second) {
            if (!first) built.push_back(Part{NJS_INTL_PART_LITERAL, ":"});
            built.push_back(Part{NJS_INTL_PART_SECOND, pad(second, 2)});
            first = false;
        }
        if (f->fractional_second_digits > 0) {
            const int digits = f->fractional_second_digits;
            int divisor = 1;
            for (int i = digits; i < 3; ++i) divisor *= 10;
            built.push_back(Part{NJS_INTL_PART_LITERAL, "."});
            built.push_back(Part{NJS_INTL_PART_FRACTION,
                                 pad(millis / divisor, digits)});
            first = false;
        }
        if (f->hour12) {
            built.push_back(Part{NJS_INTL_PART_LITERAL, " "});
            built.push_back(Part{NJS_INTL_PART_DAY_PERIOD, hour < 12 ? "AM" : "PM"});
        }
        if (!f->time_zone_name.empty()) {
            built.push_back(Part{NJS_INTL_PART_LITERAL, " "});
            built.push_back(Part{NJS_INTL_PART_TIME_ZONE_NAME, "UTC"});
        }
    } else if (!f->time_zone_name.empty()) {
        built.push_back(Part{NJS_INTL_PART_LITERAL, " "});
        built.push_back(Part{NJS_INTL_PART_TIME_ZONE_NAME, "UTC"});
    }

    f->last_parts = std::move(built);
    const size_t required = f->last_parts.size();
    if (parts == nullptr || *count < required) {
        *count = required;
        return NJS_INTL_OK;
    }
    for (size_t i = 0; i < required; ++i) {
        parts[i].type = f->last_parts[i].type;
        parts[i].text = f->last_parts[i].text.c_str();
        parts[i].length = f->last_parts[i].text.size();
    }
    *count = required;
    return NJS_INTL_OK;
}

// —— Collator ——

std::vector<uint32_t> decodeUtf8(const char* text, size_t length) {
    std::vector<uint32_t> out;
    out.reserve(length);
    size_t i = 0;
    while (i < length) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t cp = c;
        size_t extra = 0;
        if (c < 0x80) {
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            out.push_back(c);
            ++i;
            continue;
        }
        if (i + extra >= length) {
            out.push_back(c);
            ++i;
            continue;
        }
        bool ok = true;
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) {
            out.push_back(c);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

bool isAsciiPunctuation(uint32_t cp) {
    return (cp >= 33 && cp <= 47) || (cp >= 58 && cp <= 64) ||
           (cp >= 91 && cp <= 96) || (cp >= 123 && cp <= 126);
}

bool isCombiningMark(uint32_t cp) { return cp >= 0x0300 && cp <= 0x036F; }

uint32_t foldCase(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp - 'A' + 'a';
    return cp;
}

std::vector<uint32_t> transform(const NJS_IntlCollator* c,
                                const std::vector<uint32_t>& input) {
    std::vector<uint32_t> out;
    out.reserve(input.size());
    const bool ignore_case = c->sensitivity != "variant";
    const bool ignore_accent =
        c->sensitivity == "base" || c->sensitivity == "accent";
    for (uint32_t cp : input) {
        if (c->ignore_punctuation && isAsciiPunctuation(cp)) continue;
        if (ignore_accent && isCombiningMark(cp)) continue;
        if (ignore_case) cp = foldCase(cp);
        out.push_back(cp);
    }
    return out;
}

int compareNumeric(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = a[i] >= '0' && a[i] <= '9';
        const bool db = b[j] >= '0' && b[j] <= '9';
        if (da && db) {
            size_t ia = i;
            size_t jb = j;
            while (ia < a.size() && a[ia] == '0') ++ia;
            while (jb < b.size() && b[jb] == '0') ++jb;
            size_t ea = ia;
            size_t eb = jb;
            while (ea < a.size() && a[ea] >= '0' && a[ea] <= '9') ++ea;
            while (eb < b.size() && b[eb] >= '0' && b[eb] <= '9') ++eb;
            const size_t la = ea - ia;
            const size_t lb = eb - jb;
            if (la != lb) return la < lb ? -1 : 1;
            for (size_t k = 0; k < la; ++k) {
                if (a[ia + k] != b[jb + k]) return a[ia + k] < b[jb + k] ? -1 : 1;
            }
            i = ea;
            j = eb;
            continue;
        }
        if (a[i] != b[j]) return a[i] < b[j] ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

NJS_IntlCollator* collatorCreate(void* /*ud*/, const char* locale,
                                 const NJS_IntlOption* opts, size_t nopts,
                                 NJS_IntlResolved* resolved,
                                 NJS_IntlStatus* status) {
    auto fail = [status](NJS_IntlStatus s) -> NJS_IntlCollator* {
        if (status != nullptr) *status = s;
        return nullptr;
    };
    if (locale == nullptr) return fail(NJS_INTL_INVALID_OPTION);

    auto* c = new (std::nothrow) NJS_IntlCollator();
    if (c == nullptr) return fail(NJS_INTL_INTERNAL);
    c->locale = locale;

    const char* usage = findOption(opts, nopts, "usage");
    if (usage != nullptr) {
        if (!equals(usage, "sort") && !equals(usage, "search")) {
            delete c;
            return fail(NJS_INTL_INVALID_OPTION);
        }
        c->usage = usage;
    }
    const char* sensitivity = findOption(opts, nopts, "sensitivity");
    if (sensitivity != nullptr) {
        if (!equals(sensitivity, "base") && !equals(sensitivity, "accent") &&
            !equals(sensitivity, "case") && !equals(sensitivity, "variant")) {
            delete c;
            return fail(NJS_INTL_INVALID_OPTION);
        }
        c->sensitivity = sensitivity;
    }
    const char* case_first = findOption(opts, nopts, "caseFirst");
    if (case_first != nullptr) {
        if (!equals(case_first, "upper") && !equals(case_first, "lower") &&
            !equals(case_first, "false")) {
            delete c;
            return fail(NJS_INTL_INVALID_OPTION);
        }
        c->case_first = case_first;
    }
    const char* collation = findOption(opts, nopts, "collation");
    if (collation != nullptr) {
        if (!equals(collation, "default")) {
            delete c;
            return fail(NJS_INTL_UNSUPPORTED);
        }
        c->collation = collation;
    }
    const char* ignore_punctuation = findOption(opts, nopts, "ignorePunctuation");
    if (ignore_punctuation != nullptr &&
        !parseBool(ignore_punctuation, c->ignore_punctuation)) {
        delete c;
        return fail(NJS_INTL_INVALID_OPTION);
    }
    const char* numeric = findOption(opts, nopts, "numeric");
    if (numeric != nullptr && !parseBool(numeric, c->numeric)) {
        delete c;
        return fail(NJS_INTL_INVALID_OPTION);
    }

    if (resolved != nullptr) {
        std::memset(resolved, 0, sizeof *resolved);
        auto copyFixed = [](char* dst, size_t size, const char* src) {
            std::snprintf(dst, size, "%s", src);
        };
        copyFixed(resolved->locale, sizeof resolved->locale, c->locale.c_str());
        copyFixed(resolved->numbering_system, sizeof resolved->numbering_system,
                  "latn");
        copyFixed(resolved->calendar, sizeof resolved->calendar, "gregory");
        copyFixed(resolved->time_zone, sizeof resolved->time_zone, "UTC");
    }
    if (status != nullptr) *status = NJS_INTL_OK;
    return c;
}

NJS_IntlStatus collatorCompare(NJS_IntlCollator* c, const char* a, size_t a_len,
                               const char* b, size_t b_len, int* result) {
    if (c == nullptr || result == nullptr) return NJS_INTL_INTERNAL;
    const std::vector<uint32_t> ta = transform(c, decodeUtf8(a, a_len));
    const std::vector<uint32_t> tb = transform(c, decodeUtf8(b, b_len));

    int comparison = 0;
    if (c->numeric) {
        comparison = compareNumeric(ta, tb);
    } else if (ta != tb) {
        comparison = std::lexicographical_compare(ta.begin(), ta.end(), tb.begin(),
                                                  tb.end())
                         ? -1
                         : 1;
    }

    if (comparison == 0 && c->case_first != "false" &&
        c->sensitivity != "base" && c->sensitivity != "accent") {
        const std::vector<uint32_t> oa = decodeUtf8(a, a_len);
        const std::vector<uint32_t> ob = decodeUtf8(b, b_len);
        const size_t n = std::min(oa.size(), ob.size());
        for (size_t i = 0; i < n; ++i) {
            const bool ua = oa[i] >= 'A' && oa[i] <= 'Z';
            const bool ub = ob[i] >= 'A' && ob[i] <= 'Z';
            if (ua != ub) {
                const bool upper_first = c->case_first == "upper";
                comparison = (ua == upper_first) ? -1 : 1;
                break;
            }
        }
    }
    *result = comparison;
    return NJS_INTL_OK;
}

void collatorDestroy(NJS_IntlCollator* collator) { delete collator; }

void formatterDestroy(NJS_IntlFormatter* formatter) { delete formatter; }

const char* lastError(void* /*ud*/) { return nullptr; }

const NJS_IntlBackend kMinimalBackend = {
    NJS_INTL_ABI_VERSION,
    sizeof(NJS_IntlBackend),
    NJS_INTL_CAP_NUMBER | NJS_INTL_CAP_DATETIME | NJS_INTL_CAP_COLLATOR |
        NJS_INTL_CAP_FORMAT_TO_PARTS,
    nullptr,
    canonicalizeLocale,
    availableLocales,
    defaultLocale,
    defaultTimeZone,
    numberFormatCreate,
    numberFormatFormat,
    formatterDestroy,
    lastError,
    dateTimeFormatCreate,
    dateTimeFormatFormat,
    collatorCreate,
    collatorCompare,
    collatorDestroy,
};

}  // namespace

extern "C" const NJS_IntlBackend* NJS_MinimalIntlBackend(void) {
    return &kMinimalBackend;
}

namespace neo::core {

const NJS_IntlBackend* minimalIntlBackend() { return &kMinimalBackend; }

}  // namespace neo::core
