// IntlBuiltins.cpp - Intl 命名空间与 Intl.NumberFormat（ECMA-402）
//
// 设计要点：
//   - core 不链接 ICU；locale 数据与格式化原语来自宿主注入的 NJS_IntlBackend，
//     未注入时使用内置最小后端（en / latn / gregory / UTC）；
//   - 选项解析、默认值、locale 协商、formatToParts 的 JS 对象组装在 core；
//   - 后端返回结构化 parts，core 负责拼字符串与构造 JS 数组。
//
// 关联 ADR: 020（标准库组织）、006（异常即值）

#include "core/IntlBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/IntlCollator.h"
#include "core/types/IntlDateTimeFormat.h"
#include "core/types/IntlPluralRules.h"
#include "core/types/IntlRelativeTimeFormat.h"
#include "core/types/IntlListFormat.h"
#include "core/types/IntlDisplayNames.h"
#include "core/types/IntlSegmenter.h"
#include "core/types/IntlSegments.h"
#include "core/types/IntlLocale.h"
#include "unicode/Utf8.h"
#include "core/types/IntlNumberFormat.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace neo::core {

namespace {

// —— 后端调用辅助 ——

const NJS_IntlBackend* backendOf(IContext* ctx) { return ctx->intlBackend(); }

Value* backendStatusError(IContext* ctx, NJS_IntlStatus status,
                          const char* what) {
    switch (status) {
        case NJS_INTL_INVALID_OPTION:
            return detail::rangeError(ctx, std::string("Invalid value for ") + what);
        case NJS_INTL_UNSUPPORTED:
            return detail::rangeError(ctx, std::string(what) + " is not supported");
        case NJS_INTL_INVALID_VALUE:
            return detail::rangeError(ctx, "Invalid time value");
        case NJS_INTL_INTERNAL:
        default:
            return detail::typeError(ctx, std::string("Intl backend failure in ") + what);
    }
}

// 两段式后端字符串调用。
Value* backendString(IContext* ctx, NJS_IntlStatus (*fn)(void*, char*, size_t*),
                     const char* what, std::string& out) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || fn == nullptr) {
        return detail::typeError(ctx, "Intl backend is not available");
    }
    size_t len = 0;
    NJS_IntlStatus status = fn(backend->user_data, nullptr, &len);
    if (status != NJS_INTL_OK) return backendStatusError(ctx, status, what);
    std::string buffer(len, '\0');
    status = fn(backend->user_data, buffer.data(), &len);
    if (status != NJS_INTL_OK) return backendStatusError(ctx, status, what);
    out.assign(buffer.data(), len > 0 ? len - 1 : 0);
    return nullptr;
}

Value* canonicalizeLocale(IContext* ctx, const std::string& tag, std::string& out) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->canonicalize_locale == nullptr) {
        return detail::typeError(ctx, "Intl backend is not available");
    }
    size_t len = 0;
    NJS_IntlStatus status = backend->canonicalize_locale(
        backend->user_data, tag.data(), tag.size(), nullptr, &len);
    if (status != NJS_INTL_OK) {
        return detail::rangeError(ctx, "Incorrect locale information provided");
    }
    std::string buffer(len, '\0');
    status = backend->canonicalize_locale(backend->user_data, tag.data(), tag.size(),
                                          buffer.data(), &len);
    if (status != NJS_INTL_OK) {
        return detail::rangeError(ctx, "Incorrect locale information provided");
    }
    out.assign(buffer.data(), len > 0 ? len - 1 : 0);
    return nullptr;
}

// CanonicalizeLocaleList（ECMA-402 §9.2.1 的简化版）。
Value* canonicalizeLocaleList(IContext* ctx, Value* locales,
                              std::vector<std::string>& out) {
    out.clear();
    if (locales->type()->kind() == TypeKind::Undefined) return nullptr;

    auto add = [&](const std::string& tag) -> Value* {
        std::string canonical;
        if (Value* err = canonicalizeLocale(ctx, tag, canonical)) return err;
        if (std::find(out.begin(), out.end(), canonical) == out.end()) {
            out.push_back(std::move(canonical));
        }
        return nullptr;
    };

    if (locales->type()->kind() == TypeKind::String) {
        return add(static_cast<StringData*>(locales->data())->value);
    }
    if (!detail::isObjectKind(locales->type()->kind())) {
        return detail::typeError(ctx, "Locales argument must be a string or object");
    }
    uint32_t length = 0;
    if (Value* err = detail::lengthOf(ctx, locales, length)) return err;
    for (uint32_t i = 0; i < length; ++i) {
        Value* item = ctx->getProperty(locales, ctx->createString(std::to_string(i)));
        if (detail::isErrorValue(item)) return item;
        if (item->type()->kind() == TypeKind::Undefined ||
            item->type()->kind() == TypeKind::Null) {
            return detail::typeError(ctx, "Locale must not be undefined or null");
        }
        Value* str = detail::toStringValue(ctx, item);
        if (detail::isErrorValue(str)) return str;
        if (Value* err = add(static_cast<StringData*>(str->data())->value)) return err;
    }
    return nullptr;
}

std::string languageOf(const std::string& tag) {
    const size_t dash = tag.find('-');
    return dash == std::string::npos ? tag : tag.substr(0, dash);
}

bool localeMatches(const std::string& requested, const std::string& available) {
    return requested == available || languageOf(requested) == languageOf(available);
}

// ResolveLocale（简化 lookup）：返回后端可用 locale 或默认 locale。
Value* resolveLocale(IContext* ctx, const std::vector<std::string>& requested,
                     std::string& out) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->available_locales == nullptr) {
        return detail::typeError(ctx, "Intl backend is not available");
    }
    size_t count = 0;
    const char* const* available = backend->available_locales(backend->user_data, &count);
    for (const std::string& tag : requested) {
        for (size_t i = 0; i < count; ++i) {
            if (available[i] != nullptr && localeMatches(tag, available[i])) {
                out = available[i];
                return nullptr;
            }
        }
    }
    return backendString(ctx, backend->default_locale, "default locale", out);
}

// —— 选项读取 ——

Value* getOptions(IContext* ctx, Value* options, Value*& out) {
    out = nullptr;
    if (options->type()->kind() == TypeKind::Undefined) return nullptr;
    if (!detail::isObjectKind(options->type()->kind())) {
        return detail::typeError(ctx, "Options must be an object");
    }
    out = options;
    return nullptr;
}

Value* readOption(IContext* ctx, Value* options, const char* name, Value*& out) {
    if (options == nullptr) {
        out = ctx->createUndefined();
        return nullptr;
    }
    out = ctx->getProperty(options, name);
    return detail::isErrorValue(out) ? out : nullptr;
}

Value* optionString(IContext* ctx, Value* options, const char* name,
                    const char* const* values, size_t valueCount,
                    const char* fallback, std::string& out) {
    Value* value = nullptr;
    if (Value* err = readOption(ctx, options, name, value)) return err;
    if (value->type()->kind() == TypeKind::Undefined) {
        out = fallback;
        return nullptr;
    }
    Value* str = detail::toStringValue(ctx, value);
    if (detail::isErrorValue(str)) return str;
    const std::string text = static_cast<StringData*>(str->data())->value;
    for (size_t i = 0; i < valueCount; ++i) {
        if (text == values[i]) {
            out = text;
            return nullptr;
        }
    }
    return detail::rangeError(ctx, std::string("Invalid value for option ") + name);
}

Value* optionBool(IContext* ctx, Value* options, const char* name, bool fallback,
                  bool& out) {
    Value* value = nullptr;
    if (Value* err = readOption(ctx, options, name, value)) return err;
    out = value->type()->kind() == TypeKind::Undefined ? fallback
                                                       : detail::toBoolean(value);
    return nullptr;
}

Value* optionInt(IContext* ctx, Value* options, const char* name, int min, int max,
                 int fallback, int& out) {
    Value* value = nullptr;
    if (Value* err = readOption(ctx, options, name, value)) return err;
    if (value->type()->kind() == TypeKind::Undefined) {
        out = fallback;
        return nullptr;
    }
    Value* number = detail::toNumberValue(ctx, value);
    if (detail::isErrorValue(number)) return number;
    const double n = static_cast<NumberData*>(number->data())->value;
    if (!std::isfinite(n) || std::trunc(n) != n || n < static_cast<double>(min) ||
        n > static_cast<double>(max)) {
        return detail::rangeError(ctx, std::string("Invalid value for option ") + name);
    }
    out = static_cast<int>(n);
    return nullptr;
}

Value* optionFreeString(IContext* ctx, Value* options, const char* name,
                        const char* fallback, std::string& out) {
    Value* value = nullptr;
    if (Value* err = readOption(ctx, options, name, value)) return err;
    if (value->type()->kind() == TypeKind::Undefined) {
        out = fallback;
        return nullptr;
    }
    Value* str = detail::toStringValue(ctx, value);
    if (detail::isErrorValue(str)) return str;
    out = static_cast<StringData*>(str->data())->value;
    return nullptr;
}

// —— 数字选项 ——

struct NumberOptions {
    std::string style = "decimal";
    std::string currency;
    std::string currencyDisplay = "symbol";
    std::string notation = "standard";
    std::string signDisplay = "auto";
    std::string numberingSystem = "latn";
    bool useGrouping = true;
    int minimumIntegerDigits = 1;
    int minimumFractionDigits = 0;
    int maximumFractionDigits = 3;
};

Value* parseNumberOptions(IContext* ctx, Value* options, NumberOptions& out) {
    static const char* const kStyles[] = {"decimal", "percent", "currency"};
    if (Value* err = optionString(ctx, options, "style", kStyles, 3, "decimal",
                                  out.style)) {
        return err;
    }

    if (out.style == "currency") {
        if (Value* err = optionFreeString(ctx, options, "currency", "", out.currency)) {
            return err;
        }
        if (out.currency.size() != 3) {
            return detail::rangeError(ctx, "Currency code must be 3 letters");
        }
        for (char& c : out.currency) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (!((c >= 'A' && c <= 'Z'))) {
                return detail::rangeError(ctx, "Currency code must be 3 letters");
            }
        }
    }

    static const char* const kCurrencyDisplays[] = {"symbol", "code", "name"};
    if (Value* err = optionString(ctx, options, "currencyDisplay", kCurrencyDisplays,
                                  3, "symbol", out.currencyDisplay)) {
        return err;
    }

    static const char* const kNotations[] = {"standard", "scientific", "engineering",
                                             "compact"};
    if (Value* err = optionString(ctx, options, "notation", kNotations, 4, "standard",
                                  out.notation)) {
        return err;
    }

    static const char* const kSignDisplays[] = {"auto", "always", "never", "exceptZero",
                                                "negative"};
    if (Value* err = optionString(ctx, options, "signDisplay", kSignDisplays, 5, "auto",
                                  out.signDisplay)) {
        return err;
    }

    if (Value* err = optionFreeString(ctx, options, "numberingSystem", "latn",
                                      out.numberingSystem)) {
        return err;
    }

    if (Value* err = optionBool(ctx, options, "useGrouping", true, out.useGrouping)) {
        return err;
    }

    if (Value* err = optionInt(ctx, options, "minimumIntegerDigits", 1, 21, 1,
                               out.minimumIntegerDigits)) {
        return err;
    }

    const int defaultFraction = out.style == "currency" ? 2 : 0;
    if (Value* err = optionInt(ctx, options, "minimumFractionDigits", 0, 100,
                               defaultFraction, out.minimumFractionDigits)) {
        return err;
    }
    const int defaultMax = out.style == "currency" ? 2 : 3;
    if (Value* err = optionInt(ctx, options, "maximumFractionDigits", 0, 100,
                               std::max(out.minimumFractionDigits, defaultMax),
                               out.maximumFractionDigits)) {
        return err;
    }
    if (out.maximumFractionDigits < out.minimumFractionDigits) {
        return detail::rangeError(
            ctx, "maximumFractionDigits must be >= minimumFractionDigits");
    }

    // 这些选项在规范中存在，但最小后端不支持；显式报错，避免静默错误。
    static const char* const kUnsupported[] = {
        "minimumSignificantDigits", "maximumSignificantDigits", "compactDisplay",
        "unit",                     "unitDisplay",               "currencySign",
        "roundingMode",             "trailingZeroDisplay",       "roundingPriority",
        "roundingIncrement",
    };
    for (const char* name : kUnsupported) {
        Value* value = nullptr;
        if (Value* err = readOption(ctx, options, name, value)) return err;
        if (value->type()->kind() != TypeKind::Undefined) {
            return detail::rangeError(ctx, std::string("Option ") + name +
                                               " is not supported");
        }
    }
    return nullptr;
}

Value* buildBackendOptions(const NumberOptions& options,
                           std::vector<std::pair<std::string, std::string>>& storage,
                           std::vector<NJS_IntlOption>& out) {
    storage.clear();
    storage.emplace_back("style", options.style);
    if (options.style == "currency") {
        storage.emplace_back("currency", options.currency);
    }
    storage.emplace_back("currencyDisplay", options.currencyDisplay);
    storage.emplace_back("notation", options.notation);
    storage.emplace_back("signDisplay", options.signDisplay);
    storage.emplace_back("numberingSystem", options.numberingSystem);
    storage.emplace_back("useGrouping", options.useGrouping ? "true" : "false");
    storage.emplace_back("minimumIntegerDigits",
                         std::to_string(options.minimumIntegerDigits));
    storage.emplace_back("minimumFractionDigits",
                         std::to_string(options.minimumFractionDigits));
    storage.emplace_back("maximumFractionDigits",
                         std::to_string(options.maximumFractionDigits));

    out.clear();
    out.reserve(storage.size());
    for (const auto& entry : storage) {
        out.push_back(NJS_IntlOption{entry.first.c_str(), entry.second.c_str()});
    }
    return nullptr;
}

// —— Intl.NumberFormat ——

Value* numberFormatConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                             Value* argv[]);

Value* numberFormatCall(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return numberFormatConstruct(ctx, self, argc, argv);
}

Value* numberFormatConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                             Value* argv[]) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->number_format_create == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support NumberFormat");
    }

    Value* locales = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* optionsArg = argc > 1 ? argv[1] : ctx->createUndefined();
    Value* options = nullptr;
    if (Value* err = getOptions(ctx, optionsArg, options)) return err;

    std::vector<std::string> requested;
    if (Value* err = canonicalizeLocaleList(ctx, locales, requested)) return err;
    std::string locale;
    if (Value* err = resolveLocale(ctx, requested, locale)) return err;

    NumberOptions parsed;
    if (Value* err = parseNumberOptions(ctx, options, parsed)) return err;

    std::vector<std::pair<std::string, std::string>> storage;
    std::vector<NJS_IntlOption> backendOptions;
    buildBackendOptions(parsed, storage, backendOptions);

    NJS_IntlResolved resolved{};
    NJS_IntlStatus status = NJS_INTL_OK;
    NJS_IntlFormatter* formatter = backend->number_format_create(
        backend->user_data, locale.c_str(), backendOptions.data(),
        backendOptions.size(), &resolved, &status);
    if (formatter == nullptr) {
        return backendStatusError(ctx, status, "Intl.NumberFormat options");
    }

    Value* object = ctx->createIntlNumberFormat(backend, formatter, resolved.locale);
    if (detail::isErrorValue(object)) return object;

    IntlNumberFormatData* d = asIntlNumberFormat(object);
    d->locale = resolved.locale;
    d->numberingSystem = resolved.numbering_system;
    d->style = parsed.style;
    d->currency = parsed.currency;
    d->currencyDisplay = parsed.currencyDisplay;
    d->notation = parsed.notation;
    d->signDisplay = parsed.signDisplay;
    d->useGrouping = parsed.useGrouping;
    d->minimumIntegerDigits = parsed.minimumIntegerDigits;
    d->minimumFractionDigits = parsed.minimumFractionDigits;
    d->maximumFractionDigits = parsed.maximumFractionDigits;
    return object;
}

