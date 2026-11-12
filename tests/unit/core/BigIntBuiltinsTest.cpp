// BigIntBuiltinsTest.cpp - BigInt 构造函数/原型/静态方法（引擎接入第二步）
//
// 覆盖：BigInt(value) 各类型转换、不可构造、prototype 元数据与双向引用、
// toString/valueOf/toLocaleString、asIntN/asUintN、包装对象互操作、Number(bigint)。

#include "bigint/BigInt.h"
#include "core/ErrorKind.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/BigInt.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>

namespace {

using neo::bigint::BigInt;
using neo::core::IContext;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

struct Fixture {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    NullSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx = createContext(alloc.get(), &console);

    Value* bigintFn() { return ctx->global()->lookup("BigInt"); }
    Value* big(int64_t v) { return ctx->createBigInt(BigInt(v)); }
    Value* num(double v) { return ctx->createNumber(v); }
    Value* str(const std::string& v) { return ctx->createString(v); }

    Value* callBigInt(size_t argc, Value* argv[]) {
        return ctx->call(bigintFn(), ctx->createUndefined(), argc, argv);
    }
    Value* callMethod(Value* obj, const char* name, size_t argc, Value* argv[]) {
        Value* fn = ctx->getProperty(obj, name);
        return ctx->call(fn, obj, argc, argv);
    }
    Value* callStatic(const char* name, size_t argc, Value* argv[]) {
        Value* fn = ctx->getProperty(bigintFn(), name);
        return ctx->call(fn, bigintFn(), argc, argv);
    }
};

const BigInt& valueOf(Value* v) {
    return static_cast<neo::core::BigIntData*>(v->data())->value;
}

std::string text(Value* v) {
    return static_cast<neo::core::StringData*>(v->data())->value;
}

double numOf(Value* v) {
    return static_cast<neo::core::NumberData*>(v->data())->value;
}

bool isException(Value* v) {
    return v->type()->kind() == TypeKind::Exception;
}

std::string exceptionName(IContext* ctx, Value* v) {
    auto* e = static_cast<neo::core::ExceptionData*>(v->data());
    return text(ctx->getProperty(e->child, "name"));
}

Value* returnTwoBigInt(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createBigInt(BigInt(static_cast<int64_t>(2)));
}

}  // namespace

TEST(BigIntBuiltinsTest, ConstructorConversions) {
    Fixture f;
    ASSERT_NE(f.bigintFn(), nullptr);

    Value* none[1] = {nullptr};
    EXPECT_EQ(valueOf(f.callBigInt(0, none)).toString(), "0");
    Value* n42[1] = {f.num(42)};
    EXPECT_EQ(valueOf(f.callBigInt(1, n42)).toString(), "42");
    Value* negZero[1] = {f.num(-0.0)};
    EXPECT_EQ(valueOf(f.callBigInt(1, negZero)).toString(), "0");
    Value* s123[1] = {f.str("123")};
    EXPECT_EQ(valueOf(f.callBigInt(1, s123)).toString(), "123");
    Value* shex[1] = {f.str("0x10")};
    EXPECT_EQ(valueOf(f.callBigInt(1, shex)).toString(), "16");
    Value* sempty[1] = {f.str("   ")};
    EXPECT_EQ(valueOf(f.callBigInt(1, sempty)).toString(), "0");
    Value* btrue[1] = {f.ctx->createBoolean(true)};
    EXPECT_EQ(valueOf(f.callBigInt(1, btrue)).toString(), "1");
    Value* already[1] = {f.big(-7)};
    EXPECT_EQ(valueOf(f.callBigInt(1, already)).toString(), "-7");

    // 对象先 ToPrimitive(number)。
    Value* obj = f.ctx->createObject();
    f.ctx->defineProperty(obj, "valueOf", f.ctx->createNativeFunction(returnTwoBigInt, 0),
                          true, true, true);
    Value* objArg[1] = {obj};
    EXPECT_EQ(valueOf(f.callBigInt(1, objArg)).toString(), "2");

    // 非整数 / 非有限 Number → RangeError。
    Value* frac[1] = {f.num(1.5)};
    Value* r = f.callBigInt(1, frac);
    ASSERT_TRUE(isException(r));
    EXPECT_EQ(exceptionName(f.ctx.get(), r), "RangeError");
    Value* nan[1] = {f.num(std::nan(""))};
    EXPECT_EQ(exceptionName(f.ctx.get(), f.callBigInt(1, nan)), "RangeError");
    Value* inf[1] = {f.num(INFINITY)};
    EXPECT_EQ(exceptionName(f.ctx.get(), f.callBigInt(1, inf)), "RangeError");

    // 非法字符串 → SyntaxError。
    Value* bad[1] = {f.str("1.5")};
    EXPECT_EQ(exceptionName(f.ctx.get(), f.callBigInt(1, bad)), "SyntaxError");
    Value* bad2[1] = {f.str("x")};
    EXPECT_EQ(exceptionName(f.ctx.get(), f.callBigInt(1, bad2)), "SyntaxError");

    // null/undefined/Symbol → TypeError。
    Value* nullArg[1] = {f.ctx->createNull()};
    EXPECT_EQ(exceptionName(f.ctx.get(), f.callBigInt(1, nullArg)), "TypeError");
    Value* undefArg[1] = {f.ctx->createUndefined()};
    EXPECT_EQ(exceptionName(f.ctx.get(), f.callBigInt(1, undefArg)), "TypeError");
    Value* symArg[1] = {f.ctx->createSymbol("s")};
    EXPECT_EQ(exceptionName(f.ctx.get(), f.callBigInt(1, symArg)), "TypeError");
}

