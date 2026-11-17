// RegExpBuiltins.cpp - RegExp 构造器/prototype/Symbol 方法与 String 接入
//
// 设计要点：
//   - RegExpData 持不可变 regex::Program；lastIndex 是普通自有属性；
//   - exec 把 UTF-8 StringData 转成 UTF-16 Subject 后调用引擎，再构造结果数组；
//   - Symbol.match/replace/search/split/matchAll 与 String 六个方法共用同一套
//     内部 exec/replace 辅助，避免 JS 层与引擎层语义分叉；
//   - 编译错误映射为 SyntaxError。
//
// 关联 ADR: 020（标准库组织）、062（正则引擎 R4）

#include "core/RegExpBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Iterator.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/RegExp.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "regex/RegexEngine.h"
#include "regex/RegexHost.h"
#include "regex/RegexMatch.h"
#include "regex/RegexSubject.h"
#include "unicode/Utf8.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neo::core {

namespace {

// —— Unicode 转换 ——

std::u16string toUtf16(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        std::uint32_t codePoint = 0;
        if (!unicode::decodeCodePoint(text, i, codePoint)) {
            out.push_back(static_cast<char16_t>(
                static_cast<unsigned char>(text[i])));
            ++i;
            continue;
        }
        if (codePoint > 0xFFFFu) {
            const std::uint32_t value = codePoint - 0x10000u;
            out.push_back(static_cast<char16_t>(0xD800u + (value >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00u + (value & 0x3FFu)));
        } else {
            out.push_back(static_cast<char16_t>(codePoint));
        }
    }
    return out;
}

std::string fromUtf16(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        std::uint32_t codePoint = text[i];
        if (codePoint >= 0xD800u && codePoint <= 0xDBFFu &&
            i + 1 < text.size() && text[i + 1] >= 0xDC00u &&
            text[i + 1] <= 0xDFFFu) {
            codePoint = 0x10000u + ((codePoint - 0xD800u) << 10) +
                        (static_cast<std::uint32_t>(text[i + 1]) - 0xDC00u);
            ++i;
        }
        unicode::encodeCodePoint(codePoint, out);
        ++i;
    }
    return out;
}

std::string slice(const std::u16string& text, std::size_t begin,
                  std::size_t end) {
    if (begin > text.size()) begin = text.size();
    if (end > text.size()) end = text.size();
    if (end < begin) end = begin;
    return fromUtf16(
        std::u16string_view(text).substr(begin, end - begin));
}

// —— Host ——

class EmptyUnicodeTables final : public regex::UnicodeTables {};

class RegexHostImpl final : public regex::RegexHost {
public:
    const regex::UnicodeTables& unicode() const override { return tables_; }
    bool shouldAbort(std::uint64_t) override { return false; }

private:
    EmptyUnicodeTables tables_;
};

regex::RegexHost& regexHost() {
    static RegexHostImpl host;
    return host;
}

// 进程内共享的引擎实例：Program 缓存需要跨调用存活，且缓存有界。
regex::RegexEngine& regexEngine() {
    static regex::RegexEngine engine(regexHost());
    return engine;
}

// —— 小工具 ——

Value* undefinedOf(IContext* ctx) { return ctx->createUndefined(); }

Value* nullOf(IContext* ctx) { return ctx->createNull(); }

bool isError(Value* v) { return detail::isErrorValue(v); }

Value* toStringArg(IContext* ctx, Value* value) {
    return detail::toStringValue(ctx, value);
}

const std::string* stringData(Value* value) {
    if (value == nullptr || value->type()->kind() != TypeKind::String) {
        return nullptr;
    }
    return &static_cast<StringData*>(value->data())->value;
}

Value* captureValue(IContext* ctx, const std::u16string& subject,
                    const regex::Capture& capture) {
    if (!capture.matched) return undefinedOf(ctx);
    return ctx->createString(slice(subject, capture.start, capture.end));
}

Value* setLastIndex(IContext* ctx, Value* regexp, std::size_t value) {
    return ctx->setProperty(regexp, "lastIndex",
                            ctx->createNumber(static_cast<double>(value)));
}

bool readLastIndex(IContext* ctx, Value* regexp, std::size_t& out,
                   Value*& error) {
    error = nullptr;
    Value* value = ctx->getProperty(regexp, "lastIndex");
    if (isError(value)) {
        error = value;
        return false;
    }
    Value* number = detail::toNumberValue(ctx, value);
    if (isError(number)) {
        error = number;
        return false;
    }
    const double n = static_cast<NumberData*>(number->data())->value;
    if (!(n > 0.0)) {
        out = 0;
    } else if (n > 9007199254740991.0) {
        out = 9007199254740991ull;
    } else {
        out = static_cast<std::size_t>(n);
    }
    return true;
}

// —— 引擎执行 ——

// 单次 exec 的默认指令预算：高阈值，只用于兜住灾难性回溯，不影响正常匹配。
constexpr std::uint64_t kDefaultStepLimit = 10'000'000u;

bool runEngine(IContext* ctx, const RegExpData& data,
               const std::u16string& subject, std::size_t start,
               bool anchored, regex::MatchResult& result, Value*& error) {
    error = nullptr;
    const regex::Subject view = regex::Subject::fromUtf16(subject);
    regex::RegexEngine& engine = regexEngine();
    regex::ExecOptions options;
    options.startIndex = start;
    options.anchored = anchored;
    options.stepLimit = kDefaultStepLimit;
    result = engine.exec(*data.program, view, options);
    if (result.status == regex::MatchStatus::Aborted) {
        error = detail::errorOf(ctx, ErrorKind::RangeError,
                                "RegExp execution aborted");
        return false;
    }
    if (result.status == regex::MatchStatus::Oom) {
        error = ctx->oomValue();
        return false;
    }
    return true;
}

