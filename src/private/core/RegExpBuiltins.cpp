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
#include "core/OperatorBehavior.h"
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

// 线程内共享的引擎实例：Program 缓存需要跨调用存活，且缓存有界。
// 多线程模型下每个 isolate 独占一个线程，thread_local 即天然隔离；
// 缓存中的 Program 是不可变 shared_ptr，即使同线程多 Context 共享也安全。
regex::RegexEngine& regexEngine() {
    static thread_local regex::RegexEngine engine(regexHost());
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
    // lastIndex 仅在 global/sticky 时参与匹配（§22.2.7.2 step 9-10）。
    if (data.global || data.sticky) {
        if (!readLastIndex(ctx, regexp, start, error)) return error;
    }
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

namespace {

// §22.2.4.1：构造路径按 NewTarget.prototype 决定实例原型（支持子类化）。
// 普通调用（construct=false）不读 new.target，避免受外层构造上下文影响。
Value* applyNewTargetPrototype(IContext* ctx, Value* regexp, bool construct) {
    if (!construct || isError(regexp)) return regexp;
    Value* newTarget = ctx->newTarget();
    if (newTarget == nullptr) return regexp;
    Value* proto = ctx->getProperty(newTarget, "prototype");
    if (isError(proto)) return proto;
    if (!detail::isObjectKind(proto->type()->kind())) return regexp;
    return ctx->setPrototype(regexp, proto);
}

}  // namespace

Value* regexpCreate(IContext* ctx, size_t argc, Value* argv[], bool construct) {
    Value* patternArg = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* flagsArg = argc > 1 ? argv[1] : undefinedOf(ctx);

    if (patternArg->type()->kind() == TypeKind::RegExp) {
        RegExpData* source = regexpData(patternArg);
        if (flagsArg->type()->kind() == TypeKind::Undefined) {
            if (!construct) return patternArg;
            Value* result = ctx->createRegExp(source->source, source->flags);
            return applyNewTargetPrototype(ctx, result, construct);
        }
        Value* flags = toStringArg(ctx, flagsArg);
        if (isError(flags)) return flags;
        Value* result = ctx->createRegExp(source->source, *stringData(flags));
        return applyNewTargetPrototype(ctx, result, construct);
    }

    Value* pattern = undefinedOf(ctx);
    if (argc > 0 && patternArg->type()->kind() != TypeKind::Undefined) {
        pattern = toStringArg(ctx, patternArg);
        if (isError(pattern)) return pattern;
    } else {
        pattern = ctx->createString("");
    }

    Value* flags = undefinedOf(ctx);
    if (argc > 1 && flagsArg->type()->kind() != TypeKind::Undefined) {
        flags = toStringArg(ctx, flagsArg);
        if (isError(flags)) return flags;
    } else {
        flags = ctx->createString("");
    }

    Value* result = ctx->createRegExp(*stringData(pattern), *stringData(flags));
    return applyNewTargetPrototype(ctx, result, construct);
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
    // §22.2.6.15：source 与 flags 均经属性读取（flags 为规范顺序且可被覆盖）。
    Value* sourceValue = ctx->getProperty(self, "source");
    if (isError(sourceValue)) return sourceValue;
    Value* sourceText = toStringArg(ctx, sourceValue);
    if (isError(sourceText)) return sourceText;
    Value* flagsValue = ctx->getProperty(self, "flags");
    if (isError(flagsValue)) return flagsValue;
    Value* flagsText = toStringArg(ctx, flagsValue);
    if (isError(flagsText)) return flagsText;
    std::string text = "/";
    text += *stringData(sourceText);
    text += "/";
    text += *stringData(flagsText);
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

    if (!data->global) return regExpExecValue(ctx, self, *data, input);

    Value* results = ctx->createArray();
    if (results == ctx->oomValue()) return results;
    if (isError(setLastIndex(ctx, self, 0))) return ctx->oomValue();

    bool any = false;
    for (;;) {
        Value* match = regExpExecValue(ctx, self, *data, input);
        if (isError(match)) return match;
        if (match->type()->kind() == TypeKind::Null) break;
        any = true;
        Value* first = ctx->getProperty(match, "0");
        if (isError(first)) return first;
        Value* firstText = toStringArg(ctx, first);
        if (isError(firstText)) return firstText;
        if (isError(ctx->arrayAppend(results, firstText))) return ctx->oomValue();
        if (stringData(firstText)->empty()) {
            std::size_t next = 0;
            Value* error = nullptr;
            if (!readLastIndex(ctx, self, next, error)) return error;
            std::size_t width = 1;
            if (data->unicode && next + 1 < subject.size()) {
                const char16_t high = subject[next];
                const char16_t low = subject[next + 1];
                if (high >= 0xD800 && high <= 0xDBFF && low >= 0xDC00 &&
                    low <= 0xDFFF) {
                    width = 2;
                }
            }
            if (isError(setLastIndex(ctx, self, next + width)))
                return ctx->oomValue();
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

    // §22.2.6.14：保存 lastIndex，非 0 时置 0，执行后恢复原值。
    Value* previous = ctx->getProperty(self, "lastIndex");
    if (isError(previous)) return previous;
    Value* zero = ctx->createNumber(0.0);
    if (isError(zero)) return zero;
    if (!detail::sameValue(previous, zero)) {
        Value* reset = ctx->setProperty(self, "lastIndex", zero);
        if (isError(reset)) return reset;
    }

    Value* match = regExpExecValue(ctx, self, *data, input);

    Value* current = ctx->getProperty(self, "lastIndex");
    if (isError(current)) return current;
    if (!detail::sameValue(current, previous)) {
        Value* restore = ctx->setProperty(self, "lastIndex", previous);
        if (isError(restore)) return restore;
    }
    if (isError(match)) return match;
    if (match->type()->kind() == TypeKind::Null) return ctx->createNumber(-1.0);
    return ctx->getProperty(match, "index");
}

Value* matchElement(IContext* ctx, Value* match, std::size_t index) {
    return ctx->getProperty(match, std::to_string(index));
}

Value* matchText(IContext* ctx, Value* match, std::size_t index) {
    Value* value = matchElement(ctx, match, index);
    if (isError(value)) return value;
    return toStringArg(ctx, value);
}

// 基于 RegExpExec 结果对象的 Get(match, "index")。
Value* matchIndexValue(IContext* ctx, Value* match, std::size_t& out) {
    Value* value = ctx->getProperty(match, "index");
    if (isError(value)) return value;
    uint32_t index = 0;
    if (Value* error = detail::toLengthValue(ctx, value, index)) return error;
    out = index;
    return nullptr;
}

Value* expandReplacementValue(IContext* ctx, const std::string& replacement,
                              const std::u16string& subject, Value* match) {
    Value* matchedValue = matchText(ctx, match, 0);
    if (isError(matchedValue)) return matchedValue;
    const std::string matched = *stringData(matchedValue);
    const std::size_t matchedLength = toUtf16(matched).size();

    std::size_t index = 0;
    if (Value* error = matchIndexValue(ctx, match, index)) return error;

    // m = 捕获组数量（match 数组长度减 1，首元素是整体匹配）。
    uint32_t matchLength = 0;
    if (Value* error = detail::lengthOf(ctx, match, matchLength)) return error;
    const std::size_t captureCount =
        matchLength > 0 ? static_cast<std::size_t>(matchLength - 1) : 0;

    std::string out;
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
            out += slice(subject, 0, index);
            ++i;
        } else if (next == '\'') {
            out += slice(subject, index + matchedLength, subject.size());
            ++i;
        } else if (next >= '0' && next <= '9') {
            // "$n" / "$nn"（nn 为 01–99）：优先两位，其次一位；
            // 无效索引（含 $0、$00）按字面量保留（Table 53）。
            std::size_t captureIndex = static_cast<std::size_t>(next - '0');
            std::size_t consumed = 1;
            bool valid = captureIndex >= 1 && captureIndex <= captureCount;
            if (i + 2 < replacement.size() && replacement[i + 2] >= '0' &&
                replacement[i + 2] <= '9') {
                const std::size_t two =
                    captureIndex * 10 +
                    static_cast<std::size_t>(replacement[i + 2] - '0');
                if (two >= 1 && two <= captureCount) {
                    captureIndex = two;
                    consumed = 2;
                    valid = true;
                }
            }
            if (!valid) {
                out += '$';
            } else {
                Value* capture = matchElement(ctx, match, captureIndex);
                if (isError(capture)) return capture;
                if (capture->type()->kind() != TypeKind::Undefined) {
                    Value* text = toStringArg(ctx, capture);
                    if (isError(text)) return text;
                    out += *stringData(text);
                }
                i += consumed;
            }
        } else if (next == '<') {
            Value* groups = ctx->getProperty(match, "groups");
            if (isError(groups)) return groups;
            // namedCaptures 为 undefined 时替换文本就是字面量 "$<"。
            if (groups->type()->kind() == TypeKind::Undefined) {
                out += "$<";
                ++i;
                continue;
            }
            const std::size_t close = replacement.find('>', i + 2);
            if (close == std::string::npos) {
                out += replacement[i];
                continue;
            }
            const std::string name = replacement.substr(i + 2, close - i - 2);
            Value* capture = ctx->getProperty(groups, name);
            if (isError(capture)) return capture;
            if (capture->type()->kind() != TypeKind::Undefined) {
                Value* text = toStringArg(ctx, capture);
                if (isError(text)) return text;
                out += *stringData(text);
            }
            i = close;
        } else {
            out += replacement[i];
        }
    }
    return ctx->createString(out);
}

Value* callReplacementValue(IContext* ctx, Value* replaceValue,
                            Value* match, Value* input) {
    uint32_t length = 0;
    if (Value* error = detail::lengthOf(ctx, match, length)) return error;

    std::vector<Value*> args;
    args.reserve(static_cast<std::size_t>(length) + 3);
    for (uint32_t i = 0; i < length; ++i) {
        Value* value = matchElement(ctx, match, i);
        if (isError(value)) return value;
        args.push_back(value);
    }
    Value* index = ctx->getProperty(match, "index");
    if (isError(index)) return index;
    args.push_back(index);
    args.push_back(input);
    Value* groups = ctx->getProperty(match, "groups");
    if (isError(groups)) return groups;
    if (groups->type()->kind() != TypeKind::Undefined) args.push_back(groups);

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

    if (data->global) {
        if (isError(setLastIndex(ctx, self, 0))) return ctx->oomValue();
    }

    std::string out;
    std::size_t last = 0;
    bool any = false;

    for (;;) {
        Value* match = regExpExecValue(ctx, self, *data, input);
        if (isError(match)) return match;
        if (match->type()->kind() == TypeKind::Null) break;
        any = true;

        std::size_t index = 0;
        if (Value* error = matchIndexValue(ctx, match, index)) return error;
        Value* matchedValue = matchText(ctx, match, 0);
        if (isError(matchedValue)) return matchedValue;
        const std::size_t matchedLength =
            toUtf16(*stringData(matchedValue)).size();

        out += slice(subject, last, index);
        if (functional) {
            Value* text = callReplacementValue(ctx, replaceValue, match, input);
            if (isError(text)) return text;
            out += *stringData(text);
        } else {
            Value* text =
                expandReplacementValue(ctx, replacement, subject, match);
            if (isError(text)) return text;
            out += *stringData(text);
        }

        last = index + matchedLength;
        if (!data->global) break;

        if (matchedLength == 0) {
            std::size_t next = 0;
            Value* error = nullptr;
            if (!readLastIndex(ctx, self, next, error)) return error;
            std::size_t width = 1;
            if (data->unicode && next + 1 < subject.size()) {
                const char16_t first = subject[next];
                const char16_t second = subject[next + 1];
                if (first >= 0xD800 && first <= 0xDBFF && second >= 0xDC00 &&
                    second <= 0xDFFF) {
                    width = 2;
                }
            }
            if (isError(setLastIndex(ctx, self, next + width)))
                return ctx->oomValue();
        }
    }

    if (!any) return input;
    out += slice(subject, last, subject.size());
    return ctx->createString(out);
}

Value* speciesConstructor(IContext* ctx, Value* regexp);

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
    // limit 为 undefined 时取 2^32-1；否则 ToUint32（NaN → 0）。
    if (argc > 1 && argv[1]->type()->kind() != TypeKind::Undefined) {
        Value* number = detail::toNumberValue(ctx, argv[1]);
        if (isError(number)) return number;
        limit = detail::toUint32(
            static_cast<NumberData*>(number->data())->value);
    }
    if (limit == 0) return ctx->createArray();

    Value* result = ctx->createArray();
    if (result == ctx->oomValue()) return result;

    // §22.2.6.14：SpeciesConstructor + flags 追加 "y" 的 sticky 克隆。
    Value* flagsValue = ctx->getProperty(self, "flags");
    if (isError(flagsValue)) return flagsValue;
    Value* flagsText = toStringArg(ctx, flagsValue);
    if (isError(flagsText)) return flagsText;
    const std::string flags = *stringData(flagsText);
    const bool unicode = flags.find('u') != std::string::npos ||
                         flags.find('v') != std::string::npos;
    const std::string newFlags =
        flags.find('y') == std::string::npos ? flags + "y" : flags;

    Value* ctor = speciesConstructor(ctx, self);
    if (isError(ctor)) return ctor;
    Value* ctorArgs[2] = {self, ctx->createString(newFlags)};
    if (isError(ctorArgs[1])) return ctorArgs[1];
    Value* splitter = ctx->construct(ctor, 2, ctorArgs);
    if (isError(splitter)) return splitter;
    RegExpData* splitData = regexpData(splitter);
    if (splitData == nullptr) {
        return detail::typeError(ctx,
                                 "RegExp species must return a RegExp object");
    }

    auto advance = [&subject, unicode](std::size_t index) {
        if (!unicode || index + 1 >= subject.size()) return index + 1;
        const char16_t high = subject[index];
        const char16_t low = subject[index + 1];
        if (high >= 0xD800 && high <= 0xDBFF && low >= 0xDC00 &&
            low <= 0xDFFF) {
            return index + 2;
        }
        return index + 1;
    };

    if (subject.empty()) {
        Value* match = regExpExecValue(ctx, splitter, *splitData, input);
        if (isError(match)) return match;
        if (match->type()->kind() != TypeKind::Null) return result;
        if (isError(ctx->arrayAppend(result, ctx->createString(""))))
            return ctx->oomValue();
        return result;
    }

    std::size_t p = 0;
    std::size_t q = 0;
    std::uint32_t count = 0;
    while (q < subject.size()) {
        Value* setResult = ctx->setProperty(
            splitter, "lastIndex",
            ctx->createNumber(static_cast<double>(q)));
        if (isError(setResult)) return setResult;

        Value* match = regExpExecValue(ctx, splitter, *splitData, input);
        if (isError(match)) return match;
        if (match->type()->kind() == TypeKind::Null) {
            q = advance(q);
            continue;
        }

        std::size_t end = 0;
        Value* error = nullptr;
        if (!readLastIndex(ctx, splitter, end, error)) return error;
        if (end > subject.size()) end = subject.size();
        if (end == p) {
            q = advance(q);
            continue;
        }

        if (isError(ctx->arrayAppend(
                result, ctx->createString(slice(subject, p, q)))))
            return ctx->oomValue();
        ++count;
        if (count >= limit) return result;
        p = end;

        uint32_t captures = 0;
        if (Value* lengthError = detail::lengthOf(ctx, match, captures))
            return lengthError;
        captures = captures > 0 ? captures - 1 : 0;
        for (uint32_t i = 1; i <= captures; ++i) {
            Value* capture = matchElement(ctx, match, i);
            if (isError(capture)) return capture;
            if (isError(ctx->arrayAppend(result, capture)))
                return ctx->oomValue();
            ++count;
            if (count >= limit) return result;
        }
        q = p;
    }

    if (count < limit) {
        if (isError(ctx->arrayAppend(
                result,
                ctx->createString(slice(subject, p, subject.size())))))
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

// RequireObjectCoercible：仅检查 null/undefined，不做 ToString。
Value* requireObjectCoercible(IContext* ctx, Value* self) {
    if (self == nullptr || self->type()->kind() == TypeKind::Null ||
        self->type()->kind() == TypeKind::Undefined) {
        return detail::typeError(
            ctx, "String.prototype method called on null or undefined");
    }
    return nullptr;
}

// IsRegExp（§7.2.8）：对象先读 @@match，非 undefined 则 ToBoolean；
// 否则看是否 [[RegExpMatcher]]（RegExp 类型）。返回错误或 nullptr。
Value* isRegExpValue(IContext* ctx, Value* value, bool& result) {
    result = false;
    if (!detail::isObjectKind(value->type()->kind())) return nullptr;
    Value* matcher =
        ctx->getProperty(value, ctx->getWellKnownSymbol(WellKnownSymbol::Match));
    if (isError(matcher)) return matcher;
    if (matcher->type()->kind() != TypeKind::Undefined) {
        result = detail::toBoolean(matcher);
        return nullptr;
    }
    result = value->type()->kind() == TypeKind::RegExp;
    return nullptr;
}

Value* toRegExp(IContext* ctx, Value* value, const char* flags = "") {
    if (value->type()->kind() == TypeKind::RegExp) return value;
    // new RegExp(undefined) 等价于空模式（§22.2.3.1）。
    if (value->type()->kind() == TypeKind::Undefined) {
        return ctx->createRegExp("", flags);
    }
    Value* text = toStringArg(ctx, value);
    if (isError(text)) return text;
    return ctx->createRegExp(*stringData(text), flags);
}

// split/replace 的字符串分隔符按字面量处理：把正则语法字符转义后再建 RegExp。
std::string escapeLiteralPattern(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
            case '^':
            case '$':
            case '\\':
            case '.':
            case '*':
            case '+':
            case '?':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
            case '|':
            case '/':
                out.push_back('\\');
                break;
            default:
                break;
        }
        out.push_back(ch);
    }
    return out;
}