TEST(BigIntBuiltinsTest, NotConstructible) {
    Fixture f;
    Value* arg[1] = {f.num(1)};
    Value* r = f.ctx->construct(f.bigintFn(), 1, arg);
    ASSERT_TRUE(isException(r));
    EXPECT_EQ(exceptionName(f.ctx.get(), r), "TypeError");
}

TEST(BigIntBuiltinsTest, PrototypeMetadata) {
    Fixture f;
    Value* ctor = f.bigintFn();
    Value* proto = f.ctx->getProperty(ctor, "prototype");
    ASSERT_EQ(proto->type()->kind(), TypeKind::Object);
    EXPECT_EQ(f.ctx->getPrototype(proto), f.ctx->objectPrototype());
    EXPECT_EQ(f.ctx->getProperty(proto, "constructor"), ctor);

    Value* tag = f.ctx->getProperty(
        proto, f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToStringTag));
    EXPECT_EQ(text(tag), "BigInt");

    EXPECT_EQ(text(f.ctx->getProperty(ctor, "name")), "BigInt");
    EXPECT_DOUBLE_EQ(numOf(f.ctx->getProperty(ctor, "length")), 1.0);
    EXPECT_DOUBLE_EQ(
        numOf(f.ctx->getProperty(f.ctx->getProperty(proto, "toString"), "length")),
        1.0);
    EXPECT_DOUBLE_EQ(
        numOf(f.ctx->getProperty(f.ctx->getProperty(ctor, "asIntN"), "length")),
        2.0);
    EXPECT_DOUBLE_EQ(
        numOf(f.ctx->getProperty(f.ctx->getProperty(ctor, "asUintN"), "length")),
        2.0);
}

TEST(BigIntBuiltinsTest, PrototypeMethods) {
    Fixture f;
    Value* proto = f.ctx->getProperty(f.bigintFn(), "prototype");

    // toString 默认/指定进制。
    Value* v = f.big(255);
    Value* noArg[1] = {nullptr};
    EXPECT_EQ(text(f.callMethod(v, "toString", 0, noArg)), "255");
    Value* radix16[1] = {f.num(16)};
    EXPECT_EQ(text(f.callMethod(v, "toString", 1, radix16)), "ff");
    Value* radix2[1] = {f.num(2)};
    EXPECT_EQ(text(f.callMethod(v, "toString", 1, radix2)), "11111111");
    Value* radix36[1] = {f.num(36)};
    EXPECT_EQ(text(f.callMethod(f.big(35), "toString", 1, radix36)), "z");

    // 非法 radix → RangeError。
    Value* radix1[1] = {f.num(1)};
    Value* r = f.callMethod(v, "toString", 1, radix1);
    ASSERT_TRUE(isException(r));
    EXPECT_EQ(exceptionName(f.ctx.get(), r), "RangeError");
    Value* radixNan[1] = {f.num(std::nan(""))};
    EXPECT_EQ(exceptionName(f.ctx.get(), f.callMethod(v, "toString", 1, radixNan)),
              "RangeError");

    // this 不是 BigInt → TypeError。
    Value* badThis[1] = {nullptr};
    EXPECT_EQ(exceptionName(f.ctx.get(),
                            f.ctx->call(f.ctx->getProperty(proto, "toString"),
                                        f.num(1), 0, badThis)),
              "TypeError");

    // valueOf / toLocaleString。
    EXPECT_EQ(f.callMethod(v, "valueOf", 0, noArg), v);
    EXPECT_EQ(text(f.callMethod(v, "toLocaleString", 0, noArg)), "255");

    // 原始值属性访问自动 ToObject：1n.toString()。
    Value* fn = f.ctx->getProperty(f.big(42), "toString");
    Value* out = f.ctx->call(fn, f.big(42), 0, noArg);
    EXPECT_EQ(text(out), "42");
}

