// ArrayBuiltinsTest.cpp - Array 构造函数与 Array.prototype 方法单元测试
//
// 验证（ES2024 §23.1）：
// - Array() / new Array() → 空数组
// - new Array(n) → 长度 n；非法 n → Exception
// - new Array(a, b, c) → 元素数组
// - Array.isArray
// - Array.prototype 自身是数组，constructor 双向引用
// - push / pop / join / toString / slice / indexOf
// - 非数组 receiver 报错
//
// 关联 ADR: 020（标准库组织）、018（对象模型 §6）、034（原型-构造函数）

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>
#include "TestHelpers.h"

#include <cmath>
#include <limits>
#include <string>

namespace {

using neo::core::ArrayData;
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
ArrayData* dataOf(Value* v) { return static_cast<ArrayData*>(v->data()); }

Value* globalOf(IContext* ctx, const char* name) { return ctx->global()->lookup(name); }

// 调用 arr 上的原型方法
Value* callMethod(IContext* ctx, Value* arr, const char* name, size_t argc, Value* argv[]) {
    Value* fn = ctx->getProperty(arr, name);
    return ctx->call(fn, arr, argc, argv);
}

}  // namespace

// ===== 原型与构造函数 =====

TEST(ArrayBuiltinsTest, ArrayRegisteredOnGlobal) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayFn = globalOf(ctx.get(), "Array");
    ASSERT_NE(arrayFn, nullptr);
    EXPECT_EQ(arrayFn->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->getPrototype(arrayFn), ctx->functionPrototype());
}

TEST(ArrayBuiltinsTest, ArrayPrototypeIsArrayWithConstructor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->arrayPrototype();
    EXPECT_EQ(proto->type()->kind(), TypeKind::Array);
    EXPECT_EQ(dataOf(proto)->length(), 0u);
    EXPECT_EQ(ctx->getProperty(globalOf(ctx.get(), "Array"), "prototype"), proto);
    EXPECT_EQ(ctx->getProperty(proto, "constructor"), globalOf(ctx.get(), "Array"));
}

TEST(ArrayBuiltinsTest, NewArrayHasArrayPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayFn = globalOf(ctx.get(), "Array");
    Value* arr = ctx->construct(arrayFn, 0, nullptr);
    ASSERT_EQ(arr->type()->kind(), TypeKind::Array);
    EXPECT_EQ(ctx->getPrototype(arr), ctx->arrayPrototype());
}

// ===== 构造函数三种形态 =====

TEST(ArrayBuiltinsTest, CallWithNoArgsCreatesEmptyArray) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayFn = globalOf(ctx.get(), "Array");
    Value* arr = ctx->call(arrayFn, nullptr, 0, nullptr);
    ASSERT_EQ(arr->type()->kind(), TypeKind::Array);
    EXPECT_EQ(dataOf(arr)->length(), 0u);
}

TEST(ArrayBuiltinsTest, NewWithSingleNumberSetsLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayFn = globalOf(ctx.get(), "Array");
    Value* args[1] = {ctx->createNumber(5)};
    Value* arr = ctx->construct(arrayFn, 1, args);
    ASSERT_EQ(arr->type()->kind(), TypeKind::Array);
    EXPECT_EQ(dataOf(arr)->length(), 5u);
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, "0"));  // 空洞
}

TEST(ArrayBuiltinsTest, NewWithSingleNumberZero) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayFn = globalOf(ctx.get(), "Array");
    Value* args[1] = {ctx->createNumber(0)};
    Value* arr = ctx->construct(arrayFn, 1, args);
    EXPECT_EQ(dataOf(arr)->length(), 0u);
}

TEST(ArrayBuiltinsTest, NewWithInvalidLengthRejected) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayFn = globalOf(ctx.get(), "Array");
    Value* negative[1] = {ctx->createNumber(-1)};
    EXPECT_EQ(ctx->construct(arrayFn, 1, negative)->type()->kind(), TypeKind::Exception);

    Value* fractional[1] = {ctx->createNumber(1.5)};
    EXPECT_EQ(ctx->construct(arrayFn, 1, fractional)->type()->kind(), TypeKind::Exception);

    Value* tooBig[1] = {ctx->createNumber(4294967296.0)};
    EXPECT_EQ(ctx->construct(arrayFn, 1, tooBig)->type()->kind(), TypeKind::Exception);
}