Value* makeGroups(IContext* ctx, const RegExpData& data,
                  const std::u16string& subject,
                  const regex::MatchResult& result) {
    Value* groups = ctx->createObject();
    if (groups == ctx->oomValue()) return groups;
    for (const auto& entry : data.program->namedGroups) {
        const std::size_t index = entry.second;
        Value* value = index < result.captures.size()
                           ? captureValue(ctx, subject, result.captures[index])
                           : undefinedOf(ctx);
        if (isError(value)) return value;
        Value* r = ctx->defineProperty(groups, entry.first, value, true, true,
                                       true);
        if (isError(r)) return r;
    }
    return groups;
}

Value* makeIndices(IContext* ctx, const RegExpData& data,
                   const regex::MatchResult& result) {
    Value* indices = ctx->createArray();
    if (indices == ctx->oomValue()) return indices;
    for (const regex::Capture& capture : result.captures) {
        Value* pair = nullptr;
        if (capture.matched) {
            pair = ctx->createArray();
            if (pair == ctx->oomValue()) return pair;
            if (isError(ctx->arrayAppend(
                    pair, ctx->createNumber(static_cast<double>(capture.start)))))
                return ctx->oomValue();
            if (isError(ctx->arrayAppend(
                    pair, ctx->createNumber(static_cast<double>(capture.end)))))
                return ctx->oomValue();
        } else {
            pair = undefinedOf(ctx);
        }
        if (isError(ctx->arrayAppend(indices, pair))) return ctx->oomValue();
    }

    if (!data.program->namedGroups.empty()) {
        Value* groups = ctx->createObject();
        if (groups == ctx->oomValue()) return groups;
        for (const auto& entry : data.program->namedGroups) {
            const std::size_t index = entry.second;
            Value* value = undefinedOf(ctx);
            if (index < result.captures.size() &&
                result.captures[index].matched) {
                const regex::Capture& capture = result.captures[index];
                value = ctx->createArray();
                if (value == ctx->oomValue()) return value;
                if (isError(ctx->arrayAppend(
                        value, ctx->createNumber(
                                   static_cast<double>(capture.start)))))
                    return ctx->oomValue();
                if (isError(ctx->arrayAppend(
                        value, ctx->createNumber(
                                   static_cast<double>(capture.end)))))
                    return ctx->oomValue();
            }
            Value* r = ctx->defineProperty(groups, entry.first, value, true,
                                           true, true);
            if (isError(r)) return r;
        }
        Value* r = ctx->defineProperty(indices, "groups", groups, true, true,
                                       true);
        if (isError(r)) return r;
    }
    return indices;
}

Value* makeResultArray(IContext* ctx, const RegExpData& data,
                       const std::u16string& subject,
                       const regex::MatchResult& result, Value* input) {
    Value* array = ctx->createArray();
    if (array == ctx->oomValue()) return array;

    for (const regex::Capture& capture : result.captures) {
        Value* value = captureValue(ctx, subject, capture);
        if (isError(value)) return value;
        Value* r = ctx->arrayAppend(array, value);
        if (isError(r)) return r;
    }

    if (isError(ctx->defineProperty(
            array, "index",
            ctx->createNumber(static_cast<double>(result.start)), true, true,
            true)))
        return ctx->oomValue();
    if (isError(ctx->defineProperty(array, "input", input, true, true, true)))
        return ctx->oomValue();

    if (!data.program->namedGroups.empty()) {
        Value* groups = makeGroups(ctx, data, subject, result);
        if (isError(groups)) return groups;
        if (isError(ctx->defineProperty(array, "groups", groups, true, true,
                                        true)))
            return ctx->oomValue();
    }

    if (data.hasIndices) {
        Value* indices = makeIndices(ctx, data, result);
        if (isError(indices)) return indices;
        if (isError(ctx->defineProperty(array, "indices", indices, true, true,
                                        true)))
            return ctx->oomValue();
    }

    return array;
}

// 读取 lastIndex 并在 global/sticky 时更新；返回结果数组 / null / 错误值。
Value* execString(IContext* ctx, Value* regexp, const RegExpData& data,
                  const std::u16string& subject, Value* input) {
    std::size_t start = 0;
    Value* error = nullptr;
    if (!readLastIndex(ctx, regexp, start, error)) return error;
    if (start > subject.size()) {
        // §22.2.7.2 step 13.a：lastIndex 越界时重置并直接失败。
        if (data.global || data.sticky) {
            Value* reset = setLastIndex(ctx, regexp, 0);
            if (isError(reset)) return reset;
        }
        return nullOf(ctx);
    }

    regex::MatchResult result;
    if (!runEngine(ctx, data, subject, start, data.sticky, result, error)) {
        return error;
    }

    if (!result.matched()) {
        if (data.global || data.sticky) {
            Value* r = setLastIndex(ctx, regexp, 0);
            if (isError(r)) return r;
        }
        return nullOf(ctx);
    }

    if (data.global || data.sticky) {
        Value* r = setLastIndex(ctx, regexp, result.end);
        if (isError(r)) return r;
    }
    return makeResultArray(ctx, data, subject, result, input);
}

// —— 构造器 ——