Value* requireNumberFormat(IContext* ctx, Value* self, IntlNumberFormatData*& out) {
    out = asIntlNumberFormat(self);
    if (out == nullptr || out->formatter == nullptr || out->backend == nullptr) {
        return detail::typeError(
            ctx, "Intl.NumberFormat method called on incompatible receiver");
    }
    return nullptr;
}

Value* formatParts(IContext* ctx, IntlNumberFormatData* d, double value,
                   std::vector<NJS_IntlPart>& parts) {
    const NJS_IntlBackend* backend = d->backend;
    if (backend->number_format_format == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support formatting");
    }
    size_t count = 0;
    NJS_IntlStatus status =
        backend->number_format_format(d->formatter, value, nullptr, &count);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.NumberFormat.format");
    }
    parts.resize(count);
    if (count == 0) return nullptr;
    status = backend->number_format_format(d->formatter, value, parts.data(), &count);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.NumberFormat.format");
    }
    parts.resize(count);
    return nullptr;
}

Value* numberFormatFormat(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    IntlNumberFormatData* d = nullptr;
    if (Value* err = requireNumberFormat(ctx, self, d)) return err;
    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* number = detail::toNumberValue(ctx, value);
    if (detail::isErrorValue(number)) return number;

    std::vector<NJS_IntlPart> parts;
    if (Value* err = formatParts(ctx, d, static_cast<NumberData*>(number->data())->value,
                                 parts)) {
        return err;
    }
    std::string out;
    for (const NJS_IntlPart& part : parts) {
        out.append(part.text, part.length);
    }
    return ctx->createString(out);
}

const char* partTypeName(NJS_IntlPartType type) {
    switch (type) {
        case NJS_INTL_PART_LITERAL: return "literal";
        case NJS_INTL_PART_INTEGER: return "integer";
        case NJS_INTL_PART_GROUP: return "group";
        case NJS_INTL_PART_DECIMAL: return "decimal";
        case NJS_INTL_PART_FRACTION: return "fraction";
        case NJS_INTL_PART_CURRENCY: return "currency";
        case NJS_INTL_PART_UNIT: return "unit";
        case NJS_INTL_PART_PERCENT: return "percent";
        case NJS_INTL_PART_MINUS_SIGN: return "minusSign";
        case NJS_INTL_PART_PLUS_SIGN: return "plusSign";
        case NJS_INTL_PART_YEAR: return "year";
        case NJS_INTL_PART_MONTH: return "month";
        case NJS_INTL_PART_DAY: return "day";
        case NJS_INTL_PART_HOUR: return "hour";
        case NJS_INTL_PART_MINUTE: return "minute";
        case NJS_INTL_PART_SECOND: return "second";
        case NJS_INTL_PART_DAY_PERIOD: return "dayPeriod";
        case NJS_INTL_PART_TIME_ZONE_NAME: return "timeZoneName";
        case NJS_INTL_PART_ELEMENT: return "element";
    }
    return "literal";
}

Value* numberFormatFormatToParts(IContext* ctx, Value* self, size_t argc,
                                 Value* argv[]) {
    IntlNumberFormatData* d = nullptr;
    if (Value* err = requireNumberFormat(ctx, self, d)) return err;
    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* number = detail::toNumberValue(ctx, value);
    if (detail::isErrorValue(number)) return number;

    std::vector<NJS_IntlPart> parts;
    if (Value* err = formatParts(ctx, d, static_cast<NumberData*>(number->data())->value,
                                 parts)) {
        return err;
    }

    Value* array = ctx->createArray();
    if (detail::isErrorValue(array)) return array;
    auto* arrayData = static_cast<ArrayData*>(array->data());
    for (size_t i = 0; i < parts.size(); ++i) {
        Value* item = ctx->createObject();
        if (detail::isErrorValue(item)) return item;
        Value* type = ctx->createString(partTypeName(parts[i].type));
        Value* text = ctx->createString(std::string(parts[i].text, parts[i].length));
        if (Value* err = ctx->defineProperty(item, "type", type, true, true, true)) {
            if (detail::isErrorValue(err)) return err;
        }
        if (Value* err = ctx->defineProperty(item, "value", text, true, true, true)) {
            if (detail::isErrorValue(err)) return err;
        }
        arrayData->setElement(static_cast<uint32_t>(i), item);
    }
    return array;
}

Value* numberFormatResolvedOptions(IContext* ctx, Value* self, size_t /*argc*/,
                                   Value* /*argv*/[]) {
    IntlNumberFormatData* d = nullptr;
    if (Value* err = requireNumberFormat(ctx, self, d)) return err;

    Value* result = ctx->createObject();
    if (detail::isErrorValue(result)) return result;
    auto define = [&](const char* key, Value* value) -> Value* {
        Value* r = ctx->defineProperty(result, key, value, true, true, true);
        return detail::isErrorValue(r) ? r : nullptr;
    };
    if (Value* err = define("locale", ctx->createString(d->locale))) return err;
    if (Value* err = define("numberingSystem",
                            ctx->createString(d->numberingSystem))) {
        return err;
    }
    if (Value* err = define("style", ctx->createString(d->style))) return err;
    if (d->style == "currency") {
        if (Value* err = define("currency", ctx->createString(d->currency))) return err;
        if (Value* err = define("currencyDisplay",
                                ctx->createString(d->currencyDisplay))) {
            return err;
        }
    }
    if (Value* err = define("notation", ctx->createString(d->notation))) return err;
    if (Value* err = define("signDisplay", ctx->createString(d->signDisplay))) {
        return err;
    }
    if (Value* err = define("useGrouping", ctx->createBoolean(d->useGrouping))) {
        return err;
    }
    if (Value* err = define("minimumIntegerDigits",
                            ctx->createNumber(d->minimumIntegerDigits))) {
        return err;
    }
    if (Value* err = define("minimumFractionDigits",
                            ctx->createNumber(d->minimumFractionDigits))) {
        return err;
    }
    if (Value* err = define("maximumFractionDigits",
                            ctx->createNumber(d->maximumFractionDigits))) {
        return err;
    }
    return result;
}

Value* numberFormatFormatGetter(IContext* ctx, Value* self, size_t /*argc*/,
                                Value* /*argv*/[]) {
    IntlNumberFormatData* d = nullptr;
    if (Value* err = requireNumberFormat(ctx, self, d)) return err;
    Value* fn = ctx->createNativeFunction(numberFormatFormat, 0);
    if (fn == ctx->oomValue()) return fn;
    ctx->setFunctionName(fn, "format");
    return ctx->createBoundFunction(fn, self, 0, nullptr);
}

Value* supportedLocalesOfImpl(IContext* ctx, size_t argc, Value* argv[]) {
    Value* locales = argc > 0 ? argv[0] : ctx->createUndefined();
    std::vector<std::string> requested;
    if (Value* err = canonicalizeLocaleList(ctx, locales, requested)) return err;

    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->available_locales == nullptr) {
        return detail::typeError(ctx, "Intl backend is not available");
    }
    size_t count = 0;
    const char* const* available = backend->available_locales(backend->user_data, &count);

    Value* array = ctx->createArray();
    if (detail::isErrorValue(array)) return array;
    auto* arrayData = static_cast<ArrayData*>(array->data());
    size_t index = 0;
    for (const std::string& tag : requested) {
        for (size_t i = 0; i < count; ++i) {
            if (available[i] != nullptr && localeMatches(tag, available[i])) {
                arrayData->setElement(static_cast<uint32_t>(index++),
                                      ctx->createString(available[i]));
                break;
            }
        }
    }
    return array;
}

Value* numberFormatSupportedLocalesOf(IContext* ctx, Value* /*self*/, size_t argc,
                                      Value* argv[]) {
    return supportedLocalesOfImpl(ctx, argc, argv);
}

Value* dateTimeFormatSupportedLocalesOf(IContext* ctx, Value* /*self*/, size_t argc,
                                        Value* argv[]) {
    return supportedLocalesOfImpl(ctx, argc, argv);
}

Value* getCanonicalLocales(IContext* ctx, Value* /*self*/, size_t argc,
                           Value* argv[]) {
    Value* locales = argc > 0 ? argv[0] : ctx->createUndefined();
    std::vector<std::string> canonical;
    if (Value* err = canonicalizeLocaleList(ctx, locales, canonical)) return err;
    Value* array = ctx->createArray();
    if (detail::isErrorValue(array)) return array;
    auto* arrayData = static_cast<ArrayData*>(array->data());
    for (size_t i = 0; i < canonical.size(); ++i) {
        arrayData->setElement(static_cast<uint32_t>(i),
                              ctx->createString(canonical[i]));
    }
    return array;
}

// —— Intl.DateTimeFormat ——

struct DateTimeOptions {
    std::string dateStyle;
    std::string timeStyle;
    std::string timeZone;
    std::string calendar = "gregory";
    std::string numberingSystem = "latn";
    std::string hourCycle;
    std::string timeZoneName;
    std::string weekday;
    std::string era;
    bool hour12 = false;
    bool hasHour12 = false;
    bool hasYear = false;
    bool hasMonth = false;
    bool hasDay = false;
    bool hasHour = false;
    bool hasMinute = false;
    bool hasSecond = false;
    bool year2Digit = false;
    bool month2Digit = false;
    bool day2Digit = false;
    bool hour2Digit = false;
    bool minute2Digit = false;
    bool second2Digit = false;
    int fractionalSecondDigits = -1;
    std::string yearValue;
    std::string monthValue;
    std::string dayValue;
    std::string hourValue;
    std::string minuteValue;
    std::string secondValue;
};

Value* optionComponent(IContext* ctx, Value* options, const char* name,
                       const char* const* values, size_t valueCount, bool& present,
                       bool& twoDigit, std::string& resolved) {
    Value* value = nullptr;
    if (Value* err = readOption(ctx, options, name, value)) return err;
    if (value->type()->kind() == TypeKind::Undefined) {
        present = false;
        return nullptr;
    }
    Value* str = detail::toStringValue(ctx, value);
    if (detail::isErrorValue(str)) return str;
    const std::string text = static_cast<StringData*>(str->data())->value;
    for (size_t i = 0; i < valueCount; ++i) {
        if (text == values[i]) {
            present = true;
            twoDigit = text == "2-digit";
            resolved = text;
            return nullptr;
        }
    }
    return detail::rangeError(ctx, std::string("Invalid value for option ") + name);
}

Value* parseDateTimeOptions(IContext* ctx, Value* options, DateTimeOptions& out) {
    static const char* const kStyles[] = {"full", "long", "medium", "short"};
    if (Value* err = optionString(ctx, options, "dateStyle", kStyles, 4, "",
                                  out.dateStyle)) {
        return err;
    }
    if (Value* err = optionString(ctx, options, "timeStyle", kStyles, 4, "",
                                  out.timeStyle)) {
        return err;
    }
    if (Value* err = optionFreeString(ctx, options, "calendar", "gregory",
                                      out.calendar)) {
        return err;
    }
    if (Value* err = optionFreeString(ctx, options, "numberingSystem", "latn",
                                      out.numberingSystem)) {
        return err;
    }

    Value* timeZone = nullptr;
    if (Value* err = readOption(ctx, options, "timeZone", timeZone)) return err;
    if (timeZone->type()->kind() == TypeKind::Undefined) {
        if (Value* err = backendString(ctx, backendOf(ctx)->default_time_zone,
                                       "default time zone", out.timeZone)) {
            return err;
        }
    } else {
        Value* str = detail::toStringValue(ctx, timeZone);
        if (detail::isErrorValue(str)) return str;
        out.timeZone = static_cast<StringData*>(str->data())->value;
    }

    Value* hour12 = nullptr;
    if (Value* err = readOption(ctx, options, "hour12", hour12)) return err;
    if (hour12->type()->kind() != TypeKind::Undefined) {
        out.hasHour12 = true;
        out.hour12 = detail::toBoolean(hour12);
    }

    static const char* const kHourCycles[] = {"h11", "h12", "h23", "h24"};
    if (Value* err = optionString(ctx, options, "hourCycle", kHourCycles, 4, "",
                                  out.hourCycle)) {
        return err;
    }

    static const char* const kNumeric[] = {"numeric", "2-digit"};
    bool any_component = false;
    auto mark = [&](bool present) { any_component = any_component || present; };

    if (Value* err = optionComponent(ctx, options, "year", kNumeric, 2, out.hasYear,
                                     out.year2Digit, out.yearValue)) {
        return err;
    }
    mark(out.hasYear);

    static const char* const kMonths[] = {"numeric", "2-digit", "long", "short",
                                          "narrow"};
    if (Value* err = optionComponent(ctx, options, "month", kMonths, 5, out.hasMonth,
                                     out.month2Digit, out.monthValue)) {
        return err;
    }
    mark(out.hasMonth);

    if (Value* err = optionComponent(ctx, options, "day", kNumeric, 2, out.hasDay,
                                     out.day2Digit, out.dayValue)) {
        return err;
    }
    mark(out.hasDay);

    if (Value* err = optionComponent(ctx, options, "hour", kNumeric, 2, out.hasHour,
                                     out.hour2Digit, out.hourValue)) {
        return err;
    }
    mark(out.hasHour);

    if (Value* err = optionComponent(ctx, options, "minute", kNumeric, 2,
                                     out.hasMinute, out.minute2Digit,
                                     out.minuteValue)) {
        return err;
    }
    mark(out.hasMinute);

    if (Value* err = optionComponent(ctx, options, "second", kNumeric, 2,
                                     out.hasSecond, out.second2Digit,
                                     out.secondValue)) {
        return err;
    }
    mark(out.hasSecond);

    Value* fractional = nullptr;
    if (Value* err = readOption(ctx, options, "fractionalSecondDigits", fractional)) {
        return err;
    }
    if (fractional->type()->kind() != TypeKind::Undefined) {
        if (Value* err = optionInt(ctx, options, "fractionalSecondDigits", 0, 3, -1,
                                   out.fractionalSecondDigits)) {
            return err;
        }
        any_component = true;
    }

    static const char* const kTimeZoneNames[] = {"short", "long", "shortOffset",
                                                 "longOffset"};
    if (Value* err = optionString(ctx, options, "timeZoneName", kTimeZoneNames, 4, "",
                                  out.timeZoneName)) {
        return err;
    }
    if (!out.timeZoneName.empty()) any_component = true;

    static const char* const kWeekdays[] = {"long", "short", "narrow"};
    if (Value* err = optionString(ctx, options, "weekday", kWeekdays, 3, "",
                                  out.weekday)) {
        return err;
    }
    if (!out.weekday.empty()) any_component = true;

    static const char* const kEras[] = {"long", "short", "narrow"};
    if (Value* err = optionString(ctx, options, "era", kEras, 3, "", out.era)) {
        return err;
    }
    if (!out.era.empty()) any_component = true;

    const bool has_style = !out.dateStyle.empty() || !out.timeStyle.empty();
    if (has_style && any_component) {
        return detail::rangeError(
            ctx, "dateStyle/timeStyle cannot be combined with component options");
    }
    if (!has_style && !any_component) {
        out.hasYear = true;
        out.hasMonth = true;
        out.hasDay = true;
    }
    if (!out.dateStyle.empty()) {
        out.hasYear = true;
        out.hasMonth = true;
        out.hasDay = true;
    }
    if (!out.timeStyle.empty()) {
        out.hasHour = true;
        out.hasMinute = true;
        out.hasSecond = true;
    }

    if (out.hasHour && !out.hasHour12 && !out.hourCycle.empty()) {
        out.hour12 = out.hourCycle == "h11" || out.hourCycle == "h12";
        out.hasHour12 = true;
    }
    return nullptr;
}