TEST(ArrayBuiltinsTest, NewWithMultipleArgsCreatesElements) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayFn = globalOf(ctx.get(), "Array");
    Value* args[3] = {ctx->createNumber(1), ctx->createString("two"), ctx->createNumber(3)};
    Value* arr = ctx->construct(arrayFn, 3, args);
    ASSERT_EQ(arr->type()->kind(), TypeKind::Array);
    EXPECT_EQ(dataOf(arr)->length(), 3u);
    EXPECT_EQ(numOf(ctx->getProperty(arr, "0")), 1.0);
    EXPECT_EQ(strOf(ctx->getProperty(arr, "1")), "two");
}

TEST(ArrayBuiltinsTest, SingleNonNumberArgBecomesElement) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayFn = globalOf(ctx.get(), "Array");
    Value* args[1] = {ctx->createString("5")};
    Value* arr = ctx->construct(arrayFn, 1, args);
    EXPECT_EQ(dataOf(arr)->length(), 1u);
    EXPECT_EQ(strOf(ctx->getProperty(arr, "0")), "5");
}

// ===== Array.isArray =====

TEST(ArrayBuiltinsTest, IsArrayTrue) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* isArrayFn = ctx->getProperty(globalOf(ctx.get(), "Array"), "isArray");
    Value* args[1] = {ctx->createArray()};
    Value* r = ctx->call(isArrayFn, nullptr, 1, args);
    ASSERT_EQ(r->type()->kind(), TypeKind::Boolean);
    EXPECT_EQ(static_cast<neo::core::BooleanData*>(r->data())->value, true);
}

TEST(ArrayBuiltinsTest, IsArrayFalseForNonArray) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* isArrayFn = ctx->getProperty(globalOf(ctx.get(), "Array"), "isArray");
    Value* args[1] = {ctx->createObject()};
    Value* r = ctx->call(isArrayFn, nullptr, 1, args);
    EXPECT_EQ(static_cast<neo::core::BooleanData*>(r->data())->value, false);

    // 无参 → false
    Value* r2 = ctx->call(isArrayFn, nullptr, 0, nullptr);
    EXPECT_EQ(static_cast<neo::core::BooleanData*>(r2->data())->value, false);
}

// ===== push / pop =====

TEST(ArrayBuiltinsTest, PushAppendsAndReturnsLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* args[2] = {ctx->createNumber(1), ctx->createNumber(2)};
    Value* r = callMethod(ctx.get(), arr, "push", 2, args);
    EXPECT_EQ(numOf(r), 2.0);
    EXPECT_EQ(dataOf(arr)->length(), 2u);
    EXPECT_EQ(numOf(ctx->getProperty(arr, "0")), 1.0);
}

TEST(ArrayBuiltinsTest, PushWithNoArgsReturnsCurrentLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "push", 0, nullptr)), 1.0);
}

TEST(ArrayBuiltinsTest, PushGrowsBeyondDenseLimit) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* arg[1] = {ctx->createNumber(1)};
    for (int i = 0; i < 100; ++i) {
        callMethod(ctx.get(), arr, "push", 1, arg);
    }
    EXPECT_EQ(dataOf(arr)->length(), 100u);
    EXPECT_TRUE(dataOf(arr)->sparseMode);
}

TEST(ArrayBuiltinsTest, PopRemovesAndReturnsLast) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    Value* r = callMethod(ctx.get(), arr, "pop", 0, nullptr);
    EXPECT_EQ(numOf(r), 2.0);
    EXPECT_EQ(dataOf(arr)->length(), 1u);
}

TEST(ArrayBuiltinsTest, PopOnEmptyReturnsUndefined) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    EXPECT_EQ(callMethod(ctx.get(), arr, "pop", 0, nullptr)->type()->kind(),
              TypeKind::Undefined);
}

// ===== join / toString =====

TEST(ArrayBuiltinsTest, JoinWithDefaultSeparator) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    EXPECT_EQ(strOf(callMethod(ctx.get(), arr, "join", 0, nullptr)), "1,2");
}

TEST(ArrayBuiltinsTest, JoinWithCustomSeparator) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    Value* args[1] = {ctx->createString("-")};
    EXPECT_EQ(strOf(callMethod(ctx.get(), arr, "join", 1, args)), "1-2");
}

TEST(ArrayBuiltinsTest, JoinWithUndefinedSeparatorUsesComma) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    Value* args[1] = {ctx->createUndefined()};
    EXPECT_EQ(strOf(callMethod(ctx.get(), arr, "join", 1, args)), "1,2");
}

