// IcuIntlBackend.cpp - 基于 ICU 的 NJS_IntlBackend 实现（独立静态库）
//
// 设计要点：
//   - 本文件不依赖 core 私有头，只实现 include/neojs.h 的 C ABI；
//   - 所有 ICU 对象由后端持有，destroy 时归还；
//   - 两段式输出（先问长度、再写内容）与 parts 的“formatter 持有文本”约定
//     与内置最小后端一致；
//   - 应用按需链接本库：custom > ICU > mini 三级回退由 NJS_SelectIntlBackend
//     与 core 的 createContext 共同完成。
//
// 关联 ADR: 020（标准库组织）

#include "neojs_intl_icu.h"

#include <unicode/brkiter.h>
#include <unicode/coll.h>
#include <unicode/datefmt.h>
#include <unicode/decimfmt.h>
#include <unicode/dtptngen.h>
#include <unicode/fieldpos.h>
#include <unicode/fpositer.h>
#include <unicode/listformatter.h>
#include <unicode/locdspnm.h>
#include <unicode/locid.h>
#include <unicode/numfmt.h>
#include <unicode/numsys.h>
#include <unicode/plurrule.h>
#include <unicode/rbbi.h>
#include <unicode/reldatefmt.h>
#include <unicode/smpdtfmt.h>
#include <unicode/timezone.h>
#include <unicode/ubrk.h>
#include <unicode/ucal.h>
#include <unicode/ucol.h>
#include <unicode/ucurr.h>
#include <unicode/uenum.h>
#include <unicode/uloc.h>
#include <unicode/unistr.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

extern "C" int icu_data_init(void);

// 不透明句柄必须与 neojs.h 的全局 typedef 同名同作用域。
struct Part {
    NJS_IntlPartType type;
    std::string text;
};

struct NJS_IntlFormatter {
    icu::NumberFormat* number = nullptr;
    icu::DateFormat* date = nullptr;
    std::string sign_display = "auto";
    std::vector<Part> last_parts;
};

struct NJS_IntlCollator {
    icu::Collator* collator = nullptr;
};

struct NJS_IntlPluralRules {
    icu::PluralRules* rules = nullptr;
    std::string keyword;
};

struct NJS_IntlRelativeTimeFormat {
    icu::RelativeDateTimeFormatter* formatter = nullptr;
    bool numeric_always = false;
    std::vector<Part> last_parts;
};

struct NJS_IntlListFormat {
    icu::ListFormatter* formatter = nullptr;
    std::vector<Part> last_parts;
};

struct NJS_IntlDisplayNames {
    icu::LocaleDisplayNames* names = nullptr;
    std::string type;
    std::string fallback;
};

struct NJS_IntlSegmenter {
    icu::BreakIterator* iterator = nullptr;
    bool word = false;
};

namespace {

using icu::BreakIterator;
using icu::Collator;
using icu::ConstrainedFieldPosition;
using icu::CurrencyUnit;
using icu::DateFormat;
using icu::DecimalFormat;
using icu::FieldPosition;
using icu::FieldPositionIterator;
using icu::FormattedList;
using icu::ListFormatter;
using icu::Locale;
using icu::LocaleDisplayNames;
using icu::NumberFormat;
using icu::PluralRules;
using icu::RelativeDateTimeFormatter;
using icu::RuleBasedBreakIterator;
using icu::SimpleDateFormat;
using icu::TimeZone;
using icu::UnicodeString;

// —— 通用辅助 ——

std::string toUtf8(const UnicodeString& text) {
    std::string out;
    text.toUTF8String(out);
    return out;
}

UnicodeString fromUtf8(const char* text, size_t len) {
    return UnicodeString::fromUTF8(icu::StringPiece(text, static_cast<int32_t>(len)));
}

UnicodeString fromUtf8(const std::string& text) {
    return fromUtf8(text.data(), text.size());
}

NJS_IntlStatus copyOut(const std::string& text, char* out, size_t* out_len) {
    const size_t needed = text.size() + 1;
    if (out == nullptr || *out_len < needed) {
        *out_len = needed;
        return NJS_INTL_OK;
    }
    std::memcpy(out, text.data(), text.size());
    out[text.size()] = '\0';
    *out_len = needed;
    return NJS_INTL_OK;
}

void copyFixed(char* dst, size_t capacity, const std::string& text) {
    if (capacity == 0) return;
    const size_t n = std::min(text.size(), capacity - 1);
    std::memcpy(dst, text.data(), n);
    dst[n] = '\0';
}

const char* findOption(const NJS_IntlOption* opts, size_t nopts, const char* key) {
    if (opts == nullptr) return nullptr;
    for (size_t i = 0; i < nopts; ++i) {
        if (opts[i].key != nullptr && std::strcmp(opts[i].key, key) == 0) {
            return opts[i].value;
        }
    }
    return nullptr;
}

std::string optionString(const NJS_IntlOption* opts, size_t nopts, const char* key,
                         const char* fallback = "") {
    const char* value = findOption(opts, nopts, key);
    return value != nullptr ? std::string(value) : std::string(fallback);
}

bool optionBool(const NJS_IntlOption* opts, size_t nopts, const char* key,
                bool fallback) {
    const char* value = findOption(opts, nopts, key);
    if (value == nullptr) return fallback;
    return std::strcmp(value, "true") == 0;
}

int optionInt(const NJS_IntlOption* opts, size_t nopts, const char* key,
              int fallback) {
    const char* value = findOption(opts, nopts, key);
    if (value == nullptr) return fallback;
    return std::atoi(value);
}

Locale resolveLocale(const char* locale, const NJS_IntlOption* opts, size_t nopts) {
    std::string tag = locale != nullptr ? locale : "en";
    const std::string nu = optionString(opts, nopts, "numberingSystem");
    if (!nu.empty() && nu != "latn" && tag.find("-u-") == std::string::npos) {
        tag += "-u-nu-" + nu;
    }
    UErrorCode status = U_ZERO_ERROR;
    Locale loc = Locale::forLanguageTag(icu::StringPiece(tag), status);
    if (U_FAILURE(status)) {
        status = U_ZERO_ERROR;
        loc = Locale(tag.c_str());
    }
    return loc;
}

std::string localeTag(const Locale& loc) {
    UErrorCode status = U_ZERO_ERROR;
    std::string tag = loc.toLanguageTag<std::string>(status);
    if (U_FAILURE(status) || tag.empty()) return std::string("en");
    return tag;
}

void fillResolved(NJS_IntlResolved* resolved, const Locale& loc,
                  const std::string& numbering, const std::string& calendar,
                  const std::string& time_zone) {
    if (resolved == nullptr) return;
    copyFixed(resolved->locale, sizeof resolved->locale, localeTag(loc));
    copyFixed(resolved->numbering_system, sizeof resolved->numbering_system,
              numbering.empty() ? "latn" : numbering);
    copyFixed(resolved->calendar, sizeof resolved->calendar,
              calendar.empty() ? "gregory" : calendar);
    copyFixed(resolved->time_zone, sizeof resolved->time_zone,
              time_zone.empty() ? "UTC" : time_zone);
}

NJS_IntlStatus fillParts(const std::vector<Part>& parts, NJS_IntlPart* out,
                         size_t* count) {
    if (out == nullptr || *count < parts.size()) {
        *count = parts.size();
        return NJS_INTL_OK;
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        out[i].type = parts[i].type;
        out[i].text = parts[i].text.c_str();
        out[i].length = parts[i].text.size();
    }
    *count = parts.size();
    return NJS_INTL_OK;
}

// —— locale 数据 ——

struct AvailableLocales {
    std::vector<std::string> storage;
    std::vector<const char*> pointers;
};

const AvailableLocales& availableLocales() {
    static const AvailableLocales data = [] {
        AvailableLocales result;
        const int32_t count = uloc_countAvailable();
        result.storage.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            const char* id = uloc_getAvailable(i);
            if (id == nullptr) continue;
            char buffer[256];
            UErrorCode status = U_ZERO_ERROR;
            const int32_t len = uloc_toLanguageTag(id, buffer, sizeof buffer, false,
                                                   &status);
            if (U_FAILURE(status) || len <= 0) continue;
            result.storage.emplace_back(buffer, static_cast<size_t>(len));
        }
        std::sort(result.storage.begin(), result.storage.end());
        result.storage.erase(
            std::unique(result.storage.begin(), result.storage.end()),
            result.storage.end());
        result.pointers.reserve(result.storage.size());
        for (const std::string& tag : result.storage) {
            result.pointers.push_back(tag.c_str());
        }
        return result;
    }();
    return data;
}