void buildDateTimeBackendOptions(
    const DateTimeOptions& options,
    std::vector<std::pair<std::string, std::string>>& storage,
    std::vector<NJS_IntlOption>& out) {
    storage.clear();
    auto add = [&](const char* key, const std::string& value) {
        if (!value.empty()) storage.emplace_back(key, value);
    };
    add("dateStyle", options.dateStyle);
    add("timeStyle", options.timeStyle);
    add("timeZone", options.timeZone);
    add("calendar", options.calendar);
    add("numberingSystem", options.numberingSystem);
    add("hourCycle", options.hourCycle);
    add("timeZoneName", options.timeZoneName);
    add("weekday", options.weekday);
    add("era", options.era);
    if (options.hasYear) add("year", options.yearValue);
    if (options.hasMonth) add("month", options.monthValue);
    if (options.hasDay) add("day", options.dayValue);
    if (options.hasHour) add("hour", options.hourValue);
    if (options.hasMinute) add("minute", options.minuteValue);
    if (options.hasSecond) add("second", options.secondValue);
    if (options.fractionalSecondDigits >= 0) {
        storage.emplace_back("fractionalSecondDigits",
                             std::to_string(options.fractionalSecondDigits));
    }
    if (options.hasHour || options.hasHour12) {
        storage.emplace_back("hour12", options.hour12 ? "true" : "false");
    }

    out.clear();
    out.reserve(storage.size());
    for (const auto& entry : storage) {
        out.push_back(NJS_IntlOption{entry.first.c_str(), entry.second.c_str()});
    }
}

Value* dateTimeFormatConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                               Value* argv[]);

Value* dateTimeFormatCall(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return dateTimeFormatConstruct(ctx, self, argc, argv);
}

Value* dateTimeFormatConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                               Value* argv[]) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->date_time_format_create == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support DateTimeFormat");
    }

    Value* locales = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* optionsArg = argc > 1 ? argv[1] : ctx->createUndefined();
    Value* options = nullptr;
    if (Value* err = getOptions(ctx, optionsArg, options)) return err;

    std::vector<std::string> requested;
    if (Value* err = canonicalizeLocaleList(ctx, locales, requested)) return err;
    std::string locale;
    if (Value* err = resolveLocale(ctx, requested, locale)) return err;

    DateTimeOptions parsed;
    if (Value* err = parseDateTimeOptions(ctx, options, parsed)) return err;

    std::vector<std::pair<std::string, std::string>> storage;
    std::vector<NJS_IntlOption> backendOptions;
    buildDateTimeBackendOptions(parsed, storage, backendOptions);

    NJS_IntlResolved resolved{};
    NJS_IntlStatus status = NJS_INTL_OK;
    NJS_IntlFormatter* formatter = backend->date_time_format_create(
        backend->user_data, locale.c_str(), backendOptions.data(),
        backendOptions.size(), &resolved, &status);
    if (formatter == nullptr) {
        return backendStatusError(ctx, status, "Intl.DateTimeFormat options");
    }

    Value* object = ctx->createIntlDateTimeFormat(backend, formatter, resolved.locale);
    if (detail::isErrorValue(object)) return object;

    IntlDateTimeFormatData* d = asIntlDateTimeFormat(object);
    d->locale = resolved.locale;
    d->calendar = resolved.calendar;
    d->numberingSystem = resolved.numbering_system;
    d->timeZone = resolved.time_zone;
    d->dateStyle = parsed.dateStyle;
    d->timeStyle = parsed.timeStyle;
    d->hourCycle = parsed.hourCycle;
    d->timeZoneName = parsed.timeZoneName;
    d->hour12 = parsed.hour12;
    d->hasHour12 = parsed.hasHour12;
    d->hasYear = parsed.hasYear;
    d->hasMonth = parsed.hasMonth;
    d->hasDay = parsed.hasDay;
    d->hasHour = parsed.hasHour;
    d->hasMinute = parsed.hasMinute;
    d->hasSecond = parsed.hasSecond;
    d->year2Digit = parsed.year2Digit;
    d->month2Digit = parsed.month2Digit;
    d->day2Digit = parsed.day2Digit;
    d->hour2Digit = parsed.hour2Digit;
    d->minute2Digit = parsed.minute2Digit;
    d->second2Digit = parsed.second2Digit;
    d->fractionalSecondDigits = parsed.fractionalSecondDigits;
    return object;
}

Value* requireDateTimeFormat(IContext* ctx, Value* self,
                             IntlDateTimeFormatData*& out) {
    out = asIntlDateTimeFormat(self);
    if (out == nullptr || out->formatter == nullptr || out->backend == nullptr) {
        return detail::typeError(
            ctx, "Intl.DateTimeFormat method called on incompatible receiver");
    }
    return nullptr;
}

Value* formatDateParts(IContext* ctx, IntlDateTimeFormatData* d, double time,
                       std::vector<NJS_IntlPart>& parts) {
    const NJS_IntlBackend* backend = d->backend;
    if (backend->date_time_format_format == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support formatting");
    }
    size_t count = 0;
    NJS_IntlStatus status =
        backend->date_time_format_format(d->formatter, time, nullptr, &count);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.DateTimeFormat.format");
    }
    parts.resize(count);
    if (count == 0) return nullptr;
    status = backend->date_time_format_format(d->formatter, time, parts.data(), &count);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.DateTimeFormat.format");
    }
    parts.resize(count);
    return nullptr;
}

Value* dateTimeFormatFormat(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    IntlDateTimeFormatData* d = nullptr;
    if (Value* err = requireDateTimeFormat(ctx, self, d)) return err;
    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* number = detail::toNumberValue(ctx, value);
    if (detail::isErrorValue(number)) return number;

    std::vector<NJS_IntlPart> parts;
    if (Value* err = formatDateParts(
            ctx, d, static_cast<NumberData*>(number->data())->value, parts)) {
        return err;
    }
    std::string out;
    for (const NJS_IntlPart& part : parts) {
        out.append(part.text, part.length);
    }
    return ctx->createString(out);
}

Value* dateTimeFormatFormatToParts(IContext* ctx, Value* self, size_t argc,
                                   Value* argv[]) {
    IntlDateTimeFormatData* d = nullptr;
    if (Value* err = requireDateTimeFormat(ctx, self, d)) return err;
    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* number = detail::toNumberValue(ctx, value);
    if (detail::isErrorValue(number)) return number;

    std::vector<NJS_IntlPart> parts;
    if (Value* err = formatDateParts(
            ctx, d, static_cast<NumberData*>(number->data())->value, parts)) {
        return err;
    }

    Value* array = ctx->createArray();
    if (detail::isErrorValue(array)) return array;
    auto* arrayData = static_cast<ArrayData*>(array->data());
    for (size_t i = 0; i < parts.size(); ++i) {
        Value* item = ctx->createObject();
        if (detail::isErrorValue(item)) return item;
        Value* type = ctx->createString(partTypeName(parts[i].type));
        Value* text = ctx->createString(std::string(parts[i].text, parts[i].length));
        Value* r1 = ctx->defineProperty(item, "type", type, true, true, true);
        if (detail::isErrorValue(r1)) return r1;
        Value* r2 = ctx->defineProperty(item, "value", text, true, true, true);
        if (detail::isErrorValue(r2)) return r2;
        arrayData->setElement(static_cast<uint32_t>(i), item);
    }
    return array;
}

Value* dateTimeFormatResolvedOptions(IContext* ctx, Value* self, size_t /*argc*/,
                                     Value* /*argv*/[]) {
    IntlDateTimeFormatData* d = nullptr;
    if (Value* err = requireDateTimeFormat(ctx, self, d)) return err;

    Value* result = ctx->createObject();
    if (detail::isErrorValue(result)) return result;
    auto define = [&](const char* key, Value* value) -> Value* {
        Value* r = ctx->defineProperty(result, key, value, true, true, true);
        return detail::isErrorValue(r) ? r : nullptr;
    };
    if (Value* err = define("locale", ctx->createString(d->locale))) return err;
    if (Value* err = define("calendar", ctx->createString(d->calendar))) return err;
    if (Value* err = define("numberingSystem",
                            ctx->createString(d->numberingSystem))) {
        return err;
    }
    if (Value* err = define("timeZone", ctx->createString(d->timeZone))) return err;
    if (!d->dateStyle.empty()) {
        if (Value* err = define("dateStyle", ctx->createString(d->dateStyle))) {
            return err;
        }
    }
    if (!d->timeStyle.empty()) {
        if (Value* err = define("timeStyle", ctx->createString(d->timeStyle))) {
            return err;
        }
    }
    if (d->dateStyle.empty() && d->timeStyle.empty()) {
        if (d->hasYear) {
            if (Value* err = define("year", ctx->createString(
                                                d->year2Digit ? "2-digit" : "numeric"))) {
                return err;
            }
        }
        if (d->hasMonth) {
            if (Value* err = define("month", ctx->createString(
                                                 d->month2Digit ? "2-digit" : "numeric"))) {
                return err;
            }
        }
        if (d->hasDay) {
            if (Value* err = define("day", ctx->createString(
                                                d->day2Digit ? "2-digit" : "numeric"))) {
                return err;
            }
        }
        if (d->hasHour) {
            if (Value* err = define("hour", ctx->createString(
                                                 d->hour2Digit ? "2-digit" : "numeric"))) {
                return err;
            }
        }
        if (d->hasMinute) {
            if (Value* err = define("minute", ctx->createString(
                                                   d->minute2Digit ? "2-digit" : "numeric"))) {
                return err;
            }
        }
        if (d->hasSecond) {
            if (Value* err = define("second", ctx->createString(
                                                   d->second2Digit ? "2-digit" : "numeric"))) {
                return err;
            }
        }
        if (d->fractionalSecondDigits >= 0) {
            if (Value* err = define("fractionalSecondDigits",
                                    ctx->createNumber(d->fractionalSecondDigits))) {
                return err;
            }
        }
        if (!d->timeZoneName.empty()) {
            if (Value* err = define("timeZoneName",
                                    ctx->createString(d->timeZoneName))) {
                return err;
            }
        }
    }
    if (d->hasHour || d->hasHour12) {
        if (Value* err = define("hour12", ctx->createBoolean(d->hour12))) return err;
        if (!d->hourCycle.empty()) {
            if (Value* err = define("hourCycle", ctx->createString(d->hourCycle))) {
                return err;
            }
        }
    }
    return result;
}

Value* dateTimeFormatFormatGetter(IContext* ctx, Value* self, size_t /*argc*/,
                                  Value* /*argv*/[]) {
    IntlDateTimeFormatData* d = nullptr;
    if (Value* err = requireDateTimeFormat(ctx, self, d)) return err;
    Value* fn = ctx->createNativeFunction(dateTimeFormatFormat, 0);
    if (fn == ctx->oomValue()) return fn;
    ctx->setFunctionName(fn, "format");
    return ctx->createBoundFunction(fn, self, 0, nullptr);
}

// —— Intl.Collator ——

struct CollatorOptions {
    std::string usage = "sort";
    std::string sensitivity = "variant";
    std::string caseFirst = "false";
    std::string collation = "default";
    bool ignorePunctuation = false;
    bool numeric = false;
};

Value* parseCollatorOptions(IContext* ctx, Value* options, CollatorOptions& out) {
    static const char* const kUsages[] = {"sort", "search"};
    if (Value* err = optionString(ctx, options, "usage", kUsages, 2, "sort",
                                  out.usage)) {
        return err;
    }
    static const char* const kSensitivities[] = {"base", "accent", "case",
                                                 "variant"};
    if (Value* err = optionString(ctx, options, "sensitivity", kSensitivities, 4,
                                  "variant", out.sensitivity)) {
        return err;
    }
    static const char* const kCaseFirsts[] = {"upper", "lower", "false"};
    if (Value* err = optionString(ctx, options, "caseFirst", kCaseFirsts, 3,
                                  "false", out.caseFirst)) {
        return err;
    }
    if (Value* err = optionFreeString(ctx, options, "collation", "default",
                                      out.collation)) {
        return err;
    }
    if (Value* err = optionBool(ctx, options, "ignorePunctuation", false,
                                out.ignorePunctuation)) {
        return err;
    }
    if (Value* err = optionBool(ctx, options, "numeric", false, out.numeric)) {
        return err;
    }
    return nullptr;
}

void buildCollatorBackendOptions(
    const CollatorOptions& options,
    std::vector<std::pair<std::string, std::string>>& storage,
    std::vector<NJS_IntlOption>& out) {
    storage.clear();
    storage.emplace_back("usage", options.usage);
    storage.emplace_back("sensitivity", options.sensitivity);
    storage.emplace_back("caseFirst", options.caseFirst);
    storage.emplace_back("collation", options.collation);
    storage.emplace_back("ignorePunctuation",
                         options.ignorePunctuation ? "true" : "false");
    storage.emplace_back("numeric", options.numeric ? "true" : "false");
    out.clear();
    out.reserve(storage.size());
    for (const auto& entry : storage) {
        out.push_back(NJS_IntlOption{entry.first.c_str(), entry.second.c_str()});
    }
}

Value* collatorConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                         Value* argv[]);

Value* collatorCall(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return collatorConstruct(ctx, self, argc, argv);
}

Value* collatorConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                         Value* argv[]) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->collator_create == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support Collator");
    }

    Value* locales = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* optionsArg = argc > 1 ? argv[1] : ctx->createUndefined();
    Value* options = nullptr;
    if (Value* err = getOptions(ctx, optionsArg, options)) return err;

    std::vector<std::string> requested;
    if (Value* err = canonicalizeLocaleList(ctx, locales, requested)) return err;
    std::string locale;
    if (Value* err = resolveLocale(ctx, requested, locale)) return err;

    CollatorOptions parsed;
    if (Value* err = parseCollatorOptions(ctx, options, parsed)) return err;

    std::vector<std::pair<std::string, std::string>> storage;
    std::vector<NJS_IntlOption> backendOptions;
    buildCollatorBackendOptions(parsed, storage, backendOptions);

    NJS_IntlResolved resolved{};
    NJS_IntlStatus status = NJS_INTL_OK;
    NJS_IntlCollator* collator = backend->collator_create(
        backend->user_data, locale.c_str(), backendOptions.data(),
        backendOptions.size(), &resolved, &status);
    if (collator == nullptr) {
        return backendStatusError(ctx, status, "Intl.Collator options");
    }

    Value* object = ctx->createIntlCollator(backend, collator, resolved.locale);
    if (detail::isErrorValue(object)) return object;

    IntlCollatorData* d = asIntlCollator(object);
    d->locale = resolved.locale;
    d->usage = parsed.usage;
    d->sensitivity = parsed.sensitivity;
    d->caseFirst = parsed.caseFirst;
    d->collation = parsed.collation;
    d->ignorePunctuation = parsed.ignorePunctuation;
    d->numeric = parsed.numeric;
    return object;
}

Value* requireCollator(IContext* ctx, Value* self, IntlCollatorData*& out) {
    out = asIntlCollator(self);
    if (out == nullptr || out->collator == nullptr || out->backend == nullptr) {
        return detail::typeError(
            ctx, "Intl.Collator method called on incompatible receiver");
    }
    return nullptr;
}

