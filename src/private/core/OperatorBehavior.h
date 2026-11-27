// OperatorBehavior.h (private) - Engine 运算符语义实现
//
// VM 只负责取操作数；这里的函数落实 ES2024 的运算符规则：
//   相等（§7.2.14 / §7.2.16）、关系（§7.2.13）、位运算（§13.12）、
//   取模/幂（§13.15）、typeof（§13.5.3）、in（§13.10.1）。
// 所有隐式转换复用 TypeBehavior.h 的唯一转换链，错误走 Value* 通道。
//
// 关联 ADR: 016（Type 体系）、006（异常即值）

#ifndef NEO_CORE_PRIVATE_OPERATOR_BEHAVIOR_H
#define NEO_CORE_PRIVATE_OPERATOR_BEHAVIOR_H

#include "core/IContext.h"
#include "core/OperatorKind.h"
#include "core/types/Boolean.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "unicode/Utf8.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace neo::core::detail {

// —— IsLooselyEqual（ES2024 §7.2.14）——
// 同类型走严格相等；null/undefined 互等；其余按规范逐步转换。
inline Value* abstractEquals(IContext* ctx, Value* a, Value* b) {
    if (isErrorValue(a)) return a;
    if (isErrorValue(b)) return b;

    TypeKind ak = a->type()->kind();
    TypeKind bk = b->type()->kind();
    if (ak == bk) return ctx->createBoolean(strictEquals(a, b));

    // null == undefined
    if ((ak == TypeKind::Null && bk == TypeKind::Undefined) ||
        (ak == TypeKind::Undefined && bk == TypeKind::Null)) {
        return ctx->createBoolean(true);
    }

    // Number == String：字符串转数值
    if (ak == TypeKind::Number && bk == TypeKind::String) {
        Value* bn = toNumberValue(ctx, b);
        if (isErrorValue(bn)) return bn;
        return ctx->createBoolean(strictEquals(a, bn));
    }
    if (ak == TypeKind::String && bk == TypeKind::Number) {
        Value* an = toNumberValue(ctx, a);
        if (isErrorValue(an)) return an;
        return ctx->createBoolean(strictEquals(an, b));
    }

    // BigInt == Number：按数学值精确比较（非整数/NaN/Infinity 均不等）。
    if (ak == TypeKind::BigInt && bk == TypeKind::Number) {
        bool unordered = false;
        int cmp = compareBigIntDouble(bigIntValue(a),
                                      static_cast<NumberData*>(b->data())->value,
                                      unordered);
        return ctx->createBoolean(!unordered && cmp == 0);
    }
    if (ak == TypeKind::Number && bk == TypeKind::BigInt) {
        bool unordered = false;
        int cmp = compareBigIntDouble(bigIntValue(b),
                                      static_cast<NumberData*>(a->data())->value,
                                      unordered);
        return ctx->createBoolean(!unordered && cmp == 0);
    }
    // BigInt == String：StringToBigInt 后比较；非法数字串 → false。
    if (ak == TypeKind::BigInt && bk == TypeKind::String) {
        neo::bigint::BigInt parsed;
        if (!stringToBigInt(static_cast<StringData*>(b->data())->value, parsed)) {
            return ctx->createBoolean(false);
        }
        return ctx->createBoolean(bigIntValue(a) == parsed);
    }
    if (ak == TypeKind::String && bk == TypeKind::BigInt) {
        neo::bigint::BigInt parsed;
        if (!stringToBigInt(static_cast<StringData*>(a->data())->value, parsed)) {
            return ctx->createBoolean(false);
        }
        return ctx->createBoolean(parsed == bigIntValue(b));
    }

    // Boolean 先转数值
    if (ak == TypeKind::Boolean) {
        Value* an = toNumberValue(ctx, a);
        if (isErrorValue(an)) return an;
        return abstractEquals(ctx, an, b);
    }
    if (bk == TypeKind::Boolean) {
        Value* bn = toNumberValue(ctx, b);
        if (isErrorValue(bn)) return bn;
        return abstractEquals(ctx, a, bn);
    }

    // 对象与原始值（String/Number/Symbol）：对象先 ToPrimitive
    const bool aObject = isObjectKind(ak);
    const bool bObject = isObjectKind(bk);
    const bool aPrimitive = ak == TypeKind::String || ak == TypeKind::Number ||
                            ak == TypeKind::BigInt || ak == TypeKind::Symbol;
    const bool bPrimitive = bk == TypeKind::String || bk == TypeKind::Number ||
                            bk == TypeKind::BigInt || bk == TypeKind::Symbol;
    if (aObject && bPrimitive) {
        Value* ap = toPrimitive(ctx, a);
        if (isErrorValue(ap)) return ap;
        return abstractEquals(ctx, ap, b);
    }
    if (bObject && aPrimitive) {
        Value* bp = toPrimitive(ctx, b);
        if (isErrorValue(bp)) return bp;
        return abstractEquals(ctx, a, bp);
    }
    return ctx->createBoolean(false);
}

