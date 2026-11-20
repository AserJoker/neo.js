// AtomicsTest.cpp - Atomics 原子操作（ES2024 §25.4）
//
// 覆盖：load/store、RMW 族、compareExchange、整数回绕、BigInt、
// isLockFree、非共享/浮点类型拒绝、wait/notify 基本语义。

#include "bigint/BigInt.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/BigInt.h"
#include "core/types/Boolean.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/IOutputSink.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using neo::bigint::BigInt;
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

    Value* construct(const char* name, size_t argc, Value* argv[]) {
        return ctx->construct(ctor(name), argc, argv);
    }

    Value* sharedBuffer(double bytes) {
        Value* args[1] = {ctx->createNumber(bytes)};
        return construct("SharedArrayBuffer", 1, args);
    }

    Value* arrayBuffer(double bytes) {
        Value* args[1] = {ctx->createNumber(bytes)};
        return construct("ArrayBuffer", 1, args);
    }

    Value* view(const char* name, Value* buffer) {
        Value* args[1] = {buffer};
        return construct(name, 1, args);
    }

    Value* atomics(const char* name, size_t argc, Value* argv[]) {
        Value* object = ctor("Atomics");
        Value* fn = ctx->getProperty(object, name);
        return ctx->call(fn, object, argc, argv);
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

Value* atomics1(Fixture& f, const char* name, Value* a) {
    Value* args[1] = {a};
    return f.atomics(name, 1, args);
}

Value* atomics2(Fixture& f, const char* name, Value* a, Value* b) {
    Value* args[2] = {a, b};
    return f.atomics(name, 2, args);
}

Value* atomics3(Fixture& f, const char* name, Value* a, Value* b, Value* c) {
    Value* args[3] = {a, b, c};
    return f.atomics(name, 3, args);
}

Value* atomics4(Fixture& f, const char* name, Value* a, Value* b, Value* c,
                Value* d) {
    Value* args[4] = {a, b, c, d};
    return f.atomics(name, 4, args);
}

}  // namespace

TEST(AtomicsTest, LoadStoreAndRmw) {
    Fixture f;
    Value* sab = f.sharedBuffer(16);
    ASSERT_FALSE(isError(sab));
    Value* ta = f.view("Int32Array", sab);
    ASSERT_FALSE(isError(ta));

    EXPECT_EQ(numOf(atomics3(f, "store", ta, f.ctx->createNumber(0),
                             f.ctx->createNumber(5))),
              5.0);
    EXPECT_EQ(numOf(atomics2(f, "load", ta, f.ctx->createNumber(0))), 5.0);

    // add/sub/and/or/xor 返回旧值。
    EXPECT_EQ(numOf(atomics3(f, "add", ta, f.ctx->createNumber(0),
                             f.ctx->createNumber(3))),
              5.0);
    EXPECT_EQ(numOf(atomics2(f, "load", ta, f.ctx->createNumber(0))), 8.0);
    EXPECT_EQ(numOf(atomics3(f, "sub", ta, f.ctx->createNumber(0),
                             f.ctx->createNumber(2))),
              8.0);
    EXPECT_EQ(numOf(atomics2(f, "load", ta, f.ctx->createNumber(0))), 6.0);
    EXPECT_EQ(numOf(atomics3(f, "and", ta, f.ctx->createNumber(0),
                             f.ctx->createNumber(3))),
              6.0);
    EXPECT_EQ(numOf(atomics2(f, "load", ta, f.ctx->createNumber(0))), 2.0);
    EXPECT_EQ(numOf(atomics3(f, "or", ta, f.ctx->createNumber(0),
                             f.ctx->createNumber(8))),
              2.0);
    EXPECT_EQ(numOf(atomics2(f, "load", ta, f.ctx->createNumber(0))), 10.0);
    EXPECT_EQ(numOf(atomics3(f, "xor", ta, f.ctx->createNumber(0),
                             f.ctx->createNumber(15))),
              10.0);
    EXPECT_EQ(numOf(atomics2(f, "load", ta, f.ctx->createNumber(0))), 5.0);
    EXPECT_EQ(numOf(atomics3(f, "exchange", ta, f.ctx->createNumber(0),
                             f.ctx->createNumber(7))),
              5.0);
    EXPECT_EQ(numOf(atomics2(f, "load", ta, f.ctx->createNumber(0))), 7.0);

    // 成功交换返回旧值；失败返回实际值且不写入。
    EXPECT_EQ(numOf(atomics4(f, "compareExchange", ta, f.ctx->createNumber(0),
                             f.ctx->createNumber(7), f.ctx->createNumber(9))),
              7.0);
    EXPECT_EQ(numOf(atomics2(f, "load", ta, f.ctx->createNumber(0))), 9.0);
    EXPECT_EQ(numOf(atomics4(f, "compareExchange", ta, f.ctx->createNumber(0),
                             f.ctx->createNumber(7), f.ctx->createNumber(11))),
              9.0);
    EXPECT_EQ(numOf(atomics2(f, "load", ta, f.ctx->createNumber(0))), 9.0);
}

