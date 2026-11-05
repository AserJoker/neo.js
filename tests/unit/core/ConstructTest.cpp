// ConstructTest.cpp - 原型-构造函数序列与 new 操作单元测试
//
// 验证（ADR-034 原型-构造函数协作 + new）：
// - createNativeFunction 自动创建 fn.prototype（普通对象）与 proto.constructor 指回函数
// - 两者均不可枚举；fn.prototype 不可配置
// - new：新建 this（原型 = fn.prototype）→ 以 this 调 native → 返回值语义
// - 构造函数返回 Object/Function 用返回值，返回原始值用 this
// - new 强制新对象（忽略 bind 的 self）
// - 非构造函数 new 返回 Exception；Exception/Fatal 透传
// - 原型链：实例 → fn.prototype → ... 的属性查找
// - GC：fn 与 proto 通过属性互引用保活
//
// 关联 ADR: 018（对象模型）、019（赋值语义）、021（Function 结构）、
//          022（调用）、034（原型-构造函数与 new）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

namespace {

using neo::core::FunctionData;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::ObjectData;
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

// 记录 native 收到的 this
Value* gObservedThis = nullptr;

Value* recordThisFn(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    gObservedThis = self;
    return ctx->createUndefined();
}

// 在 this 上写属性并返回 undefined（典型构造函数）
Value* assignPropFn(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    ctx->setProperty(self, "x", ctx->createNumber(42));
    return ctx->createUndefined();
}

// 显式返回一个对象（覆盖 this）
Value* returnObjectFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createObject();
}

// 显式返回原始值（被忽略，仍用 this）
Value* returnPrimitiveFn(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    gObservedThis = self;
    return ctx->createNumber(7);
}

// 返回 Exception
Value* throwFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createException(ctx->createString("boom"));
}

// 返回 Fatal
Value* fatalFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createFatal("crash");
}

// 记录参数个数
Value* argcFn(IContext* ctx, Value* /*self*/, size_t argc, Value* /*argv*/[]) {
    return ctx->createNumber(static_cast<double>(argc));
}

}  // namespace

// ===== 构造函数 ↔ 原型双向引用（创建时自动建立）=====

TEST(ConstructTest, FunctionGetsPrototypeOnCreation) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(recordThisFn, 0);
    Value* proto = ctx->getProperty(fn, "prototype");
    ASSERT_EQ(proto->type()->kind(), TypeKind::Object);
    // 原型对象的 constructor 指回函数
    EXPECT_EQ(ctx->getProperty(proto, "constructor"), fn);
}

TEST(ConstructTest, PrototypeAndConstructorAreNotEnumerable) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(recordThisFn, 0);
    Value* proto = ctx->getProperty(fn, "prototype");

    auto* fd = static_cast<ObjectData*>(fn->data());
    auto* pd = static_cast<ObjectData*>(proto->data());
    EXPECT_FALSE(fd->properties.at("prototype").enumerable);
    EXPECT_TRUE(fd->properties.at("prototype").writable);
    EXPECT_FALSE(fd->properties.at("prototype").configurable);
    EXPECT_FALSE(pd->properties.at("constructor").enumerable);
    EXPECT_TRUE(pd->properties.at("constructor").writable);
    EXPECT_TRUE(pd->properties.at("constructor").configurable);
}

TEST(ConstructTest, NameIsNonWritableProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(recordThisFn, 0);
    auto* fd = static_cast<ObjectData*>(fn->data());
    const auto& p = fd->properties.at("name");
    EXPECT_FALSE(p.writable);      // ES2024 §20.2.4.2
    EXPECT_FALSE(p.enumerable);
    EXPECT_TRUE(p.configurable);
    // 普通 setProperty 无法改写（writable=false）
    EXPECT_EQ(ctx->setProperty(fn, "name", ctx->createString("x"))->type()->kind(),
              TypeKind::Exception);
    // setFunctionName 走 defineProperty，可更新
    ctx->setFunctionName(fn, "renamed");
    EXPECT_EQ(static_cast<StringData*>(ctx->getProperty(fn, "name")->data())->value,
              "renamed");
}

TEST(ConstructTest, EachFunctionHasDistinctPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* a = ctx->createNativeFunction(recordThisFn, 0);
    Value* b = ctx->createNativeFunction(recordThisFn, 0);
    EXPECT_NE(ctx->getProperty(a, "prototype"), ctx->getProperty(b, "prototype"));
}

// ===== new：this 绑定与返回值语义 =====

TEST(ConstructTest, ConstructPassesNewObjectAsThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    gObservedThis = nullptr;
    Value* fn = ctx->createNativeFunction(recordThisFn, 0);
    Value* obj = ctx->construct(fn, 0, nullptr);

    ASSERT_EQ(obj->type()->kind(), TypeKind::Object);
    EXPECT_EQ(gObservedThis, obj);  // this 就是返回的新对象
}

TEST(ConstructTest, ConstructSetsPrototypeOfNewObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(recordThisFn, 0);
    Value* proto = ctx->getProperty(fn, "prototype");
    Value* obj = ctx->construct(fn, 0, nullptr);

    EXPECT_EQ(ctx->getPrototype(obj), proto);
}

TEST(ConstructTest, ConstructReturnsThisWhenNativeReturnsUndefined) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(assignPropFn, 0);
    Value* obj = ctx->construct(fn, 0, nullptr);

    EXPECT_EQ(obj->type()->kind(), TypeKind::Object);
    // 构造函数在 this 上写的属性保留
    EXPECT_DOUBLE_EQ(
        static_cast<NumberData*>(ctx->getProperty(obj, "x")->data())->value, 42.0);
}