// 关系比较按 UTF-16 码元序列（§7.2.13），不是码点/UTF-8 字节序：
// 例如 "\u{10000}"（D800 DC00）< "\uFFFF"。
inline void appendUtf16CodeUnits(std::u16string& out, const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = 0;
        if (!neo::unicode::decodeCodePoint(s, i, cp)) {
            out.push_back(static_cast<char16_t>(0xFFFD));
            continue;
        }
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
}

// —— IsLessThan（ES2024 §7.2.13）——
// leftFirst 决定两侧 ToPrimitive 的顺序（x > y / x <= y 传 false）。
// 字符串按 UTF-16 码元比较；BigInt 与 String 用 StringToBigInt 精确比较；
// NaN/非法数字串返回 undefined（undefinedOut 标记）。
inline Value* abstractLessThan(IContext* ctx, Value* a, Value* b,
                               bool leftFirst, bool& undefinedOut) {
    undefinedOut = false;
    Value* ap = nullptr;
    Value* bp = nullptr;
    if (leftFirst) {
        ap = toPrimitive(ctx, a, PreferredType::Number);
        if (isErrorValue(ap)) return ap;
        bp = toPrimitive(ctx, b, PreferredType::Number);
        if (isErrorValue(bp)) return bp;
    } else {
        bp = toPrimitive(ctx, b, PreferredType::Number);
        if (isErrorValue(bp)) return bp;
        ap = toPrimitive(ctx, a, PreferredType::Number);
        if (isErrorValue(ap)) return ap;
    }

    const TypeKind apk = ap->type()->kind();
    const TypeKind bpk = bp->type()->kind();
    if (apk == TypeKind::String && bpk == TypeKind::String) {
        const std::string& x = static_cast<StringData*>(ap->data())->value;
        const std::string& y = static_cast<StringData*>(bp->data())->value;
        std::u16string xs;
        std::u16string ys;
        appendUtf16CodeUnits(xs, x);
        appendUtf16CodeUnits(ys, y);
        return ctx->createBoolean(xs < ys);
    }
    // BigInt 与 String：StringToBigInt 精确比较，非法数字串 → undefined。
    if (apk == TypeKind::BigInt && bpk == TypeKind::String) {
        neo::bigint::BigInt parsed;
        if (!stringToBigInt(static_cast<StringData*>(bp->data())->value,
                            parsed)) {
            undefinedOut = true;
            return ctx->createBoolean(false);
        }
        return ctx->createBoolean(bigIntValue(ap) < parsed);
    }
    if (apk == TypeKind::String && bpk == TypeKind::BigInt) {
        neo::bigint::BigInt parsed;
        if (!stringToBigInt(static_cast<StringData*>(ap->data())->value,
                            parsed)) {
            undefinedOut = true;
            return ctx->createBoolean(false);
        }
        return ctx->createBoolean(parsed < bigIntValue(bp));
    }

    Value* an = toNumericValue(ctx, ap);
    if (isErrorValue(an)) return an;
    Value* bn = toNumericValue(ctx, bp);
    if (isErrorValue(bn)) return bn;
    const TypeKind ak = an->type()->kind();
    const TypeKind bk = bn->type()->kind();
    if (ak == TypeKind::BigInt || bk == TypeKind::BigInt) {
        if (ak == TypeKind::BigInt && bk == TypeKind::BigInt) {
            return ctx->createBoolean(bigIntValue(an) < bigIntValue(bn));
        }
        // 混合 BigInt/Number：精确数学比较；NaN → undefined。
        bool unordered = false;
        int cmp = 0;
        if (ak == TypeKind::BigInt) {
            cmp = compareBigIntDouble(
                bigIntValue(an), static_cast<NumberData*>(bn->data())->value,
                unordered);
        } else {
            cmp = compareBigIntDouble(
                bigIntValue(bn), static_cast<NumberData*>(an->data())->value,
                unordered);
            cmp = -cmp;
        }
        if (unordered) {
            undefinedOut = true;
            return ctx->createBoolean(false);
        }
        return ctx->createBoolean(cmp < 0);
    }
    double x = static_cast<NumberData*>(an->data())->value;
    double y = static_cast<NumberData*>(bn->data())->value;
    if (std::isnan(x) || std::isnan(y)) {
        undefinedOut = true;
        return ctx->createBoolean(false);
    }
    return ctx->createBoolean(x < y);
}

