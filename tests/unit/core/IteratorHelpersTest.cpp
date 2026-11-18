// IteratorHelpersTest.cpp - Iterator.prototype 辅助方法
//
// 验证（ES2025 §27.1.3.3）：
// - map/filter/take/drop/flatMap 的惰性语义
// - toArray/forEach/some/every/find/reduce

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
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
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace {

using neo::core::ArrayData;
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

    Value* make(std::initializer_list<double> values) {
        Value* arr = ctx->createArray();
        auto* d = static_cast<ArrayData*>(arr->data());
        uint32_t i = 0;
        for (double v : values) d->setElement(i++, ctx->createNumber(v));
        return arr;
    }

    // 数组的 values 迭代器
    Value* iterator(Value* arr) {
        Value* fn = ctx->getProperty(arr, "values");
        return ctx->call(fn, arr, 0, nullptr);
    }

    Value* call(Value* self, const char* name, size_t argc = 0, Value* argv[] = nullptr) {
        Value* fn = ctx->getProperty(self, name);
        return ctx->call(fn, self, argc, argv);
    }

    Value* call1(Value* self, const char* name, Value* a) {
        Value* args[1] = {a};
        return call(self, name, 1, args);
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

std::vector<double> numbers(Value* arr) {
    auto* d = static_cast<ArrayData*>(arr->data());
    std::vector<double> out;
    for (uint32_t i = 0; i < d->length(); ++i) out.push_back(numOf(d->elementAt(i)));
    return out;
}

Value* doubleCallback(IContext* ctx, Value*, size_t, Value* argv[]) {
    return ctx->createNumber(numOf(argv[0]) * 2.0);
}

Value* addIndexCallback(IContext* ctx, Value*, size_t, Value* argv[]) {
    return ctx->createNumber(numOf(argv[0]) + numOf(argv[1]));
}

Value* evenPredicate(IContext* ctx, Value*, size_t, Value* argv[]) {
    return ctx->createBoolean(std::fmod(numOf(argv[0]), 2.0) == 0.0);
}

Value* greaterThanTwo(IContext* ctx, Value*, size_t, Value* argv[]) {
    return ctx->createBoolean(numOf(argv[0]) > 2.0);
}

Value* sumCallback(IContext* ctx, Value*, size_t, Value* argv[]) {
    return ctx->createNumber(numOf(argv[0]) + numOf(argv[1]));
}

Value* pairMapper(IContext* ctx, Value*, size_t, Value* argv[]) {
    Value* arr = ctx->createArray();
    auto* d = static_cast<ArrayData*>(arr->data());
    const double v = numOf(argv[0]);
    d->setElement(0, ctx->createNumber(v));
    d->setElement(1, ctx->createNumber(v + 10.0));
    return arr;
}

}  // namespace

TEST(IteratorHelpersTest, MapFilter) {
    Fixture f;
    Value* it = f.iterator(f.make({1, 2, 3}));
    Value* mapped = f.call1(it, "map", f.ctx->createNativeFunction(doubleCallback, 0));
    ASSERT_FALSE(isError(mapped));
    Value* out = f.call(mapped, "toArray");
    ASSERT_FALSE(isError(out));
    EXPECT_EQ(numbers(out), (std::vector<double>{2, 4, 6}));

    Value* it2 = f.iterator(f.make({1, 2, 3, 4}));
    Value* indexed =
        f.call1(it2, "map", f.ctx->createNativeFunction(addIndexCallback, 0));
    Value* out2 = f.call(indexed, "toArray");
    EXPECT_EQ(numbers(out2), (std::vector<double>{1, 3, 5, 7}));

    Value* it3 = f.iterator(f.make({1, 2, 3, 4}));
    Value* filtered =
        f.call1(it3, "filter", f.ctx->createNativeFunction(evenPredicate, 0));
    Value* out3 = f.call(filtered, "toArray");
    EXPECT_EQ(numbers(out3), (std::vector<double>{2, 4}));
}

TEST(IteratorHelpersTest, TakeDrop) {
    Fixture f;
    Value* it = f.iterator(f.make({1, 2, 3, 4, 5}));
    Value* taken = f.call1(it, "take", f.ctx->createNumber(2));
    Value* out = f.call(taken, "toArray");
    EXPECT_EQ(numbers(out), (std::vector<double>{1, 2}));

    Value* it2 = f.iterator(f.make({1, 2, 3, 4, 5}));
    Value* dropped = f.call1(it2, "drop", f.ctx->createNumber(2));
    Value* out2 = f.call(dropped, "toArray");
    EXPECT_EQ(numbers(out2), (std::vector<double>{3, 4, 5}));

    Value* it3 = f.iterator(f.make({1, 2}));
    Value* none = f.call1(it3, "take", f.ctx->createNumber(0));
    Value* out3 = f.call(none, "toArray");
    EXPECT_EQ(numbers(out3), (std::vector<double>{}));

    Value* it4 = f.iterator(f.make({1, 2}));
    EXPECT_TRUE(isError(f.call1(it4, "take", f.ctx->createNumber(-1))));
}

TEST(IteratorHelpersTest, FlatMap) {
    Fixture f;
    Value* it = f.iterator(f.make({1, 2}));
    Value* flat = f.call1(it, "flatMap", f.ctx->createNativeFunction(pairMapper, 0));
    ASSERT_FALSE(isError(flat));
    Value* out = f.call(flat, "toArray");
    ASSERT_FALSE(isError(out));
    EXPECT_EQ(numbers(out), (std::vector<double>{1, 11, 2, 12}));
}

TEST(IteratorHelpersTest, EagerConsumers) {
    Fixture f;
    Value* it = f.iterator(f.make({1, 2, 3}));
    Value* reduced = f.call1(
        it, "reduce", f.ctx->createNativeFunction(sumCallback, 0));
    EXPECT_EQ(numOf(reduced), 6.0);

    Value* it2 = f.iterator(f.make({1, 2, 3}));
    Value* some = f.call1(it2, "some", f.ctx->createNativeFunction(greaterThanTwo, 0));
    EXPECT_TRUE(boolOf(some));

    Value* it3 = f.iterator(f.make({1, 2, 3}));
    Value* every =
        f.call1(it3, "every", f.ctx->createNativeFunction(greaterThanTwo, 0));
    EXPECT_FALSE(boolOf(every));

    Value* it4 = f.iterator(f.make({1, 2, 3}));
    Value* found =
        f.call1(it4, "find", f.ctx->createNativeFunction(greaterThanTwo, 0));
    EXPECT_EQ(numOf(found), 3.0);

    Value* it5 = f.iterator(f.make({1, 2, 3}));
    Value* forEachResult =
        f.call1(it5, "forEach", f.ctx->createNativeFunction(doubleCallback, 0));
    EXPECT_EQ(forEachResult->type()->kind(), TypeKind::Undefined);
}
