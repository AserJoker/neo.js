// ToPrimitiveTest.cpp - ToPrimitive / OrdinaryToPrimitive 转换链单元测试
//
// 验证（ES2024 §7.1.1 / §7.1.1.1 / §7.1.4 / §7.1.14 / §7.1.19 / §13.15.3）：
// - 原始值 ToPrimitive 原样返回
// - OrdinaryToPrimitive 按 hint 选择 valueOf/toString 顺序
// - @@toPrimitive 优先于 valueOf/toString，hint 字符串正确
// - @@toPrimitive 返回对象 → TypeError；不可调用 → TypeError
// - ToNumber / ToString / ToPropertyKey 对对象先 ToPrimitive
// - Add 规则：任一侧 ToPrimitive 为 string → 拼接
// - Symbol 的 ToNumber/ToString 抛 TypeError
// - 异常透传（valueOf 抛错、Fatal 优先）
//
// 关联 ADR: 016（Type 体系）、006（异常即值）、017（Fatal 透传）

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

namespace {

using neo::core::IContext;
using neo::core::NumberData;
using neo::core::StringData;
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

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }

// 造一个带 valueOf/toString 的对象（返回固定原始值）。
// native 函数无捕获，故调用计数存在对象自身的 __voc/__tsc 属性上。
Value* makeObject(IContext* ctx, double num, const std::string& str) {
    Value* obj = ctx->createObject();
    Value* vo = ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t, Value*[]) {
            Value* n = c->getProperty(self, "__num");
            Value* cnt = c->getProperty(self, "__voc");
            c->setProperty(self, "__voc", c->createNumber(
                static_cast<NumberData*>(cnt->data())->value + 1));
            return n;
        },
        0);
    Value* ts = ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t, Value*[]) {
            Value* s = c->getProperty(self, "__str");
            Value* cnt = c->getProperty(self, "__tsc");
            c->setProperty(self, "__tsc", c->createNumber(
                static_cast<NumberData*>(cnt->data())->value + 1));
            return s;
        },
        0);
    ctx->defineProperty(obj, "__num", ctx->createNumber(num));
    ctx->defineProperty(obj, "__str", ctx->createString(str));
    ctx->defineProperty(obj, "__voc", ctx->createNumber(0));
    ctx->defineProperty(obj, "__tsc", ctx->createNumber(0));
    ctx->defineProperty(obj, "valueOf", vo);
    ctx->defineProperty(obj, "toString", ts);
    return obj;
}

int counterOf(IContext* ctx, Value* obj, const char* name) {
    return static_cast<int>(numOf(ctx->getProperty(obj, name)));
}

}  // namespace

// ===== 原始值：ToPrimitive 原样返回 =====

TEST(ToPrimitiveTest, PrimitivesPassThrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* u = ctx->createUndefined();
    Value* n = ctx->createNull();
    Value* b = ctx->createBoolean(true);
    Value* num = ctx->createNumber(1);
    Value* s = ctx->createString("x");
    Value* sym = ctx->createSymbol("s");

    // 原始值参与运算时不被转换（+ 走拼接/数值）
    EXPECT_EQ(strOf(ctx->add(u, s)), "undefinedx");
    EXPECT_EQ(strOf(ctx->add(n, s)), "nullx");
    EXPECT_EQ(strOf(ctx->add(b, s)), "truex");
    EXPECT_EQ(strOf(ctx->add(num, s)), "1x");
    EXPECT_EQ(strOf(ctx->add(s, s)), "xx");
    // symbol 无法转字符串
    EXPECT_EQ(ctx->add(sym, s)->type()->kind(), TypeKind::Exception);
}

// ===== OrdinaryToPrimitive：hint 决定顺序 =====

TEST(ToPrimitiveTest, DefaultHintPrefersValueOf) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = makeObject(ctx.get(), 7, "seven");
    // 缺省 hint 等价 NUMBER → 先 valueOf
    Value* sum = ctx->add(obj, ctx->createNumber(1));
    ASSERT_EQ(sum->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(sum), 8.0);
    EXPECT_EQ(counterOf(ctx.get(), obj, "__voc"), 1);
    EXPECT_EQ(counterOf(ctx.get(), obj, "__tsc"), 0);
}