Value* regexpCreate(IContext* ctx, size_t argc, Value* argv[], bool construct) {
    Value* patternArg = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* flagsArg = argc > 1 ? argv[1] : undefinedOf(ctx);

    if (patternArg->type()->kind() == TypeKind::RegExp) {
        RegExpData* source = regexpData(patternArg);
        if (flagsArg->type()->kind() == TypeKind::Undefined) {
            if (!construct) return patternArg;
            return ctx->createRegExp(source->source, source->flags);
        }
        Value* flags = toStringArg(ctx, flagsArg);
        if (isError(flags)) return flags;
        return ctx->createRegExp(source->source, *stringData(flags));
    }

    Value* pattern = undefinedOf(ctx);
    if (argc > 0) {
        pattern = toStringArg(ctx, patternArg);
        if (isError(pattern)) return pattern;
    } else {
        pattern = ctx->createString("");
    }

    Value* flags = undefinedOf(ctx);
    if (argc > 1) {
        flags = toStringArg(ctx, flagsArg);
        if (isError(flags)) return flags;
    } else {
        flags = ctx->createString("");
    }

    return ctx->createRegExp(*stringData(pattern), *stringData(flags));
}

Value* regexpCtor(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return regexpCreate(ctx, argc, argv, false);
}

// Annex B B.2.4.1 RegExp.prototype.compile：原地重新初始化。
Value* regexpCompile(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(
            ctx, "RegExp.prototype.compile called on incompatible receiver");
    }

    Value* patternArg = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* flagsArg = argc > 1 ? argv[1] : undefinedOf(ctx);

    std::string pattern;
    std::string flags;
    if (patternArg->type()->kind() == TypeKind::RegExp) {
        // §22.2.4.1 step 4：P 取 [[OriginalSource]]，与 flags 是否给出无关。
        RegExpData* source = regexpData(patternArg);
        pattern = source->source;
        if (flagsArg->type()->kind() == TypeKind::Undefined) {
            flags = source->flags;
        } else {
            Value* text = toStringArg(ctx, flagsArg);
            if (isError(text)) return text;
            flags = *stringData(text);
        }
    } else {
        if (patternArg->type()->kind() == TypeKind::Undefined) {
            pattern.clear();
        } else {
            Value* text = toStringArg(ctx, patternArg);
            if (isError(text)) return text;
            pattern = *stringData(text);
        }
        if (argc > 1) {
            Value* text = toStringArg(ctx, flagsArg);
            if (isError(text)) return text;
            flags = *stringData(text);
        }
    }

    std::shared_ptr<const regex::Program> program;
    regex::Flags parsedFlags;
    std::string error;
    if (!compileRegExp(pattern, flags, program, parsedFlags, error)) {
        return detail::errorOf(ctx, ErrorKind::SyntaxError, error);
    }

    data->source = std::move(pattern);
    data->flags = std::move(flags);
    data->program = std::move(program);
    data->global = parsedFlags.global;
    data->ignoreCase = parsedFlags.ignoreCase;
    data->multiline = parsedFlags.multiline;
    data->dotAll = parsedFlags.dotAll;
    data->unicode = parsedFlags.unicode;
    data->unicodeSets = parsedFlags.unicodeSets;
    data->sticky = parsedFlags.sticky;
    data->hasIndices = parsedFlags.hasIndices;

    Value* reset = setLastIndex(ctx, self, 0);
    if (isError(reset)) return reset;
    return self;
}

Value* regexpConstruct(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return regexpCreate(ctx, argc, argv, true);
}

// —— prototype 方法 ——

Value* regexpExec(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(
            ctx, "RegExp.prototype.exec called on incompatible receiver");
    }
    Value* input = argc > 0 ? toStringArg(ctx, argv[0])
                            : ctx->createString("undefined");
    if (isError(input)) return input;
    return execString(ctx, self, *data, toUtf16(*stringData(input)), input);
}

// ES2024 §22.2.7.1 RegExpExec：优先调用可覆写的 exec。
Value* regExpExecValue(IContext* ctx, Value* regexp, const RegExpData& data,
                       Value* input) {
    Value* exec = ctx->getProperty(regexp, "exec");
    if (isError(exec)) return exec;
    if (detail::isCallable(exec)) {
        Value* args[1] = {input};
        Value* result = ctx->call(exec, regexp, 1, args);
        if (isError(result)) return result;
        if (result->type()->kind() != TypeKind::Null &&
            !detail::isObjectKind(result->type()->kind())) {
            return detail::typeError(
                ctx, "RegExp exec result must be an object or null");
        }
        return result;
    }
    return execString(ctx, regexp, data, toUtf16(*stringData(input)), input);
}

Value* regexpTest(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(
            ctx, "RegExp.prototype.test called on incompatible receiver");
    }
    Value* input = argc > 0 ? toStringArg(ctx, argv[0])
                            : ctx->createString("undefined");
    if (isError(input)) return input;
    Value* result = regExpExecValue(ctx, self, *data, input);
    if (isError(result)) return result;
    return ctx->createBoolean(result->type()->kind() != TypeKind::Null);
}

// ES2024 §22.2.6.13 EscapeRegExpPattern：转义 '/' 与行终止符。
std::string escapeRegExpPattern(const std::string& source) {
    if (source.empty()) return "(?:)";
    std::string out;
    out.reserve(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(source[i]);
        switch (byte) {
            case '/':
                out += "\\/";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case 0xE2:
                if (i + 2 < source.size() &&
                    static_cast<unsigned char>(source[i + 1]) == 0x80 &&
                    (static_cast<unsigned char>(source[i + 2]) == 0xA8 ||
                     static_cast<unsigned char>(source[i + 2]) == 0xA9)) {
                    out += static_cast<unsigned char>(source[i + 2]) == 0xA8
                               ? "\\u2028"
                               : "\\u2029";
                    i += 2;
                } else {
                    out.push_back(source[i]);
                }
                break;
            default:
                out.push_back(source[i]);
                break;
        }
    }
    return out;
}

