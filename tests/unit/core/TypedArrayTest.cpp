// TypedArrayTest.cpp - TypedArray 构造与原型方法
//
// 验证（ES2024 §25.2）。

#include "core/BigIntOps.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
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

using neo::core::BooleanData;
using neo::core::IContext;
using neo::core::NumberData;
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

    Value* ctor(const char* name) { return ctx->global()->lookup(name); }

    Value* construct(const char* name, size_t argc = 0, Value* argv[] = nullptr) {
        return ctx->construct(ctor(name), argc, argv);
    }

    Value* call(Value* self, const char* name, size_t argc = 0, Value* argv[] = nullptr) {
        Value* fn = ctx->getProperty(self, name);
        return ctx->call(fn, self, argc, argv);
    }

    Value* call1(Value* self, const char* name, Value* a) {
        Value* args[1] = {a};
        return call(self, name, 1, args);
    }

    Value* call2(Value* self, const char* name, Value* a, Value* b) {
        Value* args[2] = {a, b};
        return call(self, name, 2, args);
    }

    Value* call3(Value* self, const char* name, Value* a, Value* b, Value* c) {
        Value* args[3] = {a, b, c};
        return call(self, name, 3, args);
    }

    Value* element(Value* view, uint32_t index) {
        return ctx->getProperty(view, ctx->createString(std::to_string(index)));
    }

    void setElement(Value* view, uint32_t index, Value* value) {
        ctx->setProperty(view, ctx->createString(std::to_string(index)), value);
    }
};

bool isError(Value* v) {
    return v != nullptr && (v->type()->kind() == TypeKind::Exception ||
                            v->type()->kind() == TypeKind::Fatal);
}

double numOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Number
               ? static_cast<NumberData*>(v->data())->value
               : -999.0;
}

bool boolOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Boolean &&
           static_cast<BooleanData*>(v->data())->value;
}

std::string strOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::String
               ? static_cast<neo::core::StringData*>(v->data())->value
               : std::string();
}

Value* doubleCallback(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createNumber(0);
    return ctx->createNumber(static_cast<NumberData*>(argv[0]->data())->value * 2.0);
}

Value* keepEvenCallback(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createBoolean(false);
    const double n = static_cast<NumberData*>(argv[0]->data())->value;
    return ctx->createBoolean(std::fmod(n, 2.0) == 0.0);
}

Value* addCallback(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc < 2) return ctx->createNumber(0);
    return ctx->createNumber(static_cast<NumberData*>(argv[0]->data())->value +
                             static_cast<NumberData*>(argv[1]->data())->value);
}

Value* descendingCallback(IContext* ctx, Value* /*self*/, size_t argc,
                          Value* argv[]) {
    if (argc < 2) return ctx->createNumber(0);
    return ctx->createNumber(static_cast<NumberData*>(argv[1]->data())->value -
                             static_cast<NumberData*>(argv[0]->data())->value);
}

}  // namespace

TEST(TypedArrayTest, ConstructFromLengthAndIndexAccess) {
    Fixture f;
    Value* args[1] = {f.ctx->createNumber(4)};
    Value* view = f.construct("Int32Array", 1, args);
    ASSERT_FALSE(isError(view));

    EXPECT_EQ(numOf(f.ctx->getProperty(view, "length")), 4.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(view, "byteLength")), 16.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(view, "byteOffset")), 0.0);
    Value* buffer = f.ctx->getProperty(view, "buffer");
    ASSERT_FALSE(isError(buffer));
    EXPECT_EQ(numOf(f.ctx->getProperty(buffer, "byteLength")), 16.0);

    f.setElement(view, 0, f.ctx->createNumber(42));
    EXPECT_EQ(numOf(f.element(view, 0)), 42.0);
    // 越界索引是普通属性（不进入缓冲区）。
    f.setElement(view, 9, f.ctx->createNumber(7));
    EXPECT_EQ(f.element(view, 9)->type()->kind(), TypeKind::Undefined);
}

