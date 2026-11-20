// ArrayBufferTest.cpp - ArrayBuffer 与 DataView
//
// 验证（ES2024 §25.1 / §25.3）。

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

    Value* arrayBuffer(double length) {
        Value* args[1] = {ctx->createNumber(length)};
        return ctx->construct(ctor("ArrayBuffer"), 1, args);
    }

    Value* dataView(Value* buffer, size_t argc = 1, Value* extra[] = nullptr) {
        Value* args[3] = {buffer, nullptr, nullptr};
        for (size_t i = 1; i < argc; ++i) args[i] = extra[i - 1];
        return ctx->construct(ctor("DataView"), argc, args);
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

}  // namespace

TEST(ArrayBufferTest, ByteLengthAndSlice) {
    Fixture f;
    Value* buffer = f.arrayBuffer(8.0);
    ASSERT_FALSE(isError(buffer));
    EXPECT_EQ(numOf(f.ctx->getProperty(buffer, "byteLength")), 8.0);

    Value* args[2] = {f.ctx->createNumber(2.0), f.ctx->createNumber(6.0)};
    Value* sliced = f.call(buffer, "slice", 2, args);
    ASSERT_FALSE(isError(sliced));
    EXPECT_EQ(numOf(f.ctx->getProperty(sliced, "byteLength")), 4.0);

    Value* isView = f.ctx->getProperty(f.ctor("ArrayBuffer"), "isView");
    Value* view = f.dataView(buffer);
    Value* viewArg[1] = {view};
    EXPECT_TRUE(boolOf(f.ctx->call(isView, f.ctx->createUndefined(), 1, viewArg)));
    Value* bufferArg[1] = {buffer};
    EXPECT_FALSE(boolOf(f.ctx->call(isView, f.ctx->createUndefined(), 1, bufferArg)));
}

TEST(ArrayBufferTest, DataViewWindow) {
    Fixture f;
    Value* buffer = f.arrayBuffer(8.0);
    Value* offsetArgs[1] = {f.ctx->createNumber(4.0)};
    Value* view = f.dataView(buffer, 2, offsetArgs);
    ASSERT_FALSE(isError(view));
    EXPECT_EQ(numOf(f.ctx->getProperty(view, "byteOffset")), 4.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(view, "byteLength")), 4.0);
    EXPECT_EQ(f.ctx->getProperty(view, "buffer"), buffer);
}

TEST(ArrayBufferTest, IntAndEndianness) {
    Fixture f;
    Value* buffer = f.arrayBuffer(8.0);
    Value* view = f.dataView(buffer);

    f.call2(view, "setUint8", f.ctx->createNumber(0), f.ctx->createNumber(255.0));
    EXPECT_EQ(numOf(f.call1(view, "getUint8", f.ctx->createNumber(0))), 255.0);
    EXPECT_EQ(numOf(f.call1(view, "getInt8", f.ctx->createNumber(0))), -1.0);

    // 默认大端
    f.call2(view, "setInt16", f.ctx->createNumber(0), f.ctx->createNumber(0x1234));
    EXPECT_EQ(numOf(f.call1(view, "getUint8", f.ctx->createNumber(0))), 0x12);
    EXPECT_EQ(numOf(f.call1(view, "getUint8", f.ctx->createNumber(1))), 0x34);
    EXPECT_EQ(numOf(f.call1(view, "getInt16", f.ctx->createNumber(0))), 0x1234);

    // 小端
    f.call3(view, "setInt16", f.ctx->createNumber(0), f.ctx->createNumber(0x1234),
            f.ctx->createBoolean(true));
    EXPECT_EQ(numOf(f.call1(view, "getUint8", f.ctx->createNumber(0))), 0x34);
    EXPECT_EQ(numOf(f.call2(view, "getUint16", f.ctx->createNumber(0),
                            f.ctx->createBoolean(true))),
              0x1234);
}

