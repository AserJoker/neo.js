// GlobalFunctionBuiltins.cpp - 全局函数（ES2024 §19.2）
//
// 设计要点：
//   - parseInt/parseFloat 手写文法，不把 strtod 的宽松接受集泄漏给 JS；
//   - URI 编解码按码点处理，百分号序列严格 UTF-8 校验；
//   - 所有参数先 ToString/ToNumber，错误值原样透传。
//
// 关联 ADR: 020（标准库组织）

#include "core/GlobalFunctionBuiltins.h"

#include "core/ErrorKind.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/OperatorBehavior.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "unicode/Utf8.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neo::core {

namespace {

double nanValue() { return std::numeric_limits<double>::quiet_NaN(); }

bool isAsciiWhitespace(unsigned char c) {
    return c == 0x09 || c == 0x0A || c == 0x0B || c == 0x0C || c == 0x0D ||
           c == 0x20;
}

bool isUnicodeWhitespace(uint32_t cp) {
    if (cp >= 0x2000 && cp <= 0x200A) return true;
    switch (cp) {
        case 0x00A0:
        case 0x1680:
        case 0x2028:
        case 0x2029:
        case 0x202F:
        case 0x205F:
        case 0x3000:
        case 0xFEFF:
            return true;
        default:
            return false;
    }
}

// 跳过前导 StrWhiteSpace（§11.2 / §11.3）。
size_t skipWhitespace(std::string_view s, size_t i) {
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            if (!isAsciiWhitespace(c)) break;
            ++i;
            continue;
        }
        size_t j = i;
        uint32_t cp = 0;
        if (!unicode::decodeCodePoint(s, j, cp) || !isUnicodeWhitespace(cp)) break;
        i = j;
    }
    return i;
}

int digitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

Value* stringArg(IContext* ctx, size_t argc, Value* argv[], size_t i) {
    Value* v = i < argc ? argv[i] : ctx->createUndefined();
    return ctx->toString(v);
}

// parseInt（ES2024 §19.2.5）。
Value* globalParseInt(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* text = stringArg(ctx, argc, argv, 0);
    if (detail::isErrorValue(text)) return text;
    std::string_view s = static_cast<StringData*>(text->data())->value;

    int radix = 0;
    if (argc > 1) {
        Value* r = ctx->toNumber(argv[1]);
        if (detail::isErrorValue(r)) return r;
        radix = detail::toInt32(static_cast<NumberData*>(r->data())->value);
    }

    size_t i = skipWhitespace(s, 0);
    bool negative = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        negative = s[i] == '-';
        ++i;
    }

    if (radix != 0) {
        if (radix < 2 || radix > 36) return ctx->createNumber(nanValue());
    } else {
        radix = 10;
    }
    const bool hasHexPrefix = i + 1 < s.size() && s[i] == '0' &&
                              (s[i + 1] == 'x' || s[i + 1] == 'X');
    // §19.2.5 step 13：默认基数遇到 0x 前缀时提升为 16。
    if (radix == 10 && hasHexPrefix) radix = 16;
    if (radix == 16 && hasHexPrefix) i += 2;

    double value = 0.0;
    bool any = false;
    while (i < s.size()) {
        int d = digitValue(s[i]);
        if (d < 0 || d >= radix) break;
        value = value * static_cast<double>(radix) + static_cast<double>(d);
        any = true;
        ++i;
    }
    if (!any) return ctx->createNumber(nanValue());
    return ctx->createNumber(negative ? -value : value);
}

// parseFloat（ES2024 §19.2.4）：只消费最长 StrDecimalLiteral 前缀。
Value* globalParseFloat(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* text = stringArg(ctx, argc, argv, 0);
    if (detail::isErrorValue(text)) return text;
    std::string_view s = static_cast<StringData*>(text->data())->value;

    size_t i = skipWhitespace(s, 0);
    size_t start = i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;

    // Infinity 精确匹配（大小写敏感，§19.2.4）。
    if (s.compare(i, 8, "Infinity") == 0) {
        bool negative = i > start && s[start] == '-';
        return ctx->createNumber(negative ? -std::numeric_limits<double>::infinity()
                                          : std::numeric_limits<double>::infinity());
    }

    size_t digitsStart = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    bool hasDigits = i > digitsStart;
    if (i < s.size() && s[i] == '.') {
        ++i;
        size_t fracStart = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        hasDigits = hasDigits || i > fracStart;
    }
    if (!hasDigits) return ctx->createNumber(nanValue());

    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        size_t j = i + 1;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
        size_t expStart = j;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
        if (j > expStart) i = j;
    }

    // 前缀已按文法校验，strtod 只负责数值舍入（含溢出到 ±Infinity）。
    std::string literal(s.substr(start, i - start));
    return ctx->createNumber(std::strtod(literal.c_str(), nullptr));
}

