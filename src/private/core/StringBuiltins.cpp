// StringBuiltins.cpp - String.prototype 常用方法（ES2024 §22.1.3）
//
// 设计要点：
//   - 规范以 UTF-16 码元为索引单位，故统一转成 vector<uint16_t> 再操作；
//   - 孤立代理按 WTF-8 保留，charAt/codePointAt 与规范一致；
//   - 大小写映射用 CaseFolding 的简单映射（无 ß→SS 等 special casing）。
//
// 关联 ADR: 020（标准库组织）、053（Unicode 分类）

#include "core/StringBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "regex/RegexUnicode.h"
#include "unicode/UnicodeClassification.h"
#include "unicode/Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace neo::core {

namespace {

constexpr size_t kNotFound = std::numeric_limits<size_t>::max();

// —— UTF-8 ↔ UTF-16 码元 ——

std::vector<uint16_t> toUtf16(std::string_view s) {
    std::vector<uint16_t> out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        size_t j = i;
        uint32_t cp = 0;
        if (!unicode::decodeCodePoint(s, j, cp)) {
            out.push_back(static_cast<uint16_t>(static_cast<unsigned char>(s[i])));
            ++i;
            continue;
        }
        if (cp > 0xFFFF) {
            uint32_t v = cp - 0x10000;
            out.push_back(static_cast<uint16_t>(0xD800 + (v >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00 + (v & 0x3FF)));
        } else {
            out.push_back(static_cast<uint16_t>(cp));
        }
        i = j;
    }
    return out;
}

std::string fromUtf16(const std::vector<uint16_t>& units) {
    std::string out;
    for (size_t i = 0; i < units.size(); ++i) {
        uint32_t cp = units[i];
        if (unicode::isHighSurrogate(cp) && i + 1 < units.size() &&
            unicode::isLowSurrogate(units[i + 1])) {
            cp = unicode::combineSurrogates(cp, units[i + 1]);
            ++i;
        }
        unicode::encodeCodePoint(cp, out);
    }
    return out;
}

std::vector<uint32_t> toCodePoints(const std::vector<uint16_t>& units) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < units.size(); ++i) {
        uint32_t cp = units[i];
        if (unicode::isHighSurrogate(cp) && i + 1 < units.size() &&
            unicode::isLowSurrogate(units[i + 1])) {
            cp = unicode::combineSurrogates(cp, units[i + 1]);
            ++i;
        }
        out.push_back(cp);
    }
    return out;
}

std::vector<uint16_t> fromCodePoints(const std::vector<uint32_t>& cps) {
    std::vector<uint16_t> out;
    for (uint32_t cp : cps) {
        if (cp > 0xFFFF) {
            uint32_t v = cp - 0x10000;
            out.push_back(static_cast<uint16_t>(0xD800 + (v >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00 + (v & 0x3FF)));
        } else {
            out.push_back(static_cast<uint16_t>(cp));
        }
    }
    return out;
}

// —— 参数与 this ——

Value* requireStringThis(IContext* ctx, Value* self, const char* method) {
    TypeKind k = self->type()->kind();
    if (k == TypeKind::Undefined || k == TypeKind::Null) {
        return detail::typeError(
            ctx, std::string("String.prototype.") + method +
                     " called on null or undefined");
    }
    return ctx->toString(self);
}

const std::string& stringValue(Value* v) {
    return static_cast<StringData*>(v->data())->value;
}

// ToIntegerOrInfinity（§7.1.5）。
double toIntegerOrInfinity(IContext* ctx, Value* v, Value*& err) {
    Value* nv = detail::toNumberValue(ctx, v);
    if (detail::isErrorValue(nv)) {
        err = nv;
        return 0.0;
    }
    double n = static_cast<NumberData*>(nv->data())->value;
    if (std::isnan(n) || n == 0.0) return 0.0;
    if (std::isinf(n)) return n;
    return std::trunc(n);
}

// 相对下标（slice/substr）：负数从尾部计算。
uint32_t clampIndex(double idx, uint32_t len) {
    if (idx < 0.0) {
        double fromEnd = static_cast<double>(len) + idx;
        return fromEnd <= 0.0 ? 0u : static_cast<uint32_t>(fromEnd);
    }
    if (idx >= static_cast<double>(len)) return len;
    return static_cast<uint32_t>(idx);
}

// 绝对位置（includes/startsWith/endsWith/indexOf/substring）：负数夹到 0。
uint32_t clampPos(double idx, uint32_t len) {
    if (idx <= 0.0) return 0u;
    if (idx >= static_cast<double>(len)) return len;
    return static_cast<uint32_t>(idx);
}

Value* argOrUndefined(IContext* ctx, size_t argc, Value* argv[], size_t i) {
    return i < argc ? argv[i] : ctx->createUndefined();
}

// 在 hay 中从 from 起查找 needle，返回码元下标或 kNotFound。
size_t findUnits(const std::vector<uint16_t>& hay,
                 const std::vector<uint16_t>& needle, size_t from) {
    if (from > hay.size()) return kNotFound;
    if (needle.empty()) return from;
    if (needle.size() > hay.size() - from) return kNotFound;
    for (size_t i = from; i + needle.size() <= hay.size(); ++i) {
        if (std::equal(needle.begin(), needle.end(), hay.begin() + static_cast<ptrdiff_t>(i))) {
            return i;
        }
    }
    return kNotFound;
}

// —— 方法 ——

Value* stringCharAt(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "charAt");
    if (detail::isErrorValue(str)) return str;
    std::vector<uint16_t> units = toUtf16(stringValue(str));
    Value* err = nullptr;
    double raw = toIntegerOrInfinity(ctx, argOrUndefined(ctx, argc, argv, 0), err);
    if (err != nullptr) return err;
    if (raw < 0.0 || raw >= static_cast<double>(units.size())) {
        return ctx->createString("");
    }
    std::vector<uint16_t> one{units[static_cast<size_t>(raw)]};
    return ctx->createString(fromUtf16(one));
}