TEST(BigIntBuiltinsTest, AsUintNAndAsIntN) {
    Fixture f;
    auto asUintN = [&](double bits, Value* value) {
        Value* argv[2] = {f.num(bits), value};
        return f.callStatic("asUintN", 2, argv);
    };
    auto asIntN = [&](double bits, Value* value) {
        Value* argv[2] = {f.num(bits), value};
        return f.callStatic("asIntN", 2, argv);
    };

    EXPECT_EQ(valueOf(asUintN(8, f.big(255))).toString(), "255");
    EXPECT_EQ(valueOf(asUintN(8, f.big(256))).toString(), "0");
    EXPECT_EQ(valueOf(asUintN(8, f.big(-1))).toString(), "255");
    EXPECT_EQ(valueOf(asUintN(0, f.big(5))).toString(), "0");
    EXPECT_EQ(valueOf(asUintN(100, f.big(1))).toString(), "1");

    EXPECT_EQ(valueOf(asIntN(8, f.big(255))).toString(), "-1");
    EXPECT_EQ(valueOf(asIntN(8, f.big(128))).toString(), "-128");
    EXPECT_EQ(valueOf(asIntN(8, f.big(127))).toString(), "127");
    EXPECT_EQ(valueOf(asIntN(8, f.big(-129))).toString(), "127");
    EXPECT_EQ(valueOf(asIntN(0, f.big(5))).toString(), "0");
    EXPECT_EQ(valueOf(asIntN(100, f.big(-1))).toString(), "-1");

    // bits 为 BigInt → TypeError；负数 → RangeError；value 为 Number → TypeError。
    Value* badBits[2] = {f.big(8), f.big(1)};
    EXPECT_EQ(exceptionName(f.ctx.get(), f.callStatic("asUintN", 2, badBits)),
              "TypeError");
    Value* negBits[2] = {f.num(-1), f.big(1)};
    EXPECT_EQ(exceptionName(f.ctx.get(), f.callStatic("asUintN", 2, negBits)),
              "RangeError");
    Value* numValue[2] = {f.num(8), f.num(1)};
    EXPECT_EQ(exceptionName(f.ctx.get(), f.callStatic("asIntN", 2, numValue)),
              "TypeError");
}

TEST(BigIntBuiltinsTest, WrapperAndNumberInterop) {
    Fixture f;
    Value* primitive = f.big(42);
    Value* wrapper = f.ctx->createPrimitiveWrapper(primitive);
    ASSERT_EQ(wrapper->type()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_EQ(neo::core::wrapperPrimitiveValue(wrapper), primitive);
    EXPECT_EQ(text(f.ctx->toString(wrapper)), "42");

    Value* noArg[1] = {nullptr};
    EXPECT_EQ(text(f.callMethod(wrapper, "toString", 0, noArg)), "42");
    EXPECT_EQ(f.callMethod(wrapper, "valueOf", 0, noArg), primitive);

    // Number(bigint) 允许（ToNumber 本身不允许）。
    Value* numberFn = f.ctx->global()->lookup("Number");
    ASSERT_NE(numberFn, nullptr);
    Value* arg[1] = {f.big(42)};
    Value* n = f.ctx->call(numberFn, f.ctx->createUndefined(), 1, arg);
    ASSERT_EQ(n->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(n), 42.0);

    Value* huge[1] = {f.ctx->createBigInt(BigInt(1) << 100)};
    Value* hn = f.ctx->call(numberFn, f.ctx->createUndefined(), 1, huge);
    ASSERT_EQ(hn->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(hn), std::ldexp(1.0, 100));
}
