// InstanceOfTest.cpp - instanceof 运算符（ES2024 §13.10.2 / §7.3.21）
//
// 验证：
// - OrdinaryHasInstance：沿原型链查找、非对象返回 false、不可调用返回 false
// - 绑定函数以其 [[BoundTargetFunction]] 递归
// - prototype 非对象 → TypeError
// - target 非对象 / 不可调用 → TypeError
// - @@hasInstance 优先：存在时以其 ToBoolean 结果为准
// - Function.prototype[@@hasInstance] 存在且不可配置
// - 内建实例：数组 / Error 子类 / 包装对象

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/WellKnownSymbol.h"
#include "core/types/Exception.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using neo::core::IContext;
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
};

bool boolOf(Value* v) {
    return static_cast<neo::core::BooleanData*>(v->data())->value;
}

std::string str(Value* v) {
    return static_cast<neo::core::StringData*>(v->data())->value;
}

}  // namespace

// ===== 基础原型链 =====

TEST(InstanceOfTest, PlainObjectViaObjectCtor) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    Value* objectFn = f.ctx->global()->lookup("Object");
    Value* r = f.ctx->instanceOf(obj, objectFn);
    ASSERT_EQ(r->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(r));
}

TEST(InstanceOfTest, ArrayViaArrayCtor) {
    Fixture f;
    Value* arr = f.ctx->createArray();
    Value* arrayFn = f.ctx->global()->lookup("Array");
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(arr, arrayFn)));
    // 数组也是 Object
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(arr, f.ctx->global()->lookup("Object"))));
    // 但不是 Function
    EXPECT_FALSE(boolOf(f.ctx->instanceOf(arr, f.ctx->global()->lookup("Function"))));
}

TEST(InstanceOfTest, ErrorSubclassChain) {
    Fixture f;
    Value* typeFn = f.ctx->global()->lookup("TypeError");
    Value* err = f.ctx->construct(typeFn, 0, nullptr);
    // err instanceof TypeError / Error / Object 都为 true
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(err, typeFn)));
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(err, f.ctx->global()->lookup("Error"))));
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(err, f.ctx->global()->lookup("Object"))));
    // 不是别的子类
    EXPECT_FALSE(boolOf(f.ctx->instanceOf(err, f.ctx->global()->lookup("RangeError"))));
}

TEST(InstanceOfTest, AggregateErrorChain) {
    Fixture f;
    Value* aggFn = f.ctx->global()->lookup("AggregateError");
    Value* args[] = {f.ctx->createArray()};
    Value* err = f.ctx->construct(aggFn, 1, args);
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(err, aggFn)));
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(err, f.ctx->global()->lookup("Error"))));
}

TEST(InstanceOfTest, PrimitiveValueIsNeverInstance) {
    Fixture f;
    Value* objectFn = f.ctx->global()->lookup("Object");
    EXPECT_FALSE(boolOf(f.ctx->instanceOf(f.ctx->createNumber(1), objectFn)));
    EXPECT_FALSE(boolOf(f.ctx->instanceOf(f.ctx->createString("s"), objectFn)));
    EXPECT_FALSE(boolOf(f.ctx->instanceOf(f.ctx->createNull(), objectFn)));
    EXPECT_FALSE(boolOf(f.ctx->instanceOf(f.ctx->createUndefined(), objectFn)));
}

TEST(InstanceOfTest, NotInChainReturnsFalse) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    Value* arrayFn = f.ctx->global()->lookup("Array");
    EXPECT_FALSE(boolOf(f.ctx->instanceOf(obj, arrayFn)));
}

TEST(InstanceOfTest, PrototypeChainIsFollowed) {
    Fixture f;
    // 自建构造函数：proto → Object.prototype
    Value* fn = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 0);
    Value* proto = f.ctx->createObject();
    f.ctx->defineProperty(fn, "prototype", proto, false, false, false);
    Value* obj = f.ctx->construct(fn, 0, nullptr);
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(obj, fn)));
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(obj, f.ctx->global()->lookup("Object"))));
    // 中间原型也算
    f.ctx->setPrototype(proto, f.ctx->createObject());
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(obj, fn)));
}

// ===== 绑定函数 =====

TEST(InstanceOfTest, BoundFunctionUsesTarget) {
    Fixture f;
    Value* arrayFn = f.ctx->global()->lookup("Array");
    Value* bound = f.ctx->createBoundFunction(arrayFn, f.ctx->createUndefined(), 0, nullptr);
    Value* arr = f.ctx->createArray();
    // 绑定函数按 [[BoundTargetFunction]] 递归判断
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(arr, bound)));
    EXPECT_FALSE(boolOf(f.ctx->instanceOf(f.ctx->createObject(), bound)));
}

// ===== 错误路径 =====

