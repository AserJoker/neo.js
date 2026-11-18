// MathBuiltins.cpp - Math 命名空间（ES2024 §21.3）
//
// 逐方法对齐规范：
//   - 缺参按 undefined → ToNumber → NaN；
//   - max/min 的 NaN 传播与 ±0 选择；
//   - round 的「半值向 +∞」与 -0 保留；
//   - clz32/imul 走 ToUint32/ToInt32；
//   - fround 经 float 往返。
//
// 关联 ADR: 020（标准库组织）

#include "core/MathBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/OperatorBehavior.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

namespace neo::core {

namespace {

// 取第 i 个实参并 ToNumber；缺参按 undefined（→ NaN）。
Value* numberAt(IContext* ctx, size_t argc, Value* argv[], size_t i) {
    Value* v = i < argc ? argv[i] : ctx->createUndefined();
    return ctx->toNumber(v);
}

double numberOrNaN(Value* v) {
    return static_cast<NumberData*>(v->data())->value;
}

Value* unaryMath(IContext* ctx, size_t argc, Value* argv[], double (*fn)(double)) {
    Value* n = numberAt(ctx, argc, argv, 0);
    if (detail::isErrorValue(n)) return n;
    return ctx->createNumber(fn(numberOrNaN(n)));
}

Value* mathAbs(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::fabs(x); });
}
Value* mathAcos(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::acos(x); });
}
Value* mathAcosh(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::acosh(x); });
}
Value* mathAsin(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::asin(x); });
}
Value* mathAsinh(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::asinh(x); });
}
Value* mathAtan(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::atan(x); });
}
Value* mathAtanh(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::atanh(x); });
}
Value* mathCbrt(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::cbrt(x); });
}
Value* mathCeil(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::ceil(x); });
}
Value* mathCos(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::cos(x); });
}
Value* mathCosh(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::cosh(x); });
}
Value* mathExp(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::exp(x); });
}
Value* mathExpm1(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::expm1(x); });
}
Value* mathFloor(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::floor(x); });
}
Value* mathLog(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::log(x); });
}
Value* mathLog1p(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::log1p(x); });
}
Value* mathLog2(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::log2(x); });
}
Value* mathLog10(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::log10(x); });
}
Value* mathSin(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::sin(x); });
}
Value* mathSinh(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::sinh(x); });
}
Value* mathSqrt(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::sqrt(x); });
}
Value* mathTan(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::tan(x); });
}
Value* mathTanh(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::tanh(x); });
}
Value* mathTrunc(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    return unaryMath(ctx, argc, argv, [](double x) { return std::trunc(x); });
}

// Math.round：floor(x + 0.5)；[-0.5, 0) 返回 -0。
Value* mathRound(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* n = numberAt(ctx, argc, argv, 0);
    if (detail::isErrorValue(n)) return n;
    double x = numberOrNaN(n);
    if (!std::isfinite(x) || x == 0.0) return ctx->createNumber(x);
    if (x < 0.0 && x >= -0.5) return ctx->createNumber(-0.0);
    return ctx->createNumber(std::floor(x + 0.5));
}

// Math.sign：保留 ±0 与 NaN。
Value* mathSign(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* n = numberAt(ctx, argc, argv, 0);
    if (detail::isErrorValue(n)) return n;
    double x = numberOrNaN(n);
    if (std::isnan(x) || x == 0.0) return ctx->createNumber(x);
    return ctx->createNumber(x > 0.0 ? 1.0 : -1.0);
}

// Math.fround：经 float 往返。
Value* mathFround(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* n = numberAt(ctx, argc, argv, 0);
    if (detail::isErrorValue(n)) return n;
    return ctx->createNumber(static_cast<double>(static_cast<float>(numberOrNaN(n))));
}

// Math.clz32：ToUint32 后前导零个数。
Value* mathClz32(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* n = numberAt(ctx, argc, argv, 0);
    if (detail::isErrorValue(n)) return n;
    uint32_t u = detail::toUint32(numberOrNaN(n));
    uint32_t count = 0;
    if (u == 0) {
        count = 32;
    } else {
        while ((u & 0x80000000u) == 0) {
            u <<= 1;
            ++count;
        }
    }
    return ctx->createNumber(static_cast<double>(count));
}

// Math.imul：ToInt32 乘法按 32 位回绕。
Value* mathImul(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* a = numberAt(ctx, argc, argv, 0);
    if (detail::isErrorValue(a)) return a;
    Value* b = numberAt(ctx, argc, argv, 1);
    if (detail::isErrorValue(b)) return b;
    uint32_t ua = detail::toUint32(numberOrNaN(a));
    uint32_t ub = detail::toUint32(numberOrNaN(b));
    uint32_t r = ua * ub;
    return ctx->createNumber(static_cast<double>(detail::fromUint32(r)));
}

Value* mathAtan2(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* y = numberAt(ctx, argc, argv, 0);
    if (detail::isErrorValue(y)) return y;
    Value* x = numberAt(ctx, argc, argv, 1);
    if (detail::isErrorValue(x)) return x;
    return ctx->createNumber(std::atan2(numberOrNaN(y), numberOrNaN(x)));
}

Value* mathPow(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* base = numberAt(ctx, argc, argv, 0);
    if (detail::isErrorValue(base)) return base;
    Value* exponent = numberAt(ctx, argc, argv, 1);
    if (detail::isErrorValue(exponent)) return exponent;
    return ctx->createNumber(std::pow(numberOrNaN(base), numberOrNaN(exponent)));
}