NJS_IntlStatus canonicalizeLocale(void*, const char* tag, size_t tag_len,
                                  char* out, size_t* out_len) {
    UErrorCode status = U_ZERO_ERROR;
    Locale loc = Locale::forLanguageTag(
        icu::StringPiece(tag, static_cast<int32_t>(tag_len)), status);
    if (U_FAILURE(status)) return NJS_INTL_INVALID_OPTION;
    return copyOut(localeTag(loc), out, out_len);
}

const char* const* availableLocalesThunk(void*, size_t* count) {
    const AvailableLocales& data = availableLocales();
    *count = data.pointers.size();
    return data.pointers.empty() ? nullptr : data.pointers.data();
}

NJS_IntlStatus defaultLocale(void*, char* out, size_t* out_len) {
    // uloc_getDefault() 返回 ICU 形式（如 en_US），Locale 构造后再转 BCP 47。
    Locale loc(uloc_getDefault());
    return copyOut(localeTag(loc), out, out_len);
}

NJS_IntlStatus defaultTimeZone(void*, char* out, size_t* out_len) {
    TimeZone* zone = TimeZone::createDefault();
    if (zone == nullptr) return NJS_INTL_INTERNAL;
    UnicodeString id;
    zone->getID(id);
    delete zone;
    return copyOut(toUtf8(id), out, out_len);
}

// —— NumberFormat ——

NJS_IntlPartType numberFieldType(int32_t field) {
    switch (field) {
        case UNUM_INTEGER_FIELD: return NJS_INTL_PART_INTEGER;
        case UNUM_GROUPING_SEPARATOR_FIELD: return NJS_INTL_PART_GROUP;
        case UNUM_DECIMAL_SEPARATOR_FIELD: return NJS_INTL_PART_DECIMAL;
        case UNUM_FRACTION_FIELD: return NJS_INTL_PART_FRACTION;
        case UNUM_CURRENCY_FIELD: return NJS_INTL_PART_CURRENCY;
        case UNUM_PERCENT_FIELD: return NJS_INTL_PART_PERCENT;
        case UNUM_SIGN_FIELD: return NJS_INTL_PART_MINUS_SIGN;
        default: return NJS_INTL_PART_LITERAL;
    }
}

NJS_IntlFormatter* numberFormatCreate(void*, const char* locale,
                                      const NJS_IntlOption* opts, size_t nopts,
                                      NJS_IntlResolved* resolved,
                                      NJS_IntlStatus* status) {
    UErrorCode st = U_ZERO_ERROR;
    const Locale loc = resolveLocale(locale, opts, nopts);
    const std::string style = optionString(opts, nopts, "style", "decimal");
    const std::string notation = optionString(opts, nopts, "notation", "standard");

    UNumberFormatStyle fmtStyle = UNUM_DECIMAL;
    if (style == "percent") {
        fmtStyle = UNUM_PERCENT;
    } else if (style == "currency") {
        const std::string display =
            optionString(opts, nopts, "currencyDisplay", "symbol");
        if (display == "code") {
            fmtStyle = UNUM_CURRENCY_ISO;
        } else if (display == "name") {
            fmtStyle = UNUM_CURRENCY_PLURAL;
        } else {
            fmtStyle = UNUM_CURRENCY;
        }
    } else if (notation == "scientific" || notation == "engineering") {
        fmtStyle = UNUM_SCIENTIFIC;
    } else if (notation == "compact") {
        fmtStyle = UNUM_DECIMAL_COMPACT_SHORT;
    }

    NumberFormat* nf = NumberFormat::createInstance(loc, fmtStyle, st);
    if (U_FAILURE(st) || nf == nullptr) {
        delete nf;
        if (status != nullptr) *status = NJS_INTL_INTERNAL;
        return nullptr;
    }

    auto* df = dynamic_cast<DecimalFormat*>(nf);
    if (df != nullptr) {
        df->setMinimumIntegerDigits(optionInt(opts, nopts, "minimumIntegerDigits", 1));
        df->setMinimumFractionDigits(
            optionInt(opts, nopts, "minimumFractionDigits", 0));
        df->setMaximumFractionDigits(
            optionInt(opts, nopts, "maximumFractionDigits", 3));
        df->setGroupingUsed(optionBool(opts, nopts, "useGrouping", true));
        if (style == "currency") {
            const std::string currency = optionString(opts, nopts, "currency", "");
            if (currency.size() == 3) {
                UnicodeString code = fromUtf8(currency);
                df->setCurrency(code.getTerminatedBuffer(), st);
            }
        }
    }

    auto* formatter = new NJS_IntlFormatter();
    formatter->number = nf;
    formatter->sign_display = optionString(opts, nopts, "signDisplay", "auto");
    fillResolved(resolved, loc, optionString(opts, nopts, "numberingSystem"),
                 "gregory", "UTC");
    if (status != nullptr) *status = NJS_INTL_OK;
    return formatter;
}

