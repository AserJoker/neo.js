// BigIntBuiltins.cpp - BigInt 构造函数/原型/静态方法
//
// 语义要点：
//   - BigInt(value) 先 ToPrimitive(number)：Number 必须为整数（否则 RangeError），
//     String 走 StringToBigInt（非法 → SyntaxError），其余走 ToBigInt；
//   - BigInt 不可构造（new BigInt → TypeError），但仍有 prototype 属性；
//   - asUintN/asIntN 用 2^bits 取模，asIntN 再把 [2^(bits-1), 2^bits) 映射到负数；
//   - bits 上限取实现上限（避免分配不可行的 2^bits）。
//
// 关联 ADR: 020（标准库组织）、039（规范错误）、051（BigInt 引擎接入）

#include "core/BigIntBuiltins.h"

#include "core/BigIntOps.h"
#include "core/ErrorKind.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/BigInt.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/TypeBehavior.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace neo::core {

namespace {

// asIntN/asUintN 的 bits 实际上限（2^24 位 ≈ 2MB magnitude）。
// 超过时只在结果可证明落在范围内才返回原值，否则 RangeError。
constexpr uint64_t kMaxAsIntNBits = 1ULL << 24;

// 取 this 的 BigInt 原始值：this 是 bigint 或 BigInt 包装对象。
Value* unwrapBigInt(Value* self) {
    if (self == nullptr) return nullptr;
    if (self->type()->kind() == TypeKind::BigInt) return self;
    if (self->type()->kind() == TypeKind::PrimitiveWrapper) {
        Value* p = wrapperPrimitiveValue(self);
        if (p != nullptr && p->type()->kind() == TypeKind::BigInt) return p;
    }
    return nullptr;
}

Value* thisTypeError(IContext* ctx, const char* method) {
    std::string msg = std::string(method) + " requires that 'this' be a BigInt";
    return detail::typeError(ctx, msg);
}

// ToBigInt（ES2024 §7.1.13）：Number 不走这里（BigInt(number) 单独处理）。
Value* toBigIntValue(IContext* ctx, Value* v, Value*& out) {
    switch (v->type()->kind()) {
        case TypeKind::BigInt:
            out = v;
            return nullptr;
        case TypeKind::Boolean:
            out = ctx->createBigInt(
                neo::bigint::BigInt(static_cast<int64_t>(
                    static_cast<BooleanData*>(v->data())->value ? 1 : 0)));
            return nullptr;
        case TypeKind::String: {
            neo::bigint::BigInt parsed;
            if (!detail::stringToBigInt(
                    static_cast<StringData*>(v->data())->value, parsed)) {
                return ctx->createErrorException(
                    ErrorKind::SyntaxError,
                    "Cannot convert the string to a BigInt");
            }
            out = ctx->createBigInt(parsed);
            return nullptr;
        }
        case TypeKind::Number:
            return detail::typeError(
                ctx, "Cannot convert a Number value to a BigInt");
        case TypeKind::Symbol:
            return detail::typeError(
                ctx, "Cannot convert a Symbol value to a BigInt");
        case TypeKind::Undefined:
        case TypeKind::Null:
            return detail::typeError(
                ctx, "Cannot convert undefined or null to a BigInt");
        default: {
            Value* prim = detail::toPrimitive(ctx, v, detail::PreferredType::Number);
            if (detail::isErrorValue(prim)) return prim;
            return toBigIntValue(ctx, prim, out);
        }
    }
}

// ToIndex（ES2024 §7.1.22）：ToIntegerOrInfinity 后限制在 [0, 2^53-1]。
Value* toIndexValue(IContext* ctx, Value* v, uint64_t& out) {
    Value* n = detail::toNumberValue(ctx, v);
    if (detail::isErrorValue(n)) return n;
    double d = static_cast<NumberData*>(n->data())->value;
    if (std::isnan(d)) d = 0.0;
    d = std::trunc(d);
    if (d < 0.0 || d > 9007199254740991.0) {
        return detail::rangeError(ctx, "Invalid index");
    }
    out = static_cast<uint64_t>(d);
    return nullptr;
}

// BigInt(value)
Value* bigintCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createBigInt(neo::bigint::BigInt());

    Value* prim = detail::toPrimitive(ctx, argv[0], detail::PreferredType::Number);
    if (detail::isErrorValue(prim)) return prim;

    if (prim->type()->kind() == TypeKind::Number) {
        neo::bigint::BigInt converted;
        if (!neo::bigint::BigInt::fromDouble(
                static_cast<NumberData*>(prim->data())->value, converted)) {
            return detail::rangeError(
                ctx,
                "The number cannot be converted to a BigInt because it is not an integer");
        }
        return ctx->createBigInt(converted);
    }

    Value* out = nullptr;
    Value* err = toBigIntValue(ctx, prim, out);
    if (err != nullptr) return err;
    return out;
}

// BigInt.prototype.toString(radix)
Value* bigintToString(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* p = unwrapBigInt(self);
    if (p == nullptr) return thisTypeError(ctx, "BigInt.prototype.toString");

    int radix = 10;
    if (argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined) {
        Value* n = detail::toNumberValue(ctx, argv[0]);
        if (detail::isErrorValue(n)) return n;
        double d = static_cast<NumberData*>(n->data())->value;
        if (std::isnan(d)) d = 0.0;
        d = std::trunc(d);
        if (d < 2.0 || d > 36.0) {
            return detail::rangeError(
                ctx, "toString() radix must be between 2 and 36");
        }
        radix = static_cast<int>(d);
    }
    return ctx->createString(detail::bigIntValue(p).toString(radix));
}

