// NumberBuiltins.cpp - Number 静态成员与 Number.prototype 数值格式化
//
// 设计要点：
//   - toFixed/toExponential/toPrecision 基于 double 的精确十进制展开
//     （m × 2^e 转成 m × 5^k / 10^k），按规范"最近且平局取较大 n"舍入；
//   - toString(radix) 的整数部分用十进制大整数精确换基；小数部分对
//     2 的幂基数精确展开，其余基数最多产出 52 位（double 有效精度）。
//
// 关联 ADR: 020（标准库组织）

#include "core/NumberBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/TypeBehavior.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace neo::core {

namespace {

constexpr double kMaxSafeInteger = 9007199254740991.0;

// —— 十进制大整数（base 1e9，小端）——

struct Big {
    std::vector<uint32_t> limbs;
};

void mulSmall(Big& b, uint32_t m) {
    uint64_t carry = 0;
    for (uint32_t& limb : b.limbs) {
        uint64_t v = static_cast<uint64_t>(limb) * m + carry;
        limb = static_cast<uint32_t>(v % 1000000000u);
        carry = v / 1000000000u;
    }
    while (carry != 0) {
        b.limbs.push_back(static_cast<uint32_t>(carry % 1000000000u));
        carry /= 1000000000u;
    }
}

void addSmall(Big& b, uint32_t d) {
    if (b.limbs.empty()) {
        if (d != 0) b.limbs.push_back(d);
        return;
    }
    uint64_t carry = d;
    for (size_t i = 0; carry != 0 && i < b.limbs.size(); ++i) {
        uint64_t v = static_cast<uint64_t>(b.limbs[i]) + carry;
        b.limbs[i] = static_cast<uint32_t>(v % 1000000000u);
        carry = v / 1000000000u;
    }
    if (carry != 0) b.limbs.push_back(static_cast<uint32_t>(carry));
}

std::string bigToString(const Big& b) {
    if (b.limbs.empty()) return "0";
    std::string s = std::to_string(b.limbs.back());
    for (size_t i = b.limbs.size() - 1; i-- > 0;) {
        std::string part = std::to_string(b.limbs[i]);
        s.append(9 - part.size(), '0');
        s += part;
    }
    return s;
}

uint32_t bigDivSmall(Big& b, uint32_t d) {
    uint64_t rem = 0;
    for (size_t i = b.limbs.size(); i-- > 0;) {
        uint64_t cur = rem * 1000000000u + b.limbs[i];
        b.limbs[i] = static_cast<uint32_t>(cur / d);
        rem = cur % d;
    }
    while (!b.limbs.empty() && b.limbs.back() == 0) b.limbs.pop_back();
    return static_cast<uint32_t>(rem);
}

Big bigFromDecimal(const std::string& digits) {
    Big b;
    for (char c : digits) {
        mulSmall(b, 10);
        addSmall(b, static_cast<uint32_t>(c - '0'));
    }
    return b;
}

// —— double 的精确十进制展开 ——
// 结果满足 |x| = digits × 10^exp10，digits 无前导零、无尾随零。
struct ExactDecimal {
    std::string digits;
    int exp10 = 0;
    bool negative = false;
};

ExactDecimal exactDecimal(double x) {
    ExactDecimal out;
    uint64_t bits = 0;
    std::memcpy(&bits, &x, sizeof bits);
    out.negative = (bits >> 63) != 0;
    const uint64_t absBits = bits & 0x7FFFFFFFFFFFFFFFULL;
    if (absBits == 0) {
        out.digits = "0";
        return out;
    }
    const int biasedExp = static_cast<int>((absBits >> 52) & 0x7FF);
    const uint64_t fraction = absBits & ((1ULL << 52) - 1);
    uint64_t mantissa = 0;
    int exp2 = 0;
    if (biasedExp == 0) {
        mantissa = fraction;
        exp2 = -1074;
    } else {
        mantissa = (1ULL << 52) | fraction;
        exp2 = biasedExp - 1075;
    }
    Big b;
    b.limbs.push_back(static_cast<uint32_t>(mantissa % 1000000000u));
    mantissa /= 1000000000u;
    if (mantissa != 0) b.limbs.push_back(static_cast<uint32_t>(mantissa % 1000000000u));
    mantissa /= 1000000000u;
    if (mantissa != 0) b.limbs.push_back(static_cast<uint32_t>(mantissa));
    if (exp2 >= 0) {
        for (int i = 0; i < exp2; ++i) mulSmall(b, 2);
        out.exp10 = 0;
    } else {
        for (int i = 0; i < -exp2; ++i) mulSmall(b, 5);
        out.exp10 = exp2;
    }
    out.digits = bigToString(b);
    while (out.digits.size() > 1 && out.digits.back() == '0') {
        out.digits.pop_back();
        ++out.exp10;
    }
    return out;
}

// 十进制字符串 +1（可能变长）。
std::string incrementDecimal(std::string s) {
    for (size_t i = s.size(); i-- > 0;) {
        if (s[i] != '9') {
            ++s[i];
            return s;
        }
        s[i] = '0';
    }
    return "1" + s;
}

// 舍入到 keep 位有效数字（平局进位），必要时调整 exp10。
std::string roundSignificant(const std::string& digits, int& exp10, size_t keep) {
    if (digits.size() <= keep) return digits;
    const size_t dropped = digits.size() - keep;
    std::string prefix = digits.substr(0, keep);
    if (digits[keep] >= '5') prefix = incrementDecimal(prefix);
    // 截断后最低位的指数随被丢弃的位数上移；进位再多一位。
    if (prefix.size() > keep) {
        exp10 += static_cast<int>(dropped) + 1;
        prefix.resize(keep);
    } else {
        exp10 += static_cast<int>(dropped);
    }
    return prefix;
}

char digitChar(uint32_t d) {
    static constexpr char kDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    return kDigits[d];
}

// —— this / 参数 ——

Value* unwrapNumber(IContext* ctx, Value* self, const char* method) {
    if (self != nullptr) {
        if (self->type()->kind() == TypeKind::Number) return self;
        if (self->type()->kind() == TypeKind::PrimitiveWrapper) {
            Value* p = wrapperPrimitiveValue(self);
            if (p != nullptr && p->type()->kind() == TypeKind::Number) return p;
        }
    }
    return detail::typeError(
        ctx, std::string("Number.prototype.") + method +
                 " requires that 'this' be a Number");
}

double numberValue(Value* v) {
    return static_cast<NumberData*>(v->data())->value;
}

// ToIntegerOrInfinity（§7.1.5）。
double toIntegerOrInfinity(IContext* ctx, Value* v, Value*& err) {
    Value* nv = detail::toNumberValue(ctx, v);
    if (detail::isErrorValue(nv)) {
        err = nv;
        return 0.0;
    }
    double n = numberValue(nv);
    if (std::isnan(n) || n == 0.0) return 0.0;
    if (std::isinf(n)) return n;
    return std::trunc(n);
}

Value* argOrUndefined(IContext* ctx, size_t argc, Value* argv[], size_t i) {
    return i < argc ? argv[i] : ctx->createUndefined();
}

// —— Number.prototype 方法 ——

Value* numberToFixed(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* p = unwrapNumber(ctx, self, "toFixed");
    if (detail::isErrorValue(p)) return p;
    const double x = numberValue(p);
    Value* err = nullptr;
    double raw = toIntegerOrInfinity(ctx, argOrUndefined(ctx, argc, argv, 0), err);
    if (err != nullptr) return err;
    if (raw < 0.0 || raw > 100.0) {
        return detail::rangeError(ctx, "toFixed() digits argument must be between 0 and 100");
    }
    const int f = static_cast<int>(raw);
    if (std::isnan(x)) return ctx->createString("NaN");
    if (std::isinf(x)) return ctx->createString(x > 0 ? "Infinity" : "-Infinity");
    if (std::fabs(x) >= 1e21) return ctx->createString(detail::numberToString(x));
    if (x == 0.0) {
        std::string s = "0";
        if (f > 0) {
            s += ".";
            s.append(static_cast<size_t>(f), '0');
        }
        return ctx->createString(s);
    }

    const ExactDecimal ex = exactDecimal(x);
    const int shift = ex.exp10 + f;
    std::string n;
    if (shift >= 0) {
        n = ex.digits;
        n.append(static_cast<size_t>(shift), '0');
    } else {
        const size_t drop = static_cast<size_t>(-shift);
        std::string s = ex.digits;
        if (s.size() <= drop) {
            s.insert(0, drop - s.size() + 1, '0');
        }
        std::string intPart = s.substr(0, s.size() - drop);
        const std::string frac = s.substr(s.size() - drop);
        if (frac[0] >= '5') intPart = incrementDecimal(intPart);
        n = intPart.empty() ? "0" : intPart;
    }
    const size_t nz = n.find_first_not_of('0');
    n = nz == std::string::npos ? "0" : n.substr(nz);

    std::string intPart;
    std::string fracPart;
    if (f == 0) {
        intPart = n;
    } else {
        if (n.size() <= static_cast<size_t>(f)) {
            n.insert(0, static_cast<size_t>(f) + 1 - n.size(), '0');
        }
        intPart = n.substr(0, n.size() - static_cast<size_t>(f));
        fracPart = n.substr(n.size() - static_cast<size_t>(f));
    }

    const bool allZero = intPart == "0" &&
                         fracPart.find_first_not_of('0') == std::string::npos;
    std::string out;
    if (ex.negative && !allZero) out += "-";
    out += intPart;
    if (f > 0) {
        out += ".";
        out += fracPart;
    }
    return ctx->createString(out);
}

Value* numberToExponential(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* p = unwrapNumber(ctx, self, "toExponential");
    if (detail::isErrorValue(p)) return p;
    const double x = numberValue(p);
    const bool hasDigits = argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined;
    Value* err = nullptr;
    double raw = hasDigits ? toIntegerOrInfinity(ctx, argv[0], err) : 0.0;
    if (err != nullptr) return err;
    if (hasDigits && (raw < 0.0 || raw > 100.0)) {
        return detail::rangeError(
            ctx, "toExponential() argument must be between 0 and 100");
    }
    if (std::isnan(x)) return ctx->createString("NaN");
    if (std::isinf(x)) return ctx->createString(x > 0 ? "Infinity" : "-Infinity");

    const bool negative = std::signbit(x) && x != 0.0;
    if (x == 0.0) {
        std::string s = "0";
        if (hasDigits) {
            const int f = static_cast<int>(raw);
            if (f > 0) {
                s += ".";
                s.append(static_cast<size_t>(f), '0');
            }
        }
        return ctx->createString(s + "e+0");
    }

    if (!hasDigits) {
        char buf[64];
        auto result = std::to_chars(buf, buf + sizeof buf, std::fabs(x),
                                    std::chars_format::scientific);
        std::string text(buf, result.ptr);
        const size_t epos = text.find('e');
        std::string mantissa = text.substr(0, epos);
        const int exp = std::stoi(text.substr(epos + 1));
        std::string out;
        if (negative) out += "-";
        out += mantissa;
        out += "e";
        out += exp >= 0 ? "+" : "-";
        out += std::to_string(std::abs(exp));
        return ctx->createString(out);
    }

    const int f = static_cast<int>(raw);
    ExactDecimal ex = exactDecimal(x);
    std::string digits = roundSignificant(ex.digits, ex.exp10, static_cast<size_t>(f) + 1);
    const int exp = static_cast<int>(digits.size()) + ex.exp10 - 1;
    if (digits.size() < static_cast<size_t>(f) + 1) {
        digits.append(static_cast<size_t>(f) + 1 - digits.size(), '0');
    }
    std::string out;
    if (negative) out += "-";
    out += digits[0];
    if (f > 0) {
        out += ".";
        out += digits.substr(1);
    }
    out += "e";
    out += exp >= 0 ? "+" : "-";
    out += std::to_string(std::abs(exp));
    return ctx->createString(out);
}

Value* numberToPrecision(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* p = unwrapNumber(ctx, self, "toPrecision");
    if (detail::isErrorValue(p)) return p;
    const double x = numberValue(p);
    if (argc == 0 || argv[0]->type()->kind() == TypeKind::Undefined) {
        return ctx->createString(detail::numberToString(x));
    }
    Value* err = nullptr;
    double raw = toIntegerOrInfinity(ctx, argv[0], err);
    if (err != nullptr) return err;
    if (raw < 1.0 || raw > 100.0) {
        return detail::rangeError(ctx, "toPrecision() argument must be between 1 and 100");
    }
    const size_t precision = static_cast<size_t>(raw);
    if (std::isnan(x)) return ctx->createString("NaN");
    if (std::isinf(x)) return ctx->createString(x > 0 ? "Infinity" : "-Infinity");

    ExactDecimal ex = exactDecimal(x);
    std::string digits;
    int exp = 0;
    if (x == 0.0) {
        digits = "0";
        exp = 0;
    } else {
        digits = roundSignificant(ex.digits, ex.exp10, precision);
        exp = static_cast<int>(digits.size()) + ex.exp10 - 1;
    }
    if (digits.size() < precision) digits.append(precision - digits.size(), '0');

    std::string out;
    if (ex.negative && x != 0.0) out += "-";
    if (exp < -6 || exp >= static_cast<int>(precision)) {
        out += digits[0];
        if (precision > 1) {
            out += ".";
            out += digits.substr(1);
        }
        out += "e";
        out += exp >= 0 ? "+" : "-";
        out += std::to_string(std::abs(exp));
    } else if (exp >= 0) {
        out += digits.substr(0, static_cast<size_t>(exp) + 1);
        if (digits.size() > static_cast<size_t>(exp) + 1) {
            out += ".";
            out += digits.substr(static_cast<size_t>(exp) + 1);
        }
    } else {
        out += "0.";
        out.append(static_cast<size_t>(-exp - 1), '0');
        out += digits;
    }
    return ctx->createString(out);
}

Value* numberToStringRadix(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* p = unwrapNumber(ctx, self, "toString");
    if (detail::isErrorValue(p)) return p;
    const double x = numberValue(p);
    Value* err = nullptr;
    double rawRadix = argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined
                          ? toIntegerOrInfinity(ctx, argv[0], err)
                          : 10.0;
    if (err != nullptr) return err;
    if (rawRadix < 2.0 || rawRadix > 36.0) {
        return detail::rangeError(ctx, "toString() radix must be between 2 and 36");
    }
    const uint32_t radix = static_cast<uint32_t>(rawRadix);
    if (radix == 10) return ctx->createString(detail::numberToString(x));
    if (std::isnan(x)) return ctx->createString("NaN");
    if (std::isinf(x)) return ctx->createString(x > 0 ? "Infinity" : "-Infinity");
    if (x == 0.0) return ctx->createString("0");

    const bool negative = x < 0.0;
    const double ax = std::fabs(x);
    const ExactDecimal ex = exactDecimal(ax);

    std::string intDecimal;
    if (ex.exp10 >= 0) {
        intDecimal = ex.digits;
        intDecimal.append(static_cast<size_t>(ex.exp10), '0');
    } else if (static_cast<int>(ex.digits.size()) + ex.exp10 > 0) {
        intDecimal = ex.digits.substr(
            0, static_cast<size_t>(static_cast<int>(ex.digits.size()) + ex.exp10));
    } else {
        intDecimal = "0";
    }

    Big integer = bigFromDecimal(intDecimal);
    std::string intDigits;
    if (integer.limbs.empty()) {
        intDigits = "0";
    } else {
        while (!integer.limbs.empty()) {
            intDigits.push_back(digitChar(bigDivSmall(integer, radix)));
        }
        std::reverse(intDigits.begin(), intDigits.end());
    }

    double fraction = ax - std::floor(ax);
    const bool powerOfTwo = radix == 2 || radix == 4 || radix == 8 || radix == 16 ||
                            radix == 32;
    const size_t cap = powerOfTwo ? 1200 : 52;
    std::string fracDigits;
    while (fraction > 0.0 && fracDigits.size() < cap) {
        fraction *= static_cast<double>(radix);
        const uint32_t digit = static_cast<uint32_t>(std::floor(fraction));
        fraction -= static_cast<double>(digit);
        fracDigits.push_back(digitChar(digit));
    }
    while (!fracDigits.empty() && fracDigits.back() == '0') fracDigits.pop_back();

    std::string out;
    if (negative) out += "-";
    out += intDigits;
    if (!fracDigits.empty()) {
        out += ".";
        out += fracDigits;
    }
    return ctx->createString(out);
}

Value* numberToLocaleString(IContext* ctx, Value* self, size_t argc,
                            Value* argv[]) {
    Value* p = unwrapNumber(ctx, self, "toLocaleString");
    if (detail::isErrorValue(p)) return p;

    // 有 Intl 时委托给 %NumberFormat%（ECMA-402 §19.2.1）；否则退化为 toString。
    Value* intl = ctx->global()->lookup("Intl");
    if (intl != nullptr && detail::isObjectKind(intl->type()->kind())) {
        Value* ctor = ctx->getProperty(intl, "NumberFormat");
        if (!detail::isErrorValue(ctor) && detail::isCallable(ctor)) {
            Value* formatter = ctx->construct(ctor, argc, argv);
            if (detail::isErrorValue(formatter)) return formatter;
            Value* format = ctx->getProperty(formatter, "format");
            if (detail::isErrorValue(format)) return format;
            Value* args[1] = {p};
            return ctx->call(format, formatter, 1, args);
        }
    }
    return ctx->createString(detail::numberToString(numberValue(p)));
}

// —— Number 静态方法 ——

Value* numberIsFinite(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0 || argv[0]->type()->kind() != TypeKind::Number) {
        return ctx->createBoolean(false);
    }
    return ctx->createBoolean(std::isfinite(numberValue(argv[0])));
}