Value* stringCharCodeAt(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "charCodeAt");
    if (detail::isErrorValue(str)) return str;
    std::vector<uint16_t> units = toUtf16(stringValue(str));
    Value* err = nullptr;
    double raw = toIntegerOrInfinity(ctx, argOrUndefined(ctx, argc, argv, 0), err);
    if (err != nullptr) return err;
    if (raw < 0.0 || raw >= static_cast<double>(units.size())) {
        return ctx->createNumber(std::numeric_limits<double>::quiet_NaN());
    }
    return ctx->createNumber(
        static_cast<double>(units[static_cast<size_t>(raw)]));
}

Value* stringCodePointAt(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "codePointAt");
    if (detail::isErrorValue(str)) return str;
    std::vector<uint16_t> units = toUtf16(stringValue(str));
    Value* err = nullptr;
    double raw = toIntegerOrInfinity(ctx, argOrUndefined(ctx, argc, argv, 0), err);
    if (err != nullptr) return err;
    if (raw < 0.0 || raw >= static_cast<double>(units.size())) {
        return ctx->createUndefined();
    }
    size_t i = static_cast<size_t>(raw);
    uint32_t cp = units[i];
    if (unicode::isHighSurrogate(cp) && i + 1 < units.size() &&
        unicode::isLowSurrogate(units[i + 1])) {
        cp = unicode::combineSurrogates(cp, units[i + 1]);
    }
    return ctx->createNumber(static_cast<double>(cp));
}

Value* stringAt(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "at");
    if (detail::isErrorValue(str)) return str;
    std::vector<uint16_t> units = toUtf16(stringValue(str));
    Value* err = nullptr;
    double raw = toIntegerOrInfinity(ctx, argOrUndefined(ctx, argc, argv, 0), err);
    if (err != nullptr) return err;
    double idx = raw >= 0.0 ? raw : static_cast<double>(units.size()) + raw;
    if (idx < 0.0 || idx >= static_cast<double>(units.size())) {
        return ctx->createUndefined();
    }
    std::vector<uint16_t> one{units[static_cast<size_t>(idx)]};
    return ctx->createString(fromUtf16(one));
}

Value* stringIncludes(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "includes");
    if (detail::isErrorValue(str)) return str;
    Value* search = detail::toStringValue(ctx, argOrUndefined(ctx, argc, argv, 0));
    if (detail::isErrorValue(search)) return search;
    std::vector<uint16_t> hay = toUtf16(stringValue(str));
    std::vector<uint16_t> needle = toUtf16(stringValue(search));
    Value* err = nullptr;
    double raw = argc > 1 ? toIntegerOrInfinity(ctx, argv[1], err) : 0.0;
    if (err != nullptr) return err;
    uint32_t pos = clampPos(raw, static_cast<uint32_t>(hay.size()));
    return ctx->createBoolean(findUnits(hay, needle, pos) != kNotFound);
}

