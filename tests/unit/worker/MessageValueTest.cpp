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
#include "core/types/ArrayBuffer.h"
#include "core/types/Collection.h"
#include "core/types/Date.h"
#include "core/types/Error.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
#include "core/types/RegExp.h"
#include "core/types/String.h"
#include "core/types/TypedArray.h"
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

TEST(MessageValueTest, TransfersArrayBuffer) {
    Fixture f;
    Value* buffer = f.ctx->createArrayBuffer(4);
    auto* data = neo::core::asArrayBuffer(buffer);
    data->bytes = {1, 2, 3, 4};

    std::vector<Value*> transfer{buffer};
    MessageValue mv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), buffer, mv, transfer), nullptr);
    ASSERT_EQ(mv.kind, MessageValueKind::ArrayBuffer);
    EXPECT_EQ(mv.bytes, data->bytes);
    // valueToMessage 不产生副作用；调用方在成功后分离。
    EXPECT_FALSE(data->detached);

    data->bytes.clear();
    data->detached = true;

    Value* restored = messageToValue(f.ctx.get(), mv);
    ASSERT_FALSE(neo::core::detail::isErrorValue(restored));
    auto* restoredData = neo::core::asArrayBuffer(restored);
    EXPECT_FALSE(restoredData->detached);
    EXPECT_EQ(restoredData->bytes, (std::vector<uint8_t>{1, 2, 3, 4}));
}

TEST(MessageValueTest, TransferListRejectsDuplicatesAndDetached) {
    Fixture f;
    Value* buffer = f.ctx->createArrayBuffer(4);

    std::vector<Value*> transfer;
    Value* list = f.ctx->createArray();
    f.ctx->arrayAppend(list, buffer);
    f.ctx->arrayAppend(list, buffer);
    Value* err = neo::worker::resolveTransferList(f.ctx.get(), list, transfer);
    ASSERT_TRUE(neo::core::detail::isErrorValue(err));

    neo::core::asArrayBuffer(buffer)->detached = true;
    std::vector<Value*> single;
    Value* list2 = f.ctx->createArray();
    f.ctx->arrayAppend(list2, buffer);
    Value* err2 = neo::worker::resolveTransferList(f.ctx.get(), list2, single);
    ASSERT_TRUE(neo::core::detail::isErrorValue(err2));
}

TEST(MessageValueTest, ClonesErrorObject) {
    Fixture f;
    Value* err = f.ctx->createError("boom", "TypeError");
    f.ctx->defineProperty(err, "name", f.ctx->createString("TypeError"));

    MessageValue mv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), err, mv), nullptr);
    ASSERT_EQ(mv.kind, MessageValueKind::Error);
    EXPECT_EQ(mv.string, "TypeError");
    EXPECT_EQ(mv.string2, "boom");
    EXPECT_FALSE(mv.stack.empty());

    Value* restored = messageToValue(f.ctx.get(), mv);
    ASSERT_FALSE(neo::core::detail::isErrorValue(restored));
    EXPECT_EQ(restored->type()->kind(), TypeKind::Error);
    EXPECT_EQ(stringAt(f.ctx->getProperty(restored, "name")), "TypeError");
    EXPECT_EQ(stringAt(f.ctx->getProperty(restored, "message")), "boom");
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

TEST(MessageValueTest, SupportsCyclicReferences) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    f.ctx->setProperty(obj, "self", obj);

    MessageValue mv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), obj, mv), nullptr);
    ASSERT_EQ(mv.kind, MessageValueKind::Object);
    ASSERT_NE(mv.id, 0u);
    ASSERT_EQ(mv.object.size(), 1u);
    ASSERT_EQ(mv.object[0].second.kind, MessageValueKind::Reference);
    EXPECT_EQ(mv.object[0].second.ref, mv.id);

    Value* restored = messageToValue(f.ctx.get(), mv);
    ASSERT_FALSE(neo::core::detail::isErrorValue(restored));
    Value* self = f.ctx->getProperty(restored, "self");
    EXPECT_EQ((self)->data(), (restored)->data());
}