Value* numberIsNaN(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0 || argv[0]->type()->kind() != TypeKind::Number) {
        return ctx->createBoolean(false);
    }
    return ctx->createBoolean(std::isnan(numberValue(argv[0])));
}

Value* numberIsInteger(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0 || argv[0]->type()->kind() != TypeKind::Number) {
        return ctx->createBoolean(false);
    }
    const double n = numberValue(argv[0]);
    return ctx->createBoolean(std::isfinite(n) && std::trunc(n) == n);
}

Value* numberIsSafeInteger(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0 || argv[0]->type()->kind() != TypeKind::Number) {
        return ctx->createBoolean(false);
    }
    const double n = numberValue(argv[0]);
    return ctx->createBoolean(std::isfinite(n) && std::trunc(n) == n &&
                              std::fabs(n) <= kMaxSafeInteger);
}

// —— 注册辅助 ——

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

bool defineConstant(IContext* ctx, Value* target, const char* name, double value) {
    Value* v = ctx->createNumber(value);
    if (v == ctx->oomValue()) return false;
    return ctx->defineProperty(target, name, v, false, false, false)->type()->kind() !=
           TypeKind::Exception;
}

}  // namespace

bool registerNumberBuiltins(IContext* ctx, Value* numberFn, Value* proto) {
    if (!defineMethod(ctx, proto, "toFixed", numberToFixed, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "toExponential", numberToExponential, 0, 1))
        return false;
    if (!defineMethod(ctx, proto, "toPrecision", numberToPrecision, 0, 1)) return false;
    if (!defineMethod(ctx, proto, "toLocaleString", numberToLocaleString, 0, 0))
        return false;
    // toString 已由包装类注册；这里用支持 radix 的实现覆盖。
    if (!defineMethod(ctx, proto, "toString", numberToStringRadix, 0, 1)) return false;

    if (!defineMethod(ctx, numberFn, "isFinite", numberIsFinite, 1, 1)) return false;
    if (!defineMethod(ctx, numberFn, "isNaN", numberIsNaN, 1, 1)) return false;
    if (!defineMethod(ctx, numberFn, "isInteger", numberIsInteger, 1, 1)) return false;
    if (!defineMethod(ctx, numberFn, "isSafeInteger", numberIsSafeInteger, 1, 1))
        return false;

    // Number.parseInt / Number.parseFloat 与全局函数同值。
    for (const char* name : {"parseInt", "parseFloat"}) {
        Value* fn = ctx->global()->lookup(name);
        if (fn == nullptr) return false;
        if (ctx->defineProperty(numberFn, name, fn, true, false, true)->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }

    if (!defineConstant(ctx, numberFn, "EPSILON", 2.220446049250313e-16)) return false;
    if (!defineConstant(ctx, numberFn, "MAX_SAFE_INTEGER", kMaxSafeInteger)) return false;
    if (!defineConstant(ctx, numberFn, "MIN_SAFE_INTEGER", -kMaxSafeInteger)) return false;
    if (!defineConstant(ctx, numberFn, "MAX_VALUE", 1.7976931348623157e308)) return false;
    if (!defineConstant(ctx, numberFn, "MIN_VALUE", 5e-324)) return false;
    if (!defineConstant(ctx, numberFn, "POSITIVE_INFINITY",
                        std::numeric_limits<double>::infinity())) {
        return false;
    }
    if (!defineConstant(ctx, numberFn, "NEGATIVE_INFINITY",
                        -std::numeric_limits<double>::infinity())) {
        return false;
    }
    if (!defineConstant(ctx, numberFn, "NaN", std::numeric_limits<double>::quiet_NaN())) {
        return false;
    }
    return true;
}

}  // namespace neo::core