TEST(ArrayBufferTest, FloatAndBigInt) {
    Fixture f;
    Value* buffer = f.arrayBuffer(16.0);
    Value* view = f.dataView(buffer);

    f.call2(view, "setFloat64", f.ctx->createNumber(0), f.ctx->createNumber(3.5));
    EXPECT_EQ(numOf(f.call1(view, "getFloat64", f.ctx->createNumber(0))), 3.5);

    f.call2(view, "setFloat32", f.ctx->createNumber(8), f.ctx->createNumber(1.5));
    EXPECT_EQ(numOf(f.call1(view, "getFloat32", f.ctx->createNumber(8))), 1.5);

    Value* big = f.ctx->createBigInt(neo::bigint::BigInt(static_cast<int64_t>(-2)));
    f.call2(view, "setBigInt64", f.ctx->createNumber(0), big);
    Value* read = f.call1(view, "getBigInt64", f.ctx->createNumber(0));
    ASSERT_FALSE(isError(read));
    EXPECT_EQ(neo::core::detail::bigIntValue(read).toInt64(), -2);
}

TEST(ArrayBufferTest, BoundsAndTypeErrors) {
    Fixture f;
    Value* buffer = f.arrayBuffer(4.0);
    Value* view = f.dataView(buffer);
    EXPECT_TRUE(isError(f.call1(view, "getInt32", f.ctx->createNumber(2))));
    EXPECT_TRUE(isError(f.call2(view, "setInt32", f.ctx->createNumber(2),
                                f.ctx->createNumber(1.0))));

    Value* badArgs[1] = {f.ctx->createNumber(0.0)};
    EXPECT_TRUE(isError(f.ctx->construct(f.ctor("DataView"), 1, badArgs)));
    Value* offsetArgs[1] = {f.ctx->createNumber(5.0)};
    EXPECT_TRUE(isError(f.dataView(buffer, 2, offsetArgs)));
}

TEST(ArrayBufferTest, SharedArrayBufferBasics) {
    Fixture f;
    Value* args[1] = {f.ctx->createNumber(8.0)};
    Value* sab = f.ctx->construct(f.ctor("SharedArrayBuffer"), 1, args);
    ASSERT_FALSE(isError(sab));
    EXPECT_EQ(sab->type()->kind(), TypeKind::SharedArrayBuffer);
    EXPECT_EQ(numOf(f.ctx->getProperty(sab, "byteLength")), 8.0);

    // TypedArray 视图直接读写共享块。
    Value* taCtor = f.ctor("Uint8Array");
    Value* taArgs[1] = {sab};
    Value* view = f.ctx->construct(taCtor, 1, taArgs);
    ASSERT_FALSE(isError(view));
    f.ctx->setProperty(view, "0", f.ctx->createNumber(200.0));
    EXPECT_EQ(numOf(f.ctx->getProperty(view, "0")), 200.0);

    // DataView 与 TypedArray 看到同一块内存。
    Value* dv = f.dataView(sab);
    ASSERT_FALSE(isError(dv));
    f.call2(dv, "setUint8", f.ctx->createNumber(1), f.ctx->createNumber(7.0));
    EXPECT_EQ(numOf(f.call1(dv, "getUint8", f.ctx->createNumber(1))), 7.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(view, "1")), 7.0);

    // slice 返回新的 SharedArrayBuffer，并复制当前内容。
    Value* sliced = f.call(sab, "slice", 0);
    ASSERT_FALSE(isError(sliced));
    EXPECT_EQ(sliced->type()->kind(), TypeKind::SharedArrayBuffer);
    EXPECT_EQ(numOf(f.ctx->getProperty(sliced, "byteLength")), 8.0);
    Value* slicedArgs[1] = {sliced};
    Value* slicedView = f.ctx->construct(taCtor, 1, slicedArgs);
    ASSERT_FALSE(isError(slicedView));
    EXPECT_EQ(numOf(f.ctx->getProperty(slicedView, "1")), 7.0);

    // 构造器必须用 new。
    EXPECT_TRUE(isError(f.ctx->call(f.ctor("SharedArrayBuffer"),
                                    f.ctx->createUndefined(), 1, args)));
}