TEST(TypedArrayTest, IntegerWrappingAndClamping) {
    Fixture f;
    Value* args[1] = {f.ctx->createNumber(3)};
    Value* int8 = f.construct("Int8Array", 1, args);
    ASSERT_FALSE(isError(int8));
    f.setElement(int8, 0, f.ctx->createNumber(130));
    EXPECT_EQ(numOf(f.element(int8, 0)), -126.0);
    f.setElement(int8, 1, f.ctx->createNumber(-129));
    EXPECT_EQ(numOf(f.element(int8, 1)), 127.0);

    Value* clamped = f.construct("Uint8ClampedArray", 1, args);
    ASSERT_FALSE(isError(clamped));
    f.setElement(clamped, 0, f.ctx->createNumber(2.5));
    EXPECT_EQ(numOf(f.element(clamped, 0)), 2.0);  // round-half-to-even
    f.setElement(clamped, 1, f.ctx->createNumber(3.5));
    EXPECT_EQ(numOf(f.element(clamped, 1)), 4.0);
    f.setElement(clamped, 2, f.ctx->createNumber(300));
    EXPECT_EQ(numOf(f.element(clamped, 2)), 255.0);
}

TEST(TypedArrayTest, BufferViewAndAlignment) {
    Fixture f;
    Value* bufferArgs[1] = {f.ctx->createNumber(16)};
    Value* buffer = f.construct("ArrayBuffer", 1, bufferArgs);
    ASSERT_FALSE(isError(buffer));

    Value* viewArgs[1] = {buffer};
    Value* view = f.construct("Uint16Array", 1, viewArgs);
    ASSERT_FALSE(isError(view));
    EXPECT_EQ(numOf(f.ctx->getProperty(view, "length")), 8.0);
    EXPECT_EQ(f.ctx->getProperty(view, "buffer"), buffer);

    Value* offsetArgs[2] = {buffer, f.ctx->createNumber(4)};
    Value* offsetView = f.construct("Uint16Array", 2, offsetArgs);
    ASSERT_FALSE(isError(offsetView));
    EXPECT_EQ(numOf(f.ctx->getProperty(offsetView, "byteOffset")), 4.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(offsetView, "length")), 6.0);

    // 未对齐的 byteOffset 抛 RangeError。
    Value* badArgs[2] = {buffer, f.ctx->createNumber(1)};
    EXPECT_TRUE(isError(f.construct("Uint16Array", 2, badArgs)));
}

TEST(TypedArrayTest, FromArraySetSubarraySlice) {
    Fixture f;
    Value* array = f.ctx->createArray();
    for (int i = 0; i < 4; ++i) {
        f.ctx->setProperty(array, f.ctx->createString(std::to_string(i)),
                           f.ctx->createNumber(i + 1));
    }
    Value* args[1] = {array};
    Value* view = f.construct("Int16Array", 1, args);
    ASSERT_FALSE(isError(view));
    EXPECT_EQ(numOf(f.ctx->getProperty(view, "length")), 4.0);
    EXPECT_EQ(numOf(f.element(view, 3)), 4.0);

    Value* targetArgs[1] = {f.ctx->createNumber(6)};
    Value* target = f.construct("Int16Array", 1, targetArgs);
    Value* setArgs[2] = {view, f.ctx->createNumber(1)};
    ASSERT_FALSE(isError(f.call(target, "set", 2, setArgs)));
    EXPECT_EQ(numOf(f.element(target, 0)), 0.0);
    EXPECT_EQ(numOf(f.element(target, 1)), 1.0);
    EXPECT_EQ(numOf(f.element(target, 4)), 4.0);

    Value* subArgs[2] = {f.ctx->createNumber(1), f.ctx->createNumber(3)};
    Value* sub = f.call(view, "subarray", 2, subArgs);
    ASSERT_FALSE(isError(sub));
    EXPECT_EQ(numOf(f.ctx->getProperty(sub, "length")), 2.0);
    EXPECT_EQ(f.ctx->getProperty(sub, "buffer"), f.ctx->getProperty(view, "buffer"));

    Value* sliced = f.call(view, "slice", 2, subArgs);
    ASSERT_FALSE(isError(sliced));
    EXPECT_EQ(numOf(f.element(sliced, 0)), 2.0);
    EXPECT_EQ(numOf(f.element(sliced, 1)), 3.0);
    EXPECT_NE(f.ctx->getProperty(sliced, "buffer"), f.ctx->getProperty(view, "buffer"));
}