Value* collatorCompareFn(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    IntlCollatorData* d = nullptr;
    if (Value* err = requireCollator(ctx, self, d)) return err;
    Value* a = detail::toStringValue(ctx, argc > 0 ? argv[0] : ctx->createUndefined());
    if (detail::isErrorValue(a)) return a;
    Value* b = detail::toStringValue(ctx, argc > 1 ? argv[1] : ctx->createUndefined());
    if (detail::isErrorValue(b)) return b;
    if (d->backend->collator_compare == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support comparison");
    }
    const std::string& sa = static_cast<StringData*>(a->data())->value;
    const std::string& sb = static_cast<StringData*>(b->data())->value;
    int result = 0;
    NJS_IntlStatus status = d->backend->collator_compare(
        d->collator, sa.data(), sa.size(), sb.data(), sb.size(), &result);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.Collator.compare");
    }
    return ctx->createNumber(static_cast<double>(result));
}

Value* collatorCompareGetter(IContext* ctx, Value* self, size_t /*argc*/,
                             Value* /*argv*/[]) {
    IntlCollatorData* d = nullptr;
    if (Value* err = requireCollator(ctx, self, d)) return err;
    Value* fn = ctx->createNativeFunction(collatorCompareFn, 0);
    if (fn == ctx->oomValue()) return fn;
    ctx->setFunctionName(fn, "compare");
    return ctx->createBoundFunction(fn, self, 0, nullptr);
}

Value* collatorResolvedOptions(IContext* ctx, Value* self, size_t /*argc*/,
                               Value* /*argv*/[]) {
    IntlCollatorData* d = nullptr;
    if (Value* err = requireCollator(ctx, self, d)) return err;
    Value* result = ctx->createObject();
    if (detail::isErrorValue(result)) return result;
    auto define = [&](const char* key, Value* value) -> Value* {
        Value* r = ctx->defineProperty(result, key, value, true, true, true);
        return detail::isErrorValue(r) ? r : nullptr;
    };
    if (Value* err = define("locale", ctx->createString(d->locale))) return err;
    if (Value* err = define("usage", ctx->createString(d->usage))) return err;
    if (Value* err = define("sensitivity", ctx->createString(d->sensitivity))) {
        return err;
    }
    if (Value* err = define("ignorePunctuation",
                            ctx->createBoolean(d->ignorePunctuation))) {
        return err;
    }
    if (Value* err = define("numeric", ctx->createBoolean(d->numeric))) return err;
    if (Value* err = define("caseFirst", ctx->createString(d->caseFirst))) {
        return err;
    }
    if (Value* err = define("collation", ctx->createString(d->collation))) {
        return err;
    }
    return result;
}

Value* collatorSupportedLocalesOf(IContext* ctx, Value* /*self*/, size_t argc,
                                  Value* argv[]) {
    return supportedLocalesOfImpl(ctx, argc, argv);
}

// —— Intl.PluralRules ——

struct PluralOptions {
    std::string type = "cardinal";
    std::string notation = "standard";
    int minimumIntegerDigits = 1;
    int minimumFractionDigits = 0;
    int maximumFractionDigits = 3;
    int minimumSignificantDigits = -1;
    int maximumSignificantDigits = -1;
};

Value* parsePluralOptions(IContext* ctx, Value* options, PluralOptions& out) {
    static const char* const kTypes[] = {"cardinal", "ordinal"};
    if (Value* err = optionString(ctx, options, "type", kTypes, 2, "cardinal",
                                  out.type)) {
        return err;
    }
    static const char* const kNotations[] = {"standard", "scientific", "engineering",
                                             "compact"};
    if (Value* err = optionString(ctx, options, "notation", kNotations, 4, "standard",
                                  out.notation)) {
        return err;
    }
    if (Value* err = optionInt(ctx, options, "minimumIntegerDigits", 1, 21, 1,
                               out.minimumIntegerDigits)) {
        return err;
    }
    if (Value* err = optionInt(ctx, options, "minimumFractionDigits", 0, 100, 0,
                               out.minimumFractionDigits)) {
        return err;
    }
    if (Value* err = optionInt(ctx, options, "maximumFractionDigits", 0, 100,
                               std::max(out.minimumFractionDigits, 3),
                               out.maximumFractionDigits)) {
        return err;
    }
    if (out.maximumFractionDigits < out.minimumFractionDigits) {
        return detail::rangeError(
            ctx, "maximumFractionDigits must be >= minimumFractionDigits");
    }
    Value* minSig = nullptr;
    if (Value* err = readOption(ctx, options, "minimumSignificantDigits", minSig)) {
        return err;
    }
    if (minSig->type()->kind() != TypeKind::Undefined) {
        if (Value* err = optionInt(ctx, options, "minimumSignificantDigits", 1, 21,
                                   -1, out.minimumSignificantDigits)) {
            return err;
        }
    }
    Value* maxSig = nullptr;
    if (Value* err = readOption(ctx, options, "maximumSignificantDigits", maxSig)) {
        return err;
    }
    if (maxSig->type()->kind() != TypeKind::Undefined) {
        if (Value* err = optionInt(ctx, options, "maximumSignificantDigits", 1, 21,
                                   -1, out.maximumSignificantDigits)) {
            return err;
        }
    }
    if (out.minimumSignificantDigits >= 0 && out.maximumSignificantDigits >= 0 &&
        out.maximumSignificantDigits < out.minimumSignificantDigits) {
        return detail::rangeError(
            ctx, "maximumSignificantDigits must be >= minimumSignificantDigits");
    }
    return nullptr;
}

void buildPluralBackendOptions(
    const PluralOptions& options,
    std::vector<std::pair<std::string, std::string>>& storage,
    std::vector<NJS_IntlOption>& out) {
    storage.clear();
    storage.emplace_back("type", options.type);
    storage.emplace_back("notation", options.notation);
    storage.emplace_back("minimumIntegerDigits",
                         std::to_string(options.minimumIntegerDigits));
    storage.emplace_back("minimumFractionDigits",
                         std::to_string(options.minimumFractionDigits));
    storage.emplace_back("maximumFractionDigits",
                         std::to_string(options.maximumFractionDigits));
    if (options.minimumSignificantDigits >= 0) {
        storage.emplace_back("minimumSignificantDigits",
                             std::to_string(options.minimumSignificantDigits));
    }
    if (options.maximumSignificantDigits >= 0) {
        storage.emplace_back("maximumSignificantDigits",
                             std::to_string(options.maximumSignificantDigits));
    }
    out.clear();
    out.reserve(storage.size());
    for (const auto& entry : storage) {
        out.push_back(NJS_IntlOption{entry.first.c_str(), entry.second.c_str()});
    }
}

Value* pluralRulesConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                            Value* argv[]);

Value* pluralRulesCall(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return pluralRulesConstruct(ctx, self, argc, argv);
}

Value* pluralRulesConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                            Value* argv[]) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->plural_rules_create == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support PluralRules");
    }

    Value* locales = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* optionsArg = argc > 1 ? argv[1] : ctx->createUndefined();
    Value* options = nullptr;
    if (Value* err = getOptions(ctx, optionsArg, options)) return err;

    std::vector<std::string> requested;
    if (Value* err = canonicalizeLocaleList(ctx, locales, requested)) return err;
    std::string locale;
    if (Value* err = resolveLocale(ctx, requested, locale)) return err;

    PluralOptions parsed;
    if (Value* err = parsePluralOptions(ctx, options, parsed)) return err;

    std::vector<std::pair<std::string, std::string>> storage;
    std::vector<NJS_IntlOption> backendOptions;
    buildPluralBackendOptions(parsed, storage, backendOptions);

    NJS_IntlResolved resolved{};
    NJS_IntlStatus status = NJS_INTL_OK;
    NJS_IntlPluralRules* rules = backend->plural_rules_create(
        backend->user_data, locale.c_str(), backendOptions.data(),
        backendOptions.size(), &resolved, &status);
    if (rules == nullptr) {
        return backendStatusError(ctx, status, "Intl.PluralRules options");
    }

    Value* object = ctx->createIntlPluralRules(backend, rules, resolved.locale);
    if (detail::isErrorValue(object)) return object;

    IntlPluralRulesData* d = asIntlPluralRules(object);
    d->locale = resolved.locale;
    d->type = parsed.type;
    d->notation = parsed.notation;
    d->minimumIntegerDigits = parsed.minimumIntegerDigits;
    d->minimumFractionDigits = parsed.minimumFractionDigits;
    d->maximumFractionDigits = parsed.maximumFractionDigits;
    d->minimumSignificantDigits = parsed.minimumSignificantDigits;
    d->maximumSignificantDigits = parsed.maximumSignificantDigits;
    return object;
}

Value* requirePluralRules(IContext* ctx, Value* self, IntlPluralRulesData*& out) {
    out = asIntlPluralRules(self);
    if (out == nullptr || out->rules == nullptr || out->backend == nullptr) {
        return detail::typeError(
            ctx, "Intl.PluralRules method called on incompatible receiver");
    }
    return nullptr;
}

Value* pluralRulesSelect(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    IntlPluralRulesData* d = nullptr;
    if (Value* err = requirePluralRules(ctx, self, d)) return err;
    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* number = detail::toNumberValue(ctx, value);
    if (detail::isErrorValue(number)) return number;
    if (d->backend->plural_rules_select == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support select");
    }
    const char* category = d->backend->plural_rules_select(
        d->rules, static_cast<NumberData*>(number->data())->value);
    return ctx->createString(category != nullptr ? category : "other");
}

Value* pluralRulesSelectRange(IContext* ctx, Value* self, size_t /*argc*/,
                              Value* /*argv*/[]) {
    IntlPluralRulesData* d = nullptr;
    if (Value* err = requirePluralRules(ctx, self, d)) return err;
    return detail::rangeError(ctx, "Intl.PluralRules.selectRange is not supported");
}

Value* pluralRulesResolvedOptions(IContext* ctx, Value* self, size_t /*argc*/,
                                  Value* /*argv*/[]) {
    IntlPluralRulesData* d = nullptr;
    if (Value* err = requirePluralRules(ctx, self, d)) return err;
    Value* result = ctx->createObject();
    if (detail::isErrorValue(result)) return result;
    auto define = [&](const char* key, Value* value) -> Value* {
        Value* r = ctx->defineProperty(result, key, value, true, true, true);
        return detail::isErrorValue(r) ? r : nullptr;
    };
    if (Value* err = define("locale", ctx->createString(d->locale))) return err;
    if (Value* err = define("type", ctx->createString(d->type))) return err;
    if (Value* err = define("notation", ctx->createString(d->notation))) return err;
    if (Value* err = define("minimumIntegerDigits",
                            ctx->createNumber(d->minimumIntegerDigits))) {
        return err;
    }
    if (Value* err = define("minimumFractionDigits",
                            ctx->createNumber(d->minimumFractionDigits))) {
        return err;
    }
    if (Value* err = define("maximumFractionDigits",
                            ctx->createNumber(d->maximumFractionDigits))) {
        return err;
    }
    if (d->minimumSignificantDigits >= 0) {
        if (Value* err = define("minimumSignificantDigits",
                                ctx->createNumber(d->minimumSignificantDigits))) {
            return err;
        }
    }
    if (d->maximumSignificantDigits >= 0) {
        if (Value* err = define("maximumSignificantDigits",
                                ctx->createNumber(d->maximumSignificantDigits))) {
            return err;
        }
    }

    static const char* const kCardinal[] = {"one", "other"};
    static const char* const kOrdinal[] = {"few", "one", "two", "other"};
    const char* const* categories = d->type == "ordinal" ? kOrdinal : kCardinal;
    const size_t categoryCount = d->type == "ordinal" ? 4 : 2;
    Value* list = ctx->createArray();
    if (detail::isErrorValue(list)) return list;
    auto* arrayData = static_cast<ArrayData*>(list->data());
    for (size_t i = 0; i < categoryCount; ++i) {
        arrayData->setElement(static_cast<uint32_t>(i),
                              ctx->createString(categories[i]));
    }
    if (Value* err = define("pluralCategories", list)) return err;
    return result;
}

Value* pluralRulesSupportedLocalesOf(IContext* ctx, Value* /*self*/, size_t argc,
                                     Value* argv[]) {
    return supportedLocalesOfImpl(ctx, argc, argv);
}

// —— Intl.RelativeTimeFormat ——

struct RelativeTimeOptions {
    std::string style = "long";
    std::string numeric = "always";
    std::string numberingSystem = "latn";
};

Value* parseRelativeTimeOptions(IContext* ctx, Value* options,
                                RelativeTimeOptions& out) {
    static const char* const kStyles[] = {"long", "short", "narrow"};
    if (Value* err = optionString(ctx, options, "style", kStyles, 3, "long",
                                  out.style)) {
        return err;
    }
    static const char* const kNumeric[] = {"always", "auto"};
    if (Value* err = optionString(ctx, options, "numeric", kNumeric, 2, "always",
                                  out.numeric)) {
        return err;
    }
    return optionFreeString(ctx, options, "numberingSystem", "latn",
                            out.numberingSystem);
}

void buildRelativeTimeBackendOptions(
    const RelativeTimeOptions& options,
    std::vector<std::pair<std::string, std::string>>& storage,
    std::vector<NJS_IntlOption>& out) {
    storage.clear();
    storage.emplace_back("style", options.style);
    storage.emplace_back("numeric", options.numeric);
    storage.emplace_back("numberingSystem", options.numberingSystem);
    out.clear();
    out.reserve(storage.size());
    for (const auto& entry : storage) {
        out.push_back(NJS_IntlOption{entry.first.c_str(), entry.second.c_str()});
    }
}

Value* relativeTimeFormatConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                                   Value* argv[]);

Value* relativeTimeFormatCall(IContext* ctx, Value* self, size_t argc,
                              Value* argv[]) {
    return relativeTimeFormatConstruct(ctx, self, argc, argv);
}

Value* relativeTimeFormatConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                                   Value* argv[]) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->relative_time_format_create == nullptr) {
        return detail::typeError(
            ctx, "Intl backend does not support RelativeTimeFormat");
    }

    Value* locales = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* optionsArg = argc > 1 ? argv[1] : ctx->createUndefined();
    Value* options = nullptr;
    if (Value* err = getOptions(ctx, optionsArg, options)) return err;

    std::vector<std::string> requested;
    if (Value* err = canonicalizeLocaleList(ctx, locales, requested)) return err;
    std::string locale;
    if (Value* err = resolveLocale(ctx, requested, locale)) return err;

    RelativeTimeOptions parsed;
    if (Value* err = parseRelativeTimeOptions(ctx, options, parsed)) return err;

    std::vector<std::pair<std::string, std::string>> storage;
    std::vector<NJS_IntlOption> backendOptions;
    buildRelativeTimeBackendOptions(parsed, storage, backendOptions);

    NJS_IntlResolved resolved{};
    NJS_IntlStatus status = NJS_INTL_OK;
    NJS_IntlRelativeTimeFormat* formatter = backend->relative_time_format_create(
        backend->user_data, locale.c_str(), backendOptions.data(),
        backendOptions.size(), &resolved, &status);
    if (formatter == nullptr) {
        return backendStatusError(ctx, status, "Intl.RelativeTimeFormat options");
    }

    Value* object = ctx->createIntlRelativeTimeFormat(backend, formatter,
                                                      resolved.locale);
    if (detail::isErrorValue(object)) return object;

    IntlRelativeTimeFormatData* d = asIntlRelativeTimeFormat(object);
    d->locale = resolved.locale;
    d->style = parsed.style;
    d->numeric = parsed.numeric;
    d->numberingSystem = parsed.numberingSystem;
    return object;
}

Value* requireRelativeTimeFormat(IContext* ctx, Value* self,
                                 IntlRelativeTimeFormatData*& out) {
    out = asIntlRelativeTimeFormat(self);
    if (out == nullptr || out->formatter == nullptr || out->backend == nullptr) {
        return detail::typeError(ctx,
                                 "Intl.RelativeTimeFormat method called on "
                                 "incompatible receiver");
    }
    return nullptr;
}