struct RawField {
    int32_t field;
    int32_t begin;
    int32_t end;
};

// 把 ICU 的字段位置切成互不重叠的 parts。
// NumberFormat 的 integer 字段会覆盖 group 分隔符（如 "1,234" 整体是一个
// integer 字段，同时 group 字段单独标出 ","），按“最小区间优先”拆分。
std::vector<Part> buildPartsFromFields(
    const UnicodeString& text, const std::vector<RawField>& fields,
    NJS_IntlPartType (*mapper)(int32_t)) {
    std::vector<int32_t> cuts;
    cuts.reserve(fields.size() * 2);
    for (const RawField& field : fields) {
        cuts.push_back(field.begin);
        cuts.push_back(field.end);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

    std::vector<Part> built;
    for (size_t i = 0; i + 1 < cuts.size(); ++i) {
        const int32_t begin = cuts[i];
        const int32_t end = cuts[i + 1];
        if (end <= begin) continue;
        const RawField* best = nullptr;
        for (const RawField& field : fields) {
            if (field.begin > begin || field.end < end) continue;
            if (best == nullptr) {
                best = &field;
                continue;
            }
            const int32_t span = field.end - field.begin;
            const int32_t bestSpan = best->end - best->begin;
            if (span < bestSpan) best = &field;
        }
        UnicodeString sub;
        text.extract(begin, end - begin, sub);
        NJS_IntlPartType type = best != nullptr ? mapper(best->field)
                                                : NJS_INTL_PART_LITERAL;
        if (type == NJS_INTL_PART_MINUS_SIGN && sub.length() > 0 &&
            sub.charAt(0) == u'+') {
            type = NJS_INTL_PART_PLUS_SIGN;
        }
        built.push_back({type, toUtf8(sub)});
    }
    return built;
}

NJS_IntlStatus numberFormatFormat(NJS_IntlFormatter* formatter, double value,
                                  NJS_IntlPart* parts, size_t* count) {
    if (formatter == nullptr || formatter->number == nullptr) {
        return NJS_INTL_INTERNAL;
    }
    UErrorCode st = U_ZERO_ERROR;
    double v = value;
    bool prepend_plus = false;
    if (formatter->sign_display == "never") {
        v = std::fabs(value);
    } else if (formatter->sign_display == "exceptZero" && value == 0.0) {
        v = 0.0;
    } else if (formatter->sign_display == "always" && value >= 0.0) {
        prepend_plus = true;
    }

    UnicodeString text;
    FieldPositionIterator iter;
    auto* df = dynamic_cast<DecimalFormat*>(formatter->number);
    if (df != nullptr) {
        df->format(v, text, &iter, st);
    } else {
        FieldPosition pos;
        formatter->number->format(v, text, pos);
    }
    if (U_FAILURE(st)) return NJS_INTL_INTERNAL;

    std::vector<RawField> fields;
    FieldPosition pos;
    while (iter.next(pos)) {
        if (pos.getEndIndex() <= pos.getBeginIndex()) continue;
        fields.push_back({pos.getField(), pos.getBeginIndex(), pos.getEndIndex()});
    }
    std::vector<Part> built = buildPartsFromFields(text, fields, numberFieldType);
    if (prepend_plus) built.insert(built.begin(), {NJS_INTL_PART_PLUS_SIGN, "+"});
    if (built.empty()) built.push_back({NJS_INTL_PART_LITERAL, toUtf8(text)});
    formatter->last_parts = std::move(built);
    return fillParts(formatter->last_parts, parts, count);
}

void formatterDestroy(NJS_IntlFormatter* formatter) {
    if (formatter == nullptr) return;
    delete formatter->number;
    delete formatter->date;
    delete formatter;
}

// —— DateTimeFormat ——

NJS_IntlPartType dateFieldType(int32_t field) {
    switch (field) {
        case UDAT_YEAR_FIELD:
        case UDAT_YEAR_WOY_FIELD:
        case UDAT_EXTENDED_YEAR_FIELD:
            return NJS_INTL_PART_YEAR;
        case UDAT_MONTH_FIELD:
            return NJS_INTL_PART_MONTH;
        case UDAT_DATE_FIELD:
            return NJS_INTL_PART_DAY;
        case UDAT_HOUR_OF_DAY0_FIELD:
        case UDAT_HOUR_OF_DAY1_FIELD:
        case UDAT_HOUR0_FIELD:
        case UDAT_HOUR1_FIELD:
            return NJS_INTL_PART_HOUR;
        case UDAT_MINUTE_FIELD:
            return NJS_INTL_PART_MINUTE;
        case UDAT_SECOND_FIELD:
        case UDAT_FRACTIONAL_SECOND_FIELD:
            return NJS_INTL_PART_SECOND;
        case UDAT_AM_PM_FIELD:
            return NJS_INTL_PART_DAY_PERIOD;
        case UDAT_TIMEZONE_FIELD:
        case UDAT_TIMEZONE_RFC_FIELD:
        case UDAT_TIMEZONE_GENERIC_FIELD:
        case UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD:
        case UDAT_TIMEZONE_ISO_FIELD:
        case UDAT_TIMEZONE_ISO_LOCAL_FIELD:
            return NJS_INTL_PART_TIME_ZONE_NAME;
        default:
            return NJS_INTL_PART_LITERAL;
    }
}

int dateStyleValue(const std::string& style) {
    if (style == "full") return DateFormat::kFull;
    if (style == "long") return DateFormat::kLong;
    if (style == "medium") return DateFormat::kMedium;
    if (style == "short") return DateFormat::kShort;
    return -1;
}

std::string dateSkeleton(const NJS_IntlOption* opts, size_t nopts) {
    std::string skeleton;
    const std::string year = optionString(opts, nopts, "year");
    if (!year.empty()) skeleton += year == "2-digit" ? "yy" : "y";
    const std::string month = optionString(opts, nopts, "month");
    if (month == "numeric") skeleton += "M";
    else if (month == "2-digit") skeleton += "MM";
    else if (month == "long") skeleton += "MMMM";
    else if (month == "short") skeleton += "MMM";
    else if (month == "narrow") skeleton += "MMMMM";
    const std::string day = optionString(opts, nopts, "day");
    if (day == "numeric") skeleton += "d";
    else if (day == "2-digit") skeleton += "dd";
    const std::string weekday = optionString(opts, nopts, "weekday");
    if (weekday == "long") skeleton += "EEEE";
    else if (weekday == "short") skeleton += "EEE";
    else if (weekday == "narrow") skeleton += "EEEEE";
    const std::string era = optionString(opts, nopts, "era");
    if (!era.empty()) skeleton += "G";
    const std::string hour = optionString(opts, nopts, "hour");
    if (!hour.empty()) {
        const bool hour12 = optionBool(opts, nopts, "hour12", false);
        skeleton += hour12 ? (hour == "2-digit" ? "hh" : "h")
                           : (hour == "2-digit" ? "HH" : "H");
    }
    if (!optionString(opts, nopts, "minute").empty()) skeleton += "m";
    if (!optionString(opts, nopts, "second").empty()) skeleton += "s";
    const char* fsec = findOption(opts, nopts, "fractionalSecondDigits");
    if (fsec != nullptr) {
        const int digits = std::atoi(fsec);
        for (int i = 0; i < digits; ++i) skeleton += "S";
    }
    const std::string tzName = optionString(opts, nopts, "timeZoneName");
    if (tzName == "short") skeleton += "z";
    else if (tzName == "long") skeleton += "zzzz";
    else if (tzName == "shortOffset") skeleton += "O";
    else if (tzName == "longOffset") skeleton += "OOOO";
    else if (tzName == "shortGeneric") skeleton += "v";
    else if (tzName == "longGeneric") skeleton += "vvvv";
    return skeleton;
}

NJS_IntlFormatter* dateTimeFormatCreate(void*, const char* locale,
                                        const NJS_IntlOption* opts, size_t nopts,
                                        NJS_IntlResolved* resolved,
                                        NJS_IntlStatus* status) {
    UErrorCode st = U_ZERO_ERROR;
    const Locale loc = resolveLocale(locale, opts, nopts);
    const int dateStyle = dateStyleValue(optionString(opts, nopts, "dateStyle"));
    const int timeStyle = dateStyleValue(optionString(opts, nopts, "timeStyle"));

    DateFormat* df = nullptr;
    if (dateStyle >= 0 && timeStyle >= 0) {
        df = DateFormat::createDateTimeInstance(
            static_cast<DateFormat::EStyle>(dateStyle),
            static_cast<DateFormat::EStyle>(timeStyle), loc);
    } else if (dateStyle >= 0) {
        df = DateFormat::createDateInstance(
            static_cast<DateFormat::EStyle>(dateStyle), loc);
    } else if (timeStyle >= 0) {
        df = DateFormat::createTimeInstance(
            static_cast<DateFormat::EStyle>(timeStyle), loc);
    } else {
        const std::string skeleton = dateSkeleton(opts, nopts);
        if (skeleton.empty()) {
            df = DateFormat::createDateTimeInstance(DateFormat::kMedium,
                                                    DateFormat::kMedium, loc);
        } else {
            icu::DateTimePatternGenerator* gen =
                icu::DateTimePatternGenerator::createInstance(loc, st);
            if (U_FAILURE(st) || gen == nullptr) {
                delete gen;
                if (status != nullptr) *status = NJS_INTL_INTERNAL;
                return nullptr;
            }
            UnicodeString pattern =
                gen->getBestPattern(fromUtf8(skeleton), st);
            delete gen;
            if (U_FAILURE(st)) {
                if (status != nullptr) *status = NJS_INTL_INTERNAL;
                return nullptr;
            }
            auto* simple = new SimpleDateFormat(pattern, loc, st);
            if (U_FAILURE(st)) {
                delete simple;
                if (status != nullptr) *status = NJS_INTL_INTERNAL;
                return nullptr;
            }
            df = simple;
        }
    }
    if (df == nullptr) {
        if (status != nullptr) *status = NJS_INTL_INTERNAL;
        return nullptr;
    }

    const std::string tz = optionString(opts, nopts, "timeZone");
    if (!tz.empty()) {
        df->adoptTimeZone(TimeZone::createTimeZone(fromUtf8(tz)));
    }

    auto* formatter = new NJS_IntlFormatter();
    formatter->date = df;
    fillResolved(resolved, loc, optionString(opts, nopts, "numberingSystem"),
                 optionString(opts, nopts, "calendar"), tz);
    if (status != nullptr) *status = NJS_INTL_OK;
    return formatter;
}

NJS_IntlStatus dateTimeFormatFormat(NJS_IntlFormatter* formatter, double time,
                                    NJS_IntlPart* parts, size_t* count) {
    if (formatter == nullptr || formatter->date == nullptr) {
        return NJS_INTL_INTERNAL;
    }
    if (std::isnan(time)) return NJS_INTL_INVALID_VALUE;
    UErrorCode st = U_ZERO_ERROR;
    UnicodeString text;
    FieldPositionIterator iter;
    formatter->date->format(static_cast<UDate>(time), text, &iter, st);
    if (U_FAILURE(st)) return NJS_INTL_INTERNAL;

    std::vector<RawField> fields;
    FieldPosition pos;
    while (iter.next(pos)) {
        if (pos.getEndIndex() <= pos.getBeginIndex()) continue;
        fields.push_back({pos.getField(), pos.getBeginIndex(), pos.getEndIndex()});
    }
    std::vector<Part> built = buildPartsFromFields(text, fields, dateFieldType);
    if (built.empty()) built.push_back({NJS_INTL_PART_LITERAL, toUtf8(text)});
    formatter->last_parts = std::move(built);
    return fillParts(formatter->last_parts, parts, count);
}

// —— Collator ——

NJS_IntlCollator* collatorCreate(void*, const char* locale,
                                 const NJS_IntlOption* opts, size_t nopts,
                                 NJS_IntlResolved* resolved,
                                 NJS_IntlStatus* status) {
    UErrorCode st = U_ZERO_ERROR;
    std::string tag = locale != nullptr ? locale : "en";
    const std::string collation = optionString(opts, nopts, "collation", "");
    if (!collation.empty() && tag.find("-u-") == std::string::npos) {
        tag += "-u-co-" + collation;
    }
    Locale loc = Locale::forLanguageTag(icu::StringPiece(tag), st);
    if (U_FAILURE(st)) {
        st = U_ZERO_ERROR;
        loc = Locale(tag.c_str());
    }
    Collator* collator = Collator::createInstance(loc, st);
    if (U_FAILURE(st) || collator == nullptr) {
        delete collator;
        if (status != nullptr) *status = NJS_INTL_INTERNAL;
        return nullptr;
    }

    const std::string sensitivity =
        optionString(opts, nopts, "sensitivity", "variant");
    if (sensitivity == "base") {
        collator->setStrength(Collator::PRIMARY);
    } else if (sensitivity == "accent") {
        collator->setStrength(Collator::SECONDARY);
    } else if (sensitivity == "case") {
        collator->setStrength(Collator::PRIMARY);
        collator->setAttribute(UCOL_CASE_LEVEL, UCOL_ON, st);
    } else {
        collator->setStrength(Collator::TERTIARY);
    }

    const std::string caseFirst = optionString(opts, nopts, "caseFirst", "false");
    if (caseFirst == "upper") {
        collator->setAttribute(UCOL_CASE_FIRST, UCOL_UPPER_FIRST, st);
    } else if (caseFirst == "lower") {
        collator->setAttribute(UCOL_CASE_FIRST, UCOL_LOWER_FIRST, st);
    } else {
        collator->setAttribute(UCOL_CASE_FIRST, UCOL_OFF, st);
    }

    collator->setAttribute(UCOL_NUMERIC_COLLATION,
                           optionBool(opts, nopts, "numeric", false) ? UCOL_ON
                                                                     : UCOL_OFF,
                           st);
    collator->setAttribute(
        UCOL_ALTERNATE_HANDLING,
        optionBool(opts, nopts, "ignorePunctuation", false) ? UCOL_SHIFTED
                                                            : UCOL_NON_IGNORABLE,
        st);

    auto* result = new NJS_IntlCollator();
    result->collator = collator;
    fillResolved(resolved, loc, "", "", "");
    if (status != nullptr) *status = NJS_INTL_OK;
    return result;
}

NJS_IntlStatus collatorCompare(NJS_IntlCollator* collator, const char* a,
                               size_t a_len, const char* b, size_t b_len,
                               int* result) {
    if (collator == nullptr || collator->collator == nullptr) {
        return NJS_INTL_INTERNAL;
    }
    const UnicodeString left = fromUtf8(a, a_len);
    const UnicodeString right = fromUtf8(b, b_len);
    *result = static_cast<int>(collator->collator->compare(left, right));
    return NJS_INTL_OK;
}

void collatorDestroy(NJS_IntlCollator* collator) {
    if (collator == nullptr) return;
    delete collator->collator;
    delete collator;
}

// —— PluralRules ——

NJS_IntlPluralRules* pluralRulesCreate(void*, const char* locale,
                                       const NJS_IntlOption* opts, size_t nopts,
                                       NJS_IntlResolved* resolved,
                                       NJS_IntlStatus* status) {
    UErrorCode st = U_ZERO_ERROR;
    const Locale loc = resolveLocale(locale, opts, nopts);
    const std::string type = optionString(opts, nopts, "type", "cardinal");
    PluralRules* rules =
        type == "ordinal" ? PluralRules::forLocale(loc, UPLURAL_TYPE_ORDINAL, st)
                          : PluralRules::forLocale(loc, st);
    if (U_FAILURE(st) || rules == nullptr) {
        delete rules;
        if (status != nullptr) *status = NJS_INTL_INTERNAL;
        return nullptr;
    }
    auto* result = new NJS_IntlPluralRules();
    result->rules = rules;
    fillResolved(resolved, loc, "", "", "");
    if (status != nullptr) *status = NJS_INTL_OK;
    return result;
}

const char* pluralRulesSelect(NJS_IntlPluralRules* rules, double value) {
    if (rules == nullptr || rules->rules == nullptr) return "other";
    rules->keyword = toUtf8(rules->rules->select(value));
    return rules->keyword.c_str();
}

void pluralRulesDestroy(NJS_IntlPluralRules* rules) {
    if (rules == nullptr) return;
    delete rules->rules;
    delete rules;
}

// —— RelativeTimeFormat ——

URelativeDateTimeUnit relativeUnit(const std::string& unit) {
    if (unit == "year") return UDAT_REL_UNIT_YEAR;
    if (unit == "quarter") return UDAT_REL_UNIT_QUARTER;
    if (unit == "month") return UDAT_REL_UNIT_MONTH;
    if (unit == "week") return UDAT_REL_UNIT_WEEK;
    if (unit == "day") return UDAT_REL_UNIT_DAY;
    if (unit == "hour") return UDAT_REL_UNIT_HOUR;
    if (unit == "minute") return UDAT_REL_UNIT_MINUTE;
    return UDAT_REL_UNIT_SECOND;
}

NJS_IntlRelativeTimeFormat* relativeTimeFormatCreate(
    void*, const char* locale, const NJS_IntlOption* opts, size_t nopts,
    NJS_IntlResolved* resolved, NJS_IntlStatus* status) {
    UErrorCode st = U_ZERO_ERROR;
    const Locale loc = resolveLocale(locale, opts, nopts);
    const std::string style = optionString(opts, nopts, "style", "long");
    UDateRelativeDateTimeFormatterStyle icuStyle = UDAT_STYLE_LONG;
    if (style == "short") icuStyle = UDAT_STYLE_SHORT;
    else if (style == "narrow") icuStyle = UDAT_STYLE_NARROW;
    auto* formatter = new RelativeDateTimeFormatter(
        loc, nullptr, icuStyle, UDISPCTX_CAPITALIZATION_NONE, st);
    if (U_FAILURE(st)) {
        delete formatter;
        if (status != nullptr) *status = NJS_INTL_INTERNAL;
        return nullptr;
    }
    auto* result = new NJS_IntlRelativeTimeFormat();
    result->formatter = formatter;
    result->numeric_always = optionString(opts, nopts, "numeric", "always") == "always";
    fillResolved(resolved, loc, optionString(opts, nopts, "numberingSystem"), "",
                 "");
    if (status != nullptr) *status = NJS_INTL_OK;
    return result;
}

NJS_IntlStatus relativeTimeFormatFormat(NJS_IntlRelativeTimeFormat* formatter,
                                        double value, const char* unit,
                                        NJS_IntlPart* parts, size_t* count) {
    if (formatter == nullptr || formatter->formatter == nullptr) {
        return NJS_INTL_INTERNAL;
    }
    UErrorCode st = U_ZERO_ERROR;
    UnicodeString text;
    const URelativeDateTimeUnit icuUnit = relativeUnit(unit != nullptr ? unit : "");
    if (formatter->numeric_always) {
        formatter->formatter->formatNumeric(value, icuUnit, text, st);
    } else {
        formatter->formatter->format(value, icuUnit, text, st);
    }
    if (U_FAILURE(st)) return NJS_INTL_INTERNAL;
    // RelativeDateTimeFormatter 不暴露字段迭代器；整段作为 literal。
    formatter->last_parts = {{NJS_INTL_PART_LITERAL, toUtf8(text)}};
    return fillParts(formatter->last_parts, parts, count);
}

void relativeTimeFormatDestroy(NJS_IntlRelativeTimeFormat* formatter) {
    if (formatter == nullptr) return;
    delete formatter->formatter;
    delete formatter;
}

// —— ListFormat ——

NJS_IntlListFormat* listFormatCreate(void*, const char* locale,
                                     const NJS_IntlOption* opts, size_t nopts,
                                     NJS_IntlResolved* resolved,
                                     NJS_IntlStatus* status) {
    UErrorCode st = U_ZERO_ERROR;
    const Locale loc = resolveLocale(locale, opts, nopts);
    const std::string type = optionString(opts, nopts, "type", "conjunction");
    const std::string style = optionString(opts, nopts, "style", "long");
    UListFormatterType icuType = ULISTFMT_TYPE_AND;
    if (type == "disjunction") icuType = ULISTFMT_TYPE_OR;
    else if (type == "unit") icuType = ULISTFMT_TYPE_UNITS;
    UListFormatterWidth width = ULISTFMT_WIDTH_WIDE;
    if (style == "short") width = ULISTFMT_WIDTH_SHORT;
    else if (style == "narrow") width = ULISTFMT_WIDTH_NARROW;

    ListFormatter* formatter = ListFormatter::createInstance(loc, icuType, width, st);
    if (U_FAILURE(st) || formatter == nullptr) {
        delete formatter;
        if (status != nullptr) *status = NJS_INTL_INTERNAL;
        return nullptr;
    }
    auto* result = new NJS_IntlListFormat();
    result->formatter = formatter;
    fillResolved(resolved, loc, "", "", "");
    if (status != nullptr) *status = NJS_INTL_OK;
    return result;
}

NJS_IntlStatus listFormatFormat(NJS_IntlListFormat* formatter,
                                const char* const* items, size_t count,
                                NJS_IntlPart* parts, size_t* out_count) {
    if (formatter == nullptr || formatter->formatter == nullptr) {
        return NJS_INTL_INTERNAL;
    }
    std::vector<UnicodeString> strings;
    strings.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        strings.push_back(fromUtf8(items[i] != nullptr ? items[i] : "",
                                   items[i] != nullptr ? std::strlen(items[i]) : 0));
    }

    UErrorCode st = U_ZERO_ERROR;
    FormattedList formatted =
        formatter->formatter->formatStringsToValue(
            strings.data(), static_cast<int32_t>(strings.size()), st);
    if (U_FAILURE(st)) return NJS_INTL_INTERNAL;
    const UnicodeString text = formatted.toTempString(st);
    if (U_FAILURE(st)) return NJS_INTL_INTERNAL;

    std::vector<Part> built;
    ConstrainedFieldPosition cfpos;
    cfpos.constrainCategory(UFIELD_CATEGORY_LIST);
    while (formatted.nextPosition(cfpos, st)) {
        const int32_t begin = cfpos.getStart();
        const int32_t end = cfpos.getLimit();
        if (end <= begin) continue;
        UnicodeString sub;
        text.extract(begin, end - begin, sub);
        const NJS_IntlPartType type =
            cfpos.getField() == ULISTFMT_ELEMENT_FIELD ? NJS_INTL_PART_ELEMENT
                                                       : NJS_INTL_PART_LITERAL;
        built.push_back({type, toUtf8(sub)});
    }
    if (U_FAILURE(st)) return NJS_INTL_INTERNAL;
    if (built.empty()) built.push_back({NJS_INTL_PART_LITERAL, toUtf8(text)});
    formatter->last_parts = std::move(built);
    return fillParts(formatter->last_parts, parts, out_count);
}