TEST(ToPrimitiveTest, StringHintPrefersToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = makeObject(ctx.get(), 7, "seven");
    // toString(obj) 用 STRING hint → 先 toString
    Value* s = ctx->toString(obj);
    ASSERT_EQ(s->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(s), "seven");
    EXPECT_EQ(counterOf(ctx.get(), obj, "__tsc"), 1);
    EXPECT_EQ(counterOf(ctx.get(), obj, "__voc"), 0);
}

TEST(ToPrimitiveTest, NumberHintForArithmetic) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = makeObject(ctx.get(), 10, "ten");
    // 算术用 NUMBER hint → 先 valueOf
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(obj, ctx->createNumber(4))), 6.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->mul(obj, ctx->createNumber(2))), 20.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->div(obj, ctx->createNumber(5))), 2.0);
    EXPECT_EQ(counterOf(ctx.get(), obj, "__voc"), 3);
}

TEST(ToPrimitiveTest, FallsBackToSecondMethodWhenFirstReturnsObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // valueOf 返回对象 → 继续尝试 toString
    Value* obj = ctx->createObject();
    Value* vo = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createObject(); }, 0);
    Value* ts = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString("fallback"); }, 0);
    ctx->defineProperty(obj, "valueOf", vo);
    ctx->defineProperty(obj, "toString", ts);

    EXPECT_EQ(strOf(ctx->toString(obj)), "fallback");
}

TEST(ToPrimitiveTest, NonCallableMethodsSkipped) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    // valueOf 不是函数 → 跳过；toString 可调用
    ctx->defineProperty(obj, "valueOf", ctx->createNumber(1));
    Value* ts = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString("only"); }, 0);
    ctx->defineProperty(obj, "toString", ts);
    EXPECT_EQ(strOf(ctx->toString(obj)), "only");
}

TEST(ToPrimitiveTest, BothMethodsFailReturnsTypeError) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* retObj = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createObject(); }, 0);
    ctx->defineProperty(obj, "valueOf", retObj);
    ctx->defineProperty(obj, "toString", retObj);

    EXPECT_EQ(ctx->toString(obj)->type()->kind(), TypeKind::Exception);
}

TEST(ToPrimitiveTest, NoMethodsAtAllReturnsTypeError) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 原型设为 null 且无自有方法 → 无法转换
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, ctx->createNull());
    EXPECT_EQ(ctx->toString(obj)->type()->kind(), TypeKind::Exception);
}

// ===== @@toPrimitive 优先 =====

TEST(ToPrimitiveTest, ExoticToPrimitiveTakesPrecedence) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* vo = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0);
    ctx->defineProperty(obj, "valueOf", vo);
    // @@toPrimitive 返回 "exotic"，应优先于 valueOf
    Value* exotic = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString("exotic"); }, 0);
    ctx->defineProperty(obj, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToPrimitive),
                        exotic, true, false, true);

    EXPECT_EQ(strOf(ctx->toString(obj)), "exotic");
    // + 规则：exotic 得 string → 拼接
    EXPECT_EQ(strOf(ctx->add(obj, ctx->createNumber(2))), "exotic2");
}

TEST(ToPrimitiveTest, ExoticToPrimitiveReceivesHint) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    // 回显 hint 字符串
    Value* exotic = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t argc, Value* argv[]) {
            return argc > 0 ? argv[0] : c->createUndefined();
        }, 0);
    ctx->defineProperty(obj, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToPrimitive),
                        exotic, true, false, true);

    // toString → "string"
    EXPECT_EQ(strOf(ctx->toString(obj)), "string");
    // 算术（-）→ "number"
    Value* diff = ctx->sub(obj, ctx->createNumber(1));
    // "number" 转数字是 NaN
    EXPECT_TRUE(std::isnan(numOf(diff)));
    // + 的 ToPrimitive 无 hint（ES2024 §13.15.3 step 1.a）→ "default"
    EXPECT_EQ(strOf(ctx->add(obj, ctx->createNumber(1))), "default1");
}

