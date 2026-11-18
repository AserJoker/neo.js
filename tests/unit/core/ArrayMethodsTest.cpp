// ArrayMethodsTest.cpp - Array.prototype 迭代/变换方法与 Array.from/of
//
// 验证（ES2024 §23.1.3 / §23.1.2）：
// - forEach/map/filter/some/every/reduce/reduceRight/find*
// - includes/indexOf/lastIndexOf（SameValueZero、空洞）
// - concat/splice/reverse/sort/flat/flatMap/at/fill/copyWithin
// - keys/values/entries/@@iterator
// - Array.from（array-like + iterable + mapfn）、Array.of

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
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

using neo::core::ArrayData;
using neo::core::BooleanData;
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

int g_forEachCount = 0;

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

    Value* call(Value* arr, const char* name, size_t argc = 0, Value* argv[] = nullptr) {
        Value* fn = ctx->getProperty(arr, name);
        return ctx->call(fn, arr, argc, argv);
    }

    Value* callWith(Value* arr, const char* name, Value* a) {
        Value* args[1] = {a};
        return call(arr, name, 1, args);
    }
};

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
bool boolOf(Value* v) { return static_cast<BooleanData*>(v->data())->value; }

bool isError(Value* v) {
    return v != nullptr && (v->type()->kind() == TypeKind::Exception ||
                            v->type()->kind() == TypeKind::Fatal);
}

std::string errorText(IContext* ctx, Value* e) {
    if (!isError(e) || e->type()->kind() != TypeKind::Exception) return "<not exception>";
    auto* data = static_cast<neo::core::ExceptionData*>(e->data());
    Value* msg = ctx->getProperty(data->child, ctx->createString("message"));
    if (msg != nullptr && msg->type()->kind() == TypeKind::String) {
        return static_cast<StringData*>(msg->data())->value;
    }
    return "<no message>";
}

