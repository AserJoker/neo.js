// MessageValueTest.cpp - JS Value ↔ MessageValue 深拷贝单元测试
//
// 验证 W2 的 JSON-like 克隆范围：原始值、嵌套对象/数组、undefined、稀疏数组空洞；
// 不支持的类型与循环引用抛 DataCloneError。
//
// 关联 ADR: 006（异常即值）、028（infra）

#include "worker/MessageValue.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Error.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/IOutputSink.h"
#include "infra/MallocAllocator.h"
#include "worker/MessageClone.h"  // private/worker/MessageClone.h

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>

namespace {

using neo::core::ArrayData;
using neo::core::ExceptionData;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;
using neo::worker::MessageValue;
using neo::worker::MessageValueKind;
using neo::worker::messageToValue;
using neo::worker::valueToMessage;

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
};

double numberAt(Value* v) {
    return static_cast<NumberData*>(v->data())->value;
}

std::string stringAt(Value* v) {
    return static_cast<StringData*>(v->data())->value;
}

}  // namespace

TEST(MessageValueTest, ClonesPrimitives) {
    Fixture f;

    MessageValue mv;
    EXPECT_EQ(valueToMessage(f.ctx.get(), f.ctx->createUndefined(), mv), nullptr);
    EXPECT_EQ(mv.kind, MessageValueKind::Undefined);

    EXPECT_EQ(valueToMessage(f.ctx.get(), f.ctx->createNull(), mv), nullptr);
    EXPECT_EQ(mv.kind, MessageValueKind::Null);

    EXPECT_EQ(valueToMessage(f.ctx.get(), f.ctx->createBoolean(true), mv), nullptr);
    EXPECT_EQ(mv.kind, MessageValueKind::Boolean);
    EXPECT_TRUE(mv.boolean);

    EXPECT_EQ(valueToMessage(f.ctx.get(), f.ctx->createNumber(3.5), mv), nullptr);
    EXPECT_EQ(mv.kind, MessageValueKind::Number);
    EXPECT_EQ(mv.number, 3.5);

    EXPECT_EQ(valueToMessage(f.ctx.get(), f.ctx->createNumber(NAN), mv), nullptr);
    EXPECT_TRUE(std::isnan(mv.number));

    EXPECT_EQ(valueToMessage(f.ctx.get(), f.ctx->createString("hi"), mv), nullptr);
    EXPECT_EQ(mv.kind, MessageValueKind::String);
    EXPECT_EQ(mv.string, "hi");
}

TEST(MessageValueTest, ClonesNestedObjectAndArray) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    Value* arr = f.ctx->createArray();
    f.ctx->arrayAppend(arr, f.ctx->createNumber(1));
    f.ctx->arrayAppend(arr, f.ctx->createString("two"));
    f.ctx->defineProperty(obj, "n", f.ctx->createNumber(42));
    f.ctx->defineProperty(obj, "list", arr);

    MessageValue mv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), obj, mv), nullptr);
    ASSERT_EQ(mv.kind, MessageValueKind::Object);
    ASSERT_EQ(mv.object.size(), 2u);
    EXPECT_EQ(mv.object[0].first, "n");
    EXPECT_EQ(mv.object[0].second.number, 42.0);
    ASSERT_EQ(mv.object[1].first, "list");
    ASSERT_EQ(mv.object[1].second.kind, MessageValueKind::Array);
    ASSERT_EQ(mv.object[1].second.array.size(), 2u);
    EXPECT_EQ(mv.object[1].second.array[0].number, 1.0);
    EXPECT_EQ(mv.object[1].second.array[1].string, "two");
}

TEST(MessageValueTest, PreservesSparseArrayHoles) {
    Fixture f;
    Value* arr = f.ctx->createArray();
    f.ctx->setProperty(arr, "2", f.ctx->createNumber(7));  // 0/1 为空洞

    MessageValue mv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), arr, mv), nullptr);
    ASSERT_EQ(mv.kind, MessageValueKind::Array);
    ASSERT_EQ(mv.array.size(), 3u);
    EXPECT_EQ(mv.array[0].kind, MessageValueKind::Hole);
    EXPECT_EQ(mv.array[1].kind, MessageValueKind::Hole);
    EXPECT_EQ(mv.array[2].number, 7.0);

    Value* restored = messageToValue(f.ctx.get(), mv);
    ASSERT_FALSE(neo::core::detail::isErrorValue(restored));
    auto* data = static_cast<ArrayData*>(restored->data());
    EXPECT_EQ(data->length(), 3u);
    EXPECT_FALSE(data->hasElement(0));
    EXPECT_FALSE(data->hasElement(1));
    EXPECT_EQ(numberAt(data->elementAt(2)), 7.0);
}

TEST(MessageValueTest, RoundTripsThroughContext) {
    Fixture f;
    MessageValue mv = MessageValue::objectValue({
        {"a", MessageValue::numberValue(1)},
        {"b", MessageValue::arrayValue({
                  MessageValue::stringValue("x"),
                  MessageValue::null(),
                  MessageValue::undefined(),
              })},
    });

    Value* value = messageToValue(f.ctx.get(), mv);
    ASSERT_FALSE(neo::core::detail::isErrorValue(value));

    MessageValue again;
    ASSERT_EQ(valueToMessage(f.ctx.get(), value, again), nullptr);
    ASSERT_EQ(again.kind, MessageValueKind::Object);
    ASSERT_EQ(again.object.size(), 2u);
    EXPECT_EQ(again.object[0].first, "a");
    EXPECT_EQ(again.object[0].second.number, 1.0);
    ASSERT_EQ(again.object[1].second.kind, MessageValueKind::Array);
    ASSERT_EQ(again.object[1].second.array.size(), 3u);
    EXPECT_EQ(again.object[1].second.array[0].string, "x");
    EXPECT_EQ(again.object[1].second.array[1].kind, MessageValueKind::Null);
    EXPECT_EQ(again.object[1].second.array[2].kind, MessageValueKind::Undefined);
}

TEST(MessageValueTest, UnsupportedTypeThrowsDataCloneError) {
    Fixture f;
    Value* sym = f.ctx->createSymbol("s");
    MessageValue mv;
    Value* err = valueToMessage(f.ctx.get(), sym, mv);
    ASSERT_TRUE(neo::core::detail::isErrorValue(err));
    ASSERT_EQ(err->type()->kind(), TypeKind::Exception);

    auto* ex = static_cast<ExceptionData*>(err->data());
    Value* name = f.ctx->getProperty(ex->child, "name");
    ASSERT_EQ(name->type()->kind(), TypeKind::String);
    EXPECT_EQ(stringAt(name), "DataCloneError");
}

TEST(MessageValueTest, CyclicReferenceThrowsDataCloneError) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    f.ctx->setProperty(obj, "self", obj);

    MessageValue mv;
    Value* err = valueToMessage(f.ctx.get(), obj, mv);
    ASSERT_TRUE(neo::core::detail::isErrorValue(err));
    ASSERT_EQ(err->type()->kind(), TypeKind::Exception);

    auto* ex = static_cast<ExceptionData*>(err->data());
    Value* name = f.ctx->getProperty(ex->child, "name");
    ASSERT_EQ(name->type()->kind(), TypeKind::String);
    EXPECT_EQ(stringAt(name), "DataCloneError");
}