TEST(TypedArrayTest, IterationAndCallbacks) {
    Fixture f;
    Value* args[1] = {f.ctx->createNumber(5)};
    Value* view = f.construct("Int32Array", 1, args);
    ASSERT_FALSE(isError(view));
    for (uint32_t i = 0; i < 5; ++i) {
        f.setElement(view, i, f.ctx->createNumber(i + 1));
    }

    Value* mapFn = f.ctx->createNativeFunction(doubleCallback, 1);
    Value* mapped = f.call1(view, "map", mapFn);
    ASSERT_FALSE(isError(mapped));
    EXPECT_EQ(numOf(f.element(mapped, 4)), 10.0);

    Value* filterFn = f.ctx->createNativeFunction(keepEvenCallback, 1);
    Value* filtered = f.call1(view, "filter", filterFn);
    ASSERT_FALSE(isError(filtered));
    EXPECT_EQ(numOf(f.ctx->getProperty(filtered, "length")), 2.0);
    EXPECT_EQ(numOf(f.element(filtered, 0)), 2.0);
    EXPECT_EQ(numOf(f.element(filtered, 1)), 4.0);

    Value* addFn = f.ctx->createNativeFunction(addCallback, 2);
    Value* reduced = f.call1(view, "reduce", addFn);
    ASSERT_FALSE(isError(reduced));
    EXPECT_EQ(numOf(reduced), 15.0);

    EXPECT_TRUE(boolOf(f.call1(view, "some", filterFn)));
    EXPECT_FALSE(boolOf(f.call1(view, "every", filterFn)));
    EXPECT_EQ(numOf(f.call1(view, "find", filterFn)), 2.0);
    EXPECT_EQ(numOf(f.call1(view, "findIndex", filterFn)), 1.0);
    EXPECT_EQ(numOf(f.call1(view, "indexOf", f.ctx->createNumber(3))), 2.0);
    EXPECT_TRUE(boolOf(f.call1(view, "includes", f.ctx->createNumber(5))));
    EXPECT_FALSE(boolOf(f.call1(view, "includes", f.ctx->createNumber(9))));
}

TEST(TypedArrayTest, FillReverseSortJoin) {
    Fixture f;
    Value* args[1] = {f.ctx->createNumber(4)};
    Value* view = f.construct("Int32Array", 1, args);
    ASSERT_FALSE(isError(view));

    f.call1(view, "fill", f.ctx->createNumber(7));
    EXPECT_EQ(numOf(f.element(view, 0)), 7.0);
    EXPECT_EQ(numOf(f.element(view, 3)), 7.0);

    f.setElement(view, 0, f.ctx->createNumber(3));
    f.setElement(view, 1, f.ctx->createNumber(1));
    f.setElement(view, 2, f.ctx->createNumber(2));
    f.setElement(view, 3, f.ctx->createNumber(4));
    ASSERT_FALSE(isError(f.call(view, "sort")));
    EXPECT_EQ(numOf(f.element(view, 0)), 1.0);
    EXPECT_EQ(numOf(f.element(view, 3)), 4.0);

    Value* cmp = f.ctx->createNativeFunction(descendingCallback, 2);
    ASSERT_FALSE(isError(f.call1(view, "sort", cmp)));
    EXPECT_EQ(numOf(f.element(view, 0)), 4.0);
    EXPECT_EQ(numOf(f.element(view, 3)), 1.0);

    ASSERT_FALSE(isError(f.call(view, "reverse")));
    EXPECT_EQ(numOf(f.element(view, 0)), 1.0);
    EXPECT_EQ(numOf(f.element(view, 3)), 4.0);

    Value* sepArgs[1] = {f.ctx->createString("-")};
    EXPECT_EQ(strOf(f.call(view, "join", 1, sepArgs)), "1-2-3-4");
    EXPECT_EQ(strOf(f.call(view, "toString")), "1,2,3,4");
}