Value* regexpToString(IContext* ctx, Value* self, size_t, Value*[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(
            ctx, "RegExp.prototype.toString called on incompatible receiver");
    }
    std::string text = "/";
    text += escapeRegExpPattern(data->source);
    text += "/";
    text += data->flags;
    return ctx->createString(text);
}

Value* regexpSourceGetter(IContext* ctx, Value* self, size_t, Value*[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(
            ctx, "RegExp.prototype.source called on incompatible receiver");
    }
    return ctx->createString(escapeRegExpPattern(data->source));
}

Value* regexpFlagsGetter(IContext* ctx, Value* self, size_t, Value*[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(
            ctx, "RegExp.prototype.flags called on incompatible receiver");
    }
    std::string flags;
    if (data->hasIndices) flags += 'd';
    if (data->global) flags += 'g';
    if (data->ignoreCase) flags += 'i';
    if (data->multiline) flags += 'm';
    if (data->dotAll) flags += 's';
    if (data->unicode) flags += 'u';
    if (data->unicodeSets) flags += 'v';
    if (data->sticky) flags += 'y';
    return ctx->createString(flags);
}

template <bool RegExpData::*Member>
Value* regexpBoolGetter(IContext* ctx, Value* self, size_t, Value*[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(ctx,
                                 "RegExp prototype getter called on "
                                 "incompatible receiver");
    }
    return ctx->createBoolean(data->*Member);
}

Value* regexpSpeciesGetter(IContext* ctx, Value* self, size_t, Value*[]) {
    (void)ctx;
    return self;
}

// —— Symbol.match / matchAll / search / replace / split ——

Value* regexpSymbolMatch(IContext* ctx, Value* self, size_t argc,
                         Value* argv[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(ctx,
                                 "RegExp.prototype[Symbol.match] called on "
                                 "incompatible receiver");
    }
    Value* input = argc > 0 ? toStringArg(ctx, argv[0]) : undefinedOf(ctx);
    if (isError(input)) return input;
    const std::u16string subject = toUtf16(*stringData(input));

    if (!data->global) return execString(ctx, self, *data, subject, input);

    Value* results = ctx->createArray();
    if (results == ctx->oomValue()) return results;
    if (isError(setLastIndex(ctx, self, 0))) return ctx->oomValue();

    bool any = false;
    for (;;) {
        Value* match = execString(ctx, self, *data, subject, input);
        if (isError(match)) return match;
        if (match->type()->kind() == TypeKind::Null) break;
        any = true;
        Value* first = ctx->getProperty(match, "0");
        if (isError(first)) return first;
        if (isError(ctx->arrayAppend(results, first))) return ctx->oomValue();
        if (stringData(first)->empty()) {
            std::size_t next = 0;
            Value* error = nullptr;
            if (!readLastIndex(ctx, self, next, error)) return error;
            if (next >= subject.size()) break;
            if (isError(setLastIndex(ctx, self, next + 1))) return ctx->oomValue();
        }
    }
    return any ? results : nullOf(ctx);
}

Value* regexpSymbolSearch(IContext* ctx, Value* self, size_t argc,
                          Value* argv[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(ctx,
                                 "RegExp.prototype[Symbol.search] called on "
                                 "incompatible receiver");
    }
    Value* input = argc > 0 ? toStringArg(ctx, argv[0]) : undefinedOf(ctx);
    if (isError(input)) return input;
    const std::u16string subject = toUtf16(*stringData(input));

    // §22.2.6.14：保存 lastIndex，非 0 时置 0，执行后恢复原值。
    Value* previous = ctx->getProperty(self, "lastIndex");
    if (isError(previous)) return previous;
    Value* zero = ctx->createNumber(0.0);
    if (isError(zero)) return zero;
    if (!detail::sameValue(previous, zero)) {
        Value* reset = ctx->setProperty(self, "lastIndex", zero);
        if (isError(reset)) return reset;
    }

    regex::MatchResult result;
    Value* error = nullptr;
    const bool ok =
        runEngine(ctx, *data, subject, 0, data->sticky, result, error);

    Value* current = ctx->getProperty(self, "lastIndex");
    if (isError(current)) return current;
    if (!detail::sameValue(current, previous)) {
        Value* restore = ctx->setProperty(self, "lastIndex", previous);
        if (isError(restore)) return restore;
    }
    if (!ok) return error;
    if (!result.matched()) return ctx->createNumber(-1.0);
    return ctx->createNumber(static_cast<double>(result.start));
}

std::string expandReplacement(const std::string& replacement,
                              const std::u16string& subject,
                              const RegExpData& data,
                              const regex::MatchResult& match) {
    std::string out;
    const std::string matched = slice(subject, match.start, match.end);
    for (std::size_t i = 0; i < replacement.size(); ++i) {
        if (replacement[i] != '$' || i + 1 >= replacement.size()) {
            out += replacement[i];
            continue;
        }
        const char next = replacement[i + 1];
        if (next == '$') {
            out += '$';
            ++i;
        } else if (next == '&') {
            out += matched;
            ++i;
        } else if (next == '`') {
            out += slice(subject, 0, match.start);
            ++i;
        } else if (next == '\'') {
            out += slice(subject, match.end, subject.size());
            ++i;
        } else if (next >= '1' && next <= '9') {
            std::size_t index = static_cast<std::size_t>(next - '0');
            std::size_t consumed = 1;
            if (i + 2 < replacement.size() && replacement[i + 2] >= '0' &&
                replacement[i + 2] <= '9') {
                const std::size_t two =
                    index * 10 + static_cast<std::size_t>(replacement[i + 2] - '0');
                if (two < match.captures.size()) {
                    index = two;
                    consumed = 2;
                }
            }
            if (index < match.captures.size() && match.captures[index].matched) {
                const regex::Capture& capture = match.captures[index];
                out += slice(subject, capture.start, capture.end);
            }
            i += consumed;
        } else if (next == '<') {
            const std::size_t close = replacement.find('>', i + 2);
            if (close == std::string::npos) {
                out += replacement[i];
                continue;
            }
            const std::string name = replacement.substr(i + 2, close - i - 2);
            auto it = data.program->namedGroups.find(name);
            if (it != data.program->namedGroups.end() &&
                it->second < match.captures.size() &&
                match.captures[it->second].matched) {
                const regex::Capture& capture = match.captures[it->second];
                out += slice(subject, capture.start, capture.end);
            }
            i = close;
        } else {
            out += replacement[i];
        }
    }
    return out;
}