TEST(InstanceOfTest, NonObjectTargetThrows) {
    Fixture f;
    Value* r = f.ctx->instanceOf(f.ctx->createObject(), f.ctx->createNumber(1));
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    Value* err = static_cast<neo::core::ExceptionData*>(r->data())->child;
    ASSERT_EQ(err->type()->kind(), TypeKind::Error);
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "TypeError");
}

TEST(InstanceOfTest, NonCallableObjectTargetThrows) {
    Fixture f;
    // 普通对象没有 @@hasInstance，且不可调用
    Value* r = f.ctx->instanceOf(f.ctx->createObject(), f.ctx->createObject());
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    Value* err = static_cast<neo::core::ExceptionData*>(r->data())->child;
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "TypeError");
}

TEST(InstanceOfTest, NonObjectPrototypeThrows) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 0);
    f.ctx->defineProperty(fn, "prototype", f.ctx->createNumber(1), true, false, true);
    Value* r = f.ctx->instanceOf(f.ctx->createObject(), fn);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    Value* err = static_cast<neo::core::ExceptionData*>(r->data())->child;
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "TypeError");
}

TEST(InstanceOfTest, NonCallableTargetWithoutHasInstanceIsTypeError) {
    Fixture f;
    // 普通对象：无 @@hasInstance 且不可调用
    Value* r = f.ctx->instanceOf(f.ctx->createNumber(1), f.ctx->createObject());
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
}

// ===== @@hasInstance =====

TEST(InstanceOfTest, CustomHasInstanceTakesPrecedence) {
    Fixture f;
    Value* target = f.ctx->createObject();
    Value* handler = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t argc, Value* argv[]) {
            // 恒返回 true（无论入参）
            (void)argc;
            (void)argv;
            return c->createBoolean(true);
        }, 1);
    f.ctx->defineProperty(target, f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::HasInstance),
                          handler, true, false, true);
    // target 不可调用，但 @@hasInstance 存在 → 以其结果为准
    Value* r = f.ctx->instanceOf(f.ctx->createNumber(1), target);
    ASSERT_EQ(r->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(r));
}

TEST(InstanceOfTest, CustomHasInstanceReceivesValueAsArgument) {
    Fixture f;
    Value* target = f.ctx->createObject();
    Value* probe = f.ctx->createNumber(42);
    Value* handler = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t argc, Value* argv[]) {
            bool same = argc == 1 && argv[0]->type()->kind() == TypeKind::Number &&
                        static_cast<neo::core::NumberData*>(argv[0]->data())->value == 42.0;
            return c->createBoolean(same);
        }, 1);
    f.ctx->defineProperty(target, f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::HasInstance),
                          handler, true, false, true);
    EXPECT_TRUE(boolOf(f.ctx->instanceOf(probe, target)));
}

TEST(InstanceOfTest, CustomHasInstanceResultIsToBoolean) {
    Fixture f;
    Value* target = f.ctx->createObject();
    Value* handler = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createString(""); }, 1);
    f.ctx->defineProperty(target, f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::HasInstance),
                          handler, true, false, true);
    // 空字符串 ToBoolean 为 false
    EXPECT_FALSE(boolOf(f.ctx->instanceOf(f.ctx->createObject(), target)));
}

TEST(InstanceOfTest, NonCallableHasInstanceThrows) {
    Fixture f;
    Value* target = f.ctx->createObject();
    f.ctx->defineProperty(target, f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::HasInstance),
                          f.ctx->createNumber(1), true, false, true);
    Value* r = f.ctx->instanceOf(f.ctx->createObject(), target);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    Value* err = static_cast<neo::core::ExceptionData*>(r->data())->child;
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "TypeError");
}

TEST(InstanceOfTest, FunctionProtoHasInstanceIsNonConfigurable) {
    Fixture f;
    Value* functionProto = f.ctx->functionPrototype();
    Value* sym = f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::HasInstance);
    Value* handler = f.ctx->getProperty(functionProto, sym);
    ASSERT_EQ(handler->type()->kind(), TypeKind::Function);
    EXPECT_EQ(str(f.ctx->getProperty(handler, "name")), "[Symbol.hasInstance]");
    // 不可配置：delete 失败
    Value* r = f.ctx->deleteProperty(functionProto, sym);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(InstanceOfTest, FunctionProtoHasInstanceUsesOrdinary) {
    Fixture f;
    // 直接调 Function.prototype[@@hasInstance] 等价于 OrdinaryHasInstance
    Value* handler = f.ctx->getProperty(
        f.ctx->functionPrototype(),
        f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::HasInstance));
    Value* arrayFn = f.ctx->global()->lookup("Array");
    Value* arr = f.ctx->createArray();
    Value* r = f.ctx->call(handler, arrayFn, 1, &arr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(r));
}