// isNaN（ES2024 §19.2.3）。
Value* globalIsNaN(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* n = ctx->toNumber(argc > 0 ? argv[0] : ctx->createUndefined());
    if (detail::isErrorValue(n)) return n;
    return ctx->createBoolean(std::isnan(static_cast<NumberData*>(n->data())->value));
}

// isFinite（ES2024 §19.2.2）。
Value* globalIsFinite(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* n = ctx->toNumber(argc > 0 ? argv[0] : ctx->createUndefined());
    if (detail::isErrorValue(n)) return n;
    return ctx->createBoolean(std::isfinite(static_cast<NumberData*>(n->data())->value));
}

// —— URI 编解码（ES2024 §19.2.6）——

bool isUnreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
           c == '~' || c == '!' || c == '*' || c == '\'' || c == '(' ||
           c == ')';
}

bool isReserved(unsigned char c) {
    switch (c) {
        case ';':
        case '/':
        case '?':
        case ':':
        case '@':
        case '&':
        case '=':
        case '+':
        case '$':
        case ',':
        case '#':
            return true;
        default:
            return false;
    }
}

Value* uriError(IContext* ctx) {
    return detail::errorOf(ctx, ErrorKind::URIError, "URI malformed");
}

void appendPercent(unsigned char byte, std::string& out) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    out.push_back('%');
    out.push_back(kHex[byte >> 4]);
    out.push_back(kHex[byte & 0x0F]);
}

// encodeURI / encodeURIComponent 共用；keepReserved 为 true 时保留保留字符。
Value* encodeUri(IContext* ctx, size_t argc, Value* argv[], bool keepReserved) {
    Value* text = stringArg(ctx, argc, argv, 0);
    if (detail::isErrorValue(text)) return text;
    std::string_view s = static_cast<StringData*>(text->data())->value;

    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        size_t j = i;
        uint32_t cp = 0;
        if (!unicode::decodeCodePoint(s, j, cp)) return uriError(ctx);
        i = j;
        if (cp < 0x80) {
            unsigned char c = static_cast<unsigned char>(cp);
            if (isUnreserved(c) || (keepReserved && isReserved(c))) {
                out.push_back(static_cast<char>(c));
            } else {
                appendPercent(c, out);
            }
            continue;
        }
        if (unicode::isHighSurrogate(cp) || unicode::isLowSurrogate(cp)) {
            return uriError(ctx);
        }
        std::string bytes;
        unicode::encodeCodePoint(cp, bytes);
        for (char ch : bytes) {
            appendPercent(static_cast<unsigned char>(ch), out);
        }
    }
    return ctx->createString(out);
}

Value* globalEncodeUri(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return encodeUri(ctx, argc, argv, /*keepReserved=*/true);
}

Value* globalEncodeUriComponent(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return encodeUri(ctx, argc, argv, /*keepReserved=*/false);
}