TEST(ArrayBuiltinsTest, PrototypeToStringJoins) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createString("a"));
    ctx->setProperty(arr, "1", ctx->createString("b"));
    EXPECT_EQ(strOf(callMethod(ctx.get(), arr, "toString", 0, nullptr)), "a,b");
}

// ===== slice =====

TEST(ArrayBuiltinsTest, SliceWithNoArgsCopiesAll) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    for (int i = 0; i < 3; ++i) {
        ctx->setProperty(arr, std::to_string(i), ctx->createNumber(i));
    }
    Value* copy = callMethod(ctx.get(), arr, "slice", 0, nullptr);
    ASSERT_EQ(copy->type()->kind(), TypeKind::Array);
    EXPECT_EQ(dataOf(copy)->length(), 3u);
    EXPECT_EQ(numOf(ctx->getProperty(copy, "2")), 2.0);
}

TEST(ArrayBuiltinsTest, SliceWithStartAndEnd) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    for (int i = 0; i < 5; ++i) {
        ctx->setProperty(arr, std::to_string(i), ctx->createNumber(i));
    }
    Value* args[2] = {ctx->createNumber(1), ctx->createNumber(3)};
    Value* copy = callMethod(ctx.get(), arr, "slice", 2, args);
    EXPECT_EQ(dataOf(copy)->length(), 2u);
    EXPECT_EQ(numOf(ctx->getProperty(copy, "0")), 1.0);
    EXPECT_EQ(numOf(ctx->getProperty(copy, "1")), 2.0);
}

TEST(ArrayBuiltinsTest, SliceWithNegativeStart) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    for (int i = 0; i < 4; ++i) {
        ctx->setProperty(arr, std::to_string(i), ctx->createNumber(i));
    }
    Value* args[1] = {ctx->createNumber(-2)};
    Value* copy = callMethod(ctx.get(), arr, "slice", 1, args);
    EXPECT_EQ(dataOf(copy)->length(), 2u);
    EXPECT_EQ(numOf(ctx->getProperty(copy, "0")), 2.0);
}

TEST(ArrayBuiltinsTest, SlicePreservesHoles) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "2", ctx->createNumber(3));  // 1 是空洞
    Value* copy = callMethod(ctx.get(), arr, "slice", 0, nullptr);
    EXPECT_EQ(dataOf(copy)->length(), 3u);
    EXPECT_FALSE(neo::test::hasProperty(ctx, copy, "1"));
    EXPECT_TRUE(neo::test::hasProperty(ctx, copy, "2"));
}

TEST(ArrayBuiltinsTest, SliceEmptyRange) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    Value* args[2] = {ctx->createNumber(2), ctx->createNumber(1)};
    Value* copy = callMethod(ctx.get(), arr, "slice", 2, args);
    EXPECT_EQ(dataOf(copy)->length(), 0u);
}

// ===== indexOf =====

TEST(ArrayBuiltinsTest, IndexOfFindsElement) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    Value* args[1] = {ctx->createNumber(2)};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 1, args)), 1.0);
}

TEST(ArrayBuiltinsTest, IndexOfReturnsMinusOneWhenMissing) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    Value* args[1] = {ctx->createNumber(9)};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 1, args)), -1.0);
}

TEST(ArrayBuiltinsTest, IndexOfWithNoArgsReturnsMinusOne) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 0, nullptr)), -1.0);
}

TEST(ArrayBuiltinsTest, IndexOfWithFromIndex) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    for (int i = 0; i < 4; ++i) {
        ctx->setProperty(arr, std::to_string(i), ctx->createNumber(i % 2));
    }
    // [0,1,0,1]，从 1 开始找 0 → 索引 2
    Value* args[2] = {ctx->createNumber(0), ctx->createNumber(1)};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 2, args)), 2.0);
}

TEST(ArrayBuiltinsTest, IndexOfFromIndexBeyondLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    Value* args[2] = {ctx->createNumber(1), ctx->createNumber(10)};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 2, args)), -1.0);
}

TEST(ArrayBuiltinsTest, IndexOfWithNegativeFromIndex) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    for (int i = 0; i < 4; ++i) {
        ctx->setProperty(arr, std::to_string(i), ctx->createNumber(i));
    }
    Value* args[2] = {ctx->createNumber(1), ctx->createNumber(-2)};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 2, args)), -1.0);
}