TEST(ToPrimitiveTest, ExoticToPrimitiveDefaultHint) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* exotic = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t argc, Value* argv[]) {
            return argc > 0 ? argv[0] : c->createUndefined();
        }, 0);
    ctx->defineProperty(obj, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToPrimitive),
                        exotic, true, false, true);

    // + 的 ToPrimitive 无 hint（缺省）→ "default"
    EXPECT_EQ(strOf(ctx->add(obj, ctx->createString("!"))), "default!");
}

TEST(ToPrimitiveTest, ExoticToPrimitiveReturningObjectThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* exotic = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createObject(); }, 0);
    ctx->defineProperty(obj, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToPrimitive),
                        exotic, true, false, true);
    EXPECT_EQ(ctx->toString(obj)->type()->kind(), TypeKind::Exception);
}

TEST(ToPrimitiveTest, ExoticToPrimitiveNonCallableThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    // @@toPrimitive 存在但不可调用 → TypeError（ES2024 §7.1.1 step 1.a GetMethod）
    ctx->defineProperty(obj, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToPrimitive),
                        ctx->createNumber(1), true, false, true);
    EXPECT_EQ(ctx->toString(obj)->type()->kind(), TypeKind::Exception);
}

// ===== Symbol 的转换限制 =====

TEST(ToPrimitiveTest, SymbolToNumberThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s");
    EXPECT_EQ(ctx->sub(sym, ctx->createNumber(1))->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->mul(sym, ctx->createNumber(1))->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->div(sym, ctx->createNumber(1))->type()->kind(), TypeKind::Exception);
}

TEST(ToPrimitiveTest, SymbolToStringThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s");
    EXPECT_EQ(ctx->add(sym, ctx->createString("x"))->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->add(ctx->createString("x"), sym)->type()->kind(), TypeKind::Exception);
}

TEST(ToPrimitiveTest, SymbolWrapperToPrimitiveReturnsSymbol) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s");
    Value* wrapper = ctx->createPrimitiveWrapper(sym);
    // Symbol.prototype[@@toPrimitive] 返回 symbol 本身
    Value* exotic = ctx->getProperty(
        wrapper, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToPrimitive));
    ASSERT_EQ(exotic->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->call(exotic, wrapper, 1, nullptr), sym);
}

TEST(ToPrimitiveTest, SymbolWrapperArithmeticThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* wrapper = ctx->createPrimitiveWrapper(ctx->createSymbol("s"));
    EXPECT_EQ(ctx->add(wrapper, ctx->createNumber(1))->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->sub(wrapper, ctx->createNumber(1))->type()->kind(), TypeKind::Exception);
}

// ===== ToPropertyKey =====

TEST(ToPrimitiveTest, ObjectKeyUsesToPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* ts = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString("k"); }, 0);
    ctx->defineProperty(obj, "toString", ts);

    Value* target = ctx->createObject();
    Value* v = ctx->createNumber(42);
    EXPECT_EQ(ctx->setProperty(target, obj, v), v);
    EXPECT_EQ(numOf(ctx->getProperty(target, "k")), 42.0);
    EXPECT_TRUE(ctx->hasProperty(target, obj));
}

TEST(ToPrimitiveTest, SymbolKeyStaysSymbol) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* target = ctx->createObject();
    Value* sym = ctx->createSymbol("s");
    Value* v = ctx->createNumber(1);
    ctx->setProperty(target, sym, v);
    // symbol 键保持符号身份（不进 properties 容器）
    EXPECT_EQ(numOf(ctx->getProperty(target, sym)), 1.0);
    EXPECT_FALSE(ctx->hasProperty(target, "Symbol(s)"));
}

TEST(ToPrimitiveTest, WrapperKeyUnwraps) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* target = ctx->createObject();
    Value* key = ctx->createPrimitiveWrapper(ctx->createString("w"));
    Value* v = ctx->createNumber(5);
    ctx->setProperty(target, key, v);
    EXPECT_EQ(numOf(ctx->getProperty(target, "w")), 5.0);
}

