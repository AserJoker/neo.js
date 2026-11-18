// ObjectStaticsTest.cpp - Object.is/hasOwn/getOwnPropertySymbols/fromEntries
//
// 验证（ES2024 §20.1.2）。

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
#include <memory>
#include <string>

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

struct Fixture {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    NullSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx = createContext(alloc.get(), &console);

    Value* objectCtor() { return ctx->global()->lookup("Object"); }

    Value* statik(const char* name) { return ctx->getProperty(objectCtor(), name); }

    Value* callStatic(const char* name, size_t argc, Value* argv[]) {
        return ctx->call(statik(name), ctx->createUndefined(), argc, argv);
    }
};

bool isError(Value* v) {
    return v != nullptr && (v->type()->kind() == TypeKind::Exception ||
                            v->type()->kind() == TypeKind::Fatal);
}

bool boolOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Boolean &&
           static_cast<BooleanData*>(v->data())->value;
}

uint32_t lengthOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Array
               ? static_cast<ArrayData*>(v->data())->length()
               : 0;
}

Value* elementAt(Value* v, uint32_t i) {
    return static_cast<ArrayData*>(v->data())->elementAt(i);
}

std::string strOf(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::String
               ? static_cast<StringData*>(v->data())->value
               : "<not string>";
}

}  // namespace

TEST(ObjectStaticsTest, IsUsesSameValue) {
    Fixture f;
    Value* nan = f.ctx->createNumber(std::nan(""));
    Value* args1[2] = {nan, nan};
    EXPECT_TRUE(boolOf(f.callStatic("is", 2, args1)));

    Value* args2[2] = {f.ctx->createNumber(0.0), f.ctx->createNumber(-0.0)};
    EXPECT_FALSE(boolOf(f.callStatic("is", 2, args2)));

    Value* args3[2] = {f.ctx->createNumber(1.0), f.ctx->createNumber(1.0)};
    EXPECT_TRUE(boolOf(f.callStatic("is", 2, args3)));
}

TEST(ObjectStaticsTest, HasOwn) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    f.ctx->defineProperty(obj, "a", f.ctx->createNumber(1.0), true, true, true);
    Value* args1[2] = {obj, f.ctx->createString("a")};
    EXPECT_TRUE(boolOf(f.callStatic("hasOwn", 2, args1)));
    Value* args2[2] = {obj, f.ctx->createString("b")};
    EXPECT_FALSE(boolOf(f.callStatic("hasOwn", 2, args2)));
    // 继承属性不算自有
    Value* args3[2] = {obj, f.ctx->createString("toString")};
    EXPECT_FALSE(boolOf(f.callStatic("hasOwn", 2, args3)));
}

TEST(ObjectStaticsTest, GetOwnPropertySymbols) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    Value* sym = f.ctx->createSymbol("s");
    f.ctx->defineProperty(obj, "a", f.ctx->createNumber(1.0), true, true, true);
    f.ctx->defineProperty(obj, sym, f.ctx->createNumber(2.0), true, true, true);
    Value* args[1] = {obj};
    Value* symbols = f.callStatic("getOwnPropertySymbols", 1, args);
    ASSERT_FALSE(isError(symbols));
    ASSERT_EQ(lengthOf(symbols), 1u);
    EXPECT_EQ(elementAt(symbols, 0), sym);
}

TEST(ObjectStaticsTest, FromEntries) {
    Fixture f;
    Value* pair1 = f.ctx->createArray();
    static_cast<ArrayData*>(pair1->data())->setElement(0, f.ctx->createString("a"));
    static_cast<ArrayData*>(pair1->data())->setElement(1, f.ctx->createNumber(1.0));
    Value* pair2 = f.ctx->createArray();
    static_cast<ArrayData*>(pair2->data())->setElement(0, f.ctx->createString("b"));
    static_cast<ArrayData*>(pair2->data())->setElement(1, f.ctx->createNumber(2.0));
    Value* entries = f.ctx->createArray();
    static_cast<ArrayData*>(entries->data())->setElement(0, pair1);
    static_cast<ArrayData*>(entries->data())->setElement(1, pair2);

    Value* args[1] = {entries};
    Value* result = f.callStatic("fromEntries", 1, args);
    ASSERT_FALSE(isError(result));
    EXPECT_EQ(strOf(f.ctx->getProperty(result, "a")), "<not string>");
    EXPECT_EQ(static_cast<NumberData*>(f.ctx->getProperty(result, "a")->data())->value,
              1.0);
    EXPECT_EQ(static_cast<NumberData*>(f.ctx->getProperty(result, "b")->data())->value,
              2.0);
}