TEST(MessageValueTest, ClonesDateMapSetAndRegExp) {
    Fixture f;
    Value* date = f.ctx->createDate(1234.5);
    Value* map = f.ctx->createCollection(neo::core::CollectionKind::Map);
    Value* mapArgs[] = {f.ctx->createString("k"), f.ctx->createNumber(7)};
    f.ctx->call(f.ctx->getProperty(map, "set"), map, 2, mapArgs);
    Value* set = f.ctx->createCollection(neo::core::CollectionKind::Set);
    Value* setArgs[] = {f.ctx->createNumber(9)};
    f.ctx->call(f.ctx->getProperty(set, "add"), set, 1, setArgs);
    Value* re = f.ctx->createRegExp("a+b", "gi");

    MessageValue dateMv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), date, dateMv), nullptr);
    EXPECT_EQ(dateMv.kind, MessageValueKind::Date);
    EXPECT_EQ(dateMv.number, 1234.5);

    MessageValue mapMv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), map, mapMv), nullptr);
    ASSERT_EQ(mapMv.kind, MessageValueKind::Map);
    ASSERT_EQ(mapMv.entries.size(), 1u);
    EXPECT_EQ(mapMv.entries[0].first.string, "k");
    EXPECT_EQ(mapMv.entries[0].second.number, 7.0);

    MessageValue setMv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), set, setMv), nullptr);
    ASSERT_EQ(setMv.kind, MessageValueKind::Set);
    ASSERT_EQ(setMv.items.size(), 1u);
    EXPECT_EQ(setMv.items[0].number, 9.0);

    MessageValue reMv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), re, reMv), nullptr);
    EXPECT_EQ(reMv.kind, MessageValueKind::RegExp);
    EXPECT_EQ(reMv.string, "a+b");
    EXPECT_EQ(reMv.string2, "gi");

    Value* restoredMap = messageToValue(f.ctx.get(), mapMv);
    ASSERT_FALSE(neo::core::detail::isErrorValue(restoredMap));
    Value* getArgs[] = {f.ctx->createString("k")};
    Value* got = f.ctx->call(f.ctx->getProperty(restoredMap, "get"),
                             restoredMap, 1, getArgs);
    ASSERT_EQ(got->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numberAt(got), 7.0);
}

TEST(MessageValueTest, ClonesArrayBufferAndTypedArray) {
    Fixture f;
    Value* buffer = f.ctx->createArrayBuffer(8);
    auto* bufData = neo::core::asArrayBuffer(buffer);
    bufData->bytes = {1, 2, 3, 4, 5, 6, 7, 8};
    Value* view = f.ctx->createTypedArray(buffer, 0, 2,
                                          neo::core::TypedArrayKind::Uint16);

    MessageValue bufMv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), buffer, bufMv), nullptr);
    ASSERT_EQ(bufMv.kind, MessageValueKind::ArrayBuffer);
    EXPECT_EQ(bufMv.bytes.size(), 8u);

    MessageValue viewMv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), view, viewMv), nullptr);
    ASSERT_EQ(viewMv.kind, MessageValueKind::TypedArray);
    ASSERT_EQ(viewMv.buffer.size(), 1u);
    ASSERT_EQ(viewMv.buffer[0].kind, MessageValueKind::ArrayBuffer);
    EXPECT_EQ(viewMv.length, 2u);
    EXPECT_EQ(viewMv.typedArrayKind,
              static_cast<uint8_t>(neo::core::TypedArrayKind::Uint16));

    Value* restored = messageToValue(f.ctx.get(), viewMv);
    ASSERT_FALSE(neo::core::detail::isErrorValue(restored));
    ASSERT_EQ(restored->type()->kind(), TypeKind::TypedArray);
    auto* data = neo::core::asTypedArray(restored);
    EXPECT_EQ(data->length, 2u);
    EXPECT_EQ(data->kind, neo::core::TypedArrayKind::Uint16);
    auto* restoredBuf = neo::core::asArrayBuffer(data->buffer);
    EXPECT_EQ(restoredBuf->bytes, bufData->bytes);
}

TEST(MessageValueTest, SharedArrayBufferCloneSharesBlock) {
    Fixture f;
    Value* sab = f.ctx->createSharedArrayBuffer(
        neo::core::SharedBufferBlock::create(4));
    ASSERT_FALSE(neo::core::detail::isErrorValue(sab));
    ASSERT_EQ(sab->type()->kind(), TypeKind::SharedArrayBuffer);

    auto* data = neo::core::asSharedArrayBuffer(sab);
    data->data()[0] = 5;

    MessageValue mv;
    ASSERT_EQ(valueToMessage(f.ctx.get(), sab, mv), nullptr);
    ASSERT_EQ(mv.kind, MessageValueKind::SharedArrayBuffer);
    // 克隆只增加引用，不复制内存。
    EXPECT_EQ(mv.shared, data->shared);

    Value* restored = messageToValue(f.ctx.get(), mv);
    ASSERT_FALSE(neo::core::detail::isErrorValue(restored));
    auto* restoredData = neo::core::asSharedArrayBuffer(restored);
    ASSERT_NE(restoredData, nullptr);
    EXPECT_EQ(restoredData->shared, data->shared);

    // 两个包装对象指向同一块内存。
    restoredData->data()[1] = 9;
    EXPECT_EQ(data->data()[1], 9);
}

TEST(MessageValueTest, TransferListRejectsSharedArrayBuffer) {
    Fixture f;
    Value* sab = f.ctx->createSharedArrayBuffer(
        neo::core::SharedBufferBlock::create(4));

    Value* list = f.ctx->createArray();
    f.ctx->arrayAppend(list, sab);
    std::vector<Value*> transfer;
    Value* err = neo::worker::resolveTransferList(f.ctx.get(), list, transfer);
    ASSERT_TRUE(neo::core::detail::isErrorValue(err));
}