bool isRelativeTimeUnit(const std::string& unit) {
    static const char* const kUnits[] = {"year",  "quarter", "month", "week",
                                         "day",   "hour",    "minute", "second"};
    for (const char* candidate : kUnits) {
        if (unit == candidate) return true;
    }
    return false;
}

Value* relativeTimeFormatParts(IContext* ctx, IntlRelativeTimeFormatData* d,
                               double value, const std::string& unit,
                               std::vector<NJS_IntlPart>& parts) {
    const NJS_IntlBackend* backend = d->backend;
    if (backend->relative_time_format_format == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support formatting");
    }
    size_t count = 0;
    NJS_IntlStatus status = backend->relative_time_format_format(
        d->formatter, value, unit.c_str(), nullptr, &count);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.RelativeTimeFormat.format");
    }
    parts.resize(count);
    if (count == 0) return nullptr;
    status = backend->relative_time_format_format(d->formatter, value, unit.c_str(),
                                                  parts.data(), &count);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.RelativeTimeFormat.format");
    }
    parts.resize(count);
    return nullptr;
}

// 解析 format(value, unit) 的公共参数。
Value* relativeTimeFormatArgs(IContext* ctx, Value* self, size_t argc,
                              Value* argv[], IntlRelativeTimeFormatData*& d,
                              double& value, std::string& unit) {
    if (Value* err = requireRelativeTimeFormat(ctx, self, d)) return err;
    Value* valueArg = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* number = detail::toNumberValue(ctx, valueArg);
    if (detail::isErrorValue(number)) return number;
    value = static_cast<NumberData*>(number->data())->value;

    Value* unitArg = argc > 1 ? argv[1] : ctx->createUndefined();
    Value* unitStr = detail::toStringValue(ctx, unitArg);
    if (detail::isErrorValue(unitStr)) return unitStr;
    unit = static_cast<StringData*>(unitStr->data())->value;
    if (!isRelativeTimeUnit(unit)) {
        return detail::rangeError(ctx, std::string("Invalid unit: ") + unit);
    }
    return nullptr;
}

Value* relativeTimeFormatFormat(IContext* ctx, Value* self, size_t argc,
                                Value* argv[]) {
    IntlRelativeTimeFormatData* d = nullptr;
    double value = 0;
    std::string unit;
    if (Value* err = relativeTimeFormatArgs(ctx, self, argc, argv, d, value, unit)) {
        return err;
    }
    std::vector<NJS_IntlPart> parts;
    if (Value* err = relativeTimeFormatParts(ctx, d, value, unit, parts)) return err;
    std::string out;
    for (const NJS_IntlPart& part : parts) {
        out.append(part.text, part.length);
    }
    return ctx->createString(out);
}

Value* relativeTimeFormatFormatToParts(IContext* ctx, Value* self, size_t argc,
                                       Value* argv[]) {
    IntlRelativeTimeFormatData* d = nullptr;
    double value = 0;
    std::string unit;
    if (Value* err = relativeTimeFormatArgs(ctx, self, argc, argv, d, value, unit)) {
        return err;
    }
    std::vector<NJS_IntlPart> parts;
    if (Value* err = relativeTimeFormatParts(ctx, d, value, unit, parts)) return err;

    Value* array = ctx->createArray();
    if (detail::isErrorValue(array)) return array;
    auto* arrayData = static_cast<ArrayData*>(array->data());
    for (size_t i = 0; i < parts.size(); ++i) {
        Value* item = ctx->createObject();
        if (detail::isErrorValue(item)) return item;
        Value* type = ctx->createString(partTypeName(parts[i].type));
        Value* text = ctx->createString(std::string(parts[i].text, parts[i].length));
        if (Value* err = ctx->defineProperty(item, "type", type, true, true, true)) {
            if (detail::isErrorValue(err)) return err;
        }
        if (Value* err = ctx->defineProperty(item, "value", text, true, true, true)) {
            if (detail::isErrorValue(err)) return err;
        }
        arrayData->setElement(static_cast<uint32_t>(i), item);
    }
    return array;
}

Value* relativeTimeFormatResolvedOptions(IContext* ctx, Value* self,
                                         size_t /*argc*/, Value* /*argv*/[]) {
    IntlRelativeTimeFormatData* d = nullptr;
    if (Value* err = requireRelativeTimeFormat(ctx, self, d)) return err;
    Value* result = ctx->createObject();
    if (detail::isErrorValue(result)) return result;
    auto define = [&](const char* key, Value* value) -> Value* {
        Value* r = ctx->defineProperty(result, key, value, true, true, true);
        return detail::isErrorValue(r) ? r : nullptr;
    };
    if (Value* err = define("locale", ctx->createString(d->locale))) return err;
    if (Value* err = define("style", ctx->createString(d->style))) return err;
    if (Value* err = define("numeric", ctx->createString(d->numeric))) return err;
    if (Value* err = define("numberingSystem",
                            ctx->createString(d->numberingSystem))) {
        return err;
    }
    return result;
}

Value* relativeTimeFormatSupportedLocalesOf(IContext* ctx, Value* /*self*/,
                                            size_t argc, Value* argv[]) {
    return supportedLocalesOfImpl(ctx, argc, argv);
}

// —— Intl.ListFormat ——

struct ListOptions {
    std::string type = "conjunction";
    std::string style = "long";
};

Value* parseListOptions(IContext* ctx, Value* options, ListOptions& out) {
    static const char* const kTypes[] = {"conjunction", "disjunction", "unit"};
    if (Value* err = optionString(ctx, options, "type", kTypes, 3, "conjunction",
                                  out.type)) {
        return err;
    }
    static const char* const kStyles[] = {"long", "short", "narrow"};
    return optionString(ctx, options, "style", kStyles, 3, "long", out.style);
}

void buildListBackendOptions(const ListOptions& options,
                             std::vector<std::pair<std::string, std::string>>& storage,
                             std::vector<NJS_IntlOption>& out) {
    storage.clear();
    storage.emplace_back("type", options.type);
    storage.emplace_back("style", options.style);
    out.clear();
    out.reserve(storage.size());
    for (const auto& entry : storage) {
        out.push_back(NJS_IntlOption{entry.first.c_str(), entry.second.c_str()});
    }
}

Value* listFormatConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                           Value* argv[]);

Value* listFormatCall(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return listFormatConstruct(ctx, self, argc, argv);
}

Value* listFormatConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                           Value* argv[]) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->list_format_create == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support ListFormat");
    }

    Value* locales = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* optionsArg = argc > 1 ? argv[1] : ctx->createUndefined();
    Value* options = nullptr;
    if (Value* err = getOptions(ctx, optionsArg, options)) return err;

    std::vector<std::string> requested;
    if (Value* err = canonicalizeLocaleList(ctx, locales, requested)) return err;
    std::string locale;
    if (Value* err = resolveLocale(ctx, requested, locale)) return err;

    ListOptions parsed;
    if (Value* err = parseListOptions(ctx, options, parsed)) return err;

    std::vector<std::pair<std::string, std::string>> storage;
    std::vector<NJS_IntlOption> backendOptions;
    buildListBackendOptions(parsed, storage, backendOptions);

    NJS_IntlResolved resolved{};
    NJS_IntlStatus status = NJS_INTL_OK;
    NJS_IntlListFormat* formatter = backend->list_format_create(
        backend->user_data, locale.c_str(), backendOptions.data(),
        backendOptions.size(), &resolved, &status);
    if (formatter == nullptr) {
        return backendStatusError(ctx, status, "Intl.ListFormat options");
    }

    Value* object = ctx->createIntlListFormat(backend, formatter, resolved.locale);
    if (detail::isErrorValue(object)) return object;

    IntlListFormatData* d = asIntlListFormat(object);
    d->locale = resolved.locale;
    d->type = parsed.type;
    d->style = parsed.style;
    return object;
}

Value* requireListFormat(IContext* ctx, Value* self, IntlListFormatData*& out) {
    out = asIntlListFormat(self);
    if (out == nullptr || out->formatter == nullptr || out->backend == nullptr) {
        return detail::typeError(
            ctx, "Intl.ListFormat method called on incompatible receiver");
    }
    return nullptr;
}

// 把 list 参数按 CreateListFromArrayLike 语义转为字符串数组。
Value* listFormatItems(IContext* ctx, Value* list, std::vector<std::string>& out) {
    if (list->type()->kind() == TypeKind::Undefined ||
        list->type()->kind() == TypeKind::Null) {
        return detail::typeError(ctx, "Intl.ListFormat list must not be null");
    }
    uint32_t length = 0;
    if (Value* err = detail::lengthOf(ctx, list, length)) return err;
    out.reserve(length);
    for (uint32_t i = 0; i < length; ++i) {
        Value* item = ctx->getProperty(list, ctx->createString(std::to_string(i)));
        if (detail::isErrorValue(item)) return item;
        Value* str = detail::toStringValue(ctx, item);
        if (detail::isErrorValue(str)) return str;
        out.push_back(static_cast<StringData*>(str->data())->value);
    }
    return nullptr;
}

Value* listFormatParts(IContext* ctx, IntlListFormatData* d,
                       const std::vector<std::string>& items,
                       std::vector<NJS_IntlPart>& parts) {
    const NJS_IntlBackend* backend = d->backend;
    if (backend->list_format_format == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support formatting");
    }
    std::vector<const char*> pointers;
    pointers.reserve(items.size());
    for (const std::string& item : items) pointers.push_back(item.c_str());

    size_t count = 0;
    NJS_IntlStatus status = backend->list_format_format(
        d->formatter, pointers.data(), pointers.size(), nullptr, &count);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.ListFormat.format");
    }
    parts.resize(count);
    if (count == 0) return nullptr;
    status = backend->list_format_format(d->formatter, pointers.data(),
                                         pointers.size(), parts.data(), &count);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.ListFormat.format");
    }
    parts.resize(count);
    return nullptr;
}

Value* listFormatFormat(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    IntlListFormatData* d = nullptr;
    if (Value* err = requireListFormat(ctx, self, d)) return err;
    Value* list = argc > 0 ? argv[0] : ctx->createUndefined();
    std::vector<std::string> items;
    if (Value* err = listFormatItems(ctx, list, items)) return err;
    std::vector<NJS_IntlPart> parts;
    if (Value* err = listFormatParts(ctx, d, items, parts)) return err;
    std::string out;
    for (const NJS_IntlPart& part : parts) {
        out.append(part.text, part.length);
    }
    return ctx->createString(out);
}

Value* listFormatFormatToParts(IContext* ctx, Value* self, size_t argc,
                               Value* argv[]) {
    IntlListFormatData* d = nullptr;
    if (Value* err = requireListFormat(ctx, self, d)) return err;
    Value* list = argc > 0 ? argv[0] : ctx->createUndefined();
    std::vector<std::string> items;
    if (Value* err = listFormatItems(ctx, list, items)) return err;
    std::vector<NJS_IntlPart> parts;
    if (Value* err = listFormatParts(ctx, d, items, parts)) return err;

    Value* array = ctx->createArray();
    if (detail::isErrorValue(array)) return array;
    auto* arrayData = static_cast<ArrayData*>(array->data());
    for (size_t i = 0; i < parts.size(); ++i) {
        Value* item = ctx->createObject();
        if (detail::isErrorValue(item)) return item;
        Value* type = ctx->createString(partTypeName(parts[i].type));
        Value* text = ctx->createString(std::string(parts[i].text, parts[i].length));
        if (Value* err = ctx->defineProperty(item, "type", type, true, true, true)) {
            if (detail::isErrorValue(err)) return err;
        }
        if (Value* err = ctx->defineProperty(item, "value", text, true, true, true)) {
            if (detail::isErrorValue(err)) return err;
        }
        arrayData->setElement(static_cast<uint32_t>(i), item);
    }
    return array;
}

Value* listFormatResolvedOptions(IContext* ctx, Value* self, size_t /*argc*/,
                                 Value* /*argv*/[]) {
    IntlListFormatData* d = nullptr;
    if (Value* err = requireListFormat(ctx, self, d)) return err;
    Value* result = ctx->createObject();
    if (detail::isErrorValue(result)) return result;
    auto define = [&](const char* key, Value* value) -> Value* {
        Value* r = ctx->defineProperty(result, key, value, true, true, true);
        return detail::isErrorValue(r) ? r : nullptr;
    };
    if (Value* err = define("locale", ctx->createString(d->locale))) return err;
    if (Value* err = define("type", ctx->createString(d->type))) return err;
    if (Value* err = define("style", ctx->createString(d->style))) return err;
    return result;
}

Value* listFormatSupportedLocalesOf(IContext* ctx, Value* /*self*/, size_t argc,
                                    Value* argv[]) {
    return supportedLocalesOfImpl(ctx, argc, argv);
}

// —— Intl.DisplayNames ——

struct DisplayNamesOptions {
    std::string type;
    std::string style = "long";
    std::string fallback = "code";
    std::string languageDisplay = "dialect";
};

Value* parseDisplayNamesOptions(IContext* ctx, Value* options,
                                DisplayNamesOptions& out) {
    if (Value* err = optionFreeString(ctx, options, "type", "", out.type)) return err;
    if (out.type.empty()) {
        return detail::rangeError(ctx, "Intl.DisplayNames requires a type option");
    }
    static const char* const kStyles[] = {"long", "short", "narrow"};
    if (Value* err = optionString(ctx, options, "style", kStyles, 3, "long",
                                  out.style)) {
        return err;
    }
    static const char* const kFallbacks[] = {"code", "none"};
    if (Value* err = optionString(ctx, options, "fallback", kFallbacks, 2, "code",
                                  out.fallback)) {
        return err;
    }
    static const char* const kLanguageDisplay[] = {"dialect", "standard"};
    return optionString(ctx, options, "languageDisplay", kLanguageDisplay, 2,
                        "dialect", out.languageDisplay);
}

void buildDisplayNamesBackendOptions(
    const DisplayNamesOptions& options,
    std::vector<std::pair<std::string, std::string>>& storage,
    std::vector<NJS_IntlOption>& out) {
    storage.clear();
    storage.emplace_back("type", options.type);
    storage.emplace_back("style", options.style);
    storage.emplace_back("fallback", options.fallback);
    storage.emplace_back("languageDisplay", options.languageDisplay);
    out.clear();
    out.reserve(storage.size());
    for (const auto& entry : storage) {
        out.push_back(NJS_IntlOption{entry.first.c_str(), entry.second.c_str()});
    }
}

Value* displayNamesConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                             Value* argv[]);

Value* displayNamesCall(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return displayNamesConstruct(ctx, self, argc, argv);
}

Value* displayNamesConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                             Value* argv[]) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->display_names_create == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support DisplayNames");
    }

    Value* locales = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* optionsArg = argc > 1 ? argv[1] : ctx->createUndefined();
    Value* options = nullptr;
    if (Value* err = getOptions(ctx, optionsArg, options)) return err;

    std::vector<std::string> requested;
    if (Value* err = canonicalizeLocaleList(ctx, locales, requested)) return err;
    std::string locale;
    if (Value* err = resolveLocale(ctx, requested, locale)) return err;

    DisplayNamesOptions parsed;
    if (Value* err = parseDisplayNamesOptions(ctx, options, parsed)) return err;

    std::vector<std::pair<std::string, std::string>> storage;
    std::vector<NJS_IntlOption> backendOptions;
    buildDisplayNamesBackendOptions(parsed, storage, backendOptions);

    NJS_IntlResolved resolved{};
    NJS_IntlStatus status = NJS_INTL_OK;
    NJS_IntlDisplayNames* names = backend->display_names_create(
        backend->user_data, locale.c_str(), backendOptions.data(),
        backendOptions.size(), &resolved, &status);
    if (names == nullptr) {
        return backendStatusError(ctx, status, "Intl.DisplayNames options");
    }

    Value* object = ctx->createIntlDisplayNames(backend, names, resolved.locale);
    if (detail::isErrorValue(object)) return object;

    IntlDisplayNamesData* d = asIntlDisplayNames(object);
    d->locale = resolved.locale;
    d->type = parsed.type;
    d->style = parsed.style;
    d->fallback = parsed.fallback;
    d->languageDisplay = parsed.languageDisplay;
    return object;
}