void listFormatDestroy(NJS_IntlListFormat* formatter) {
    if (formatter == nullptr) return;
    delete formatter->formatter;
    delete formatter;
}

// —— DisplayNames ——

NJS_IntlDisplayNames* displayNamesCreate(void*, const char* locale,
                                         const NJS_IntlOption* opts, size_t nopts,
                                         NJS_IntlResolved* resolved,
                                         NJS_IntlStatus* status) {
    const Locale loc = resolveLocale(locale, opts, nopts);
    const std::string languageDisplay =
        optionString(opts, nopts, "languageDisplay", "standard");
    const std::string style = optionString(opts, nopts, "style", "long");
    UDisplayContext contexts[2];
    int32_t contextCount = 0;
    contexts[contextCount++] = languageDisplay == "dialect"
                                   ? UDISPCTX_DIALECT_NAMES
                                   : UDISPCTX_STANDARD_NAMES;
    if (style == "short") contexts[contextCount++] = UDISPCTX_LENGTH_SHORT;
    LocaleDisplayNames* names =
        LocaleDisplayNames::createInstance(loc, contexts, contextCount);
    if (names == nullptr) {
        if (status != nullptr) *status = NJS_INTL_INTERNAL;
        return nullptr;
    }
    auto* result = new NJS_IntlDisplayNames();
    result->names = names;
    result->type = optionString(opts, nopts, "type", "language");
    result->fallback = optionString(opts, nopts, "fallback", "code");
    fillResolved(resolved, loc, "", "", "");
    if (status != nullptr) *status = NJS_INTL_OK;
    return result;
}