inline Value* lessThan(IContext* ctx, Value* a, Value* b) {
    bool undef = false;
    Value* r = abstractLessThan(ctx, a, b, true, undef);
    if (isErrorValue(r)) return r;
    return undef ? ctx->createBoolean(false) : r;
}

inline Value* greaterThan(IContext* ctx, Value* a, Value* b) {
    bool undef = false;
    Value* r = abstractLessThan(ctx, b, a, false, undef);
    if (isErrorValue(r)) return r;
    return undef ? ctx->createBoolean(false) : r;
}

inline Value* lessThanOrEqual(IContext* ctx, Value* a, Value* b) {
    bool undef = false;
    Value* r = abstractLessThan(ctx, b, a, false, undef);
    if (isErrorValue(r)) return r;
    if (undef) return ctx->createBoolean(false);
    return ctx->createBoolean(!toBoolean(r));
}

inline Value* greaterThanOrEqual(IContext* ctx, Value* a, Value* b) {
    bool undef = false;
    Value* r = abstractLessThan(ctx, a, b, true, undef);
    if (isErrorValue(r)) return r;
    if (undef) return ctx->createBoolean(false);
    return ctx->createBoolean(!toBoolean(r));
}

// —— ToInt32 / ToUint32（ES2024 §7.1.6 / §7.1.7）——
inline uint32_t toUint32(double n) {
    if (!std::isfinite(n) || n == 0.0) return 0;
    double m = std::fmod(std::trunc(n), 4294967296.0);
    if (m < 0.0) m += 4294967296.0;
    return static_cast<uint32_t>(m);
}

inline int32_t fromUint32(uint32_t u) {
    if (u >= 0x80000000u) {
        return static_cast<int32_t>(static_cast<int64_t>(u) - 4294967296LL);
    }
    return static_cast<int32_t>(u);
}

inline int32_t toInt32(double n) {
    return fromUint32(toUint32(n));
}

// 算术右移（避免依赖实现定义的 signed >>）：向下取整除以 2^shift。
inline int32_t arithmeticShiftRight(int32_t x, uint32_t shift) {
    if (shift == 0) return x;
    int64_t v = static_cast<int64_t>(x);
    int64_t divisor = static_cast<int64_t>(1) << shift;
    int64_t q = v / divisor;
    if (v < 0 && v % divisor != 0) --q;
    return static_cast<int32_t>(q);
}

// BigInt 移位：负移位数反向；超大移位数左移报 RangeError、右移得到 0/-1。
inline Value* bigIntShiftOp(IContext* ctx, const neo::bigint::BigInt& value,
                            const neo::bigint::BigInt& count, bool left) {
    bool shiftLeft = left;
    neo::bigint::BigInt magnitude = count;
    if (count.isNegative()) {
        shiftLeft = !shiftLeft;
        magnitude = -count;
    }
    if (!magnitude.fitsUint64()) {
        if (shiftLeft) return rangeError(ctx, "Maximum BigInt size exceeded");
        return createBigIntValue(
            ctx, value >> std::numeric_limits<uint64_t>::max());
    }
    const uint64_t bits = magnitude.toUint64();
    return createBigIntValue(ctx, shiftLeft ? (value << bits) : (value >> bits));
}