// 严格 UTF-8 解码（拒绝 overlong、代理、越界、截断）。
bool decodeUtf8Strict(const std::string& bytes, std::vector<uint32_t>& out) {
    size_t i = 0;
    while (i < bytes.size()) {
        unsigned char b0 = static_cast<unsigned char>(bytes[i]);
        if (b0 < 0x80) {
            out.push_back(b0);
            ++i;
            continue;
        }
        size_t len = 0;
        uint32_t cp = 0;
        if (b0 >= 0xC2 && b0 <= 0xDF) {
            len = 2;
            cp = b0 & 0x1Fu;
        } else if (b0 >= 0xE0 && b0 <= 0xEF) {
            len = 3;
            cp = b0 & 0x0Fu;
        } else if (b0 >= 0xF0 && b0 <= 0xF4) {
            len = 4;
            cp = b0 & 0x07u;
        } else {
            return false;
        }
        if (i + len > bytes.size()) return false;
        for (size_t k = 1; k < len; ++k) {
            unsigned char b = static_cast<unsigned char>(bytes[i + k]);
            if ((b & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (b & 0x3Fu);
        }
        // 最短编码与代理区间检查。
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
            (len == 4 && cp < 0x10000)) {
            return false;
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        if (cp > 0x10FFFF) return false;
        out.push_back(cp);
        i += len;
    }
    return true;
}

int hexValue(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// decodeURI / decodeURIComponent 共用；preserveReserved 为 true 时保留
// 解码后属于保留字符的原始百分号序列。
Value* decodeUri(IContext* ctx, size_t argc, Value* argv[], bool preserveReserved) {
    Value* text = stringArg(ctx, argc, argv, 0);
    if (detail::isErrorValue(text)) return text;
    std::string_view s = static_cast<StringData*>(text->data())->value;

    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] != '%') {
            out.push_back(s[i]);
            ++i;
            continue;
        }
        std::string bytes;
        std::string original;
        while (i < s.size() && s[i] == '%') {
            if (i + 2 >= s.size()) return uriError(ctx);
            int hi = hexValue(static_cast<unsigned char>(s[i + 1]));
            int lo = hexValue(static_cast<unsigned char>(s[i + 2]));
            if (hi < 0 || lo < 0) return uriError(ctx);
            bytes.push_back(static_cast<char>((hi << 4) | lo));
            original.append(s.substr(i, 3));
            i += 3;
        }

        std::vector<uint32_t> cps;
        if (!decodeUtf8Strict(bytes, cps)) return uriError(ctx);

        // 逐码点输出；保留字符回写原始转义（每个字节占 3 个字符）。
        size_t byteOffset = 0;
        for (uint32_t cp : cps) {
            size_t byteLen = 1;
            if (cp >= 0x80) byteLen = cp >= 0x800 ? (cp >= 0x10000 ? 4 : 3) : 2;
            if (preserveReserved && cp < 0x80 &&
                isReserved(static_cast<unsigned char>(cp))) {
                out.append(original.substr(byteOffset * 3, byteLen * 3));
            } else {
                unicode::encodeCodePoint(cp, out);
            }
            byteOffset += byteLen;
        }
    }
    return ctx->createString(out);
}

Value* globalDecodeUri(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return decodeUri(ctx, argc, argv, /*preserveReserved=*/true);
}

Value* globalDecodeUriComponent(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return decodeUri(ctx, argc, argv, /*preserveReserved=*/false);
}

// 定义全局函数（writable/不可枚举/可配置）。
bool defineGlobalFunction(IContext* ctx, const char* name, NativeFn fn,
                          size_t minArgc, int lengthOverride) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    // 全局函数均不是构造器（ES2024 §19.2.4 等）。
    ctx->setFunctionConstructible(f, false);
    ctx->setFunctionName(f, name);
    if (lengthOverride >= 0) {
        Value* len = ctx->createNumber(static_cast<double>(lengthOverride));
        if (len == ctx->oomValue()) return false;
        if (ctx->defineProperty(f, "length", len, false, false, true)->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }
    ctx->defineGlobal(name, f, true, false, true);
    return true;
}

}  // namespace

bool registerGlobalFunctionBuiltins(IContext* ctx) {
    if (!defineGlobalFunction(ctx, "parseInt", globalParseInt, 0, 2)) return false;
    if (!defineGlobalFunction(ctx, "parseFloat", globalParseFloat, 0, 1)) return false;
    if (!defineGlobalFunction(ctx, "isNaN", globalIsNaN, 0, 1)) return false;
    if (!defineGlobalFunction(ctx, "isFinite", globalIsFinite, 0, 1)) return false;
    if (!defineGlobalFunction(ctx, "encodeURI", globalEncodeUri, 0, 1)) return false;
    if (!defineGlobalFunction(ctx, "encodeURIComponent", globalEncodeUriComponent, 0, 1))
        return false;
    if (!defineGlobalFunction(ctx, "decodeURI", globalDecodeUri, 0, 1)) return false;
    if (!defineGlobalFunction(ctx, "decodeURIComponent", globalDecodeUriComponent, 0, 1))
        return false;
    return true;
}

}  // namespace neo::core