NJS_IntlStatus displayNamesOf(NJS_IntlDisplayNames* names, const char* code,
                              char* out, size_t* out_len) {
    if (names == nullptr || names->names == nullptr) return NJS_INTL_INTERNAL;
    const std::string input = code != nullptr ? code : "";
    UnicodeString result;
    if (names->type == "language") {
        names->names->languageDisplayName(input.c_str(), result);
    } else if (names->type == "region") {
        names->names->regionDisplayName(input.c_str(), result);
    } else if (names->type == "script") {
        names->names->scriptDisplayName(input.c_str(), result);
    } else if (names->type == "currency") {
        names->names->keyValueDisplayName("currency", input.c_str(), result);
    } else if (names->type == "calendar") {
        names->names->keyValueDisplayName("calendar", input.c_str(), result);
    } else if (names->type == "dateTimeField") {
        names->names->keyValueDisplayName("field", input.c_str(), result);
    } else {
        return NJS_INTL_INVALID_OPTION;
    }

    std::string text = toUtf8(result);
    if (text.empty() || text == input) {
        if (names->fallback == "none") {
            *out_len = 0;
            return NJS_INTL_OK;
        }
        if (text.empty()) text = input;
    }
    return copyOut(text, out, out_len);
}

void displayNamesDestroy(NJS_IntlDisplayNames* names) {
    if (names == nullptr) return;
    delete names->names;
    delete names;
}

