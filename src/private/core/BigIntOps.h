// BigIntOps.h (private) - BigInt 引擎辅助
//
// 收敛 bigint 与其它原始值之间的转换/比较细节，供 TypeBehavior / OperatorBehavior
// 复用。纯数学部分在 private/bigint/BigInt.h。
//
// 关联 ADR: 050（BigInt 基础设施）、051（引擎接入）

#ifndef NEO_CORE_PRIVATE_BIGINT_OPS_H
#define NEO_CORE_PRIVATE_BIGINT_OPS_H

#include "bigint/BigInt.h"
#include "core/IContext.h"
#include "core/Value.h"
#include "core/types/BigInt.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace neo::core::detail {

inline const neo::bigint::BigInt& bigIntValue(Value* v) {
    return static_cast<BigIntData*>(v->data())->value;
}

inline Value* createBigIntValue(IContext* ctx, const neo::bigint::BigInt& value) {
    return ctx->createBigInt(value);
}

// StringToBigInt（ES2024 §7.1.13）：StringIntegerLiteral 文法。
// 允许 ASCII 空白、可选符号（仅十进制）、0x/0o/0b 前缀；空串 → 0n。
// 注：Unicode 空白（NBSP 等）暂未覆盖。
inline bool stringToBigInt(std::string_view text, neo::bigint::BigInt& out) {
    size_t begin = 0;
    size_t end = text.size();
    auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
               c == '\v' || c == '\f';
    };
    while (begin < end && isSpace(text[begin])) ++begin;
    while (end > begin && isSpace(text[end - 1])) --end;
    text = text.substr(begin, end - begin);

    if (text.empty()) {
        out = neo::bigint::BigInt();
        return true;
    }

    bool negative = false;
    size_t pos = 0;
    if (text[pos] == '+' || text[pos] == '-') {
        negative = text[pos] == '-';
        ++pos;
    } else if (text.size() - pos >= 2 && text[pos] == '0') {
        int radix = 0;
        switch (text[pos + 1]) {
            case 'x': case 'X': radix = 16; break;
            case 'o': case 'O': radix = 8; break;
            case 'b': case 'B': radix = 2; break;
            default: break;
        }
        if (radix != 0) {
            return neo::bigint::BigInt::parse(text.substr(pos + 2), radix, out);
        }
    }

    if (pos >= text.size()) return false;
    if (!neo::bigint::BigInt::parse(text.substr(pos), 10, out)) return false;
    if (negative) out = -out;
    return true;
}

// 精确比较 BigInt 与 double：-1 / 0 / 1；NaN 时 unordered = true。
// 不把 BigInt 转成 double（会丢失大整数精度）。
inline int compareBigIntDouble(const neo::bigint::BigInt& a, double b,
                               bool& unordered) {
    unordered = false;
    if (std::isnan(b)) {
        unordered = true;
        return 0;
    }
    if (std::isinf(b)) return b > 0 ? -1 : 1;
    if (b == 0.0) {
        if (a.isZero()) return 0;
        return a.isNegative() ? -1 : 1;
    }

    const bool aNegative = a.isNegative();
    const bool bNegative = b < 0;
    if (aNegative != bNegative) return aNegative ? -1 : 1;

    const neo::bigint::BigInt absA = a.abs();
    const double absB = std::fabs(b);
    int exponent = 0;
    const double mantissa = std::frexp(absB, &exponent);  // absB = m * 2^e, 0.5<=m<1
    const size_t aBits = absA.bitLength();

    int cmp = 0;
    if (aBits != static_cast<size_t>(exponent)) {
        cmp = aBits < static_cast<size_t>(exponent) ? -1 : 1;
    } else if (exponent <= 53) {
        // 整数部分落在 uint64 内；小数部分决定严格大小。
        const uint64_t floorB = static_cast<uint64_t>(std::floor(absB));
        const uint64_t aLow = absA.toUint64();
        if (aLow != floorB) {
            cmp = aLow < floorB ? -1 : 1;
        } else {
            cmp = absB > static_cast<double>(floorB) ? -1 : 0;
        }
    } else {
        // e > 53 时 absB 必为整数：比较高 53 位，再比较剩余低位。
        const size_t shift = static_cast<size_t>(exponent) - 53;
        const uint64_t topA = (absA >> shift).toUint64();
        const uint64_t topB = static_cast<uint64_t>(std::ldexp(mantissa, 53));
        if (topA != topB) {
            cmp = topA < topB ? -1 : 1;
        } else {
            const neo::bigint::BigInt low = absA - ((absA >> shift) << shift);
            cmp = low.isZero() ? 0 : 1;
        }
    }
    return aNegative ? -cmp : cmp;
}

}  // namespace neo::core::detail

#endif  // NEO_CORE_PRIVATE_BIGINT_OPS_H