Value* callReplacement(IContext* ctx, Value* replaceValue,
                       const std::u16string& subject,
                       const regex::MatchResult& match, Value* input) {
    std::vector<Value*> args;
    args.reserve(match.captures.size() + 2);
    for (const regex::Capture& capture : match.captures) {
        args.push_back(captureValue(ctx, subject, capture));
        if (isError(args.back())) return args.back();
    }
    args.push_back(ctx->createNumber(static_cast<double>(match.start)));
    args.push_back(input);
    Value* result = ctx->call(replaceValue, undefinedOf(ctx), args.size(),
                              args.data());
    if (isError(result)) return result;
    return toStringArg(ctx, result);
}

Value* regexpSymbolReplace(IContext* ctx, Value* self, size_t argc,
                           Value* argv[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(ctx,
                                 "RegExp.prototype[Symbol.replace] called on "
                                 "incompatible receiver");
    }
    Value* input = argc > 0 ? toStringArg(ctx, argv[0]) : undefinedOf(ctx);
    if (isError(input)) return input;
    Value* replaceValue = argc > 1 ? argv[1] : undefinedOf(ctx);
    const bool functional = detail::isCallable(replaceValue);
    const std::u16string subject = toUtf16(*stringData(input));

    std::string replacement;
    if (!functional) {
        Value* text = toStringArg(ctx, replaceValue);
        if (isError(text)) return text;
        replacement = *stringData(text);
    }

    std::size_t start = 0;
    if (data->global) {
        if (isError(setLastIndex(ctx, self, 0))) return ctx->oomValue();
    } else {
        Value* error = nullptr;
        if (!readLastIndex(ctx, self, start, error)) return error;
        if (start > subject.size()) start = subject.size();
    }

    std::string out;
    std::size_t last = 0;
    std::size_t searchStart = start;
    bool any = false;

    for (;;) {
        regex::MatchResult match;
        Value* error = nullptr;
        if (!runEngine(ctx, *data, subject, searchStart, data->sticky, match,
                       error)) {
            return error;
        }
        if (!match.matched()) break;
        any = true;

        out += slice(subject, last, match.start);
        if (functional) {
            Value* text =
                callReplacement(ctx, replaceValue, subject, match, input);
            if (isError(text)) return text;
            out += *stringData(text);
        } else {
            out += expandReplacement(replacement, subject, *data, match);
        }

        last = match.end;
        if (data->global) {
            if (isError(setLastIndex(ctx, self, match.end)))
                return ctx->oomValue();
        }
        if (!data->global) break;

        if (match.end == match.start) {
            if (match.end >= subject.size()) {
                searchStart = subject.size() + 1;
                break;
            }
            std::size_t width = 1;
            if (data->unicode && match.end + 1 < subject.size()) {
                const char16_t first = subject[match.end];
                const char16_t second = subject[match.end + 1];
                if (first >= 0xD800 && first <= 0xDBFF && second >= 0xDC00 &&
                    second <= 0xDFFF) {
                    width = 2;
                }
            }
            out += slice(subject, match.end, match.end + width);
            last = match.end + width;
            searchStart = last;
        } else {
            searchStart = match.end;
        }
    }

    if (!any) return input;
    out += slice(subject, last, subject.size());
    return ctx->createString(out);
}

Value* regexpSymbolSplit(IContext* ctx, Value* self, size_t argc,
                         Value* argv[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(ctx,
                                 "RegExp.prototype[Symbol.split] called on "
                                 "incompatible receiver");
    }
    Value* input = argc > 0 ? toStringArg(ctx, argv[0]) : undefinedOf(ctx);
    if (isError(input)) return input;
    const std::u16string subject = toUtf16(*stringData(input));

    std::uint32_t limit = 0xFFFFFFFFu;
    if (argc > 1) {
        Value* number = detail::toNumberValue(ctx, argv[1]);
        if (isError(number)) return number;
        const double n = static_cast<NumberData*>(number->data())->value;
        if (n <= 0.0) return ctx->createArray();
        if (n < 4294967295.0) limit = static_cast<std::uint32_t>(n);
    }
    if (limit == 0) return ctx->createArray();

    Value* result = ctx->createArray();
    if (result == ctx->oomValue()) return result;

    // §22.2.6.16：内部使用 sticky 克隆，不改动原对象的 lastIndex。
    std::string splitFlags = data->flags;
    if (splitFlags.find('y') == std::string::npos) splitFlags += 'y';
    Value* splitter = ctx->createRegExp(data->source, splitFlags);
    if (isError(splitter)) return splitter;
    RegExpData* splitData = regexpData(splitter);
    if (splitData == nullptr) return ctx->oomValue();

    if (subject.empty()) {
        regex::MatchResult match;
        Value* error = nullptr;
        if (!runEngine(ctx, *splitData, subject, 0, true, match, error)) {
            return error;
        }
        if (!match.matched()) {
            (void)ctx->arrayAppend(result, ctx->createString(""));
        }
        return result;
    }

    std::size_t last = 0;
    std::size_t searchStart = 0;
    std::uint32_t count = 0;
    while (searchStart <= subject.size()) {
        regex::MatchResult match;
        Value* error = nullptr;
        if (!runEngine(ctx, *splitData, subject, searchStart, true, match,
                       error)) {
            return error;
        }
        if (!match.matched()) {
            if (searchStart >= subject.size()) break;
            ++searchStart;
            continue;
        }
        if (match.end == last && match.start == last) {
            if (searchStart >= subject.size()) break;
            ++searchStart;
            continue;
        }

        if (isError(ctx->arrayAppend(result,
                                     ctx->createString(slice(subject, last,
                                                             match.start)))))
            return ctx->oomValue();
        ++count;
        if (count >= limit) return result;

        for (std::size_t i = 1; i < match.captures.size(); ++i) {
            if (isError(ctx->arrayAppend(
                    result, captureValue(ctx, subject, match.captures[i]))))
                return ctx->oomValue();
            ++count;
            if (count >= limit) return result;
        }

        last = match.end;
        searchStart = match.end == match.start ? match.end + 1 : match.end;
    }

    if (count < limit) {
        if (isError(ctx->arrayAppend(
                result,
                ctx->createString(slice(subject, last, subject.size())))))
            return ctx->oomValue();
    }
    return result;
}

