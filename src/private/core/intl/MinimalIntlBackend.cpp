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
    std::string locale;
    std::string style = "decimal";
    std::string currency;
    std::string currency_display = "symbol";
    std::string sign_display = "auto";
    std::string numbering_system = "latn";
    bool use_grouping = true;
    int min_int = 1;
    int min_frac = 0;
    int max_frac = 3;
    std::vector<Part> last_parts;
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

void formatterDestroy(NJS_IntlFormatter* formatter) { delete formatter; }

const char* lastError(void* /*ud*/) { return nullptr; }

const NJS_IntlBackend kMinimalBackend = {
    NJS_INTL_ABI_VERSION,
    sizeof(NJS_IntlBackend),
    NJS_INTL_CAP_NUMBER | NJS_INTL_CAP_FORMAT_TO_PARTS,
    nullptr,
    canonicalizeLocale,
    availableLocales,
    defaultLocale,
    defaultTimeZone,
    numberFormatCreate,
    numberFormatFormat,
    formatterDestroy,
    lastError,
};

}  // namespace

extern "C" const NJS_IntlBackend* NJS_MinimalIntlBackend(void) {
    return &kMinimalBackend;
}

namespace neo::core {

const NJS_IntlBackend* minimalIntlBackend() { return &kMinimalBackend; }

}  // namespace neo::core