Value* stringStartsWith(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "startsWith");
    if (detail::isErrorValue(str)) return str;
    Value* search = detail::toStringValue(ctx, argOrUndefined(ctx, argc, argv, 0));
    if (detail::isErrorValue(search)) return search;
    std::vector<uint16_t> hay = toUtf16(stringValue(str));
    std::vector<uint16_t> needle = toUtf16(stringValue(search));
    Value* err = nullptr;
    double raw = argc > 1 ? toIntegerOrInfinity(ctx, argv[1], err) : 0.0;
    if (err != nullptr) return err;
    uint32_t pos = clampPos(raw, static_cast<uint32_t>(hay.size()));
    if (needle.size() > hay.size() - pos) return ctx->createBoolean(false);
    return ctx->createBoolean(
        std::equal(needle.begin(), needle.end(),
                   hay.begin() + static_cast<ptrdiff_t>(pos)));
}

Value* stringEndsWith(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "endsWith");
    if (detail::isErrorValue(str)) return str;
    Value* search = detail::toStringValue(ctx, argOrUndefined(ctx, argc, argv, 0));
    if (detail::isErrorValue(search)) return search;
    std::vector<uint16_t> hay = toUtf16(stringValue(str));
    std::vector<uint16_t> needle = toUtf16(stringValue(search));
    Value* err = nullptr;
    double raw = argc > 1 ? toIntegerOrInfinity(ctx, argv[1], err)
                          : static_cast<double>(hay.size());
    if (err != nullptr) return err;
    uint32_t end = clampPos(raw, static_cast<uint32_t>(hay.size()));
    if (needle.size() > end) return ctx->createBoolean(false);
    size_t start = static_cast<size_t>(end) - needle.size();
    return ctx->createBoolean(
        std::equal(needle.begin(), needle.end(),
                   hay.begin() + static_cast<ptrdiff_t>(start)));
}

Value* stringIndexOf(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "indexOf");
    if (detail::isErrorValue(str)) return str;
    Value* search = detail::toStringValue(ctx, argOrUndefined(ctx, argc, argv, 0));
    if (detail::isErrorValue(search)) return search;
    std::vector<uint16_t> hay = toUtf16(stringValue(str));
    std::vector<uint16_t> needle = toUtf16(stringValue(search));
    Value* err = nullptr;
    double raw = argc > 1 ? toIntegerOrInfinity(ctx, argv[1], err) : 0.0;
    if (err != nullptr) return err;
    uint32_t pos = clampPos(raw, static_cast<uint32_t>(hay.size()));
    size_t found = findUnits(hay, needle, pos);
    return ctx->createNumber(found == kNotFound ? -1.0
                                                : static_cast<double>(found));
}

Value* stringLastIndexOf(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "lastIndexOf");
    if (detail::isErrorValue(str)) return str;
    Value* search = detail::toStringValue(ctx, argOrUndefined(ctx, argc, argv, 0));
    if (detail::isErrorValue(search)) return search;
    std::vector<uint16_t> hay = toUtf16(stringValue(str));
    std::vector<uint16_t> needle = toUtf16(stringValue(search));

    double raw = std::numeric_limits<double>::infinity();
    if (argc > 1) {
        Value* nv = detail::toNumberValue(ctx, argv[1]);
        if (detail::isErrorValue(nv)) return nv;
        raw = static_cast<NumberData*>(nv->data())->value;
        // ToIntegerOrInfinity(NaN) = +0，规范中的 NaN 分支不可达。
        if (std::isnan(raw)) raw = 0.0;
    }
    double maxPos = static_cast<double>(hay.size());
    double pos = raw < 0.0 ? 0.0 : std::min(raw, maxPos);
    size_t start = static_cast<size_t>(pos);
    if (needle.empty()) return ctx->createNumber(static_cast<double>(start));
    if (needle.size() > hay.size()) return ctx->createNumber(-1.0);
    size_t last = hay.size() - needle.size();
    size_t from = std::min(start, last);
    for (size_t i = from + 1; i > 0; --i) {
        size_t idx = i - 1;
        if (std::equal(needle.begin(), needle.end(),
                       hay.begin() + static_cast<ptrdiff_t>(idx))) {
            return ctx->createNumber(static_cast<double>(idx));
        }
    }
    return ctx->createNumber(-1.0);
}

