// CollectionTest.cpp - Map/Set/WeakMap/WeakSet
//
// 验证（ES2024 §24）：
// - Map/Set 的增删查改、size、SameValueZero
// - 集合构造函数的 iterable 初始化
// - keys/values/entries/@@iterator 与 forEach
// - WeakMap/WeakSet 的弱键类型校验

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Collection.h"
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
#include <vector>

namespace {

using neo::core::ArrayData;
using neo::core::BooleanData;
using neo::core::CollectionData;
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

    Value* pair(Value* key, Value* value) {
        Value* arr = ctx->createArray();
        auto* d = static_cast<ArrayData*>(arr->data());
        d->setElement(0, key);
        d->setElement(1, value);
        return arr;
    }

    Value* arrayOf(std::initializer_list<Value*> values) {
        Value* arr = ctx->createArray();
        auto* d = static_cast<ArrayData*>(arr->data());
        uint32_t i = 0;
        for (Value* v : values) d->setElement(i++, v);
        return arr;
    }

    // 用 Iterator.prototype.toArray 消费集合迭代器
    Value* drain(Value* iterator) {
        return call(iterator, "toArray");
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
               ? static_cast<StringData*>(v->data())->value
               : "<not string>";
}

std::vector<double> numbers(Value* arr) {
    auto* d = static_cast<ArrayData*>(arr->data());
    std::vector<double> out;
    for (uint32_t i = 0; i < d->length(); ++i) out.push_back(numOf(d->elementAt(i)));
    return out;
}

}  // namespace

TEST(CollectionTest, MapBasics) {
    Fixture f;
    Value* map = f.construct("Map");
    ASSERT_FALSE(isError(map));

    Value* one = f.ctx->createNumber(1.0);
    EXPECT_EQ(f.call2(map, "set", f.ctx->createString("a"), one), map);
    EXPECT_EQ(numOf(f.call1(map, "get", f.ctx->createString("a"))), 1.0);
    EXPECT_TRUE(boolOf(f.call1(map, "has", f.ctx->createString("a"))));
    EXPECT_FALSE(boolOf(f.call1(map, "has", f.ctx->createString("b"))));
    EXPECT_EQ(numOf(f.ctx->getProperty(map, "size")), 1.0);

    // 重复 set 更新而非新增
    f.call2(map, "set", f.ctx->createString("a"), f.ctx->createNumber(2.0));
    EXPECT_EQ(numOf(f.ctx->getProperty(map, "size")), 1.0);
    EXPECT_EQ(numOf(f.call1(map, "get", f.ctx->createString("a"))), 2.0);

    EXPECT_TRUE(boolOf(f.call1(map, "delete", f.ctx->createString("a"))));
    EXPECT_FALSE(boolOf(f.call1(map, "delete", f.ctx->createString("a"))));
    EXPECT_EQ(numOf(f.ctx->getProperty(map, "size")), 0.0);
}

TEST(CollectionTest, MapSameValueZero) {
    Fixture f;
    Value* map = f.construct("Map");
    f.call2(map, "set", f.ctx->createNumber(std::nan("")), f.ctx->createNumber(1.0));
    EXPECT_EQ(numOf(f.call1(map, "get", f.ctx->createNumber(std::nan("")))), 1.0);

    f.call2(map, "set", f.ctx->createNumber(-0.0), f.ctx->createNumber(2.0));
    EXPECT_EQ(numOf(f.call1(map, "get", f.ctx->createNumber(0.0))), 2.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(map, "size")), 2.0);
}

TEST(CollectionTest, MapConstructorAndIteration) {
    Fixture f;
    Value* entries = f.arrayOf({f.pair(f.ctx->createString("a"), f.ctx->createNumber(1.0)),
                                f.pair(f.ctx->createString("b"), f.ctx->createNumber(2.0))});
    Value* args[1] = {entries};
    Value* map = f.construct("Map", 1, args);
    ASSERT_FALSE(isError(map));
    EXPECT_EQ(numOf(f.ctx->getProperty(map, "size")), 2.0);

    Value* keys = f.drain(f.call(map, "keys"));
    ASSERT_FALSE(isError(keys));
    ASSERT_EQ(static_cast<ArrayData*>(keys->data())->length(), 2u);
    EXPECT_EQ(strOf(static_cast<ArrayData*>(keys->data())->elementAt(0)), "a");

    Value* values = f.drain(f.call(map, "values"));
    EXPECT_EQ(numbers(values), (std::vector<double>{1, 2}));

    Value* pairs = f.drain(f.call(map, "entries"));
    ASSERT_EQ(static_cast<ArrayData*>(pairs->data())->length(), 2u);
    Value* first = static_cast<ArrayData*>(pairs->data())->elementAt(0);
    EXPECT_EQ(strOf(static_cast<ArrayData*>(first->data())->elementAt(0)), "a");
    EXPECT_EQ(numOf(static_cast<ArrayData*>(first->data())->elementAt(1)), 1.0);

    // @@iterator === entries
    Value* iter = f.ctx->getProperty(map, f.ctx->getWellKnownSymbol(
                                             neo::core::WellKnownSymbol::Iterator));
    Value* viaSymbol = f.ctx->call(iter, map, 0, nullptr);
    Value* symbolPairs = f.drain(viaSymbol);
    EXPECT_EQ(static_cast<ArrayData*>(symbolPairs->data())->length(), 2u);
}