TEST(ArrayBuiltinsTest, IndexOfUsesStrictEquality) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createString("1"));
    Value* args[1] = {ctx->createNumber(1)};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 1, args)), -1.0);
}

TEST(ArrayBuiltinsTest, IndexOfFindsObjectsByIdentity) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", obj);
    Value* args[1] = {obj};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 1, args)), 0.0);
}

TEST(ArrayBuiltinsTest, IndexOfStrictEqualityAllPrimitiveKinds) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createUndefined());
    ctx->setProperty(arr, "1", ctx->createNull());
    ctx->setProperty(arr, "2", ctx->createBoolean(true));
    ctx->setProperty(arr, "3", ctx->createString("s"));

    auto indexOf = [&](Value* needle) {
        Value* args[1] = {needle};
        return numOf(callMethod(ctx.get(), arr, "indexOf", 1, args));
    };
    EXPECT_EQ(indexOf(ctx->createUndefined()), 0.0);
    EXPECT_EQ(indexOf(ctx->createNull()), 1.0);
    EXPECT_EQ(indexOf(ctx->createBoolean(true)), 2.0);
    EXPECT_EQ(indexOf(ctx->createString("s")), 3.0);
    EXPECT_EQ(indexOf(ctx->createBoolean(false)), -1.0);
}

TEST(ArrayBuiltinsTest, IndexOfNaNNeverMatches) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* nan = ctx->createNumber(std::nan(""));
    ctx->setProperty(arr, "0", nan);
    Value* args[1] = {nan};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 1, args)), -1.0);
}

TEST(ArrayBuiltinsTest, IndexOfFromIndexNaNOrZero) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    Value* nanArgs[2] = {ctx->createNumber(1), ctx->createNumber(std::nan(""))};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 2, nanArgs)), 0.0);
    Value* zeroArgs[2] = {ctx->createNumber(1), ctx->createNumber(0)};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 2, zeroArgs)), 0.0);
}

TEST(ArrayBuiltinsTest, IndexOfFromIndexInfinity) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    Value* args[2] = {ctx->createNumber(1),
                      ctx->createNumber(std::numeric_limits<double>::infinity())};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 2, args)), -1.0);
}

TEST(ArrayBuiltinsTest, SliceWithUndefinedEndCopiesToEnd) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    for (int i = 0; i < 3; ++i) {
        ctx->setProperty(arr, std::to_string(i), ctx->createNumber(i));
    }
    Value* args[2] = {ctx->createNumber(1), ctx->createUndefined()};
    Value* copy = callMethod(ctx.get(), arr, "slice", 2, args);
    EXPECT_EQ(dataOf(copy)->length(), 2u);
    EXPECT_EQ(numOf(ctx->getProperty(copy, "0")), 1.0);
}

TEST(ArrayBuiltinsTest, SliceWithFractionalAndOutOfRangeIndices) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    for (int i = 0; i < 3; ++i) {
        ctx->setProperty(arr, std::to_string(i), ctx->createNumber(i));
    }
    // start=1.7 → 1；end=100 → 截到 3
    Value* args[2] = {ctx->createNumber(1.7), ctx->createNumber(100)};
    Value* copy = callMethod(ctx.get(), arr, "slice", 2, args);
    EXPECT_EQ(dataOf(copy)->length(), 2u);
    // start=-100 → 0
    Value* neg[1] = {ctx->createNumber(-100)};
    Value* copy2 = callMethod(ctx.get(), arr, "slice", 1, neg);
    EXPECT_EQ(dataOf(copy2)->length(), 3u);
}

TEST(ArrayBuiltinsTest, JoinWithNullAndUndefinedElements) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNull());
    ctx->setProperty(arr, "1", ctx->createUndefined());
    ctx->setProperty(arr, "2", ctx->createNumber(3));
    EXPECT_EQ(strOf(callMethod(ctx.get(), arr, "join", 0, nullptr)), ",,3");
}

TEST(ArrayBuiltinsTest, PopOnHoleReturnsUndefined) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "2", ctx->createNumber(3));
    ctx->deleteProperty(arr, "2");  // 末位变空洞，length 仍为 3
    EXPECT_EQ(callMethod(ctx.get(), arr, "pop", 0, nullptr)->type()->kind(),
              TypeKind::Undefined);
    EXPECT_EQ(dataOf(arr)->length(), 2u);
}