Value* stringSlice(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "slice");
    if (detail::isErrorValue(str)) return str;
    std::vector<uint16_t> units = toUtf16(stringValue(str));
    uint32_t len = static_cast<uint32_t>(units.size());
    Value* err = nullptr;
    double rawStart = argc > 0 ? toIntegerOrInfinity(ctx, argv[0], err) : 0.0;
    if (err != nullptr) return err;
    uint32_t start = clampIndex(rawStart, len);
    uint32_t end = len;
    if (argc > 1 && argv[1]->type()->kind() != TypeKind::Undefined) {
        double rawEnd = toIntegerOrInfinity(ctx, argv[1], err);
        if (err != nullptr) return err;
        end = clampIndex(rawEnd, len);
    }
    if (start >= end) return ctx->createString("");
    std::vector<uint16_t> part(units.begin() + static_cast<ptrdiff_t>(start),
                               units.begin() + static_cast<ptrdiff_t>(end));
    return ctx->createString(fromUtf16(part));
}

Value* stringSubstring(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "substring");
    if (detail::isErrorValue(str)) return str;
    std::vector<uint16_t> units = toUtf16(stringValue(str));
    uint32_t len = static_cast<uint32_t>(units.size());
    Value* err = nullptr;
    double rawStart = argc > 0 ? toIntegerOrInfinity(ctx, argv[0], err) : 0.0;
    if (err != nullptr) return err;
    double rawEnd = argc > 1 && argv[1]->type()->kind() != TypeKind::Undefined
                        ? toIntegerOrInfinity(ctx, argv[1], err)
                        : static_cast<double>(len);
    if (err != nullptr) return err;
    uint32_t start = clampPos(rawStart, len);
    uint32_t end = clampPos(rawEnd, len);
    if (start > end) std::swap(start, end);
    std::vector<uint16_t> part(units.begin() + static_cast<ptrdiff_t>(start),
                               units.begin() + static_cast<ptrdiff_t>(end));
    return ctx->createString(fromUtf16(part));
}

// Annex B.2.2.1 String.prototype.substr。
Value* stringSubstr(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "substr");
    if (detail::isErrorValue(str)) return str;
    std::vector<uint16_t> units = toUtf16(stringValue(str));
    uint32_t len = static_cast<uint32_t>(units.size());
    Value* err = nullptr;
    double rawStart = argc > 0 ? toIntegerOrInfinity(ctx, argv[0], err) : 0.0;
    if (err != nullptr) return err;
    uint32_t start = clampIndex(rawStart, len);
    double rawLength = argc > 1 && argv[1]->type()->kind() != TypeKind::Undefined
                           ? toIntegerOrInfinity(ctx, argv[1], err)
                           : static_cast<double>(len);
    if (err != nullptr) return err;
    double remaining = static_cast<double>(len) - static_cast<double>(start);
    double count = rawLength < 0.0 ? 0.0 : std::min(rawLength, remaining);
    uint32_t end = start + static_cast<uint32_t>(count);
    std::vector<uint16_t> part(units.begin() + static_cast<ptrdiff_t>(start),
                               units.begin() + static_cast<ptrdiff_t>(end));
    return ctx->createString(fromUtf16(part));
}

Value* trimImpl(IContext* ctx, Value* self, bool start, bool end,
                const char* method) {
    Value* str = requireStringThis(ctx, self, method);
    if (detail::isErrorValue(str)) return str;
    std::vector<uint16_t> units = toUtf16(stringValue(str));
    size_t begin = 0;
    size_t finish = units.size();
    if (start) {
        while (begin < finish &&
               unicode::isWhitespaceOrLineTerminator(units[begin])) {
            ++begin;
        }
    }
    if (end) {
        while (finish > begin &&
               unicode::isWhitespaceOrLineTerminator(units[finish - 1])) {
            --finish;
        }
    }
    std::vector<uint16_t> part(units.begin() + static_cast<ptrdiff_t>(begin),
                               units.begin() + static_cast<ptrdiff_t>(finish));
    return ctx->createString(fromUtf16(part));
}