TEST(CollectionTest, MapForEach) {
    Fixture f;
    Value* map = f.construct("Map");
    f.call2(map, "set", f.ctx->createString("a"), f.ctx->createNumber(1.0));
    f.call2(map, "set", f.ctx->createString("b"), f.ctx->createNumber(2.0));

    static std::vector<double> seen;
    seen.clear();
    Value* cb = f.ctx->createNativeFunction(
        [](IContext* ctx, Value*, size_t, Value* argv[]) -> Value* {
            seen.push_back(numOf(argv[0]));
            return ctx->createUndefined();
        },
        0);
    Value* r = f.call1(map, "forEach", cb);
    ASSERT_FALSE(isError(r));
    EXPECT_EQ(seen, (std::vector<double>{1, 2}));
}

TEST(CollectionTest, SetBasicsAndIteration) {
    Fixture f;
    Value* set = f.construct("Set");
    ASSERT_FALSE(isError(set));
    EXPECT_EQ(f.call1(set, "add", f.ctx->createNumber(1.0)), set);
    f.call1(set, "add", f.ctx->createNumber(2.0));
    f.call1(set, "add", f.ctx->createNumber(2.0));
    EXPECT_EQ(numOf(f.ctx->getProperty(set, "size")), 2.0);
    EXPECT_TRUE(boolOf(f.call1(set, "has", f.ctx->createNumber(1.0))));
    EXPECT_TRUE(boolOf(f.call1(set, "delete", f.ctx->createNumber(1.0))));
    EXPECT_FALSE(boolOf(f.call1(set, "has", f.ctx->createNumber(1.0))));

    Value* values = f.drain(f.call(set, "values"));
    EXPECT_EQ(numbers(values), (std::vector<double>{2}));

    // keys 与 values 是同一函数对象
    EXPECT_EQ((f.ctx->getProperty(set, "keys"))->data(), (f.ctx->getProperty(set, "values"))->data());
}

TEST(CollectionTest, SetConstructor) {
    Fixture f;
    Value* values = f.arrayOf({f.ctx->createNumber(1.0), f.ctx->createNumber(2.0),
                               f.ctx->createNumber(2.0)});
    Value* args[1] = {values};
    Value* set = f.construct("Set", 1, args);
    ASSERT_FALSE(isError(set));
    EXPECT_EQ(numOf(f.ctx->getProperty(set, "size")), 2.0);
    Value* pairs = f.drain(f.call(set, "entries"));
    ASSERT_EQ(static_cast<ArrayData*>(pairs->data())->length(), 2u);
    Value* first = static_cast<ArrayData*>(pairs->data())->elementAt(0);
    EXPECT_EQ(numOf(static_cast<ArrayData*>(first->data())->elementAt(0)), 1.0);
    EXPECT_EQ(numOf(static_cast<ArrayData*>(first->data())->elementAt(1)), 1.0);
}

TEST(CollectionTest, WeakCollections) {
    Fixture f;
    Value* weakMap = f.construct("WeakMap");
    ASSERT_FALSE(isError(weakMap));
    Value* key = f.ctx->createObject();
    f.call2(weakMap, "set", key, f.ctx->createNumber(7.0));
    EXPECT_EQ(numOf(f.call1(weakMap, "get", key)), 7.0);
    EXPECT_TRUE(boolOf(f.call1(weakMap, "has", key)));
    EXPECT_TRUE(boolOf(f.call1(weakMap, "delete", key)));
    EXPECT_FALSE(boolOf(f.call1(weakMap, "has", key)));
    // 原始值不能作为弱键
    EXPECT_TRUE(isError(f.call2(weakMap, "set", f.ctx->createNumber(1.0),
                                f.ctx->createNumber(2.0))));

    Value* weakSet = f.construct("WeakSet");
    Value* value = f.ctx->createObject();
    f.call1(weakSet, "add", value);
    EXPECT_TRUE(boolOf(f.call1(weakSet, "has", value)));
    EXPECT_TRUE(boolOf(f.call1(weakSet, "delete", value)));
    EXPECT_TRUE(isError(f.call1(weakSet, "add", f.ctx->createString("x"))));
}

TEST(CollectionTest, RequiresNew) {
    Fixture f;
    Value* mapCtor = f.ctor("Map");
    Value* r = f.ctx->call(mapCtor, f.ctx->createUndefined(), 0, nullptr);
    EXPECT_TRUE(isError(r));
}