TEST(TypedArrayTest, Iterators) {
    Fixture f;
    Value* args[1] = {f.ctx->createNumber(3)};
    Value* view = f.construct("Uint8Array", 1, args);
    ASSERT_FALSE(isError(view));
    f.setElement(view, 0, f.ctx->createNumber(10));
    f.setElement(view, 1, f.ctx->createNumber(20));
    f.setElement(view, 2, f.ctx->createNumber(30));

    Value* iter = f.call(view, "values");
    ASSERT_FALSE(isError(iter));
    Value* next = f.ctx->getProperty(iter, "next");
    Value* r1 = f.ctx->call(next, iter, 0, nullptr);
    ASSERT_FALSE(isError(r1));
    EXPECT_EQ(numOf(f.ctx->getProperty(r1, "value")), 10.0);

    Value* keys = f.call(view, "keys");
    Value* keyNext = f.ctx->getProperty(keys, "next");
    Value* k1 = f.ctx->call(keyNext, keys, 0, nullptr);
    EXPECT_EQ(numOf(f.ctx->getProperty(k1, "value")), 0.0);

    Value* entries = f.call(view, "entries");
    Value* entryNext = f.ctx->getProperty(entries, "next");
    Value* e1 = f.ctx->call(entryNext, entries, 0, nullptr);
    Value* pair = f.ctx->getProperty(e1, "value");
    EXPECT_EQ(numOf(f.ctx->getProperty(pair, "0")), 0.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(pair, "1")), 10.0);
}

TEST(TypedArrayTest, StaticsAndBigInt) {
    Fixture f;
    Value* ctor = f.ctor("Int16Array");
    EXPECT_EQ(numOf(f.ctx->getProperty(ctor, "BYTES_PER_ELEMENT")), 2.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(f.ctx->getProperty(ctor, "prototype"),
                                       "BYTES_PER_ELEMENT")),
              2.0);

    Value* fromFn = f.ctx->getProperty(ctor, "from");
    Value* array = f.ctx->createArray();
    f.ctx->setProperty(array, f.ctx->createString("0"), f.ctx->createNumber(5));
    f.ctx->setProperty(array, f.ctx->createString("1"), f.ctx->createNumber(6));
    Value* fromArgs[1] = {array};
    Value* from = f.ctx->call(fromFn, ctor, 1, fromArgs);
    ASSERT_FALSE(isError(from));
    EXPECT_EQ(numOf(f.element(from, 1)), 6.0);

    Value* ofFn = f.ctx->getProperty(ctor, "of");
    Value* ofArgs[3] = {f.ctx->createNumber(1), f.ctx->createNumber(2),
                        f.ctx->createNumber(3)};
    Value* of = f.ctx->call(ofFn, ctor, 3, ofArgs);
    ASSERT_FALSE(isError(of));
    EXPECT_EQ(numOf(f.ctx->getProperty(of, "length")), 3.0);
    EXPECT_EQ(numOf(f.element(of, 2)), 3.0);

    Value* bigArgs[1] = {f.ctx->createNumber(2)};
    Value* big = f.construct("BigInt64Array", 1, bigArgs);
    ASSERT_FALSE(isError(big));
    Value* bigValue =
        f.ctx->createBigInt(neo::bigint::BigInt(static_cast<int64_t>(-9)));
    f.call1(big, "fill", bigValue);
    Value* read = f.element(big, 1);
    ASSERT_FALSE(isError(read));
    EXPECT_EQ(neo::core::detail::bigIntValue(read).toInt64(), -9);
    // BigInt 元素类型不接受 Number。
    EXPECT_TRUE(isError(f.call1(big, "fill", f.ctx->createNumber(1))));
}

TEST(TypedArrayTest, ErrorCases) {
    Fixture f;
    // 非 new 调用。
    Value* callArgs[1] = {f.ctx->createNumber(1)};
    EXPECT_TRUE(isError(f.ctx->call(f.ctor("Int8Array"), f.ctx->createUndefined(), 1,
                                    callArgs)));
    // 负长度。
    Value* negArgs[1] = {f.ctx->createNumber(-1)};
    EXPECT_TRUE(isError(f.construct("Int8Array", 1, negArgs)));
    // 原型方法接收者不匹配。
    Value* obj = f.ctx->createObject();
    EXPECT_TRUE(isError(
        f.call1(obj, "map", f.ctx->createNativeFunction(doubleCallback, 1))));
    // set 越界。
    Value* viewArgs[1] = {f.ctx->createNumber(2)};
    Value* view = f.construct("Int8Array", 1, viewArgs);
    Value* src = f.construct("Int8Array", 1, viewArgs);
    Value* setArgs[2] = {src, f.ctx->createNumber(1)};
    EXPECT_TRUE(isError(f.call(view, "set", 2, setArgs)));
}