TEST(ToPrimitiveTest, ArrayKeyUsesJoin) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* target = ctx->createObject();
    Value* key = ctx->createArray();
    ctx->setProperty(key, "0", ctx->createNumber(1));
    ctx->setProperty(key, "1", ctx->createNumber(2));
    Value* v = ctx->createNumber(9);
    ctx->setProperty(target, key, v);
    EXPECT_EQ(numOf(ctx->getProperty(target, "1,2")), 9.0);
}

// ===== Add 规则 =====

TEST(ToPrimitiveTest, AddStringWinsWhenEitherSideIsString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(strOf(ctx->add(ctx->createNumber(1), ctx->createString("2"))), "12");
    EXPECT_EQ(strOf(ctx->add(ctx->createString("1"), ctx->createNumber(2))), "12");
    EXPECT_EQ(strOf(ctx->add(ctx->createBoolean(true), ctx->createString("x"))), "truex");
    EXPECT_EQ(strOf(ctx->add(ctx->createNull(), ctx->createString("x"))), "nullx");
    EXPECT_EQ(strOf(ctx->add(ctx->createUndefined(), ctx->createString("x"))), "undefinedx");
}

TEST(ToPrimitiveTest, AddNumericWhenNeitherIsString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createNumber(1), ctx->createNumber(2))), 3.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createBoolean(true), ctx->createNumber(2))), 3.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createNull(), ctx->createNumber(2))), 2.0);
    EXPECT_TRUE(std::isnan(numOf(ctx->add(ctx->createUndefined(), ctx->createNumber(2)))));
}

TEST(ToPrimitiveTest, ArrayAndObjectAddConcatenate) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    EXPECT_EQ(strOf(ctx->add(arr, ctx->createNumber(3))), "1,23");
    EXPECT_EQ(strOf(ctx->add(ctx->createNumber(3), arr)), "31,2");
    EXPECT_EQ(strOf(ctx->add(ctx->createObject(), ctx->createNumber(1))),
              "[object Object]1");
}

TEST(ToPrimitiveTest, WrapperAddFollowsPrimitiveRules) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* n2 = ctx->createPrimitiveWrapper(ctx->createNumber(2));
    Value* s = ctx->createPrimitiveWrapper(ctx->createString("a"));
    EXPECT_DOUBLE_EQ(numOf(ctx->add(n2, ctx->createNumber(1))), 3.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createNumber(1), n2)), 3.0);
    EXPECT_EQ(strOf(ctx->add(s, ctx->createNumber(1))), "a1");
    EXPECT_EQ(strOf(ctx->add(ctx->createNumber(1), s)), "1a");
}

// ===== 异常透传 =====

TEST(ToPrimitiveTest, ValueOfExceptionPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* boom = ctx->createException(ctx->createString("boom"));
    Value* vo = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(obj, "valueOf", vo);
    ctx->defineProperty(obj, "toString", vo);
    (void)boom;

    Value* r = ctx->toString(obj);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(strOf(static_cast<neo::core::ExceptionData*>(r->data())->child), "boom");
}

TEST(ToPrimitiveTest, FatalPropagatesFromValueOf) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* vo = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createFatal("OOM"); }, 0);
    ctx->defineProperty(obj, "valueOf", vo);
    ctx->defineProperty(obj, "toString", vo);

    // Fatal 不可捕获，立即透传（ADR-017 §4）
    EXPECT_EQ(ctx->add(obj, ctx->createNumber(1))->type()->kind(), TypeKind::Fatal);
}

TEST(ToPrimitiveTest, GetterOnValueOfPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // toString 是抛错的 getter → STRING hint 下读取时异常透传
    Value* obj = ctx->createObject();
    Value* getter = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("getter boom"));
        }, 0);
    ctx->defineAccessor(obj, "toString", getter, nullptr);
    EXPECT_EQ(ctx->toString(obj)->type()->kind(), TypeKind::Exception);
    // NUMBER hint：valueOf 返回对象 → 继续读 toString → 同样透传异常
    EXPECT_EQ(ctx->sub(obj, ctx->createNumber(1))->type()->kind(), TypeKind::Exception);
}