// BigInt.prototype.toLocaleString()：无 Intl 时等价于 toString()。
Value* bigintToLocaleString(IContext* ctx, Value* self, size_t /*argc*/,
                            Value* /*argv*/[]) {
    Value* p = unwrapBigInt(self);
    if (p == nullptr) {
        return thisTypeError(ctx, "BigInt.prototype.toLocaleString");
    }
    return ctx->createString(detail::bigIntValue(p).toString(10));
}

// BigInt.prototype.valueOf()
Value* bigintValueOf(IContext* ctx, Value* self, size_t /*argc*/,
                     Value* /*argv*/[]) {
    Value* p = unwrapBigInt(self);
    if (p == nullptr) return thisTypeError(ctx, "BigInt.prototype.valueOf");
    return p;
}

// BigInt.asUintN(bits, bigint)
Value* bigintAsUintN(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                     Value* argv[]) {
    uint64_t bits = 0;
    if (Value* err = toIndexValue(ctx, argv[0], bits)) return err;
    Value* value = nullptr;
    if (Value* err = toBigIntValue(ctx, argv[1], value)) return err;

    const neo::bigint::BigInt& v = detail::bigIntValue(value);
    if (bits == 0) return ctx->createBigInt(neo::bigint::BigInt());
    if (bits > kMaxAsIntNBits) {
        if (!v.isNegative() && v.bitLength() <= bits) return value;
        return detail::rangeError(ctx, "Maximum BigInt size exceeded");
    }
    const neo::bigint::BigInt modulus =
        neo::bigint::BigInt(static_cast<uint64_t>(1)) << bits;
    neo::bigint::BigInt r = v % modulus;
    if (r.isNegative()) r = r + modulus;
    return ctx->createBigInt(r);
}

// BigInt.asIntN(bits, bigint)
Value* bigintAsIntN(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                    Value* argv[]) {
    uint64_t bits = 0;
    if (Value* err = toIndexValue(ctx, argv[0], bits)) return err;
    Value* value = nullptr;
    if (Value* err = toBigIntValue(ctx, argv[1], value)) return err;

    const neo::bigint::BigInt& v = detail::bigIntValue(value);
    if (bits == 0) return ctx->createBigInt(neo::bigint::BigInt());
    if (bits > kMaxAsIntNBits) {
        // 可证明落在 [-2^(bits-1), 2^(bits-1)) 内则原样返回。
        if (!v.isNegative() && v.bitLength() < bits) return value;
        if (v.isNegative() && v.bitLength() <= bits) return value;
        return detail::rangeError(ctx, "Maximum BigInt size exceeded");
    }
    const neo::bigint::BigInt modulus =
        neo::bigint::BigInt(static_cast<uint64_t>(1)) << bits;
    neo::bigint::BigInt r = v % modulus;
    if (r.isNegative()) r = r + modulus;
    const neo::bigint::BigInt half =
        neo::bigint::BigInt(static_cast<uint64_t>(1)) << (bits - 1);
    if (r >= half) r = r - modulus;
    return ctx->createBigInt(r);
}

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t length) {
    Value* f = ctx->createNativeFunction(fn, 0);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    Value* len = ctx->createNumber(static_cast<double>(length));
    if (ctx->defineProperty(f, "length", len, false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    return ctx->defineProperty(target, name, f, true, false, true)
               ->type()->kind() != TypeKind::Exception;
}

bool defineStaticMethod(IContext* ctx, Value* target, const char* name,
                        NativeFn fn) {
    Value* f = ctx->createNativeFunction(fn, 2);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, false, true)
               ->type()->kind() != TypeKind::Exception;
}

}  // namespace

bool registerBigIntBuiltins(IContext* ctx) {
    // BigInt.prototype 是普通对象（不是 BigInt 包装对象，ES2024 §21.2.3）。
    Value* proto = ctx->createObject();
    if (proto == ctx->oomValue()) return false;
    ctx->setBigIntPrototype(proto);

    if (!defineMethod(ctx, proto, "toString", bigintToString, 1)) return false;
    if (!defineMethod(ctx, proto, "toLocaleString", bigintToLocaleString, 0)) {
        return false;
    }
    if (!defineMethod(ctx, proto, "valueOf", bigintValueOf, 0)) return false;
    if (ctx->defineProperty(
            proto, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
            ctx->createString("BigInt"), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // BigInt 可调用但不可构造；prototype 属性手动定义。
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* ctor = ctx->createNativeFunction(bigintCtor, 0, {}, opts);
    if (ctor == ctx->oomValue()) return false;
    ctx->setFunctionName(ctor, "BigInt");
    if (ctx->defineProperty(ctor, "length", ctx->createNumber(1.0), false, false,
                            true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(ctor, "prototype", proto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(proto, "constructor", ctor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineStaticMethod(ctx, ctor, "asUintN", bigintAsUintN)) return false;
    if (!defineStaticMethod(ctx, ctor, "asIntN", bigintAsIntN)) return false;

    ctx->global()->add(ctor);
    ctx->global()->bind("BigInt", ctor);
    return true;
}

}  // namespace neo::core