// replace/replaceAll 的字符串搜索路径：即使 searchValue 是 RegExp 对象
// （其 @@replace 被置为 undefined），也按 ToString(searchValue) 的字面量匹配。
Value* toLiteralRegExp(IContext* ctx, Value* value, const char* flags = "") {
    Value* text = toStringArg(ctx, value);
    if (isError(text)) return text;
    return ctx->createRegExp(escapeLiteralPattern(*stringData(text)), flags);
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

// GetMethod(target, @@which)：undefined/null → 无方法；非可调用 → TypeError。
// 成功返回 nullptr 并写 method（nullptr 表示无方法）。
Value* getSymbolMethod(IContext* ctx, Value* target, WellKnownSymbol which,
                       Value*& method) {
    method = nullptr;
    Value* value = ctx->getProperty(target, ctx->getWellKnownSymbol(which));
    if (isError(value)) return value;
    TypeKind kind = value->type()->kind();
    if (kind == TypeKind::Undefined || kind == TypeKind::Null) return nullptr;
    if (!detail::isCallable(value)) {
        return detail::typeError(ctx, "Symbol method is not callable");
    }
    method = value;
    return nullptr;
}

// 若 pattern 提供 @@which 方法则调用并返回其结果（§22.1.3 各方法步骤 3）。
// handled=false 表示无方法，调用方走默认 RegExp 路径。
Value* trySymbolProtocol(IContext* ctx, Value* pattern, WellKnownSymbol which,
                         size_t argc, Value* argv[], bool& handled) {
    handled = false;
    TypeKind kind = pattern->type()->kind();
    if (kind == TypeKind::Undefined || kind == TypeKind::Null) return nullptr;
    // 原始值没有 @@ 方法（不装箱读取原型上的 getter）。
    if (!detail::isObjectKind(kind)) return nullptr;
    Value* method = nullptr;
    if (Value* err = getSymbolMethod(ctx, pattern, which, method)) {
        handled = true;  // 错误同样终止默认路径
        return err;
    }
    if (method == nullptr) return nullptr;
    handled = true;
    return ctx->call(method, pattern, argc, argv);
}

Value* stringMatch(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    // §22.1.3.12：RequireObjectCoercible → GetMethod(@@match) → ToString(O)
    // → RegExpCreate(regexp) → Invoke(rx, @@match, «S»)。
    if (Value* err = requireObjectCoercible(ctx, self)) return err;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    if (pattern->type()->kind() != TypeKind::Undefined &&
        pattern->type()->kind() != TypeKind::Null) {
        Value* protocolArgs[1] = {self};
        bool handled = false;
        Value* result = trySymbolProtocol(ctx, pattern, WellKnownSymbol::Match, 1,
                                          protocolArgs, handled);
        if (handled) return result;
    }
    Value* text = toStringArg(ctx, self);
    if (isError(text)) return text;
    Value* regexp = toRegExp(ctx, pattern);
    if (isError(regexp)) return regexp;
    Value* args[1] = {text};
    return callSymbolMethod(ctx, regexp, WellKnownSymbol::Match, regexp, 1,
                            args);
}

Value* stringMatchAll(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    // §22.1.3.13：RequireObjectCoercible → GetMethod(@@matchAll) → ToString(O)
    // → RegExpCreate(regexp, "g") → Invoke(rx, @@matchAll, «S»)。
    if (Value* err = requireObjectCoercible(ctx, self)) return err;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    if (pattern->type()->kind() != TypeKind::Undefined &&
        pattern->type()->kind() != TypeKind::Null) {
        Value* protocolArgs[1] = {self};
        bool handled = false;
        Value* result = trySymbolProtocol(ctx, pattern, WellKnownSymbol::MatchAll, 1,
                                          protocolArgs, handled);
        if (handled) return result;
    }
    Value* text = toStringArg(ctx, self);
    if (isError(text)) return text;
    // RegExpCreate(regexp, "g")：RegExp 对象按 source 重建并强制 g 标志。
    Value* regexp = nullptr;
    if (pattern->type()->kind() == TypeKind::RegExp) {
        RegExpData* source = regexpData(pattern);
        regexp = ctx->createRegExp(source->source, "g");
    } else {
        regexp = toRegExp(ctx, pattern, "g");
    }
    if (isError(regexp)) return regexp;
    Value* args[1] = {text};
    return callSymbolMethod(ctx, regexp, WellKnownSymbol::MatchAll, regexp, 1,
                            args);
}

Value* stringSearch(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    // §22.1.3.18：RequireObjectCoercible → GetMethod(@@search) → ToString(O)
    // → RegExpCreate(regexp) → Invoke(rx, @@search, «S»)。
    if (Value* err = requireObjectCoercible(ctx, self)) return err;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    if (pattern->type()->kind() != TypeKind::Undefined &&
        pattern->type()->kind() != TypeKind::Null) {
        Value* protocolArgs[1] = {self};
        bool handled = false;
        Value* result = trySymbolProtocol(ctx, pattern, WellKnownSymbol::Search, 1,
                                          protocolArgs, handled);
        if (handled) return result;
    }
    Value* text = toStringArg(ctx, self);
    if (isError(text)) return text;
    Value* regexp = toRegExp(ctx, pattern);
    if (isError(regexp)) return regexp;
    Value* args[1] = {text};
    return callSymbolMethod(ctx, regexp, WellKnownSymbol::Search, regexp, 1,
                            args);
}

Value* stringReplace(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    // §22.1.3.19：RequireObjectCoercible → GetMethod(@@replace)（以原 O 调用）
    // → ToString(O) → 字符串搜索路径。
    if (Value* err = requireObjectCoercible(ctx, self)) return err;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* replaceValue = argc > 1 ? argv[1] : undefinedOf(ctx);
    if (pattern->type()->kind() != TypeKind::Undefined &&
        pattern->type()->kind() != TypeKind::Null) {
        Value* protocolArgs[2] = {self, replaceValue};
        bool handled = false;
        Value* result = trySymbolProtocol(ctx, pattern, WellKnownSymbol::Replace,
                                          2, protocolArgs, handled);
        if (handled) return result;
    }
    Value* text = toStringArg(ctx, self);
    if (isError(text)) return text;
    Value* args[2] = {text, replaceValue};
    Value* regexp = toLiteralRegExp(ctx, pattern);
    if (isError(regexp)) return regexp;
    return callSymbolMethod(ctx, regexp, WellKnownSymbol::Replace, regexp, 2,
                            args);
}

Value* stringReplaceAll(IContext* ctx, Value* self, size_t argc,
                        Value* argv[]) {
    // 步骤 1：RequireObjectCoercible(this)，此时不做 ToString。
    if (Value* err = requireObjectCoercible(ctx, self)) return err;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    // 步骤 2：searchValue 非 undefined/null 且 IsRegExp 时，flags 必须可强制
    // 转换且包含 "g"，否则抛 TypeError（§22.1.3.20）。
    if (pattern->type()->kind() != TypeKind::Undefined &&
        pattern->type()->kind() != TypeKind::Null) {
        bool regexp = false;
        if (Value* err = isRegExpValue(ctx, pattern, regexp)) return err;
        if (regexp) {
            Value* flags = ctx->getProperty(pattern, "flags");
            if (isError(flags)) return flags;
            if (flags->type()->kind() == TypeKind::Undefined ||
                flags->type()->kind() == TypeKind::Null) {
                return detail::typeError(
                    ctx, "String.prototype.replaceAll flags is not coercible");
            }
            Value* flagText = toStringArg(ctx, flags);
            if (isError(flagText)) return flagText;
            if (stringData(flagText)->find('g') == std::string::npos) {
                return detail::typeError(
                    ctx,
                    "String.prototype.replaceAll called with a non-global RegExp");
            }
        }
    }
    // 步骤 2.c-d：GetMethod(@@replace) 以原 O 调用，先于 ToString(O)。
    Value* replaceValue = argc > 1 ? argv[1] : undefinedOf(ctx);
    if (pattern->type()->kind() != TypeKind::Undefined &&
        pattern->type()->kind() != TypeKind::Null) {
        Value* protocolArgs[2] = {self, replaceValue};
        bool handled = false;
        Value* result = trySymbolProtocol(ctx, pattern, WellKnownSymbol::Replace,
                                          2, protocolArgs, handled);
        if (handled) return result;
    }
    // 步骤 3：S = ToString(O)。
    Value* text = toStringArg(ctx, self);
    if (isError(text)) return text;
    Value* args[2] = {text, replaceValue};
    Value* regexp = toLiteralRegExp(ctx, pattern, "g");
    if (isError(regexp)) return regexp;
    return callSymbolMethod(ctx, regexp, WellKnownSymbol::Replace, regexp, 2,
                            args);
}

Value* stringSplit(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    // 步骤 1：RequireObjectCoercible(this)，此时不做 ToString。
    if (Value* err = requireObjectCoercible(ctx, self)) return err;
    Value* pattern = argc > 0 ? argv[0] : undefinedOf(ctx);
    Value* limitArg = argc > 1 ? argv[1] : undefinedOf(ctx);
    // 步骤 2：separator 非 undefined/null 时优先 @@split，thisArg/首参是原 O。
    Value* args[2] = {self, limitArg};
    bool handled = false;
    Value* protocolResult = trySymbolProtocol(
        ctx, pattern, WellKnownSymbol::Split, 2, args, handled);
    if (handled) return protocolResult;

    // 步骤 3：S = ToString(O)，先于 limit 与 separator 的强制转换。
    Value* text = toStringArg(ctx, self);
    if (isError(text)) return text;
    // 步骤 4-6：ToUint32(limit) 先于 ToString(separator)，lim=0 返回空数组。
    std::uint32_t limit = 0xFFFFFFFFu;
    if (limitArg->type()->kind() != TypeKind::Undefined) {
        Value* number = detail::toNumberValue(ctx, limitArg);
        if (isError(number)) return number;
        limit = detail::toUint32(
            static_cast<NumberData*>(number->data())->value);
    }
    Value* sepValue = toStringArg(ctx, pattern);
    if (isError(sepValue)) return sepValue;
    if (limit == 0) return ctx->createArray();
    // 步骤 7：separator 为 undefined 时返回只含 S 的数组。
    if (pattern->type()->kind() == TypeKind::Undefined) {
        Value* result = ctx->createArray();
        if (isError(result)) return result;
        Value* item = ctx->createString(*stringData(text));
        if (isError(item)) return item;
        if (isError(ctx->arrayAppend(result, item))) return ctx->oomValue();
        return result;
    }

    const std::u16string subject = toUtf16(*stringData(text));
    const std::u16string sep = toUtf16(*stringData(sepValue));

    Value* result = ctx->createArray();
    if (isError(result)) return result;
    std::uint32_t count = 0;
    Value* appendErr = nullptr;
    auto append = [&](std::size_t begin, std::size_t end) {
        if (count >= limit) return false;
        Value* item = ctx->createString(
            fromUtf16(std::u16string_view(subject).substr(begin, end - begin)));
        if (isError(item)) {
            appendErr = item;
            return false;
        }
        if (isError(ctx->arrayAppend(result, item))) {
            appendErr = ctx->oomValue();
            return false;
        }
        ++count;
        return true;
    };

    if (sep.empty()) {
        // 空分隔符按码点切分（代理对算一个字符）。
        std::size_t i = 0;
        while (i < subject.size()) {
            std::size_t next = i + 1;
            if (subject[i] >= 0xD800 && subject[i] <= 0xDBFF &&
                next < subject.size() && subject[next] >= 0xDC00 &&
                subject[next] <= 0xDFFF) {
                next += 1;
            }
            if (!append(i, next)) break;
            i = next;
        }
    } else {
        std::size_t p = 0;
        while (count < limit) {
            std::size_t q = subject.find(sep, p);
            if (q == std::u16string::npos) break;
            if (!append(p, q)) break;
            p = q + sep.size();
        }
        if (appendErr == nullptr && count < limit) append(p, subject.size());
    }
    if (appendErr != nullptr) return appendErr;
    return result;
}

// ES2025 §22.2.5.1 RegExp.escape：把字符串转义为可安全嵌入 Pattern 的形式。
namespace {

bool isSyntaxCharacter(std::uint32_t cp) {
    switch (cp) {
        case '^':
        case '$':
        case '\\':
        case '.':
        case '*':
        case '+':
        case '?':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '|':
            return true;
        default:
            return false;
    }
}

bool isRegExpEscapeWhiteSpace(std::uint32_t cp) {
    if (cp == 0x20 || cp == 0xA0 || cp == 0xFEFF || cp == 0x1680 ||
        cp == 0x202F || cp == 0x205F || cp == 0x3000) {
        return true;
    }
    return cp >= 0x2000 && cp <= 0x200A;
}

bool isRegExpEscapeLineTerminator(std::uint32_t cp) {
    return cp == 0x0A || cp == 0x0D || cp == 0x2028 || cp == 0x2029;
}

bool isOtherPunctuator(std::uint32_t cp) {
    switch (cp) {
        case ',':
        case '-':
        case '=':
        case '<':
        case '>':
        case '#':
        case '&':
        case '!':
        case '%':
        case ':':
        case ';':
        case '@':
        case '~':
        case '\'':
        case '`':
        case '"':
            return true;
        default:
            return false;
    }
}

void appendHex(std::uint32_t value, int width, std::string& out) {
    static constexpr char kDigits[] = "0123456789abcdef";
    for (int shift = (width - 1) * 4; shift >= 0; shift -= 4) {
        out.push_back(kDigits[(value >> static_cast<unsigned>(shift)) & 0xFu]);
    }
}

void appendUnicodeEscape(std::uint32_t codeUnit, std::string& out) {
    out += "\\u";
    appendHex(codeUnit, 4, out);
}

// §22.2.5.1.1 EncodeForRegExpEscape。
void encodeForRegExpEscape(std::uint32_t cp, std::string& out) {
    if (isSyntaxCharacter(cp) || cp == '/') {
        out.push_back('\\');
        unicode::encodeCodePoint(cp, out);
        return;
    }
    switch (cp) {
        case 0x09:
            out += "\\t";
            return;
        case 0x0A:
            out += "\\n";
            return;
        case 0x0B:
            out += "\\v";
            return;
        case 0x0C:
            out += "\\f";
            return;
        case 0x0D:
            out += "\\r";
            return;
        default:
            break;
    }
    if (isOtherPunctuator(cp) || isRegExpEscapeWhiteSpace(cp) ||
        isRegExpEscapeLineTerminator(cp) || unicode::isHighSurrogate(cp) ||
        unicode::isLowSurrogate(cp)) {
        if (cp <= 0xFF) {
            out += "\\x";
            appendHex(cp, 2, out);
        } else if (cp <= 0xFFFF) {
            appendUnicodeEscape(cp, out);
        } else {
            const std::uint32_t high = 0xD800u + ((cp - 0x10000u) >> 10);
            const std::uint32_t low = 0xDC00u + ((cp - 0x10000u) & 0x3FFu);
            appendUnicodeEscape(high, out);
            appendUnicodeEscape(low, out);
        }
        return;
    }
    unicode::encodeCodePoint(cp, out);
}

Value* regexpEscape(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* input = argc > 0 ? argv[0] : undefinedOf(ctx);
    if (input->type()->kind() != TypeKind::String) {
        return detail::typeError(ctx, "RegExp.escape requires a string");
    }
    const std::string& text = static_cast<StringData*>(input->data())->value;
    std::string escaped;
    escaped.reserve(text.size());
    bool leading = true;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = i;
        std::uint32_t cp = 0;
        if (!unicode::decodeCodePoint(text, i, cp)) {
            // 非法字节：按原样保留并前进一个字节。
            escaped.push_back(text[start]);
            i = start + 1;
            leading = false;
            continue;
        }
        const bool asciiAlnum =
            (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') ||
            (cp >= 'a' && cp <= 'z');
        if (leading && asciiAlnum) {
            // 首个 ASCII 字母/数字用 \xHH，避免与前置 \c / \0 / \1 连读。
            escaped += "\\x";
            appendHex(cp, 2, escaped);
        } else {
            encodeForRegExpEscape(cp, escaped);
        }
        leading = false;
    }
    return ctx->createString(escaped);
}

}  // namespace

// —— 注册 ——

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t length) {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* method = ctx->createNativeFunction(fn, length, {}, opts);
    if (method == ctx->oomValue()) return false;
    ctx->setFunctionName(method, name);
    return !isError(ctx->defineProperty(target, name, method, true, false,
                                        true));
}