// —— supportedValuesOf ——

const std::vector<std::string>& supportedValuesFor(const std::string& key) {
    static std::mutex mutex;
    static std::map<std::string, std::vector<std::string>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    std::vector<std::string> values;
    UErrorCode st = U_ZERO_ERROR;
    UEnumeration* enumeration = nullptr;
    if (key == "currency") {
        enumeration = ucurr_openISOCurrencies(UCURR_COMMON | UCURR_NON_DEPRECATED, &st);
    } else if (key == "timeZone") {
        enumeration = ucal_openTimeZoneIDEnumeration(UCAL_ZONE_TYPE_CANONICAL,
                                                     nullptr, nullptr, &st);
    } else if (key == "calendar") {
        enumeration = ucal_getKeywordValuesForLocale("calendar", "und", false, &st);
    } else if (key == "collation") {
        enumeration = ucol_getKeywordValuesForLocale("collation", "und", false, &st);
    } else if (key == "numberingSystem") {
        icu::StringEnumeration* names = icu::NumberingSystem::getAvailableNames(st);
        if (!U_FAILURE(st) && names != nullptr) {
            names->reset(st);
            while (const UnicodeString* name = names->snext(st)) {
                values.push_back(toUtf8(*name));
            }
            delete names;
        }
    } else if (key == "unit") {
        // ICU 未提供 unit 关键字的统一枚举；给出常用 sanctioned unit 子集。
        static const char* const kUnits[] = {
            "acre", "bit", "byte", "celsius", "centimeter", "day", "degree",
            "fahrenheit", "foot", "gigabit", "gigabyte", "gram", "hectare",
            "hour", "inch", "kilobit", "kilobyte", "kilogram", "kilometer",
            "liter", "megabit", "megabyte", "meter", "microsecond", "mile",
            "milligram", "milliliter", "millimeter", "millisecond", "minute",
            "month", "nanosecond", "ounce", "percent", "petabyte", "pound",
            "second", "stone", "terabit", "terabyte", "week", "yard", "year"};
        for (const char* unit : kUnits) values.emplace_back(unit);
    }
    if (!U_FAILURE(st) && enumeration != nullptr) {
        int32_t len = 0;
        while (const char* value = uenum_next(enumeration, &len, &st)) {
            values.emplace_back(value, static_cast<size_t>(len));
        }
        uenum_close(enumeration);
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return cache.emplace(key, std::move(values)).first->second;
}

NJS_IntlStatus supportedValues(void*, const char* key,
                               const char* const** values, size_t* count) {
    if (key == nullptr) return NJS_INTL_INVALID_OPTION;
    const std::string name(key);
    if (name != "calendar" && name != "collation" && name != "currency" &&
        name != "numberingSystem" && name != "timeZone" && name != "unit") {
        return NJS_INTL_INVALID_OPTION;
    }
    static std::mutex mutex;
    static std::map<std::string, std::vector<const char*>> pointers;
    const std::vector<std::string>& data = supportedValuesFor(name);
    std::lock_guard<std::mutex> lock(mutex);
    auto it = pointers.find(name);
    if (it == pointers.end()) {
        std::vector<const char*> list;
        list.reserve(data.size());
        for (const std::string& value : data) list.push_back(value.c_str());
        it = pointers.emplace(name, std::move(list)).first;
    }
    *count = it->second.size();
    *values = it->second.empty() ? nullptr : it->second.data();
    return NJS_INTL_OK;
}

// —— Segmenter ——

NJS_IntlSegmenter* segmenterCreate(void*, const char* locale,
                                   const NJS_IntlOption* opts, size_t nopts,
                                   NJS_IntlResolved* resolved,
                                   NJS_IntlStatus* status) {
    UErrorCode st = U_ZERO_ERROR;
    const Locale loc = resolveLocale(locale, opts, nopts);
    const std::string granularity =
        optionString(opts, nopts, "granularity", "grapheme");
    BreakIterator* iterator = nullptr;
    bool word = false;
    if (granularity == "word") {
        iterator = BreakIterator::createWordInstance(loc, st);
        word = true;
    } else if (granularity == "sentence") {
        iterator = BreakIterator::createSentenceInstance(loc, st);
    } else {
        iterator = BreakIterator::createCharacterInstance(loc, st);
    }
    if (U_FAILURE(st) || iterator == nullptr) {
        delete iterator;
        if (status != nullptr) *status = NJS_INTL_INTERNAL;
        return nullptr;
    }
    auto* result = new NJS_IntlSegmenter();
    result->iterator = iterator;
    result->word = word;
    fillResolved(resolved, loc, "", "", "");
    if (status != nullptr) *status = NJS_INTL_OK;
    return result;
}

NJS_IntlStatus segmenterSegment(NJS_IntlSegmenter* segmenter, const char* text,
                                size_t text_len, NJS_IntlSegment* segments,
                                size_t* count) {
    if (segmenter == nullptr || segmenter->iterator == nullptr) {
        return NJS_INTL_INTERNAL;
    }
    const UnicodeString input = fromUtf8(text, text_len);
    segmenter->iterator->setText(input);

    std::vector<NJS_IntlSegment> built;
    int32_t start = segmenter->iterator->first();
    for (int32_t end = segmenter->iterator->next(); end != BreakIterator::DONE;
         start = end, end = segmenter->iterator->next()) {
        int is_word_like = 0;
        if (segmenter->word) {
            auto* rbbi = dynamic_cast<RuleBasedBreakIterator*>(segmenter->iterator);
            if (rbbi != nullptr && rbbi->getRuleStatus() != UBRK_WORD_NONE) {
                is_word_like = 1;
            }
        }
        built.push_back({static_cast<size_t>(start),
                         static_cast<size_t>(end - start), is_word_like});
    }

    if (segments == nullptr || *count < built.size()) {
        *count = built.size();
        return NJS_INTL_OK;
    }
    std::copy(built.begin(), built.end(), segments);
    *count = built.size();
    return NJS_INTL_OK;
}

void segmenterDestroy(NJS_IntlSegmenter* segmenter) {
    if (segmenter == nullptr) return;
    delete segmenter->iterator;
    delete segmenter;
}

// —— 后端单例 ——

NJS_IntlBackend buildBackend() {
    NJS_IntlBackend backend{};
    backend.abi_version = NJS_INTL_ABI_VERSION;
    backend.struct_size = sizeof(NJS_IntlBackend);
    backend.capabilities = NJS_INTL_CAP_NUMBER | NJS_INTL_CAP_DATETIME |
                           NJS_INTL_CAP_COLLATOR | NJS_INTL_CAP_PLURAL |
                           NJS_INTL_CAP_SEGMENTER | NJS_INTL_CAP_LIST |
                           NJS_INTL_CAP_RELATIVE | NJS_INTL_CAP_DISPLAY_NAMES |
                           NJS_INTL_CAP_FORMAT_TO_PARTS | NJS_INTL_CAP_COMPACT;
    backend.user_data = nullptr;
    backend.canonicalize_locale = canonicalizeLocale;
    backend.available_locales = availableLocalesThunk;
    backend.default_locale = defaultLocale;
    backend.default_time_zone = defaultTimeZone;
    backend.number_format_create = numberFormatCreate;
    backend.number_format_format = numberFormatFormat;
    backend.formatter_destroy = formatterDestroy;
    backend.last_error = nullptr;
    backend.date_time_format_create = dateTimeFormatCreate;
    backend.date_time_format_format = dateTimeFormatFormat;
    backend.collator_create = collatorCreate;
    backend.collator_compare = collatorCompare;
    backend.collator_destroy = collatorDestroy;
    backend.plural_rules_create = pluralRulesCreate;
    backend.plural_rules_select = pluralRulesSelect;
    backend.plural_rules_destroy = pluralRulesDestroy;
    backend.relative_time_format_create = relativeTimeFormatCreate;
    backend.relative_time_format_format = relativeTimeFormatFormat;
    backend.relative_time_format_destroy = relativeTimeFormatDestroy;
    backend.list_format_create = listFormatCreate;
    backend.list_format_format = listFormatFormat;
    backend.list_format_destroy = listFormatDestroy;
    backend.display_names_create = displayNamesCreate;
    backend.display_names_of = displayNamesOf;
    backend.display_names_destroy = displayNamesDestroy;
    backend.supported_values = supportedValues;
    backend.segmenter_create = segmenterCreate;
    backend.segmenter_segment = segmenterSegment;
    backend.segmenter_destroy = segmenterDestroy;
    return backend;
}

}  // namespace

extern "C" const NJS_IntlBackend* NJS_IcuIntlBackend(void) {
    static const bool initialized = [] {
        return icu_data_init() == 0;
    }();
    if (!initialized) return nullptr;
    static const NJS_IntlBackend backend = buildBackend();
    return &backend;
}

extern "C" const NJS_IntlBackend* NJS_SelectIntlBackend(
    const NJS_IntlBackend* custom) {
    if (custom != nullptr) return custom;
    return NJS_IcuIntlBackend();
}