// ===== 各类型默认行为 =====

TEST(ToPrimitiveTest, DefaultObjectToStringIsObjectTag) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(strOf(ctx->toString(ctx->createObject())), "[object Object]");
    EXPECT_EQ(strOf(ctx->toString(ctx->createArray())), "");
    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    EXPECT_EQ(strOf(ctx->toString(arr)), "1");
}

TEST(ToPrimitiveTest, DefaultWrapperToStringIsPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(strOf(ctx->toString(ctx->createPrimitiveWrapper(ctx->createNumber(5)))), "5");
    EXPECT_EQ(strOf(ctx->toString(ctx->createPrimitiveWrapper(ctx->createBoolean(true)))),
              "true");
    EXPECT_EQ(strOf(ctx->toString(ctx->createPrimitiveWrapper(ctx->createString("s")))), "s");
}

TEST(ToPrimitiveTest, FunctionToStringIsNativeCode) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 0);
    EXPECT_EQ(strOf(ctx->toString(fn)), "function () { [native code] }");
}

// ===== 嵌套：包装对象包包装对象（不合法，但转换应安全）=====

// ===== 内建参数走 ToString（可触发用户代码）=====

TEST(ToPrimitiveTest, SymbolCtorDescriptionUsesToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* obj = ctx->createObject();
    Value* ts = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString("desc"); }, 0);
    ctx->defineProperty(obj, "toString", ts);

    Value* args[1] = {obj};
    Value* sym = ctx->call(symbolFn, nullptr, 1, args);
    ASSERT_EQ(sym->type()->kind(), TypeKind::Symbol);
    EXPECT_EQ(strOf(ctx->toString(sym)), "Symbol(desc)");
}

TEST(ToPrimitiveTest, SymbolCtorDescriptionExceptionPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* obj = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(obj, "toString", boom);
    ctx->defineProperty(obj, "valueOf", boom);

    Value* args[1] = {obj};
    EXPECT_EQ(ctx->call(symbolFn, nullptr, 1, args)->type()->kind(), TypeKind::Exception);
}

TEST(ToPrimitiveTest, SymbolForKeyUsesToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* forFn = ctx->getProperty(symbolFn, "for");
    Value* obj = ctx->createObject();
    Value* ts = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString("reg"); }, 0);
    ctx->defineProperty(obj, "toString", ts);

    Value* args[1] = {obj};
    Value* sym = ctx->call(forFn, nullptr, 1, args);
    ASSERT_EQ(sym->type()->kind(), TypeKind::Symbol);
    // 同一 key 返回同一 symbol
    EXPECT_EQ(ctx->call(forFn, nullptr, 1, args), sym);
    Value* keyForArgs[1] = {sym};
    EXPECT_EQ(strOf(ctx->call(ctx->getProperty(symbolFn, "keyFor"), nullptr, 1,
                               keyForArgs)),
              "reg");
}

TEST(ToPrimitiveTest, SymbolToStringAndToPrimitiveRejectWrongThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* proto = ctx->getProperty(symbolFn, "prototype");
    Value* toStringFn = ctx->getProperty(proto, "toString");
    Value* toPrimFn = ctx->getProperty(
        proto, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToPrimitive));

    EXPECT_EQ(ctx->call(toStringFn, ctx->createNumber(1), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->call(toStringFn, nullptr, 0, nullptr)->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->call(toPrimFn, ctx->createNumber(1), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->call(toPrimFn, nullptr, 0, nullptr)->type()->kind(),
              TypeKind::Exception);
}

TEST(ToPrimitiveTest, SymbolToPrimitiveOnPrimitiveItself) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s");
    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* proto = ctx->getProperty(symbolFn, "prototype");
    Value* toPrimFn = ctx->getProperty(
        proto, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToPrimitive));
    // this 是原始 symbol 本身也可
    EXPECT_EQ(ctx->call(toPrimFn, sym, 0, nullptr), sym);
}

// ===== Number/String 构造函数走完整转换 =====