Value* stringTrim(IContext* ctx, Value* self, size_t, Value*[]) {
    return trimImpl(ctx, self, true, true, "trim");
}
Value* stringTrimStart(IContext* ctx, Value* self, size_t, Value*[]) {
    return trimImpl(ctx, self, true, false, "trimStart");
}
Value* stringTrimEnd(IContext* ctx, Value* self, size_t, Value*[]) {
    return trimImpl(ctx, self, false, true, "trimEnd");
}

Value* padImpl(IContext* ctx, Value* self, size_t argc, Value* argv[], bool atStart,
               const char* method) {
    Value* str = requireStringThis(ctx, self, method);
    if (detail::isErrorValue(str)) return str;
    std::vector<uint16_t> units = toUtf16(stringValue(str));

    Value* maxLenValue = detail::toNumberValue(ctx, argOrUndefined(ctx, argc, argv, 0));
    if (detail::isErrorValue(maxLenValue)) return maxLenValue;
    double rawMax = static_cast<NumberData*>(maxLenValue->data())->value;
    if (std::isnan(rawMax) || rawMax <= 0.0) rawMax = 0.0;
    if (rawMax > 4294967295.0) rawMax = 4294967295.0;
    uint32_t maxLength = static_cast<uint32_t>(std::floor(rawMax));
    if (maxLength <= units.size()) return ctx->createString(fromUtf16(units));

    std::string fillText = " ";
    if (argc > 1 && argv[1]->type()->kind() != TypeKind::Undefined) {
        Value* fill = detail::toStringValue(ctx, argv[1]);
        if (detail::isErrorValue(fill)) return fill;
        fillText = stringValue(fill);
    }
    std::vector<uint16_t> fill = toUtf16(fillText);
    if (fill.empty()) return ctx->createString(fromUtf16(units));

    size_t need = static_cast<size_t>(maxLength) - units.size();
    std::vector<uint16_t> padding;
    padding.reserve(need);
    while (padding.size() < need) {
        size_t take = std::min(need - padding.size(), fill.size());
        padding.insert(padding.end(), fill.begin(),
                       fill.begin() + static_cast<ptrdiff_t>(take));
    }
    std::vector<uint16_t> result;
    result.reserve(maxLength);
    if (atStart) {
        result.insert(result.end(), padding.begin(), padding.end());
        result.insert(result.end(), units.begin(), units.end());
    } else {
        result.insert(result.end(), units.begin(), units.end());
        result.insert(result.end(), padding.begin(), padding.end());
    }
    return ctx->createString(fromUtf16(result));
}

Value* stringPadStart(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return padImpl(ctx, self, argc, argv, true, "padStart");
}
Value* stringPadEnd(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return padImpl(ctx, self, argc, argv, false, "padEnd");
}

Value* stringRepeat(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "repeat");
    if (detail::isErrorValue(str)) return str;
    Value* err = nullptr;
    double raw = toIntegerOrInfinity(ctx, argOrUndefined(ctx, argc, argv, 0), err);
    if (err != nullptr) return err;
    if (raw < 0.0 || std::isinf(raw)) {
        return detail::rangeError(ctx, "Invalid count value");
    }
    std::vector<uint16_t> units = toUtf16(stringValue(str));
    if (raw == 0.0 || units.empty()) return ctx->createString("");
    double total = raw * static_cast<double>(units.size());
    if (total > 4294967295.0) {
        return detail::rangeError(ctx, "Invalid string length");
    }
    size_t count = static_cast<size_t>(raw);
    std::vector<uint16_t> result;
    result.reserve(static_cast<size_t>(total));
    for (size_t i = 0; i < count; ++i) {
        result.insert(result.end(), units.begin(), units.end());
    }
    return ctx->createString(fromUtf16(result));
}

Value* stringConcat(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "concat");
    if (detail::isErrorValue(str)) return str;
    std::string result = stringValue(str);
    for (size_t i = 0; i < argc; ++i) {
        Value* next = detail::toStringValue(ctx, argv[i]);
        if (detail::isErrorValue(next)) return next;
        result += stringValue(next);
    }
    return ctx->createString(result);
}