// 位运算：BigInt 走补码位运算；Number 走 ToInt32/ToUint32。
inline Value* bitwiseOp(IContext* ctx, BinaryOperator op, Value* a, Value* b) {
    Value* an = toNumericValue(ctx, a);
    if (isErrorValue(an)) return an;
    Value* bn = toNumericValue(ctx, b);
    if (isErrorValue(bn)) return bn;

    const TypeKind ak = an->type()->kind();
    const TypeKind bk = bn->type()->kind();
    if (ak == TypeKind::BigInt || bk == TypeKind::BigInt) {
        if (ak != bk) {
            return typeError(ctx,
                             "Cannot mix BigInt and other types, use explicit conversions");
        }
        const neo::bigint::BigInt& x = bigIntValue(an);
        const neo::bigint::BigInt& y = bigIntValue(bn);
        switch (op) {
            case BinaryOperator::BitAnd: return createBigIntValue(ctx, x & y);
            case BinaryOperator::BitOr:  return createBigIntValue(ctx, x | y);
            case BinaryOperator::BitXor: return createBigIntValue(ctx, x ^ y);
            case BinaryOperator::ShiftLeft:
                return bigIntShiftOp(ctx, x, y, true);
            case BinaryOperator::ShiftRight:
                return bigIntShiftOp(ctx, x, y, false);
            case BinaryOperator::UnsignedShiftRight:
                return typeError(
                    ctx, "BigInts have no unsigned right shift, use >> instead");
            default: break;
        }
    }

    double x = static_cast<NumberData*>(an->data())->value;
    double y = static_cast<NumberData*>(bn->data())->value;

    switch (op) {
        case BinaryOperator::BitAnd:
            return ctx->createEphemeralNumber(
                static_cast<double>(toInt32(x) & toInt32(y)));
        case BinaryOperator::BitOr:
            return ctx->createEphemeralNumber(
                static_cast<double>(toInt32(x) | toInt32(y)));
        case BinaryOperator::BitXor:
            return ctx->createEphemeralNumber(
                static_cast<double>(toInt32(x) ^ toInt32(y)));
        case BinaryOperator::ShiftLeft: {
            uint32_t shift = toUint32(y) & 31u;
            uint32_t r = static_cast<uint32_t>(toInt32(x)) << shift;
            return ctx->createEphemeralNumber(static_cast<double>(fromUint32(r)));
        }
        case BinaryOperator::ShiftRight: {
            uint32_t shift = toUint32(y) & 31u;
            return ctx->createEphemeralNumber(
                static_cast<double>(arithmeticShiftRight(toInt32(x), shift)));
        }
        case BinaryOperator::UnsignedShiftRight: {
            uint32_t shift = toUint32(y) & 31u;
            return ctx->createEphemeralNumber(
                static_cast<double>(toUint32(x) >> shift));
        }
        default:
            return ctx->createEphemeralNumber(0);
    }
}

// —— in（ES2024 §13.10.1）——
inline Value* inOperator(IContext* ctx, Value* key, Value* obj) {
    if (isErrorValue(key)) return key;
    if (isErrorValue(obj)) return obj;
    if (!isObjectKind(obj->type()->kind())) {
        return typeError(ctx, "right-hand side of 'in' is not an object");
    }
    return ctx->hasProperty(obj, key);
}

// —— typeof（ES2024 §13.5.3）——
inline const char* typeofName(Value* v) {
    TypeKind k = v->type()->kind();
    switch (k) {
        case TypeKind::Undefined: return "undefined";
        case TypeKind::Null:      return "object";
        case TypeKind::Boolean:   return "boolean";
        case TypeKind::Number:    return "number";
        case TypeKind::BigInt:    return "bigint";
        case TypeKind::String:    return "string";
        case TypeKind::Symbol:    return "symbol";
        case TypeKind::Function:  return "function";
        case TypeKind::Proxy:     return isCallable(v) ? "function" : "object";
        default:
            // 其余对象布局类型（对象/数组/包装/Error/Promise/迭代器/协程...）
            // 都是 "object"；内部非对象值（Exception/Fatal/Interrupt/Uninitialized）
            // 不可观测，按 "undefined" 处理。
            return isObjectKind(k) ? "object" : "undefined";
    }
}