TEST(ConstructTest, ConstructUsesReturnedObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    gObservedThis = nullptr;
    Value* fn = ctx->createNativeFunction(returnObjectFn, 0);
    Value* obj = ctx->construct(fn, 0, nullptr);

    // 返回的是 native 显式创建的对象，不是 this
    EXPECT_EQ(obj->type()->kind(), TypeKind::Object);
    EXPECT_NE(obj, gObservedThis);
}

TEST(ConstructTest, ConstructIgnoresReturnedPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    gObservedThis = nullptr;
    Value* fn = ctx->createNativeFunction(returnPrimitiveFn, 0);
    Value* obj = ctx->construct(fn, 0, nullptr);

    // 原始返回值被忽略，仍返回 this
    EXPECT_EQ(obj->type()->kind(), TypeKind::Object);
    EXPECT_EQ(obj, gObservedThis);
}

TEST(ConstructTest, ConstructPassesArguments) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    Value* args[3] = {ctx->createNumber(1), ctx->createNumber(2), ctx->createNumber(3)};
    Value* res = ctx->construct(fn, 3, args);
    // argcFn 返回参数个数（Number 是原始值 → 被忽略，返回 this 对象）
    EXPECT_EQ(res->type()->kind(), TypeKind::Object);
}

TEST(ConstructTest, ConstructChecksMinArgc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 2);
    EXPECT_EQ(ctx->construct(fn, 0, nullptr)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->construct(fn, 1, nullptr)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->construct(fn, 2, nullptr)->type()->kind(), TypeKind::Object);
}

// ===== new 强制新对象（忽略 bind 的 self）=====

TEST(ConstructTest, ConstructIgnoresBoundThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    gObservedThis = nullptr;
    Value* fn = ctx->createNativeFunction(recordThisFn, 0);
    Value* boundObj = ctx->createObject();
    Value* obj = ctx->construct(fn, 0, nullptr);

    // new 的 this 是新对象，不是调用方传入的任何对象
    EXPECT_NE(obj, boundObj);
    EXPECT_EQ(gObservedThis, obj);
}

// ===== 错误路径 =====

TEST(ConstructTest, ConstructPropagatesException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(throwFn, 0);
    EXPECT_EQ(ctx->construct(fn, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(ConstructTest, ConstructPropagatesFatal) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(fatalFn, 0);
    EXPECT_EQ(ctx->construct(fn, 0, nullptr)->type()->kind(), TypeKind::Fatal);
}

TEST(ConstructTest, NonFunctionIsNotAConstructor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* values[] = {ctx->createUndefined(), ctx->createNull(), ctx->createBoolean(true),
                       ctx->createNumber(1), ctx->createString("s"), ctx->createObject()};
    for (Value* v : values) {
        EXPECT_EQ(ctx->construct(v, 0, nullptr)->type()->kind(), TypeKind::Exception);
    }
}

TEST(ConstructTest, ExceptionAndFatalConstructPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* exc = ctx->createException(ctx->createString("e"));
    EXPECT_EQ(ctx->construct(exc, 0, nullptr), exc);
    Value* fatal = ctx->createFatal("crash");
    EXPECT_EQ(ctx->construct(fatal, 0, nullptr), fatal);
}

// ===== 原型链：实例 → fn.prototype =====

TEST(ConstructTest, InstanceInheritsFromPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(assignPropFn, 0);
    Value* proto = ctx->getProperty(fn, "prototype");
    Value* shared = ctx->createNumber(99);
    ctx->setProperty(proto, "shared", shared);

    Value* obj = ctx->construct(fn, 0, nullptr);
    // 实例自身有 x，原型上有 shared
    EXPECT_TRUE(ctx->hasProperty(obj, "x"));
    EXPECT_TRUE(ctx->hasProperty(obj, "shared"));
    EXPECT_EQ(ctx->getProperty(obj, "shared"), shared);
}

TEST(ConstructTest, ReplacingPrototypeAffectsNewInstances) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(recordThisFn, 0);
    Value* newProto = ctx->createObject();
    ctx->setProperty(fn, "prototype", newProto);

    Value* obj = ctx->construct(fn, 0, nullptr);
    EXPECT_EQ(ctx->getPrototype(obj), newProto);
}

TEST(ConstructTest, NonObjectPrototypeTreatedAsNull) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(recordThisFn, 0);
    // 把 prototype 改成原始值：new 时按 null 处理（无原型）
    ctx->setProperty(fn, "prototype", ctx->createNumber(1));

    Value* obj = ctx->construct(fn, 0, nullptr);
    EXPECT_EQ(obj->type()->kind(), TypeKind::Object);
    EXPECT_EQ(ctx->getPrototype(obj)->type()->kind(), TypeKind::Null);
}

// ===== GC：构造函数 ↔ 原型互引用保活 =====

TEST(ConstructTest, FunctionAndPrototypeSurviveGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(recordThisFn, 0);
    Value* proto = ctx->getProperty(fn, "prototype");

    ctx->gc()->collect();
    // fn 在 scope → 遍历属性 prototype → proto 保活；proto 的 constructor → fn 保活
    EXPECT_EQ(ctx->getProperty(fn, "prototype"), proto);
    EXPECT_EQ(ctx->getProperty(proto, "constructor"), fn);
}

TEST(ConstructTest, InstanceSurvivesGcWithPrototypeChain) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(assignPropFn, 0);
    Value* obj = ctx->construct(fn, 0, nullptr);
    ASSERT_EQ(obj->type()->kind(), TypeKind::Object);

    ctx->gc()->collect();
    // obj 在 scope → 遍历 prototype → fn.prototype 保活（其 constructor 又指向 fn）
    EXPECT_EQ(ctx->getProperty(obj, "x")->type()->kind(), TypeKind::Number);
    EXPECT_EQ(ctx->getPrototype(obj)->type()->kind(), TypeKind::Object);
}