Value* stringLocaleCompare(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* str = requireStringThis(ctx, self, "localeCompare");
    if (detail::isErrorValue(str)) return str;
    Value* that = detail::toStringValue(ctx, argOrUndefined(ctx, argc, argv, 0));
    if (detail::isErrorValue(that)) return that;
    std::vector<uint16_t> a = toUtf16(stringValue(str));
    std::vector<uint16_t> b = toUtf16(stringValue(that));
    // 无 locale/ICU 时退化为码元序比较。
    if (std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end())) {
        return ctx->createNumber(-1.0);
    }
    if (std::lexicographical_compare(b.begin(), b.end(), a.begin(), a.end())) {
        return ctx->createNumber(1.0);
    }
    return ctx->createNumber(0.0);
}

Value* caseMap(IContext* ctx, Value* self, bool upper, const char* method) {
    Value* str = requireStringThis(ctx, self, method);
    if (detail::isErrorValue(str)) return str;
    std::vector<uint32_t> cps = toCodePoints(toUtf16(stringValue(str)));
    for (uint32_t& cp : cps) {
        cp = upper ? regex::simpleUpper(cp) : regex::simpleFold(cp);
    }
    return ctx->createString(fromUtf16(fromCodePoints(cps)));
}

Value* stringToLowerCase(IContext* ctx, Value* self, size_t, Value*[]) {
    return caseMap(ctx, self, false, "toLowerCase");
}
Value* stringToUpperCase(IContext* ctx, Value* self, size_t, Value*[]) {
    return caseMap(ctx, self, true, "toUpperCase");
}
Value* stringToLocaleLowerCase(IContext* ctx, Value* self, size_t, Value*[]) {
    return caseMap(ctx, self, false, "toLocaleLowerCase");
}
Value* stringToLocaleUpperCase(IContext* ctx, Value* self, size_t, Value*[]) {
    return caseMap(ctx, self, true, "toLocaleUpperCase");
}

// 定义 prototype 方法（writable/不可枚举/可配置）。
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc, int lengthOverride) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    if (lengthOverride >= 0) {
        Value* len = ctx->createNumber(static_cast<double>(lengthOverride));
        if (len == ctx->oomValue()) return false;
        if (ctx->defineProperty(f, "length", len, false, false, true)->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

}  // namespace

bool registerStringBuiltins(IContext* ctx, Value* proto) {
    if (!defineMethod(ctx, proto, "charAt", stringCharAt, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "charCodeAt", stringCharCodeAt, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "codePointAt", stringCodePointAt, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "at", stringAt, 0, 1)) return false;

    if (!defineMethod(ctx, proto, "includes", stringIncludes, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "startsWith", stringStartsWith, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "endsWith", stringEndsWith, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "indexOf", stringIndexOf, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "lastIndexOf", stringLastIndexOf, 0, 1)) return false;

    if (!defineMethod(ctx, proto, "slice", stringSlice, 0, 2)) return false;
    if (!defineMethod(ctx, proto, "substring", stringSubstring, 0, 2)) return false;
    if (!defineMethod(ctx, proto, "substr", stringSubstr, 0, 2)) return false;

    if (!defineMethod(ctx, proto, "trim", stringTrim, 0, 0)) return false;
    if (!defineMethod(ctx, proto, "trimStart", stringTrimStart, 0, 0)) return false;
    if (!defineMethod(ctx, proto, "trimEnd", stringTrimEnd, 0, 0)) return false;
    if (!defineMethod(ctx, proto, "padStart", stringPadStart, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "padEnd", stringPadEnd, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "repeat", stringRepeat, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "concat", stringConcat, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "localeCompare", stringLocaleCompare, 0, 1))
        return false;

    if (!defineMethod(ctx, proto, "toLowerCase", stringToLowerCase, 0, 0)) return false;
    if (!defineMethod(ctx, proto, "toUpperCase", stringToUpperCase, 0, 0)) return false;
    if (!defineMethod(ctx, proto, "toLocaleLowerCase", stringToLocaleLowerCase, 0, 0))
        return false;
    if (!defineMethod(ctx, proto, "toLocaleUpperCase", stringToLocaleUpperCase, 0, 0))
        return false;
    return true;
}

}  // namespace neo::core