TEST(ToPrimitiveTest, NumberCtorUsesToPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* numberFn = ctx->global()->lookup("Number");
    Value* obj = ctx->createObject();
    Value* vo = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(33); }, 0);
    ctx->defineProperty(obj, "valueOf", vo);

    Value* args[1] = {obj};
    EXPECT_DOUBLE_EQ(numOf(ctx->call(numberFn, nullptr, 1, args)), 33.0);
    // new Number(obj) 同样转换
    Value* w = ctx->construct(numberFn, 1, args);
    ASSERT_EQ(w->type()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_DOUBLE_EQ(numOf(neo::core::wrapperPrimitiveValue(w)), 33.0);
}

TEST(ToPrimitiveTest, NumberCtorPropagatesConversionError) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* numberFn = ctx->global()->lookup("Number");
    Value* obj = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(obj, "valueOf", boom);
    ctx->defineProperty(obj, "toString", boom);

    Value* args[1] = {obj};
    EXPECT_EQ(ctx->call(numberFn, nullptr, 1, args)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->construct(numberFn, 1, args)->type()->kind(), TypeKind::Exception);
}

TEST(ToPrimitiveTest, StringCtorUsesToPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* stringFn = ctx->global()->lookup("String");
    Value* obj = ctx->createObject();
    Value* ts = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString("converted"); }, 0);
    ctx->defineProperty(obj, "toString", ts);

    Value* args[1] = {obj};
    EXPECT_EQ(strOf(ctx->call(stringFn, nullptr, 1, args)), "converted");
    Value* w = ctx->construct(stringFn, 1, args);
    ASSERT_EQ(w->type()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_EQ(strOf(neo::core::wrapperPrimitiveValue(w)), "converted");
}

TEST(ToPrimitiveTest, StringCtorPropagatesConversionError) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* stringFn = ctx->global()->lookup("String");
    Value* obj = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(obj, "toString", boom);
    ctx->defineProperty(obj, "valueOf", boom);

    Value* args[1] = {obj};
    EXPECT_EQ(ctx->call(stringFn, nullptr, 1, args)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->construct(stringFn, 1, args)->type()->kind(), TypeKind::Exception);
}

// ===== 数组内建参数走完整转换 =====

TEST(ToPrimitiveTest, ArrayJoinSeparatorUsesToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    Value* sep = ctx->createObject();
    Value* ts = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString("-"); }, 0);
    ctx->defineProperty(sep, "toString", ts);

    Value* joinFn = ctx->getProperty(arr, "join");
    Value* args[1] = {sep};
    EXPECT_EQ(strOf(ctx->call(joinFn, arr, 1, args)), "1-2");
}

TEST(ToPrimitiveTest, ArrayJoinElementUsesToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* elem = ctx->createObject();
    Value* ts = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString("E"); }, 0);
    ctx->defineProperty(elem, "toString", ts);
    ctx->setProperty(arr, "0", elem);

    Value* joinFn = ctx->getProperty(arr, "join");
    EXPECT_EQ(strOf(ctx->call(joinFn, arr, 0, nullptr)), "E");
}

TEST(ToPrimitiveTest, ArrayJoinPropagatesElementError) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* elem = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(elem, "toString", boom);
    ctx->defineProperty(elem, "valueOf", boom);
    ctx->setProperty(arr, "0", elem);

    Value* joinFn = ctx->getProperty(arr, "join");
    EXPECT_EQ(ctx->call(joinFn, arr, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(ToPrimitiveTest, ArraySliceIndexUsesToPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    for (int i = 0; i < 4; ++i) {
        ctx->setProperty(arr, std::to_string(i), ctx->createNumber(i));
    }
    Value* start = ctx->createObject();
    Value* vo = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(2); }, 0);
    ctx->defineProperty(start, "valueOf", vo);

    Value* sliceFn = ctx->getProperty(arr, "slice");
    Value* args[1] = {start};
    Value* copy = ctx->call(sliceFn, arr, 1, args);
    ASSERT_EQ(copy->type()->kind(), TypeKind::Array);
    EXPECT_EQ(static_cast<neo::core::ArrayData*>(copy->data())->length(), 2u);
}