Value* requireDisplayNames(IContext* ctx, Value* self,
                           IntlDisplayNamesData*& out) {
    out = asIntlDisplayNames(self);
    if (out == nullptr || out->names == nullptr || out->backend == nullptr) {
        return detail::typeError(
            ctx, "Intl.DisplayNames method called on incompatible receiver");
    }
    return nullptr;
}

bool isAsciiAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

bool isWellFormedCode(const std::string& type, const std::string& code) {
    if (code.empty()) return false;
    if (type == "language") {
        if (!isAsciiAlpha(code[0])) return false;
        for (char c : code) {
            if (!isAsciiAlpha(c) && !isAsciiDigit(c) && c != '-') return false;
        }
        return true;
    }
    if (type == "region") {
        if (code.size() == 2) {
            return isAsciiAlpha(code[0]) && isAsciiAlpha(code[1]);
        }
        if (code.size() == 3) {
            return isAsciiDigit(code[0]) && isAsciiDigit(code[1]) &&
                   isAsciiDigit(code[2]);
        }
        return false;
    }
    if (type == "script") {
        if (code.size() != 4) return false;
        for (char c : code) {
            if (!isAsciiAlpha(c)) return false;
        }
        return true;
    }
    if (type == "currency") {
        if (code.size() != 3) return false;
        for (char c : code) {
            if (!isAsciiAlpha(c)) return false;
        }
        return true;
    }
    if (type == "calendar") {
        for (char c : code) {
            if (!isAsciiAlpha(c) && !isAsciiDigit(c) && c != '-') return false;
        }
        return true;
    }
    static const char* const kFields[] = {
        "year", "quarter", "month", "week", "weekOfYear", "day", "dayPeriod",
        "hour", "minute", "second", "weekday", "era", "timeZoneName"};
    for (const char* field : kFields) {
        if (code == field) return true;
    }
    return false;
}

Value* displayNamesOf(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    IntlDisplayNamesData* d = nullptr;
    if (Value* err = requireDisplayNames(ctx, self, d)) return err;
    Value* codeArg = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* codeStr = detail::toStringValue(ctx, codeArg);
    if (detail::isErrorValue(codeStr)) return codeStr;
    const std::string code = static_cast<StringData*>(codeStr->data())->value;
    if (!isWellFormedCode(d->type, code)) {
        return detail::rangeError(ctx, std::string("Invalid code: ") + code);
    }
    if (d->backend->display_names_of == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support of");
    }

    size_t length = 0;
    NJS_IntlStatus status =
        d->backend->display_names_of(d->names, code.c_str(), nullptr, &length);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.DisplayNames.of");
    }
    if (length == 0) return ctx->createUndefined();
    std::vector<char> buffer(length);
    status = d->backend->display_names_of(d->names, code.c_str(), buffer.data(),
                                          &length);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.DisplayNames.of");
    }
    return ctx->createString(std::string(buffer.data()));
}

Value* displayNamesResolvedOptions(IContext* ctx, Value* self, size_t /*argc*/,
                                   Value* /*argv*/[]) {
    IntlDisplayNamesData* d = nullptr;
    if (Value* err = requireDisplayNames(ctx, self, d)) return err;
    Value* result = ctx->createObject();
    if (detail::isErrorValue(result)) return result;
    auto define = [&](const char* key, Value* value) -> Value* {
        Value* r = ctx->defineProperty(result, key, value, true, true, true);
        return detail::isErrorValue(r) ? r : nullptr;
    };
    if (Value* err = define("locale", ctx->createString(d->locale))) return err;
    if (Value* err = define("style", ctx->createString(d->style))) return err;
    if (Value* err = define("type", ctx->createString(d->type))) return err;
    if (Value* err = define("fallback", ctx->createString(d->fallback))) return err;
    if (d->type == "language") {
        if (Value* err = define("languageDisplay",
                                ctx->createString(d->languageDisplay))) {
            return err;
        }
    }
    return result;
}

Value* displayNamesSupportedLocalesOf(IContext* ctx, Value* /*self*/, size_t argc,
                                      Value* argv[]) {
    return supportedLocalesOfImpl(ctx, argc, argv);
}

// —— Intl.supportedValuesOf ——

Value* supportedValuesOf(IContext* ctx, Value* /*self*/, size_t argc,
                         Value* argv[]) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->supported_values == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support values");
    }
    Value* keyArg = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* keyStr = detail::toStringValue(ctx, keyArg);
    if (detail::isErrorValue(keyStr)) return keyStr;
    const std::string key = static_cast<StringData*>(keyStr->data())->value;

    const char* const* values = nullptr;
    size_t count = 0;
    NJS_IntlStatus status =
        backend->supported_values(backend->user_data, key.c_str(), &values, &count);
    if (status != NJS_INTL_OK) {
        return detail::rangeError(ctx, std::string("Invalid key: ") + key);
    }

    std::vector<std::string> sorted;
    sorted.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (values[i] != nullptr) sorted.emplace_back(values[i]);
    }
    std::sort(sorted.begin(), sorted.end());

    Value* array = ctx->createArray();
    if (detail::isErrorValue(array)) return array;
    auto* arrayData = static_cast<ArrayData*>(array->data());
    for (size_t i = 0; i < sorted.size(); ++i) {
        arrayData->setElement(static_cast<uint32_t>(i), ctx->createString(sorted[i]));
    }
    return array;
}

// —— Intl.Segmenter ——

std::vector<uint16_t> toUtf16Units(std::string_view text) {
    std::vector<uint16_t> units;
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = 0;
        if (!neo::unicode::decodeCodePoint(text, i, cp)) {
            units.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (cp > 0xFFFF) {
            const uint32_t v = cp - 0x10000;
            units.push_back(static_cast<uint16_t>(0xD800 + (v >> 10)));
            units.push_back(static_cast<uint16_t>(0xDC00 + (v & 0x3FF)));
        } else {
            units.push_back(static_cast<uint16_t>(cp));
        }
    }
    return units;
}

std::string fromUtf16Units(const std::vector<uint16_t>& units, size_t start,
                           size_t length) {
    std::string out;
    size_t i = start;
    const size_t end = start + length;
    while (i < end) {
        uint32_t cp = units[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < end && units[i + 1] >= 0xDC00 &&
            units[i + 1] <= 0xDFFF) {
            cp = neo::unicode::combineSurrogates(cp, units[i + 1]);
            i += 2;
        } else {
            ++i;
        }
        neo::unicode::encodeCodePoint(cp, out);
    }
    return out;
}

struct SegmenterOptions {
    std::string granularity = "grapheme";
};

Value* parseSegmenterOptions(IContext* ctx, Value* options,
                             SegmenterOptions& out) {
    static const char* const kGranularity[] = {"grapheme", "word", "sentence"};
    return optionString(ctx, options, "granularity", kGranularity, 3, "grapheme",
                        out.granularity);
}

void buildSegmenterBackendOptions(
    const SegmenterOptions& options,
    std::vector<std::pair<std::string, std::string>>& storage,
    std::vector<NJS_IntlOption>& out) {
    storage.clear();
    storage.emplace_back("granularity", options.granularity);
    out.clear();
    out.reserve(storage.size());
    for (const auto& entry : storage) {
        out.push_back(NJS_IntlOption{entry.first.c_str(), entry.second.c_str()});
    }
}

Value* segmenterConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                          Value* argv[]);

Value* segmenterCall(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return segmenterConstruct(ctx, self, argc, argv);
}

Value* segmenterConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                          Value* argv[]) {
    const NJS_IntlBackend* backend = backendOf(ctx);
    if (backend == nullptr || backend->segmenter_create == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support Segmenter");
    }

    Value* locales = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* optionsArg = argc > 1 ? argv[1] : ctx->createUndefined();
    Value* options = nullptr;
    if (Value* err = getOptions(ctx, optionsArg, options)) return err;

    std::vector<std::string> requested;
    if (Value* err = canonicalizeLocaleList(ctx, locales, requested)) return err;
    std::string locale;
    if (Value* err = resolveLocale(ctx, requested, locale)) return err;

    SegmenterOptions parsed;
    if (Value* err = parseSegmenterOptions(ctx, options, parsed)) return err;

    std::vector<std::pair<std::string, std::string>> storage;
    std::vector<NJS_IntlOption> backendOptions;
    buildSegmenterBackendOptions(parsed, storage, backendOptions);

    NJS_IntlResolved resolved{};
    NJS_IntlStatus status = NJS_INTL_OK;
    NJS_IntlSegmenter* segmenter = backend->segmenter_create(
        backend->user_data, locale.c_str(), backendOptions.data(),
        backendOptions.size(), &resolved, &status);
    if (segmenter == nullptr) {
        return backendStatusError(ctx, status, "Intl.Segmenter options");
    }

    Value* object = ctx->createIntlSegmenter(backend, segmenter, resolved.locale);
    if (detail::isErrorValue(object)) return object;

    IntlSegmenterData* d = asIntlSegmenter(object);
    d->locale = resolved.locale;
    d->granularity = parsed.granularity;
    return object;
}

Value* requireSegmenter(IContext* ctx, Value* self, IntlSegmenterData*& out) {
    out = asIntlSegmenter(self);
    if (out == nullptr || out->segmenter == nullptr || out->backend == nullptr) {
        return detail::typeError(
            ctx, "Intl.Segmenter method called on incompatible receiver");
    }
    return nullptr;
}

Value* segmenterSegment(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    IntlSegmenterData* d = nullptr;
    if (Value* err = requireSegmenter(ctx, self, d)) return err;
    Value* inputArg = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* inputStr = detail::toStringValue(ctx, inputArg);
    if (detail::isErrorValue(inputStr)) return inputStr;
    const std::string input = static_cast<StringData*>(inputStr->data())->value;

    const NJS_IntlBackend* backend = d->backend;
    if (backend->segmenter_segment == nullptr) {
        return detail::typeError(ctx, "Intl backend does not support segment");
    }
    size_t count = 0;
    NJS_IntlStatus status = backend->segmenter_segment(
        d->segmenter, input.data(), input.size(), nullptr, &count);
    if (status != NJS_INTL_OK) {
        return backendStatusError(ctx, status, "Intl.Segmenter.segment");
    }
    std::vector<NJS_IntlSegment> segments(count);
    if (count > 0) {
        status = backend->segmenter_segment(d->segmenter, input.data(),
                                            input.size(), segments.data(), &count);
        if (status != NJS_INTL_OK) {
            return backendStatusError(ctx, status, "Intl.Segmenter.segment");
        }
        segments.resize(count);
    }

    const std::vector<uint16_t> units = toUtf16Units(input);
    Value* records = ctx->createArray();
    if (detail::isErrorValue(records)) return records;
    auto* recordsData = static_cast<ArrayData*>(records->data());
    std::vector<size_t> starts;
    std::vector<size_t> lengths;
    starts.reserve(segments.size());
    lengths.reserve(segments.size());
    for (size_t i = 0; i < segments.size(); ++i) {
        const NJS_IntlSegment& segment = segments[i];
        starts.push_back(segment.index);
        lengths.push_back(segment.length);

        Value* record = ctx->createObject();
        if (detail::isErrorValue(record)) return record;
        auto define = [&](const char* key, Value* value) -> Value* {
            Value* r = ctx->defineProperty(record, key, value, true, true, true);
            return detail::isErrorValue(r) ? r : nullptr;
        };
        if (Value* err = define(
                "segment",
                ctx->createString(fromUtf16Units(units, segment.index,
                                                 segment.length)))) {
            return err;
        }
        if (Value* err = define("index",
                                ctx->createNumber(static_cast<double>(segment.index)))) {
            return err;
        }
        if (Value* err = define("input", ctx->createString(input))) return err;
        if (d->granularity == "word") {
            if (Value* err = define("isWordLike",
                                    ctx->createBoolean(segment.is_word_like != 0))) {
                return err;
            }
        }
        recordsData->setElement(static_cast<uint32_t>(i), record);
    }

    return ctx->createIntlSegments(input, std::move(starts), std::move(lengths),
                                   records);
}

Value* segmenterResolvedOptions(IContext* ctx, Value* self, size_t /*argc*/,
                                Value* /*argv*/[]) {
    IntlSegmenterData* d = nullptr;
    if (Value* err = requireSegmenter(ctx, self, d)) return err;
    Value* result = ctx->createObject();
    if (detail::isErrorValue(result)) return result;
    auto define = [&](const char* key, Value* value) -> Value* {
        Value* r = ctx->defineProperty(result, key, value, true, true, true);
        return detail::isErrorValue(r) ? r : nullptr;
    };
    if (Value* err = define("locale", ctx->createString(d->locale))) return err;
    if (Value* err = define("granularity",
                            ctx->createString(d->granularity))) {
        return err;
    }
    return result;
}

Value* segmenterSupportedLocalesOf(IContext* ctx, Value* /*self*/, size_t argc,
                                   Value* argv[]) {
    return supportedLocalesOfImpl(ctx, argc, argv);
}

// —— Segments 对象方法 ——

Value* requireSegments(IContext* ctx, Value* self, IntlSegmentsData*& out) {
    out = asIntlSegments(self);
    if (out == nullptr) {
        return detail::typeError(
            ctx, "Intl.Segmenter Segments method called on incompatible receiver");
    }
    return nullptr;
}

Value* segmentsContaining(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    IntlSegmentsData* d = nullptr;
    if (Value* err = requireSegments(ctx, self, d)) return err;
    Value* indexArg = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* number = detail::toNumberValue(ctx, indexArg);
    if (detail::isErrorValue(number)) return number;
    const double raw = static_cast<NumberData*>(number->data())->value;
    if (!std::isfinite(raw)) return ctx->createUndefined();
    const double truncated = std::trunc(raw);
    if (truncated < 0) return ctx->createUndefined();
    const size_t index = static_cast<size_t>(truncated);
    const size_t inputLength = toUtf16Units(d->input).size();
    if (index >= inputLength) return ctx->createUndefined();

    for (size_t i = 0; i < d->starts.size(); ++i) {
        if (index >= d->starts[i] && index < d->starts[i] + d->lengths[i]) {
            return ctx->getProperty(
                d->records, ctx->createString(std::to_string(i)));
        }
    }
    return ctx->createUndefined();
}

Value* segmentsIterator(IContext* ctx, Value* self, size_t /*argc*/,
                        Value* /*argv*/[]) {
    IntlSegmentsData* d = nullptr;
    if (Value* err = requireSegments(ctx, self, d)) return err;
    return ctx->createArrayIterator(d->records, ArrayIterationKind::Value);
}

// —— 注册辅助 ——

// —— Intl.Locale（ECMA-402 §14）——
//
// 只做纯计算：语言标签按 BCP 47 / ECMA-402 的 grammar 解析与规范化，
// unicode 扩展关键字承载 calendar/collation/hourCycle/caseFirst/numberingSystem/
// numeric 内部槽；[[TimeZone]] 单独保存（不在标签中）。
// maximize/minimize 使用内置 likely-subtags 子集；表中没有的语言保持原样。

bool localeIsAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool localeIsDigit(char c) { return c >= '0' && c <= '9'; }
bool localeIsAlnum(char c) { return localeIsAlpha(c) || localeIsDigit(c); }