Value* makeIterResult(IContext* ctx, Value* value, bool done) {
    Value* result = ctx->createObject();
    if (isError(result)) return result;
    if (isError(ctx->defineProperty(result, "value", value, true, true, true)))
        return ctx->oomValue();
    if (isError(ctx->defineProperty(result, "done", ctx->createBoolean(done),
                                    true, true, true)))
        return ctx->oomValue();
    return result;
}

// ES2024 §22.2.9.1.1 %RegExpStringIteratorPrototype%.next。
Value* regexpStringIteratorNext(IContext* ctx, Value* self, size_t,
                                Value*[]) {
    if (self == nullptr ||
        self->type()->kind() != TypeKind::RegExpStringIterator) {
        return detail::typeError(
            ctx, "RegExp String Iterator.prototype.next called on "
                 "incompatible receiver");
    }
    auto* data = static_cast<RegExpStringIteratorData*>(self->data());
    if (data->done) {
        return makeIterResult(ctx, ctx->createUndefined(), true);
    }

    RegExpData* matcher = regexpData(data->regexp);
    if (matcher == nullptr) {
        data->done = true;
        return makeIterResult(ctx, ctx->createUndefined(), true);
    }

    Value* match = regExpExecValue(ctx, data->regexp, *matcher, data->string);
    if (isError(match)) return match;
    if (match->type()->kind() == TypeKind::Null) {
        data->done = true;
        return makeIterResult(ctx, ctx->createUndefined(), true);
    }

    if (data->global) {
        Value* matched = ctx->getProperty(match, "0");
        if (isError(matched)) return matched;
        if (stringData(matched)->empty()) {
            std::size_t last = 0;
            Value* error = nullptr;
            if (!readLastIndex(ctx, data->regexp, last, error)) return error;
            const std::u16string subject = toUtf16(*stringData(data->string));
            std::size_t width = 1;
            if (data->unicode && last + 1 < subject.size()) {
                const char16_t high = subject[last];
                const char16_t low = subject[last + 1];
                if (high >= 0xD800 && high <= 0xDBFF && low >= 0xDC00 &&
                    low <= 0xDFFF) {
                    width = 2;
                }
            }
            Value* advanced = setLastIndex(ctx, data->regexp, last + width);
            if (isError(advanced)) return advanced;
        }
        return makeIterResult(ctx, match, false);
    }

    data->done = true;
    return makeIterResult(ctx, match, false);
}

// ES2024 §7.3.20 SpeciesConstructor。
Value* speciesConstructor(IContext* ctx, Value* regexp) {
    Value* ctor = ctx->getProperty(regexp, "constructor");
    if (isError(ctor)) return ctor;
    if (ctor->type()->kind() == TypeKind::Undefined) return ctx->regexpCtor();
    if (!detail::isObjectKind(ctor->type()->kind())) {
        return detail::typeError(ctx, "RegExp constructor is not an object");
    }

    Value* species = ctx->getProperty(
        ctor, ctx->getWellKnownSymbol(WellKnownSymbol::Species));
    if (isError(species)) return species;
    if (species->type()->kind() == TypeKind::Null ||
        species->type()->kind() == TypeKind::Undefined) {
        return ctx->regexpCtor();
    }
    if (!detail::isConstructor(species)) {
        return detail::typeError(ctx, "RegExp species is not a constructor");
    }
    return species;
}