TEST(ToPrimitiveTest, ArraySliceIndexErrorPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    Value* bad = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(bad, "valueOf", boom);
    ctx->defineProperty(bad, "toString", boom);

    Value* sliceFn = ctx->getProperty(arr, "slice");
    Value* args[1] = {bad};
    EXPECT_EQ(ctx->call(sliceFn, arr, 1, args)->type()->kind(), TypeKind::Exception);
}

TEST(ToPrimitiveTest, ArrayIndexOfFromIndexUsesToPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(1));
    Value* from = ctx->createObject();
    Value* vo = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0);
    ctx->defineProperty(from, "valueOf", vo);

    Value* indexOfFn = ctx->getProperty(arr, "indexOf");
    Value* args[2] = {ctx->createNumber(1), from};
    EXPECT_DOUBLE_EQ(numOf(ctx->call(indexOfFn, arr, 2, args)), 1.0);
}

TEST(ToPrimitiveTest, ArrayIndexOfFromIndexErrorPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    Value* bad = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(bad, "valueOf", boom);
    ctx->defineProperty(bad, "toString", boom);

    Value* indexOfFn = ctx->getProperty(arr, "indexOf");
    Value* args[2] = {ctx->createNumber(1), bad};
    EXPECT_EQ(ctx->call(indexOfFn, arr, 2, args)->type()->kind(), TypeKind::Exception);
}

// ===== 属性键转换失败路径 =====

TEST(ToPrimitiveTest, KeyConversionErrorPropagatesOnAllOps) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* bad = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(bad, "toString", boom);
    ctx->defineProperty(bad, "valueOf", boom);

    Value* target = ctx->createObject();
    EXPECT_EQ(ctx->getProperty(target, bad)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->setProperty(target, bad, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->deleteProperty(target, bad)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->defineProperty(target, bad, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->defineAccessor(target, bad, nullptr, nullptr)->type()->kind(),
              TypeKind::Exception);
    EXPECT_FALSE(ctx->hasProperty(target, bad));
}

TEST(ToPrimitiveTest, ArrayKeyConversionErrorPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* bad = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(bad, "toString", boom);
    ctx->defineProperty(bad, "valueOf", boom);

    Value* arr = ctx->createArray();
    EXPECT_EQ(ctx->getProperty(arr, bad)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->setProperty(arr, bad, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->deleteProperty(arr, bad)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->defineProperty(arr, bad, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->defineAccessor(arr, bad, nullptr, nullptr)->type()->kind(),
              TypeKind::Exception);
    EXPECT_FALSE(ctx->hasProperty(arr, bad));
}

TEST(ToPrimitiveTest, StringWrapperKeyConversionErrorPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* bad = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(bad, "toString", boom);
    ctx->defineProperty(bad, "valueOf", boom);

    Value* w = ctx->createPrimitiveWrapper(ctx->createString("abc"));
    EXPECT_EQ(ctx->getProperty(w, bad)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->setProperty(w, bad, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->deleteProperty(w, bad)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->defineProperty(w, bad, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->defineAccessor(w, bad, nullptr, nullptr)->type()->kind(),
              TypeKind::Exception);
    EXPECT_FALSE(ctx->hasProperty(w, bad));
}

TEST(ToPrimitiveTest, HasOwnPropertyKeyConversionErrorPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* bad = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(bad, "toString", boom);
    ctx->defineProperty(bad, "valueOf", boom);

    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* target = ctx->createObject();
    Value* args[1] = {bad};
    EXPECT_EQ(ctx->call(hasOwn, target, 1, args)->type()->kind(), TypeKind::Exception);
}

TEST(ToPrimitiveTest, WrapperOfWrapperUnwrapsOnce) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* inner = ctx->createPrimitiveWrapper(ctx->createNumber(3));
    // createPrimitiveWrapper 拒绝非原始值
    EXPECT_EQ(ctx->createPrimitiveWrapper(inner)->type()->kind(), TypeKind::Exception);
}