std::string localeLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string localeUpper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}

std::string localeTitle(std::string s) {
    s = localeLower(std::move(s));
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') {
        s[0] = static_cast<char>(s[0] - 'a' + 'A');
    }
    return s;
}

bool localeAll(const std::string& s, bool (*pred)(char)) {
    for (char c : s) {
        if (!pred(c)) return false;
    }
    return !s.empty();
}

bool validLanguageSubtag(const std::string& s) {
    if (!localeAll(s, localeIsAlpha)) return false;
    return s.size() == 2 || s.size() == 3 || s.size() == 4 || s.size() == 5 ||
           s.size() == 6 || s.size() == 7 || s.size() == 8;
}

bool validScriptSubtag(const std::string& s) {
    return s.size() == 4 && localeAll(s, localeIsAlpha);
}

bool validRegionSubtag(const std::string& s) {
    if (s.size() == 2) return localeAll(s, localeIsAlpha);
    if (s.size() == 3) return localeAll(s, localeIsDigit);
    return false;
}

bool validVariantSubtag(const std::string& s) {
    if (s.size() >= 5 && s.size() <= 8) return localeAll(s, localeIsAlnum);
    if (s.size() == 4) {
        return localeIsDigit(s[0]) && localeIsAlnum(s[1]) && localeIsAlnum(s[2]) &&
               localeIsAlnum(s[3]);
    }
    return false;
}

struct LocaleRecord {
    std::string language;
    std::string script;
    std::string region;
    std::vector<std::string> variants;
    std::vector<std::string> unicodeAttributes;
    // unicode 扩展关键字（key 小写，type 小写；type 为空表示无类型）。
    std::vector<std::pair<std::string, std::string>> keywords;
    // 非 u/x 的扩展，按完整子标签串保存（如 "t-en-us"）。
    std::vector<std::string> extensions;
    std::vector<std::string> privateUse;
};

std::vector<std::string> splitLocaleTag(const std::string& tag) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= tag.size()) {
        size_t dash = tag.find('-', start);
        if (dash == std::string::npos) {
            parts.push_back(tag.substr(start));
            break;
        }
        parts.push_back(tag.substr(start, dash - start));
        start = dash + 1;
    }
    return parts;
}

bool parseLocaleTag(const std::string& tag, LocaleRecord& out) {
    out = LocaleRecord{};
    if (tag.empty()) return false;
    const std::vector<std::string> parts = splitLocaleTag(tag);
    for (const std::string& p : parts) {
        if (p.empty()) return false;
    }
    size_t i = 0;
    if (!validLanguageSubtag(parts[i])) return false;
    out.language = localeLower(parts[i++]);
    if (i < parts.size() && validScriptSubtag(parts[i])) {
        out.script = localeTitle(parts[i++]);
    }
    if (i < parts.size() && validRegionSubtag(parts[i])) {
        out.region = localeUpper(parts[i++]);
    }
    while (i < parts.size() && validVariantSubtag(parts[i])) {
        const std::string v = localeLower(parts[i]);
        if (std::find(out.variants.begin(), out.variants.end(), v) !=
            out.variants.end()) {
            return false;  // 重复 variant
        }
        out.variants.push_back(v);
        ++i;
    }
    while (i < parts.size()) {
        const std::string& singletonTag = parts[i];
        if (singletonTag.size() != 1 || !localeIsAlnum(singletonTag[0])) {
            return false;
        }
        const char singleton = localeLower(singletonTag)[0];
        ++i;
        if (singleton == 'x') {
            if (i >= parts.size()) return false;
            while (i < parts.size()) {
                const std::string& p = parts[i];
                if (p.size() < 1 || p.size() > 8 || !localeAll(p, localeIsAlnum)) {
                    return false;
                }
                out.privateUse.push_back(localeLower(p));
                ++i;
            }
            break;
        }
        if (singleton == 'u') {
            const size_t extStart = i;
            // 属性：3*8alphanum，出现在关键字之前。
            while (i < parts.size() && parts[i].size() >= 3 &&
                   parts[i].size() <= 8 && localeAll(parts[i], localeIsAlnum)) {
                out.unicodeAttributes.push_back(localeLower(parts[i++]));
            }
            // 关键字：2alphanum [ "-" 3*8alphanum ]。
            while (i < parts.size() && parts[i].size() == 2 &&
                   localeAll(parts[i], localeIsAlnum)) {
                const std::string key = localeLower(parts[i++]);
                std::string type;
                if (i < parts.size() && parts[i].size() >= 3 &&
                    parts[i].size() <= 8 && localeAll(parts[i], localeIsAlnum)) {
                    type = localeLower(parts[i++]);
                }
                for (const auto& kv : out.keywords) {
                    if (kv.first == key) return false;  // 重复关键字
                }
                out.keywords.emplace_back(key, type);
            }
            if (i == extStart) return false;  // "u" 后必须有内容
            continue;
        }
        // 其它扩展：singleton 后至少一个 2*8alphanum 子标签。
        std::string ext(1, singleton);
        const size_t extStart = i;
        while (i < parts.size() && parts[i].size() >= 2 && parts[i].size() <= 8 &&
               localeAll(parts[i], localeIsAlnum)) {
            ext += '-';
            ext += localeLower(parts[i++]);
        }
        if (i == extStart) return false;
        out.extensions.push_back(std::move(ext));
    }
    return true;
}

std::string localeBaseNameOf(const LocaleRecord& r) {
    std::string s = r.language;
    if (!r.script.empty()) s += '-' + r.script;
    if (!r.region.empty()) s += '-' + r.region;
    for (const std::string& v : r.variants) s += '-' + v;
    return s;
}

std::string buildLocaleTag(const LocaleRecord& r) {
    std::string s = localeBaseNameOf(r);
    if (!r.unicodeAttributes.empty() || !r.keywords.empty()) {
        s += "-u";
        for (const std::string& a : r.unicodeAttributes) s += '-' + a;
        for (const auto& kv : r.keywords) {
            s += '-' + kv.first;
            if (!kv.second.empty()) s += '-' + kv.second;
        }
    }
    for (const std::string& e : r.extensions) s += '-' + e;
    if (!r.privateUse.empty()) {
        s += "-x";
        for (const std::string& p : r.privateUse) s += '-' + p;
    }
    return s;
}

const std::string* localeKeyword(const LocaleRecord& r, const char* key) {
    for (const auto& kv : r.keywords) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

void setLocaleKeyword(LocaleRecord& r, const std::string& key,
                      const std::string& type) {
    for (auto it = r.keywords.begin(); it != r.keywords.end(); ++it) {
        if (it->first != key) continue;
        if (type.empty()) {
            r.keywords.erase(it);
        } else {
            it->second = type;
        }
        return;
    }
    if (!type.empty()) r.keywords.emplace_back(key, type);
}

struct LikelySubtag {
    const char* language;
    const char* script;
    const char* region;
};

const LikelySubtag kLikelySubtags[] = {
    {"en", "Latn", "US"}, {"zh", "Hans", "CN"}, {"ja", "Jpan", "JP"},
    {"ko", "Kore", "KR"}, {"fr", "Latn", "FR"}, {"de", "Latn", "DE"},
    {"es", "Latn", "ES"}, {"ru", "Cyrl", "RU"}, {"ar", "Arab", "EG"},
    {"hi", "Deva", "IN"}, {"pt", "Latn", "BR"}, {"it", "Latn", "IT"},
    {"nl", "Latn", "NL"}, {"sv", "Latn", "SE"}, {"pl", "Latn", "PL"},
    {"tr", "Latn", "TR"}, {"th", "Thai", "TH"}, {"he", "Hebr", "IL"},
    {"vi", "Latn", "VN"}, {"id", "Latn", "ID"},
};

// Maximize：补齐缺失的 script/region。表中没有的语言保持原样。
void maximizeLocale(LocaleRecord& r) {
    for (const LikelySubtag& e : kLikelySubtags) {
        if (r.language != e.language) continue;
        if (r.script.empty()) r.script = e.script;
        if (r.region.empty()) r.region = e.region;
        return;
    }
}

Value* requireLocale(IContext* ctx, Value* self, IntlLocaleData*& out) {
    out = asIntlLocale(self);
    if (out == nullptr) {
        return detail::typeError(
            ctx, "Intl.Locale method called on incompatible receiver");
    }
    return nullptr;
}

// 把某个字符串选项读成 LocaleRecord 的字段；未提供则保持原值。
Value* applyLocaleTagOption(IContext* ctx, Value* options, const char* name,
                            bool (*validator)(const std::string&),
                            std::string& field, bool titleCase) {
    Value* value = nullptr;
    if (Value* err = readOption(ctx, options, name, value)) return err;
    if (value->type()->kind() == TypeKind::Undefined) return nullptr;
    Value* str = detail::toStringValue(ctx, value);
    if (detail::isErrorValue(str)) return str;
    const std::string text = static_cast<StringData*>(str->data())->value;
    if (!validator(text)) {
        return detail::rangeError(ctx, std::string("Invalid value for option ") + name);
    }
    field = titleCase ? localeTitle(text) : localeLower(text);
    return nullptr;
}

// 把某个 unicode 关键字选项写入 LocaleRecord；未提供则保持原值。
Value* applyLocaleKeywordOption(IContext* ctx, Value* options, const char* name,
                                const char* key, LocaleRecord& rec) {
    Value* value = nullptr;
    if (Value* err = readOption(ctx, options, name, value)) return err;
    if (value->type()->kind() == TypeKind::Undefined) return nullptr;
    Value* str = detail::toStringValue(ctx, value);
    if (detail::isErrorValue(str)) return str;
    const std::string text = static_cast<StringData*>(str->data())->value;
    if (!text.empty() &&
        !(text.size() >= 3 && text.size() <= 8 && localeAll(text, localeIsAlnum))) {
        return detail::rangeError(ctx, std::string("Invalid value for option ") + name);
    }
    setLocaleKeyword(rec, key, localeLower(text));
    return nullptr;
}

Value* localeCall(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                  Value* /*argv*/[]) {
    return detail::typeError(ctx, "Constructor Intl.Locale requires 'new'");
}

Value* localeConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                       Value* argv[]) {
    Value* tagArg = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* tagValue = detail::toStringValue(ctx, tagArg);
    if (detail::isErrorValue(tagValue)) return tagValue;
    const std::string tag = static_cast<StringData*>(tagValue->data())->value;

    LocaleRecord rec;
    if (!parseLocaleTag(tag, rec)) {
        return detail::rangeError(ctx, "Incorrect locale information provided");
    }

    Value* options = nullptr;
    Value* optionsArg = argc > 1 ? argv[1] : ctx->createUndefined();
    if (Value* err = getOptions(ctx, optionsArg, options)) return err;

    if (options != nullptr) {
        if (Value* err = applyLocaleTagOption(ctx, options, "language",
                                              validLanguageSubtag, rec.language,
                                              false)) {
            return err;
        }
        if (Value* err = applyLocaleTagOption(ctx, options, "script",
                                              validScriptSubtag, rec.script, true)) {
            return err;
        }
        if (Value* err = applyLocaleTagOption(ctx, options, "region",
                                              validRegionSubtag, rec.region, false)) {
            return err;
        }
        if (Value* err = applyLocaleKeywordOption(ctx, options, "calendar", "ca", rec)) {
            return err;
        }
        if (Value* err =
                applyLocaleKeywordOption(ctx, options, "collation", "co", rec)) {
            return err;
        }
        if (Value* err =
                applyLocaleKeywordOption(ctx, options, "hourCycle", "hc", rec)) {
            return err;
        }
        if (Value* err =
                applyLocaleKeywordOption(ctx, options, "caseFirst", "kf", rec)) {
            return err;
        }
        if (Value* err = applyLocaleKeywordOption(ctx, options, "numberingSystem",
                                                  "nu", rec)) {
            return err;
        }
        Value* numeric = nullptr;
        if (Value* err = readOption(ctx, options, "numeric", numeric)) return err;
        if (numeric->type()->kind() != TypeKind::Undefined) {
            setLocaleKeyword(rec, "kn",
                             detail::toBoolean(numeric) ? "true" : "false");
        }
    }

    std::string timeZone;
    if (options != nullptr) {
        Value* tzValue = nullptr;
        if (Value* err = readOption(ctx, options, "timeZone", tzValue)) return err;
        if (tzValue->type()->kind() != TypeKind::Undefined) {
            Value* tzStr = detail::toStringValue(ctx, tzValue);
            if (detail::isErrorValue(tzStr)) return tzStr;
            timeZone = static_cast<StringData*>(tzStr->data())->value;
            if (timeZone.empty()) {
                return detail::rangeError(ctx, "Invalid value for option timeZone");
            }
        }
    }

    std::sort(rec.keywords.begin(), rec.keywords.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return ctx->createIntlLocale(buildLocaleTag(rec), timeZone);
}

Value* localeBaseName(IContext* ctx, Value* self, size_t /*argc*/,
                      Value* /*argv*/[]) {
    IntlLocaleData* d = nullptr;
    if (Value* err = requireLocale(ctx, self, d)) return err;
    LocaleRecord rec;
    if (!parseLocaleTag(d->localeName, rec)) return ctx->createUndefined();
    return ctx->createString(localeBaseNameOf(rec));
}

Value* localeLanguage(IContext* ctx, Value* self, size_t, Value*[]) {
    IntlLocaleData* d = nullptr;
    if (Value* err = requireLocale(ctx, self, d)) return err;
    LocaleRecord rec;
    if (!parseLocaleTag(d->localeName, rec)) return ctx->createUndefined();
    return ctx->createString(rec.language);
}

Value* localeScript(IContext* ctx, Value* self, size_t, Value*[]) {
    IntlLocaleData* d = nullptr;
    if (Value* err = requireLocale(ctx, self, d)) return err;
    LocaleRecord rec;
    if (!parseLocaleTag(d->localeName, rec)) return ctx->createUndefined();
    return rec.script.empty() ? ctx->createUndefined() : ctx->createString(rec.script);
}

Value* localeRegion(IContext* ctx, Value* self, size_t, Value*[]) {
    IntlLocaleData* d = nullptr;
    if (Value* err = requireLocale(ctx, self, d)) return err;
    LocaleRecord rec;
    if (!parseLocaleTag(d->localeName, rec)) return ctx->createUndefined();
    return rec.region.empty() ? ctx->createUndefined() : ctx->createString(rec.region);
}

Value* localeKeywordGetter(IContext* ctx, Value* self, const char* key) {
    IntlLocaleData* d = nullptr;
    if (Value* err = requireLocale(ctx, self, d)) return err;
    LocaleRecord rec;
    if (!parseLocaleTag(d->localeName, rec)) return ctx->createUndefined();
    const std::string* type = localeKeyword(rec, key);
    if (type == nullptr || type->empty()) return ctx->createUndefined();
    return ctx->createString(*type);
}

Value* localeCalendar(IContext* ctx, Value* self, size_t, Value*[]) {
    return localeKeywordGetter(ctx, self, "ca");
}
Value* localeCollation(IContext* ctx, Value* self, size_t, Value*[]) {
    return localeKeywordGetter(ctx, self, "co");
}
Value* localeHourCycle(IContext* ctx, Value* self, size_t, Value*[]) {
    return localeKeywordGetter(ctx, self, "hc");
}
Value* localeCaseFirst(IContext* ctx, Value* self, size_t, Value*[]) {
    return localeKeywordGetter(ctx, self, "kf");
}
Value* localeNumberingSystem(IContext* ctx, Value* self, size_t, Value*[]) {
    return localeKeywordGetter(ctx, self, "nu");
}

Value* localeNumeric(IContext* ctx, Value* self, size_t, Value*[]) {
    IntlLocaleData* d = nullptr;
    if (Value* err = requireLocale(ctx, self, d)) return err;
    LocaleRecord rec;
    if (!parseLocaleTag(d->localeName, rec)) return ctx->createUndefined();
    const std::string* type = localeKeyword(rec, "kn");
    if (type == nullptr || type->empty()) return ctx->createUndefined();
    return ctx->createBoolean(*type == "true");
}

Value* localeTimeZone(IContext* ctx, Value* self, size_t, Value*[]) {
    IntlLocaleData* d = nullptr;
    if (Value* err = requireLocale(ctx, self, d)) return err;
    return d->timeZone.empty() ? ctx->createUndefined()
                               : ctx->createString(d->timeZone);
}

Value* localeToString(IContext* ctx, Value* self, size_t, Value*[]) {
    IntlLocaleData* d = nullptr;
    if (Value* err = requireLocale(ctx, self, d)) return err;
    return ctx->createString(d->localeName);
}

Value* localeMaximize(IContext* ctx, Value* self, size_t, Value*[]) {
    IntlLocaleData* d = nullptr;
    if (Value* err = requireLocale(ctx, self, d)) return err;
    LocaleRecord rec;
    if (!parseLocaleTag(d->localeName, rec)) {
        return detail::rangeError(ctx, "Incorrect locale information provided");
    }
    maximizeLocale(rec);
    return ctx->createIntlLocale(buildLocaleTag(rec), d->timeZone);
}

Value* localeMinimize(IContext* ctx, Value* self, size_t, Value*[]) {
    IntlLocaleData* d = nullptr;
    if (Value* err = requireLocale(ctx, self, d)) return err;
    LocaleRecord rec;
    if (!parseLocaleTag(d->localeName, rec)) {
        return detail::rangeError(ctx, "Incorrect locale information provided");
    }
    LocaleRecord maxRec = rec;
    maximizeLocale(maxRec);
    const std::string maxTag = buildLocaleTag(maxRec);

    if (!rec.script.empty()) {
        LocaleRecord trial = rec;
        trial.script.clear();
        LocaleRecord trialMax = trial;
        maximizeLocale(trialMax);
        if (buildLocaleTag(trialMax) == maxTag) rec = trial;
    }
    if (!rec.region.empty()) {
        LocaleRecord trial = rec;
        trial.region.clear();
        LocaleRecord trialMax = trial;
        maximizeLocale(trialMax);
        if (buildLocaleTag(trialMax) == maxTag) rec = trial;
    }
    return ctx->createIntlLocale(buildLocaleTag(rec), d->timeZone);
}

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

bool defineGetter(IContext* ctx, Value* target, const char* name, NativeFn getter) {
    Value* f = ctx->createNativeFunction(getter, 0);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, std::string("get ") + name);
    return ctx->defineAccessor(target, name, f, nullptr, false, true)
               ->type()->kind() != TypeKind::Exception;
}