Value* mathMax(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    double result = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < argc; ++i) {
        Value* n = numberAt(ctx, argc, argv, i);
        if (detail::isErrorValue(n)) return n;
        double x = numberOrNaN(n);
        if (std::isnan(x)) return ctx->createNumber(x);
        if (x > result || (x == 0.0 && result == 0.0 && !std::signbit(x))) {
            result = x;
        }
    }
    return ctx->createNumber(result);
}

Value* mathMin(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    double result = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < argc; ++i) {
        Value* n = numberAt(ctx, argc, argv, i);
        if (detail::isErrorValue(n)) return n;
        double x = numberOrNaN(n);
        if (std::isnan(x)) return ctx->createNumber(x);
        if (x < result || (x == 0.0 && result == 0.0 && std::signbit(x))) {
            result = x;
        }
    }
    return ctx->createNumber(result);
}

// Math.hypot：逐项 hypot 避免中间平方溢出；NaN 传播，Infinity 优先。
Value* mathHypot(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    double result = 0.0;
    bool hasNaN = false;
    for (size_t i = 0; i < argc; ++i) {
        Value* n = numberAt(ctx, argc, argv, i);
        if (detail::isErrorValue(n)) return n;
        double x = numberOrNaN(n);
        if (std::isinf(x)) return ctx->createNumber(std::numeric_limits<double>::infinity());
        if (std::isnan(x)) {
            hasNaN = true;
            continue;
        }
        result = std::hypot(result, x);
    }
    if (hasNaN) return ctx->createNumber(std::numeric_limits<double>::quiet_NaN());
    return ctx->createNumber(result);
}

// Math.random：宿主无 RNG 注入时的进程内 fallback。
// 值域 [0,1)，线程局部引擎避免每次构造开销。
Value* mathRandom(IContext* ctx, Value*, size_t /*argc*/, Value* /*argv*/[]) {
    static thread_local std::mt19937_64 engine(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return ctx->createNumber(dist(engine));
}

// 定义 Math 方法（writable/不可枚举/可配置）。
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc, int lengthOverride = -1) {
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

// 定义只读常量（不可写/不可枚举/不可配置）。
bool defineConstant(IContext* ctx, Value* target, const char* name, double value) {
    Value* v = ctx->createNumber(value);
    if (v == ctx->oomValue()) return false;
    return ctx->defineProperty(target, name, v, false, false, false)->type()->kind() !=
           TypeKind::Exception;
}

}  // namespace

bool registerMathBuiltins(IContext* ctx) {
    Value* math = ctx->createObject();
    if (math == ctx->oomValue()) return false;

    // 一元方法：minArgc=0（缺参 → NaN），length=1。
    struct UnaryEntry {
        const char* name;
        NativeFn fn;
    };
    const UnaryEntry unary[] = {
        {"abs", mathAbs},       {"acos", mathAcos},     {"acosh", mathAcosh},
        {"asin", mathAsin},     {"asinh", mathAsinh},   {"atan", mathAtan},
        {"atanh", mathAtanh},   {"cbrt", mathCbrt},     {"ceil", mathCeil},
        {"cos", mathCos},       {"cosh", mathCosh},     {"exp", mathExp},
        {"expm1", mathExpm1},   {"floor", mathFloor},   {"fround", mathFround},
        {"log", mathLog},       {"log1p", mathLog1p},   {"log2", mathLog2},
        {"log10", mathLog10},   {"round", mathRound},   {"sign", mathSign},
        {"sin", mathSin},       {"sinh", mathSinh},     {"sqrt", mathSqrt},
        {"tan", mathTan},       {"tanh", mathTanh},     {"trunc", mathTrunc},
        {"clz32", mathClz32},
    };
    for (const UnaryEntry& entry : unary) {
        if (!defineMethod(ctx, math, entry.name, entry.fn, 0, 1)) return false;
    }

    // 二元方法：length=2，但缺参合法（按 undefined → NaN / 恒等值处理）。
    if (!defineMethod(ctx, math, "atan2", mathAtan2, 0, 2)) return false;
    if (!defineMethod(ctx, math, "pow", mathPow, 0, 2)) return false;
    if (!defineMethod(ctx, math, "imul", mathImul, 0, 2)) return false;
    if (!defineMethod(ctx, math, "max", mathMax, 0, 2)) return false;
    if (!defineMethod(ctx, math, "min", mathMin, 0, 2)) return false;
    if (!defineMethod(ctx, math, "hypot", mathHypot, 0, 2)) return false;
    if (!defineMethod(ctx, math, "random", mathRandom, 0)) return false;

    if (!defineConstant(ctx, math, "E", 2.7182818284590452354)) return false;
    if (!defineConstant(ctx, math, "LN10", 2.302585092994046)) return false;
    if (!defineConstant(ctx, math, "LN2", 0.6931471805599453)) return false;
    if (!defineConstant(ctx, math, "LOG10E", 0.4342944819032518)) return false;
    if (!defineConstant(ctx, math, "LOG2E", 1.4426950408889634)) return false;
    if (!defineConstant(ctx, math, "PI", 3.14159265358979323846)) return false;
    if (!defineConstant(ctx, math, "SQRT1_2", 0.7071067811865476)) return false;
    if (!defineConstant(ctx, math, "SQRT2", 1.4142135623730951)) return false;

    Value* tag = ctx->createString("Math");
    if (tag == ctx->oomValue()) return false;
    if (ctx->defineProperty(math, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            tag, false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    ctx->defineGlobal("Math", math, true, false, true);
    return true;
}

}  // namespace neo::core