TEST(AtomicsTest, UnsignedWrapAndBigInt) {
    Fixture f;

    Value* sab = f.sharedBuffer(8);
    Value* u8 = f.view("Uint8Array", sab);
    ASSERT_FALSE(isError(u8));
    EXPECT_EQ(numOf(atomics3(f, "store", u8, f.ctx->createNumber(0),
                             f.ctx->createNumber(255))),
              255.0);
    EXPECT_EQ(numOf(atomics3(f, "add", u8, f.ctx->createNumber(0),
                             f.ctx->createNumber(1))),
              255.0);
    EXPECT_EQ(numOf(atomics2(f, "load", u8, f.ctx->createNumber(0))), 0.0);

    Value* big = f.view("BigInt64Array", sab);
    ASSERT_FALSE(isError(big));
    Value* two = f.ctx->createBigInt(BigInt(static_cast<int64_t>(2)));
    Value* three = f.ctx->createBigInt(BigInt(static_cast<int64_t>(3)));
    Value* stored = atomics3(f, "store", big, f.ctx->createNumber(0), two);
    ASSERT_EQ(stored->type()->kind(), TypeKind::BigInt);
    EXPECT_EQ(neo::core::detail::bigIntValue(stored).toUint64(), 2u);
    Value* old = atomics3(f, "add", big, f.ctx->createNumber(0), three);
    ASSERT_EQ(old->type()->kind(), TypeKind::BigInt);
    EXPECT_EQ(neo::core::detail::bigIntValue(old).toUint64(), 2u);
    Value* now = atomics2(f, "load", big, f.ctx->createNumber(0));
    ASSERT_EQ(now->type()->kind(), TypeKind::BigInt);
    EXPECT_EQ(neo::core::detail::bigIntValue(now).toUint64(), 5u);
}

TEST(AtomicsTest, RejectsNonSharedAndFloat) {
    Fixture f;

    Value* ab = f.arrayBuffer(8);
    Value* ta = f.view("Int32Array", ab);
    ASSERT_FALSE(isError(ta));
    EXPECT_TRUE(isError(atomics2(f, "load", ta, f.ctx->createNumber(0))));

    // ArrayBuffer.isView 同时识别 DataView 与 TypedArray。
    Value* isView = f.ctx->getProperty(f.ctor("ArrayBuffer"), "isView");
    Value* viewArg[1] = {ta};
    EXPECT_TRUE(boolOf(f.ctx->call(isView, f.ctx->createUndefined(), 1,
                                   viewArg)));

    Value* sab = f.sharedBuffer(8);
    Value* f32 = f.view("Float32Array", sab);
    ASSERT_FALSE(isError(f32));
    EXPECT_TRUE(isError(atomics2(f, "load", f32, f.ctx->createNumber(0))));

    EXPECT_TRUE(boolOf(atomics1(f, "isLockFree", f.ctx->createNumber(4))));
    EXPECT_FALSE(boolOf(atomics1(f, "isLockFree", f.ctx->createNumber(3))));
}

TEST(AtomicsTest, WaitRejectedOnMainContext) {
    Fixture f;
    EXPECT_FALSE(f.ctx->isWorkerContext());

    Value* sab = f.sharedBuffer(8);
    Value* ta = f.view("Int32Array", sab);
    ASSERT_FALSE(isError(ta));

    atomics3(f, "store", ta, f.ctx->createNumber(0), f.ctx->createNumber(5));

    // 主上下文禁止阻塞：即使 timeout=0 也抛 TypeError。
    EXPECT_TRUE(isError(atomics4(f, "wait", ta, f.ctx->createNumber(0),
                                 f.ctx->createNumber(5),
                                 f.ctx->createNumber(0))));
    EXPECT_TRUE(isError(atomics4(f, "wait", ta, f.ctx->createNumber(0),
                                 f.ctx->createNumber(1),
                                 f.ctx->createNumber(0))));

    // notify 在主上下文仍可用；无等待者时返回 0。
    EXPECT_EQ(numOf(atomics3(f, "notify", ta, f.ctx->createNumber(0),
                             f.ctx->createNumber(1))),
              0.0);
}