bool defineTag(IContext* ctx, Value* target, const char* tag) {
    return ctx->defineProperty(target,
                               ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                               ctx->createString(tag), false, false, true)
               ->type()->kind() != TypeKind::Exception;
}

}  // namespace

bool registerIntlBuiltins(IContext* ctx) {
    Value* intl = ctx->createObject();
    Value* proto = ctx->createObject();
    if (intl == ctx->oomValue() || proto == ctx->oomValue()) return false;
    if (ctx->setPrototype(intl, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    if (ctx->setPrototype(proto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setIntlNumberFormatPrototype(proto);

    // Intl.NumberFormat.prototype
    if (!defineGetter(ctx, proto, "format", numberFormatFormatGetter)) return false;
    if (!defineMethod(ctx, proto, "formatToParts", numberFormatFormatToParts, 0)) {
        return false;
    }
    if (!defineMethod(ctx, proto, "resolvedOptions", numberFormatResolvedOptions, 0)) {
        return false;
    }
    if (!defineTag(ctx, proto, "Intl.NumberFormat")) return false;

    // Intl.NumberFormat 构造函数
    Value* ctor = ctx->createNativeFunction(numberFormatCall, 0);
    if (ctor == ctx->oomValue()) return false;
    ctx->setFunctionName(ctor, "NumberFormat");
    ctx->setFunctionConstructor(ctor, numberFormatConstruct);
    if (ctx->defineProperty(ctor, "prototype", proto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(proto, "constructor", ctor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, ctor, "supportedLocalesOf", numberFormatSupportedLocalesOf,
                      0)) {
        return false;
    }

    // —— Intl.DateTimeFormat ——
    Value* dateProto = ctx->createObject();
    if (dateProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(dateProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setIntlDateTimeFormatPrototype(dateProto);
    if (!defineGetter(ctx, dateProto, "format", dateTimeFormatFormatGetter)) {
        return false;
    }
    if (!defineMethod(ctx, dateProto, "formatToParts", dateTimeFormatFormatToParts, 0)) {
        return false;
    }
    if (!defineMethod(ctx, dateProto, "resolvedOptions",
                      dateTimeFormatResolvedOptions, 0)) {
        return false;
    }
    if (!defineTag(ctx, dateProto, "Intl.DateTimeFormat")) return false;

    Value* dateCtor = ctx->createNativeFunction(dateTimeFormatCall, 0);
    if (dateCtor == ctx->oomValue()) return false;
    ctx->setFunctionName(dateCtor, "DateTimeFormat");
    ctx->setFunctionConstructor(dateCtor, dateTimeFormatConstruct);
    if (ctx->defineProperty(dateCtor, "prototype", dateProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(dateProto, "constructor", dateCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, dateCtor, "supportedLocalesOf",
                      dateTimeFormatSupportedLocalesOf, 0)) {
        return false;
    }

    // —— Intl.Collator ——
    Value* collatorProto = ctx->createObject();
    if (collatorProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(collatorProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setIntlCollatorPrototype(collatorProto);
    if (!defineGetter(ctx, collatorProto, "compare", collatorCompareGetter)) {
        return false;
    }
    if (!defineMethod(ctx, collatorProto, "resolvedOptions", collatorResolvedOptions,
                      0)) {
        return false;
    }
    if (!defineTag(ctx, collatorProto, "Intl.Collator")) return false;

    Value* collatorCtor = ctx->createNativeFunction(collatorCall, 0);
    if (collatorCtor == ctx->oomValue()) return false;
    ctx->setFunctionName(collatorCtor, "Collator");
    ctx->setFunctionConstructor(collatorCtor, collatorConstruct);
    if (ctx->defineProperty(collatorCtor, "prototype", collatorProto, false, false,
                            false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(collatorProto, "constructor", collatorCtor, true, false,
                            true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, collatorCtor, "supportedLocalesOf",
                      collatorSupportedLocalesOf, 0)) {
        return false;
    }

    // —— Intl.PluralRules ——
    Value* pluralProto = ctx->createObject();
    if (pluralProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(pluralProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setIntlPluralRulesPrototype(pluralProto);
    if (!defineMethod(ctx, pluralProto, "select", pluralRulesSelect, 0)) return false;
    if (!defineMethod(ctx, pluralProto, "selectRange", pluralRulesSelectRange, 0)) {
        return false;
    }
    if (!defineMethod(ctx, pluralProto, "resolvedOptions", pluralRulesResolvedOptions,
                      0)) {
        return false;
    }
    if (!defineTag(ctx, pluralProto, "Intl.PluralRules")) return false;

    Value* pluralCtor = ctx->createNativeFunction(pluralRulesCall, 0);
    if (pluralCtor == ctx->oomValue()) return false;
    ctx->setFunctionName(pluralCtor, "PluralRules");
    ctx->setFunctionConstructor(pluralCtor, pluralRulesConstruct);
    if (ctx->defineProperty(pluralCtor, "prototype", pluralProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(pluralProto, "constructor", pluralCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, pluralCtor, "supportedLocalesOf",
                      pluralRulesSupportedLocalesOf, 0)) {
        return false;
    }

    // —— Intl.RelativeTimeFormat ——
    Value* relativeProto = ctx->createObject();
    if (relativeProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(relativeProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setIntlRelativeTimeFormatPrototype(relativeProto);
    if (!defineMethod(ctx, relativeProto, "format", relativeTimeFormatFormat, 0)) {
        return false;
    }
    if (!defineMethod(ctx, relativeProto, "formatToParts",
                      relativeTimeFormatFormatToParts, 0)) {
        return false;
    }
    if (!defineMethod(ctx, relativeProto, "resolvedOptions",
                      relativeTimeFormatResolvedOptions, 0)) {
        return false;
    }
    if (!defineTag(ctx, relativeProto, "Intl.RelativeTimeFormat")) return false;

    Value* relativeCtor = ctx->createNativeFunction(relativeTimeFormatCall, 0);
    if (relativeCtor == ctx->oomValue()) return false;
    ctx->setFunctionName(relativeCtor, "RelativeTimeFormat");
    ctx->setFunctionConstructor(relativeCtor, relativeTimeFormatConstruct);
    if (ctx->defineProperty(relativeCtor, "prototype", relativeProto, false, false,
                            false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(relativeProto, "constructor", relativeCtor, true, false,
                            true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, relativeCtor, "supportedLocalesOf",
                      relativeTimeFormatSupportedLocalesOf, 0)) {
        return false;
    }

    // —— Intl.ListFormat ——
    Value* listProto = ctx->createObject();
    if (listProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(listProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setIntlListFormatPrototype(listProto);
    if (!defineMethod(ctx, listProto, "format", listFormatFormat, 0)) return false;
    if (!defineMethod(ctx, listProto, "formatToParts", listFormatFormatToParts, 0)) {
        return false;
    }
    if (!defineMethod(ctx, listProto, "resolvedOptions", listFormatResolvedOptions,
                      0)) {
        return false;
    }
    if (!defineTag(ctx, listProto, "Intl.ListFormat")) return false;

    Value* listCtor = ctx->createNativeFunction(listFormatCall, 0);
    if (listCtor == ctx->oomValue()) return false;
    ctx->setFunctionName(listCtor, "ListFormat");
    ctx->setFunctionConstructor(listCtor, listFormatConstruct);
    if (ctx->defineProperty(listCtor, "prototype", listProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(listProto, "constructor", listCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, listCtor, "supportedLocalesOf",
                      listFormatSupportedLocalesOf, 0)) {
        return false;
    }

    // —— Intl.DisplayNames ——
    Value* displayProto = ctx->createObject();
    if (displayProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(displayProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setIntlDisplayNamesPrototype(displayProto);
    if (!defineMethod(ctx, displayProto, "of", displayNamesOf, 0)) return false;
    if (!defineMethod(ctx, displayProto, "resolvedOptions",
                      displayNamesResolvedOptions, 0)) {
        return false;
    }
    if (!defineTag(ctx, displayProto, "Intl.DisplayNames")) return false;

    Value* displayCtor = ctx->createNativeFunction(displayNamesCall, 0);
    if (displayCtor == ctx->oomValue()) return false;
    ctx->setFunctionName(displayCtor, "DisplayNames");
    ctx->setFunctionConstructor(displayCtor, displayNamesConstruct);
    if (ctx->defineProperty(displayCtor, "prototype", displayProto, false, false,
                            false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(displayProto, "constructor", displayCtor, true, false,
                            true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, displayCtor, "supportedLocalesOf",
                      displayNamesSupportedLocalesOf, 0)) {
        return false;
    }

    // —— Intl.Segmenter ——
    Value* segmentsProto = ctx->createObject();
    if (segmentsProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(segmentsProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setIntlSegmentsPrototype(segmentsProto);
    if (!defineMethod(ctx, segmentsProto, "containing", segmentsContaining, 0)) {
        return false;
    }
    if (ctx->defineProperty(segmentsProto,
                            ctx->getWellKnownSymbol(WellKnownSymbol::Iterator),
                            ctx->createNativeFunction(segmentsIterator, 0), true,
                            false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineTag(ctx, segmentsProto, "Intl.Segments")) return false;

    Value* segmenterProto = ctx->createObject();
    if (segmenterProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(segmenterProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setIntlSegmenterPrototype(segmenterProto);
    if (!defineMethod(ctx, segmenterProto, "segment", segmenterSegment, 0)) {
        return false;
    }
    if (!defineMethod(ctx, segmenterProto, "resolvedOptions",
                      segmenterResolvedOptions, 0)) {
        return false;
    }
    if (!defineTag(ctx, segmenterProto, "Intl.Segmenter")) return false;

    Value* segmenterCtor = ctx->createNativeFunction(segmenterCall, 0);
    if (segmenterCtor == ctx->oomValue()) return false;
    ctx->setFunctionName(segmenterCtor, "Segmenter");
    ctx->setFunctionConstructor(segmenterCtor, segmenterConstruct);
    if (ctx->defineProperty(segmenterCtor, "prototype", segmenterProto, false,
                            false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(segmenterProto, "constructor", segmenterCtor, true,
                            false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, segmenterCtor, "supportedLocalesOf",
                      segmenterSupportedLocalesOf, 0)) {
        return false;
    }

    // —— Intl.Locale ——
    Value* localeProto = ctx->createObject();
    if (localeProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(localeProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    ctx->setIntlLocalePrototype(localeProto);
    if (!defineGetter(ctx, localeProto, "baseName", localeBaseName)) return false;
    if (!defineGetter(ctx, localeProto, "calendar", localeCalendar)) return false;
    if (!defineGetter(ctx, localeProto, "caseFirst", localeCaseFirst)) return false;
    if (!defineGetter(ctx, localeProto, "collation", localeCollation)) return false;
    if (!defineGetter(ctx, localeProto, "hourCycle", localeHourCycle)) return false;
    if (!defineGetter(ctx, localeProto, "language", localeLanguage)) return false;
    if (!defineGetter(ctx, localeProto, "numberingSystem", localeNumberingSystem)) {
        return false;
    }
    if (!defineGetter(ctx, localeProto, "numeric", localeNumeric)) return false;
    if (!defineGetter(ctx, localeProto, "region", localeRegion)) return false;
    if (!defineGetter(ctx, localeProto, "script", localeScript)) return false;
    if (!defineGetter(ctx, localeProto, "timeZone", localeTimeZone)) return false;
    if (!defineMethod(ctx, localeProto, "maximize", localeMaximize, 0)) return false;
    if (!defineMethod(ctx, localeProto, "minimize", localeMinimize, 0)) return false;
    if (!defineMethod(ctx, localeProto, "toString", localeToString, 0)) return false;
    if (!defineTag(ctx, localeProto, "Intl.Locale")) return false;

    Value* localeCtor = ctx->createNativeFunction(localeCall, 0);
    if (localeCtor == ctx->oomValue()) return false;
    ctx->setFunctionName(localeCtor, "Locale");
    ctx->setFunctionConstructor(localeCtor, localeConstruct);
    if (ctx->defineProperty(localeCtor, "prototype", localeProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(localeProto, "constructor", localeCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // Intl 命名空间
    if (!defineMethod(ctx, intl, "getCanonicalLocales", getCanonicalLocales, 0)) {
        return false;
    }
    if (!defineMethod(ctx, intl, "supportedValuesOf", supportedValuesOf, 0)) {
        return false;
    }
    if (ctx->defineProperty(intl, "NumberFormat", ctor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(intl, "DateTimeFormat", dateCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(intl, "Collator", collatorCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(intl, "PluralRules", pluralCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(intl, "RelativeTimeFormat", relativeCtor, true, false,
                            true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(intl, "ListFormat", listCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(intl, "DisplayNames", displayCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(intl, "Segmenter", segmenterCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(intl, "Locale", localeCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineTag(ctx, intl, "Intl")) return false;
    return ctx->defineGlobal("Intl", intl, true, false, true);
}

}  // namespace neo::core