// —— 二元运算符统一入口 ——
inline Value* binaryOpValues(IContext* ctx, BinaryOperator op, Value* a,
                             Value* b) {
    if (isErrorValue(a)) return a;
    if (isErrorValue(b)) return b;

    switch (op) {
        case BinaryOperator::Add: return ctx->add(a, b);
        case BinaryOperator::Sub: return ctx->sub(a, b);
        case BinaryOperator::Mul: return ctx->mul(a, b);
        case BinaryOperator::Div: return ctx->div(a, b);
        case BinaryOperator::Mod: {
            Value* an = toNumericValue(ctx, a);
            if (isErrorValue(an)) return an;
            Value* bn = toNumericValue(ctx, b);
            if (isErrorValue(bn)) return bn;
            const TypeKind ak = an->type()->kind();
            const TypeKind bk = bn->type()->kind();
            if (ak == TypeKind::BigInt || bk == TypeKind::BigInt) {
                if (ak != bk) {
                    return typeError(
                        ctx,
                        "Cannot mix BigInt and other types, use explicit conversions");
                }
                if (bigIntValue(bn).isZero()) {
                    return rangeError(ctx, "Division by zero");
                }
                return createBigIntValue(ctx, bigIntValue(an) % bigIntValue(bn));
            }
            double x = static_cast<NumberData*>(an->data())->value;
            double y = static_cast<NumberData*>(bn->data())->value;
            return ctx->createEphemeralNumber(std::fmod(x, y));
        }
        case BinaryOperator::Exp: {
            Value* an = toNumericValue(ctx, a);
            if (isErrorValue(an)) return an;
            Value* bn = toNumericValue(ctx, b);
            if (isErrorValue(bn)) return bn;
            const TypeKind ak = an->type()->kind();
            const TypeKind bk = bn->type()->kind();
            if (ak == TypeKind::BigInt || bk == TypeKind::BigInt) {
                if (ak != bk) {
                    return typeError(
                        ctx,
                        "Cannot mix BigInt and other types, use explicit conversions");
                }
                const neo::bigint::BigInt& exponent = bigIntValue(bn);
                if (exponent.isNegative()) {
                    return rangeError(ctx, "Exponent must be non-negative");
                }
                if (!exponent.fitsUint64()) {
                    return rangeError(ctx, "Maximum BigInt size exceeded");
                }
                return createBigIntValue(ctx,
                                         bigIntValue(an).pow(exponent.toUint64()));
            }
            double x = static_cast<NumberData*>(an->data())->value;
            double y = static_cast<NumberData*>(bn->data())->value;
            // std::pow 对 1^NaN / (-1)^±Infinity 返回 1，规范要求 NaN
            // （§6.1.6.1.3 Number::exponentiate）。
            if (std::isnan(y) || ((x == 1.0 || x == -1.0) && std::isinf(y))) {
                return ctx->createEphemeralNumber(std::numeric_limits<double>::quiet_NaN());
            }
            return ctx->createEphemeralNumber(std::pow(x, y));
        }
        case BinaryOperator::StrictEqual:
            return ctx->createBoolean(strictEquals(a, b));
        case BinaryOperator::AbstractEqual:
            return abstractEquals(ctx, a, b);
        case BinaryOperator::LessThan:
            return lessThan(ctx, a, b);
        case BinaryOperator::GreaterThan:
            return greaterThan(ctx, a, b);
        case BinaryOperator::LessThanOrEqual:
            return lessThanOrEqual(ctx, a, b);
        case BinaryOperator::GreaterThanOrEqual:
            return greaterThanOrEqual(ctx, a, b);
        case BinaryOperator::BitAnd:
        case BinaryOperator::BitOr:
        case BinaryOperator::BitXor:
        case BinaryOperator::ShiftLeft:
        case BinaryOperator::ShiftRight:
        case BinaryOperator::UnsignedShiftRight:
            return bitwiseOp(ctx, op, a, b);
        case BinaryOperator::In:
            return inOperator(ctx, a, b);
        case BinaryOperator::InstanceOf:
            return ctx->instanceOf(a, b);
    }
    return ctx->createUndefined();
}

// —— 一元运算符统一入口 ——
inline Value* unaryOpValues(IContext* ctx, UnaryOperator op, Value* v) {
    if (isErrorValue(v)) return v;

    switch (op) {
        case UnaryOperator::Plus:
            return toNumberValue(ctx, v);
        case UnaryOperator::Minus: {
            // 规范：ToNumeric(GetValue(expr)) 后再按 T 分派，对象包装的 BigInt
            // 须先经 ToPrimitive 得到 bigint，而不是直接 ToNumber 抛错。
            Value* n = toNumericValue(ctx, v);
            if (isErrorValue(n)) return n;
            if (n->type()->kind() == TypeKind::BigInt) {
                return createBigIntValue(ctx, -bigIntValue(n));
            }
            return ctx->createEphemeralNumber(-static_cast<NumberData*>(n->data())->value);
        }
        case UnaryOperator::LogicalNot:
            return ctx->createBoolean(!toBoolean(v));
        case UnaryOperator::BitNot: {
            Value* n = toNumericValue(ctx, v);
            if (isErrorValue(n)) return n;
            if (n->type()->kind() == TypeKind::BigInt) {
                return createBigIntValue(ctx, ~bigIntValue(n));
            }
            uint32_t u = static_cast<uint32_t>(
                toInt32(static_cast<NumberData*>(n->data())->value));
            return ctx->createEphemeralNumber(static_cast<double>(fromUint32(~u)));
        }
        case UnaryOperator::TypeOf:
            return ctx->createEphemeralString(typeofName(v));
        case UnaryOperator::Void:
            return ctx->createUndefined();
    }
    return ctx->createUndefined();
}

}  // namespace neo::core::detail

#endif  // NEO_CORE_PRIVATE_OPERATOR_BEHAVIOR_H
