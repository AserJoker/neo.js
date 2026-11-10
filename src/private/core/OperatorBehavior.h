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

#include <cmath>
#include <cstdint>
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
                            ak == TypeKind::Symbol;
    const bool bPrimitive = bk == TypeKind::String || bk == TypeKind::Number ||
                            bk == TypeKind::Symbol;
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

// —— IsLessThan（ES2024 §7.2.13）——
// 两侧 ToPrimitive(NUMBER)：都为 string 按字符串比较，否则 ToNumber 后比较。
// NaN 时规范返回 undefined；用 undefinedOut 标记，由调用方按运算符决定结果。
inline Value* abstractLessThan(IContext* ctx, Value* a, Value* b,
                               bool& undefinedOut) {
    undefinedOut = false;
    Value* ap = toPrimitive(ctx, a, PreferredType::Number);
    if (isErrorValue(ap)) return ap;
    Value* bp = toPrimitive(ctx, b, PreferredType::Number);
    if (isErrorValue(bp)) return bp;

    if (ap->type()->kind() == TypeKind::String &&
        bp->type()->kind() == TypeKind::String) {
        const std::string& x = static_cast<StringData*>(ap->data())->value;
        const std::string& y = static_cast<StringData*>(bp->data())->value;
        return ctx->createBoolean(x < y);
    }

    Value* an = toNumberValue(ctx, ap);
    if (isErrorValue(an)) return an;
    Value* bn = toNumberValue(ctx, bp);
    if (isErrorValue(bn)) return bn;
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
    Value* r = abstractLessThan(ctx, a, b, undef);
    if (isErrorValue(r)) return r;
    return undef ? ctx->createBoolean(false) : r;
}

inline Value* greaterThan(IContext* ctx, Value* a, Value* b) {
    bool undef = false;
    Value* r = abstractLessThan(ctx, b, a, undef);
    if (isErrorValue(r)) return r;
    return undef ? ctx->createBoolean(false) : r;
}

inline Value* lessThanOrEqual(IContext* ctx, Value* a, Value* b) {
    bool undef = false;
    Value* r = abstractLessThan(ctx, b, a, undef);
    if (isErrorValue(r)) return r;
    if (undef) return ctx->createBoolean(false);
    return ctx->createBoolean(!toBoolean(r));
}

inline Value* greaterThanOrEqual(IContext* ctx, Value* a, Value* b) {
    bool undef = false;
    Value* r = abstractLessThan(ctx, a, b, undef);
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

// 位运算：两侧 ToNumber 后按 ToInt32/ToUint32 计算。
inline Value* bitwiseOp(IContext* ctx, BinaryOperator op, Value* a, Value* b) {
    Value* an = toNumberValue(ctx, a);
    if (isErrorValue(an)) return an;
    Value* bn = toNumberValue(ctx, b);
    if (isErrorValue(bn)) return bn;
    double x = static_cast<NumberData*>(an->data())->value;
    double y = static_cast<NumberData*>(bn->data())->value;

    switch (op) {
        case BinaryOperator::BitAnd:
            return ctx->createNumber(
                static_cast<double>(toInt32(x) & toInt32(y)));
        case BinaryOperator::BitOr:
            return ctx->createNumber(
                static_cast<double>(toInt32(x) | toInt32(y)));
        case BinaryOperator::BitXor:
            return ctx->createNumber(
                static_cast<double>(toInt32(x) ^ toInt32(y)));
        case BinaryOperator::ShiftLeft: {
            uint32_t shift = toUint32(y) & 31u;
            uint32_t r = static_cast<uint32_t>(toInt32(x)) << shift;
            return ctx->createNumber(static_cast<double>(fromUint32(r)));
        }
        case BinaryOperator::ShiftRight: {
            uint32_t shift = toUint32(y) & 31u;
            return ctx->createNumber(
                static_cast<double>(arithmeticShiftRight(toInt32(x), shift)));
        }
        case BinaryOperator::UnsignedShiftRight: {
            uint32_t shift = toUint32(y) & 31u;
            return ctx->createNumber(
                static_cast<double>(toUint32(x) >> shift));
        }
        default:
            return ctx->createNumber(0);
    }
}

// —— in（ES2024 §13.10.1）——
inline Value* inOperator(IContext* ctx, Value* key, Value* obj) {
    if (isErrorValue(key)) return key;
    if (isErrorValue(obj)) return obj;
    if (!isObjectKind(obj->type()->kind())) {
        return typeError(ctx, "right-hand side of 'in' is not an object");
    }
    return ctx->createBoolean(ctx->hasProperty(obj, key));
}

// —— typeof（ES2024 §13.5.3）——
inline const char* typeofName(Value* v) {
    TypeKind k = v->type()->kind();
    switch (k) {
        case TypeKind::Undefined: return "undefined";
        case TypeKind::Null:      return "object";
        case TypeKind::Boolean:   return "boolean";
        case TypeKind::Number:    return "number";
        case TypeKind::String:    return "string";
        case TypeKind::Symbol:    return "symbol";
        case TypeKind::Function:  return "function";
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
            Value* an = toNumberValue(ctx, a);
            if (isErrorValue(an)) return an;
            Value* bn = toNumberValue(ctx, b);
            if (isErrorValue(bn)) return bn;
            double x = static_cast<NumberData*>(an->data())->value;
            double y = static_cast<NumberData*>(bn->data())->value;
            return ctx->createNumber(std::fmod(x, y));
        }
        case BinaryOperator::Exp: {
            Value* an = toNumberValue(ctx, a);
            if (isErrorValue(an)) return an;
            Value* bn = toNumberValue(ctx, b);
            if (isErrorValue(bn)) return bn;
            double x = static_cast<NumberData*>(an->data())->value;
            double y = static_cast<NumberData*>(bn->data())->value;
            return ctx->createNumber(std::pow(x, y));
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
            Value* n = toNumberValue(ctx, v);
            if (isErrorValue(n)) return n;
            return ctx->createNumber(-static_cast<NumberData*>(n->data())->value);
        }
        case UnaryOperator::LogicalNot:
            return ctx->createBoolean(!toBoolean(v));
        case UnaryOperator::BitNot: {
            Value* n = toNumberValue(ctx, v);
            if (isErrorValue(n)) return n;
            uint32_t u = static_cast<uint32_t>(
                toInt32(static_cast<NumberData*>(n->data())->value));
            return ctx->createNumber(static_cast<double>(fromUint32(~u)));
        }
        case UnaryOperator::TypeOf:
            return ctx->createString(typeofName(v));
        case UnaryOperator::Void:
            return ctx->createUndefined();
    }
    return ctx->createUndefined();
}

}  // namespace neo::core::detail

#endif  // NEO_CORE_PRIVATE_OPERATOR_BEHAVIOR_H