std::vector<double> numbers(Value* arr) {
    auto* d = static_cast<ArrayData*>(arr->data());
    std::vector<double> out;
    for (uint32_t i = 0; i < d->length(); ++i) {
        if (!d->hasElement(i)) {
            out.push_back(std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        out.push_back(numOf(d->elementAt(i)));
    }
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

Value* sumCallback(IContext* ctx, Value*, size_t, Value* argv[]) {
    return ctx->createNumber(numOf(argv[0]) + numOf(argv[1]));
}

Value* countCallback(IContext* ctx, Value*, size_t, Value*[]) {
    ++g_forEachCount;
    return ctx->createUndefined();
}

Value* descending(IContext* ctx, Value*, size_t, Value* argv[]) {
    return ctx->createNumber(numOf(argv[1]) - numOf(argv[0]));
}

}  // namespace

TEST(ArrayMethodsTest, ForEachMapFilter) {
    Fixture f;
    Value* arr = f.make({1, 2, 3});
    g_forEachCount = 0;
    Value* cb = f.ctx->createNativeFunction(countCallback, 0);
    ASSERT_FALSE(isError(f.callWith(arr, "forEach", cb)));
    EXPECT_EQ(g_forEachCount, 3);

    Value* mapCb = f.ctx->createNativeFunction(doubleCallback, 0);
    Value* mapped = f.callWith(arr, "map", mapCb);
    ASSERT_FALSE(isError(mapped));
    EXPECT_EQ(numbers(mapped), (std::vector<double>{2, 4, 6}));

    Value* indexCb = f.ctx->createNativeFunction(addIndexCallback, 0);
    Value* withIndex = f.callWith(arr, "map", indexCb);
    EXPECT_EQ(numbers(withIndex), (std::vector<double>{1, 3, 5}));

    Value* evenCb = f.ctx->createNativeFunction(evenPredicate, 0);
    Value* filtered = f.callWith(arr, "filter", evenCb);
    EXPECT_EQ(numbers(filtered), (std::vector<double>{2}));
}

TEST(ArrayMethodsTest, SomeEveryReduce) {
    Fixture f;
    Value* arr = f.make({1, 2, 3});
    Value* even = f.ctx->createNativeFunction(evenPredicate, 0);
    EXPECT_TRUE(boolOf(f.callWith(arr, "some", even)));
    EXPECT_FALSE(boolOf(f.callWith(arr, "every", even)));

    Value* sum = f.ctx->createNativeFunction(sumCallback, 0);
    EXPECT_DOUBLE_EQ(numOf(f.callWith(arr, "reduce", sum)), 6.0);
    Value* initArgs[2] = {sum, f.ctx->createNumber(10.0)};
    EXPECT_DOUBLE_EQ(numOf(f.call(arr, "reduce", 2, initArgs)), 16.0);
    EXPECT_DOUBLE_EQ(numOf(f.callWith(arr, "reduceRight", sum)), 6.0);
}

TEST(ArrayMethodsTest, ReduceEmptyWithoutInitialThrows) {
    Fixture f;
    Value* arr = f.make({});
    Value* sum = f.ctx->createNativeFunction(sumCallback, 0);
    EXPECT_TRUE(isError(f.callWith(arr, "reduce", sum)));
}

TEST(ArrayMethodsTest, FindVariants) {
    Fixture f;
    Value* arr = f.make({1, 2, 3, 4});
    Value* even = f.ctx->createNativeFunction(evenPredicate, 0);
    EXPECT_DOUBLE_EQ(numOf(f.callWith(arr, "find", even)), 2.0);
    EXPECT_DOUBLE_EQ(numOf(f.callWith(arr, "findIndex", even)), 1.0);
    EXPECT_DOUBLE_EQ(numOf(f.callWith(arr, "findLast", even)), 4.0);
    EXPECT_DOUBLE_EQ(numOf(f.callWith(arr, "findLastIndex", even)), 3.0);
}

TEST(ArrayMethodsTest, IncludesUsesSameValueZero) {
    Fixture f;
    Value* arr = f.make({});
    auto* d = static_cast<ArrayData*>(arr->data());
    d->setLength(3);
    d->setElement(0, f.ctx->createNumber(std::numeric_limits<double>::quiet_NaN()));
    d->setElement(2, f.ctx->createNumber(5.0));

    Value* nan = f.ctx->createNumber(std::numeric_limits<double>::quiet_NaN());
    EXPECT_TRUE(boolOf(f.callWith(arr, "includes", nan)));
    // indexOf 跳过空洞：indexOf(undefined) 不应命中空洞。
    Value* undef = f.ctx->createUndefined();
    EXPECT_DOUBLE_EQ(numOf(f.callWith(arr, "indexOf", undef)), -1.0);
    EXPECT_DOUBLE_EQ(numOf(f.callWith(arr, "lastIndexOf", f.ctx->createNumber(5.0))), 2.0);
}

TEST(ArrayMethodsTest, ConcatSpreadableAndHoles) {
    Fixture f;
    Value* a = f.make({1, 2});
    Value* b = f.make({3});
    Value* args[1] = {b};
    Value* result = f.call(a, "concat", 1, args);
    EXPECT_EQ(numbers(result), (std::vector<double>{1, 2, 3}));

    // 非展开对象作为单个元素。
    Value* obj = f.ctx->createObject();
    Value* args2[1] = {obj};
    Value* result2 = f.call(a, "concat", 1, args2);
    auto* d = static_cast<ArrayData*>(result2->data());
    ASSERT_EQ(d->length(), 3u);
    EXPECT_EQ(d->elementAt(2), obj);
}

TEST(ArrayMethodsTest, SpliceInsertDeleteAndReturn) {
    Fixture f;
    Value* arr = f.make({1, 2, 3, 4});
    Value* args[3] = {f.ctx->createNumber(1.0), f.ctx->createNumber(2.0),
                      f.ctx->createNumber(9.0)};
    Value* removed = f.call(arr, "splice", 3, args);
    ASSERT_FALSE(isError(removed));
    EXPECT_EQ(numbers(removed), (std::vector<double>{2, 3}));
    EXPECT_EQ(numbers(arr), (std::vector<double>{1, 9, 4}));

    Value* delArgs[2] = {f.ctx->createNumber(0.0), f.ctx->createNumber(1.0)};
    Value* removed2 = f.call(arr, "splice", 2, delArgs);
    EXPECT_EQ(numbers(removed2), (std::vector<double>{1}));
    EXPECT_EQ(numbers(arr), (std::vector<double>{9, 4}));
}

TEST(ArrayMethodsTest, SortDefaultAndComparator) {
    Fixture f;
    Value* arr = f.make({10, 2, 1});
    ASSERT_FALSE(isError(f.call(arr, "sort")));
    // 默认按字符串排序：1, 10, 2。
    EXPECT_EQ(numbers(arr), (std::vector<double>{1, 10, 2}));

    Value* cmp = f.ctx->createNativeFunction(descending, 0);
    Value* args[1] = {cmp};
    ASSERT_FALSE(isError(f.call(arr, "sort", 1, args)));
    EXPECT_EQ(numbers(arr), (std::vector<double>{10, 2, 1}));
}

TEST(ArrayMethodsTest, SortPutsUndefinedLast) {
    Fixture f;
    Value* arr = f.make({2, 1});
    auto* d = static_cast<ArrayData*>(arr->data());
    d->setElement(2, f.ctx->createUndefined());
    Value* sortResult = f.call(arr, "sort");
    ASSERT_FALSE(isError(sortResult)) << errorText(f.ctx.get(), sortResult);
    auto* rd = static_cast<ArrayData*>(arr->data());
    EXPECT_DOUBLE_EQ(numOf(rd->elementAt(0)), 1.0);
    EXPECT_DOUBLE_EQ(numOf(rd->elementAt(1)), 2.0);
    EXPECT_EQ(rd->elementAt(2)->type()->kind(), TypeKind::Undefined);
}

TEST(ArrayMethodsTest, ReverseAndAt) {
    Fixture f;
    Value* arr = f.make({1, 2, 3});
    ASSERT_FALSE(isError(f.call(arr, "reverse")));
    EXPECT_EQ(numbers(arr), (std::vector<double>{3, 2, 1}));
    EXPECT_DOUBLE_EQ(numOf(f.callWith(arr, "at", f.ctx->createNumber(-1.0))), 1.0);
    EXPECT_EQ(f.callWith(arr, "at", f.ctx->createNumber(5.0))->type()->kind(),
              TypeKind::Undefined);
}

TEST(ArrayMethodsTest, FlatAndFlatMap) {
    Fixture f;
    Value* inner = f.make({3, 4});
    Value* arr = f.make({1, 2});
    static_cast<ArrayData*>(arr->data())->setElement(2, inner);
    Value* flat = f.call(arr, "flat");
    EXPECT_EQ(numbers(flat), (std::vector<double>{1, 2, 3, 4}));

    Value* base = f.make({1, 2});
    Value* mapCb = f.ctx->createNativeFunction(doubleCallback, 0);
    Value* flatMapped = f.callWith(base, "flatMap", mapCb);
    EXPECT_EQ(numbers(flatMapped), (std::vector<double>{2, 4}));
}

TEST(ArrayMethodsTest, FillAndCopyWithin) {
    Fixture f;
    Value* arr = f.make({1, 2, 3, 4});
    Value* fillArgs[3] = {f.ctx->createNumber(9.0), f.ctx->createNumber(1.0),
                          f.ctx->createNumber(3.0)};
    ASSERT_FALSE(isError(f.call(arr, "fill", 3, fillArgs)));
    EXPECT_EQ(numbers(arr), (std::vector<double>{1, 9, 9, 4}));

    Value* copyArgs[2] = {f.ctx->createNumber(0.0), f.ctx->createNumber(2.0)};
    ASSERT_FALSE(isError(f.call(arr, "copyWithin", 2, copyArgs)));
    EXPECT_EQ(numbers(arr), (std::vector<double>{9, 4, 9, 4}));
}

TEST(ArrayMethodsTest, IteratorMethods) {
    Fixture f;
    Value* arr = f.make({7, 8});
    std::vector<Value*> values;
    ASSERT_FALSE(isError(neo::core::detail::iterableToList(f.ctx.get(), arr, values)));
    ASSERT_EQ(values.size(), 2u);
    EXPECT_DOUBLE_EQ(numOf(values[0]), 7.0);

    Value* keys = f.call(arr, "keys");
    Value* next = f.ctx->getProperty(keys, "next");
    Value* r = f.ctx->call(next, keys, 0, nullptr);
    Value* value = f.ctx->getProperty(r, "value");
    EXPECT_DOUBLE_EQ(numOf(value), 0.0);
}

TEST(ArrayMethodsTest, FromArrayLikeAndIterable) {
    Fixture f;
    Value* arrayFn = f.ctx->global()->lookup("Array");

    Value* obj = f.ctx->createObject();
    f.ctx->setProperty(obj, "length", f.ctx->createNumber(2.0));
    f.ctx->setProperty(obj, "0", f.ctx->createNumber(4.0));
    f.ctx->setProperty(obj, "1", f.ctx->createNumber(5.0));
    Value* args[1] = {obj};
    Value* fromLike = f.ctx->call(f.ctx->getProperty(arrayFn, "from"), arrayFn, 1, args);
    EXPECT_EQ(numbers(fromLike), (std::vector<double>{4, 5}));

    Value* source = f.make({1, 2});
    Value* iterArgs[1] = {source};
    Value* fromIter = f.ctx->call(f.ctx->getProperty(arrayFn, "from"), arrayFn, 1,
                                  iterArgs);
    EXPECT_EQ(numbers(fromIter), (std::vector<double>{1, 2}));

    Value* mapCb = f.ctx->createNativeFunction(doubleCallback, 0);
    Value* mapArgs[2] = {source, mapCb};
    Value* mapped = f.ctx->call(f.ctx->getProperty(arrayFn, "from"), arrayFn, 2, mapArgs);
    EXPECT_EQ(numbers(mapped), (std::vector<double>{2, 4}));
}

TEST(ArrayMethodsTest, OfCreatesArray) {
    Fixture f;
    Value* arrayFn = f.ctx->global()->lookup("Array");
    Value* args[2] = {f.ctx->createNumber(1.0), f.ctx->createNumber(2.0)};
    Value* arr = f.ctx->call(f.ctx->getProperty(arrayFn, "of"), arrayFn, 2, args);
    EXPECT_EQ(numbers(arr), (std::vector<double>{1, 2}));
}