Value* regexpSymbolMatchAll(IContext* ctx, Value* self, size_t argc,
                            Value* argv[]) {
    RegExpData* data = regexpData(self);
    if (data == nullptr) {
        return detail::typeError(ctx,
                                 "RegExp.prototype[Symbol.matchAll] called on "
                                 "incompatible receiver");
    }

    Value* flagsValue = ctx->getProperty(self, "flags");
    if (isError(flagsValue)) return flagsValue;
    Value* flagsText = toStringArg(ctx, flagsValue);
    if (isError(flagsText)) return flagsText;
    const std::string flags = *stringData(flagsText);
    if (flags.find('g') == std::string::npos) {
        return detail::typeError(
            ctx, "String.prototype.matchAll called with a non-global RegExp");
    }

    Value* input = argc > 0 ? toStringArg(ctx, argv[0]) : undefinedOf(ctx);
    if (isError(input)) return input;

    Value* ctor = speciesConstructor(ctx, self);
    if (isError(ctor)) return ctor;

    Value* ctorArgs[2] = {self, flagsText};
    Value* matcher = ctx->construct(ctor, 2, ctorArgs);
    if (isError(matcher)) return matcher;

    std::size_t start = 0;
    Value* error = nullptr;
    if (!readLastIndex(ctx, self, start, error)) return error;
    Value* lastIndex =
        ctx->createNumber(static_cast<double>(start));
    if (isError(lastIndex)) return lastIndex;
    Value* setResult = ctx->setProperty(matcher, "lastIndex", lastIndex);
    if (isError(setResult)) return setResult;

    const bool unicode = flags.find('u') != std::string::npos ||
                         flags.find('v') != std::string::npos;
    Value* iterator =
        ctx->createRegExpStringIterator(matcher, input, true, unicode);
    if (isError(iterator)) return iterator;

    Value* next = ctx->createNativeFunction(regexpStringIteratorNext, 0);
    if (isError(next)) return next;
    ctx->setFunctionName(next, "next");
    if (isError(ctx->defineProperty(iterator, "next", next, true, false, true)))
        return ctx->oomValue();
    if (isError(ctx->defineProperty(
            iterator,
            ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
            ctx->createString("RegExp String Iterator"), false, false, true)))
        return ctx->oomValue();
    return iterator;
}

// —— String.prototype 六个方法 ——

Value* stringReceiver(IContext* ctx, Value* self) {
    if (self == nullptr || self->type()->kind() == TypeKind::Null ||
        self->type()->kind() == TypeKind::Undefined) {
        return detail::typeError(
            ctx, "String.prototype method called on null or undefined");
    }
    return toStringArg(ctx, self);
}

Value* toRegExp(IContext* ctx, Value* value, const char* flags = "") {
    if (value->type()->kind() == TypeKind::RegExp) return value;
    Value* text = toStringArg(ctx, value);
    if (isError(text)) return text;
    return ctx->createRegExp(*stringData(text), flags);
}

Value* callSymbolMethod(IContext* ctx, Value* target, WellKnownSymbol which,
                        Value* thisArg, size_t argc, Value* argv[]) {
    Value* method = ctx->getProperty(target, ctx->getWellKnownSymbol(which));
    if (isError(method)) return method;
    if (!detail::isCallable(method)) {
        return detail::typeError(ctx, "RegExp method is not callable");
    }
    return ctx->call(method, thisArg, argc, argv);
}

Value* stringMatch(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* text = stringReceiver(ctx, self);
    if (isError(text)) return text;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* regexp = toRegExp(ctx, pattern);
    if (isError(regexp)) return regexp;
    Value* args[1] = {text};
    return callSymbolMethod(ctx, regexp, WellKnownSymbol::Match, regexp, 1,
                            args);
}

Value* stringMatchAll(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* text = stringReceiver(ctx, self);
    if (isError(text)) return text;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* regexp = toRegExp(ctx, pattern, "g");
    if (isError(regexp)) return regexp;
    Value* args[1] = {text};
    return callSymbolMethod(ctx, regexp, WellKnownSymbol::MatchAll, regexp, 1,
                            args);
}

Value* stringSearch(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* text = stringReceiver(ctx, self);
    if (isError(text)) return text;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* regexp = toRegExp(ctx, pattern);
    if (isError(regexp)) return regexp;
    Value* args[1] = {text};
    return callSymbolMethod(ctx, regexp, WellKnownSymbol::Search, regexp, 1,
                            args);
}

Value* stringReplace(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* text = stringReceiver(ctx, self);
    if (isError(text)) return text;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* regexp = toRegExp(ctx, pattern);
    if (isError(regexp)) return regexp;
    Value* args[2] = {text, argc > 1 ? argv[1] : undefinedOf(ctx)};
    return callSymbolMethod(ctx, regexp, WellKnownSymbol::Replace, regexp, 2,
                            args);
}

Value* stringReplaceAll(IContext* ctx, Value* self, size_t argc,
                        Value* argv[]) {
    Value* text = stringReceiver(ctx, self);
    if (isError(text)) return text;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* regexp = toRegExp(ctx, pattern, "g");
    if (isError(regexp)) return regexp;
    RegExpData* data = regexpData(regexp);
    if (data == nullptr || !data->global) {
        return detail::typeError(
            ctx, "String.prototype.replaceAll called with a non-global RegExp");
    }
    Value* args[2] = {text, argc > 1 ? argv[1] : undefinedOf(ctx)};
    return callSymbolMethod(ctx, regexp, WellKnownSymbol::Replace, regexp, 2,
                            args);
}

Value* stringSplit(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* text = stringReceiver(ctx, self);
    if (isError(text)) return text;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* regexp = toRegExp(ctx, pattern);
    if (isError(regexp)) return regexp;
    Value* args[2] = {text, argc > 1 ? argv[1] : undefinedOf(ctx)};
    return callSymbolMethod(ctx, regexp, WellKnownSymbol::Split, regexp, 2,
                            args);
}

// —— 注册 ——

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t length) {
    Value* method = ctx->createNativeFunction(fn, length);
    if (method == ctx->oomValue()) return false;
    ctx->setFunctionName(method, name);
    return !isError(ctx->defineProperty(target, name, method, true, false,
                                        true));
}

bool defineSymbolMethod(IContext* ctx, Value* target, WellKnownSymbol which,
                        NativeFn fn, size_t length) {
    Value* method = ctx->createNativeFunction(fn, length);
    if (method == ctx->oomValue()) return false;
    ctx->setFunctionName(method, wellKnownSymbolName(which));
    return !isError(ctx->defineProperty(
        target, ctx->getWellKnownSymbol(which), method, true, false, true));
}