// String.prototype 方法：length 是规范属性值，不是最小实参数（缺参合法）。
bool defineStringMethod(IContext* ctx, Value* target, const char* name,
                        NativeFn fn, size_t length) {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* method = ctx->createNativeFunction(fn, 0, {}, opts);
    if (method == ctx->oomValue()) return false;
    ctx->setFunctionName(method, name);
    Value* len = ctx->createNumber(static_cast<double>(length));
    if (len == ctx->oomValue()) return false;
    if (isError(ctx->defineProperty(method, "length", len, false, false, true))) {
        return false;
    }
    return !isError(ctx->defineProperty(target, name, method, true, false,
                                        true));
}

bool defineSymbolMethod(IContext* ctx, Value* target, WellKnownSymbol which,
                        NativeFn fn, size_t length) {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* method = ctx->createNativeFunction(fn, length, {}, opts);
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

    if (!defineMethod(ctx, ctor, "escape", regexpEscape, 1)) return false;

    ctx->defineGlobal("RegExp", ctor, true, false, true);

    // String.prototype 六个方法
    Value* stringCtor = ctx->global()->lookup("String");
    if (stringCtor == nullptr) return false;
    Value* stringProto = ctx->getProperty(stringCtor, "prototype");
    if (isError(stringProto)) return false;
    if (!defineStringMethod(ctx, stringProto, "match", stringMatch, 1))
        return false;
    if (!defineStringMethod(ctx, stringProto, "matchAll", stringMatchAll, 1))
        return false;
    if (!defineStringMethod(ctx, stringProto, "replace", stringReplace, 2))
        return false;
    if (!defineStringMethod(ctx, stringProto, "replaceAll", stringReplaceAll, 2))
        return false;
    if (!defineStringMethod(ctx, stringProto, "search", stringSearch, 1))
        return false;
    if (!defineStringMethod(ctx, stringProto, "split", stringSplit, 2))
        return false;

    return true;
}

}  // namespace neo::core
