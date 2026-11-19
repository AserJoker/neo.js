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
#include "core/types/IntlDateTimeFormat.h"
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

// —— 注册辅助 ——

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

    // Intl 命名空间
    if (!defineMethod(ctx, intl, "getCanonicalLocales", getCanonicalLocales, 0)) {
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
    if (!defineTag(ctx, intl, "Intl")) return false;
    return ctx->defineGlobal("Intl", intl, true, false, true);
}

}  // namespace neo::core