bool defineGetter(IContext* ctx, Value* target, const char* name,
                  NativeFn getter) {
    Value* fn = ctx->createNativeFunction(getter, 0);
    if (fn == ctx->oomValue()) return false;
    ctx->setFunctionName(fn, std::string("get ") + name);
    return !isError(ctx->defineAccessor(target, name, fn, nullptr, false,
                                        true));
}

}  // namespace

bool compileRegExp(std::string_view pattern, std::string_view flags,
                   std::shared_ptr<const neo::regex::Program>& program,
                   neo::regex::Flags& outFlags, std::string& error) {
    auto parsed = neo::regex::parseFlags(flags);
    if (!parsed.ok()) {
        error = parsed.error ? parsed.error->message : "Invalid flags";
        return false;
    }
    neo::regex::RegexEngine& engine = regexEngine();
    neo::regex::CompileResult result = engine.compile(pattern, parsed.flags);
    if (!result.ok()) {
        error = result.error ? result.error->message : "Invalid regular expression";
        return false;
    }
    program = std::move(result.program);
    outFlags = parsed.flags;
    return true;
}

bool registerRegExpBuiltins(IContext* ctx) {
    Value* proto = ctx->createObject();
    if (proto == ctx->oomValue()) return false;
    ctx->setRegExpPrototype(proto);

    Value* ctor = ctx->createNativeFunction(regexpCtor, 0);
    if (ctor == ctx->oomValue()) return false;
    ctx->setFunctionName(ctor, "RegExp");
    ctx->setFunctionConstructor(ctor, regexpConstruct);
    ctx->setRegExpCtor(ctor);

    if (isError(ctx->defineProperty(ctor, "prototype", proto, false, false,
                                    false)))
        return false;
    if (isError(ctx->defineProperty(proto, "constructor", ctor, true, false,
                                    true)))
        return false;

    if (!defineMethod(ctx, proto, "exec", regexpExec, 0)) return false;
    if (!defineMethod(ctx, proto, "test", regexpTest, 0)) return false;
    if (!defineMethod(ctx, proto, "compile", regexpCompile, 0)) return false;
    if (!defineMethod(ctx, proto, "toString", regexpToString, 0)) return false;

    if (!defineGetter(ctx, proto, "source", regexpSourceGetter)) return false;
    if (!defineGetter(ctx, proto, "flags", regexpFlagsGetter)) return false;
    if (!defineGetter(ctx, proto, "global",
                      regexpBoolGetter<&RegExpData::global>))
        return false;
    if (!defineGetter(ctx, proto, "ignoreCase",
                      regexpBoolGetter<&RegExpData::ignoreCase>))
        return false;
    if (!defineGetter(ctx, proto, "multiline",
                      regexpBoolGetter<&RegExpData::multiline>))
        return false;
    if (!defineGetter(ctx, proto, "dotAll",
                      regexpBoolGetter<&RegExpData::dotAll>))
        return false;
    if (!defineGetter(ctx, proto, "unicode",
                      regexpBoolGetter<&RegExpData::unicode>))
        return false;
    if (!defineGetter(ctx, proto, "unicodeSets",
                      regexpBoolGetter<&RegExpData::unicodeSets>))
        return false;
    if (!defineGetter(ctx, proto, "sticky",
                      regexpBoolGetter<&RegExpData::sticky>))
        return false;
    if (!defineGetter(ctx, proto, "hasIndices",
                      regexpBoolGetter<&RegExpData::hasIndices>))
        return false;

    if (!defineSymbolMethod(ctx, proto, WellKnownSymbol::Match,
                            regexpSymbolMatch, 0))
        return false;
    if (!defineSymbolMethod(ctx, proto, WellKnownSymbol::MatchAll,
                            regexpSymbolMatchAll, 0))
        return false;
    if (!defineSymbolMethod(ctx, proto, WellKnownSymbol::Search,
                            regexpSymbolSearch, 0))
        return false;
    if (!defineSymbolMethod(ctx, proto, WellKnownSymbol::Replace,
                            regexpSymbolReplace, 0))
        return false;
    if (!defineSymbolMethod(ctx, proto, WellKnownSymbol::Split,
                            regexpSymbolSplit, 0))
        return false;

    if (isError(ctx->defineProperty(
            proto, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
            ctx->createString("RegExp"), false, false, true)))
        return false;

    Value* species = ctx->createNativeFunction(regexpSpeciesGetter, 0);
    if (species == ctx->oomValue()) return false;
    ctx->setFunctionName(species, "get [Symbol.species]");
    if (isError(ctx->defineAccessor(
            ctor, ctx->getWellKnownSymbol(WellKnownSymbol::Species), species,
            nullptr, false, true)))
        return false;

    ctx->global()->add(ctor);
    ctx->global()->bind("RegExp", ctor);

    // String.prototype 六个方法
    Value* stringCtor = ctx->global()->lookup("String");
    if (stringCtor == nullptr) return false;
    Value* stringProto = ctx->getProperty(stringCtor, "prototype");
    if (isError(stringProto)) return false;
    if (!defineMethod(ctx, stringProto, "match", stringMatch, 0)) return false;
    if (!defineMethod(ctx, stringProto, "matchAll", stringMatchAll, 0))
        return false;
    if (!defineMethod(ctx, stringProto, "replace", stringReplace, 0))
        return false;
    if (!defineMethod(ctx, stringProto, "replaceAll", stringReplaceAll, 0))
        return false;
    if (!defineMethod(ctx, stringProto, "search", stringSearch, 0))
        return false;
    if (!defineMethod(ctx, stringProto, "split", stringSplit, 0)) return false;

    return true;
}

}  // namespace neo::core