TEST(ArrayBuiltinsTest, NewArrayWithInfinityRejected) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayFn = globalOf(ctx.get(), "Array");
    Value* args[1] = {ctx->createNumber(std::numeric_limits<double>::infinity())};
    EXPECT_EQ(ctx->construct(arrayFn, 1, args)->type()->kind(), TypeKind::Exception);
}

// ===== 非数组 receiver =====

TEST(ArrayBuiltinsTest, MethodsAreGenericOverArrayLike) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->arrayPrototype();
    // 普通对象作为 array-like：push 写索引与 length。
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "length", ctx->createNumber(0));
    Value* push = ctx->getProperty(proto, "push");
    Value* pushArg = ctx->createNumber(7);
    Value* pushed = ctx->call(push, obj, 1, &pushArg);
    ASSERT_EQ(pushed->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(pushed), 1.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "0")), 7.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "length")), 1.0);

    // join / toString / indexOf / slice / pop 同样走泛型路径。
    Value* join = ctx->getProperty(proto, "join");
    EXPECT_EQ(strOf(ctx->call(join, obj, 0, nullptr)), "7");
    // 普通对象没有 join → 回退 Object.prototype.toString。
    Value* toStringFn = ctx->getProperty(proto, "toString");
    EXPECT_EQ(strOf(ctx->call(toStringFn, obj, 0, nullptr)), "[object Object]");
    Value* indexOf = ctx->getProperty(proto, "indexOf");
    Value* needle = ctx->createNumber(7);
    EXPECT_DOUBLE_EQ(numOf(ctx->call(indexOf, obj, 1, &needle)), 0.0);
    Value* slice = ctx->getProperty(proto, "slice");
    Value* sliced = ctx->call(slice, obj, 0, nullptr);
    ASSERT_EQ(sliced->type()->kind(), TypeKind::Array);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(sliced, "0")), 7.0);
    Value* pop = ctx->getProperty(proto, "pop");
    EXPECT_DOUBLE_EQ(numOf(ctx->call(pop, obj, 0, nullptr)), 7.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "length")), 0.0);

    // null/undefined 接收者 → TypeError。
    for (const char* name : {"push", "pop", "join", "toString", "slice", "indexOf"}) {
        Value* fn = ctx->getProperty(proto, name);
        ASSERT_EQ(fn->type()->kind(), TypeKind::Function) << name;
        EXPECT_EQ(ctx->call(fn, ctx->createNull(), 0, nullptr)->type()->kind(),
                  TypeKind::Exception)
            << name;
    }
}

// ===== Object.prototype 与 Array =====

TEST(ArrayBuiltinsTest, ObjectProtoToStringTagsArray) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* toStringFn = ctx->getProperty(ctx->objectPrototype(), "toString");
    Value* arr = ctx->createArray();
    EXPECT_EQ(strOf(ctx->call(toStringFn, arr, 0, nullptr)), "[object Array]");
}

TEST(ArrayBuiltinsTest, HasOwnPropertySeesIndexAndLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));

    Value* idx[1] = {ctx->createString("0")};
    Value* len[1] = {ctx->createString("length")};
    Value* hole[1] = {ctx->createString("1")};
    EXPECT_EQ(static_cast<neo::core::BooleanData*>(
                  ctx->call(hasOwn, arr, 1, idx)->data())->value, true);
    EXPECT_EQ(static_cast<neo::core::BooleanData*>(
                  ctx->call(hasOwn, arr, 1, len)->data())->value, true);
    EXPECT_EQ(static_cast<neo::core::BooleanData*>(
                  ctx->call(hasOwn, arr, 1, hole)->data())->value, false);
}

TEST(ArrayBuiltinsTest, ObjectCtorReturnsArrayAsIs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* objectFn = globalOf(ctx.get(), "Object");
    Value* arr = ctx->createArray();
    Value* args[1] = {arr};
    EXPECT_EQ(ctx->call(objectFn, nullptr, 1, args), arr);
}

TEST(ArrayBuiltinsTest, ConstructorReturningArrayReplacesThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createArray(); }, 0);
    Value* result = ctx->construct(fn, 0, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Array);
}

TEST(ArrayBuiltinsTest, ConstructorPrototypeMayBeArray) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 0);
    Value* proto = ctx->createArray();
    ctx->defineProperty(fn, "prototype", proto, true, false, false);
    Value* obj = ctx->construct(fn, 0, nullptr);
    EXPECT_EQ(ctx->getPrototype(obj), proto);
}
