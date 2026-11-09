// PromiseTest.cpp - Promise 类型与内部槽基础设施（M1）
//
// 验证（ADR-043）：
// - Promise 是 [[PromiseData]] 对象：TypeKind::Promise、具备对象布局
// - 原型 = %Promise.prototype%，双向 constructor，@@toStringTag = "Promise"
// - 新建 Promise 处于 Pending 状态，result = undefined
// - Promise 实例不可调用/不可构造
// - 参与运算走 ToPrimitive
// - captureValue：向 native 函数闭包注入任意 Value

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/WellKnownSymbol.h"
#include "core/types/Exception.h"
#include "core/types/Function.h"
#include "core/types/Promise.h"
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
using neo::core::PromiseData;
using neo::core::PromiseState;
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
};

std::string str(Value* v) {
    return static_cast<StringData*>(v->data())->value;
}

}  // namespace

// ===== 类型与原型 =====

TEST(PromiseTest, KindAndObjectLayout) {
    Fixture f;
    Value* p = f.ctx->createPromise();
    EXPECT_EQ(p->type()->kind(), TypeKind::Promise);
    EXPECT_TRUE(p->type()->hasObjectLayout());
    EXPECT_TRUE(neo::core::kindHasObjectLayout(TypeKind::Promise));
}

TEST(PromiseTest, PrototypeIsPromisePrototype) {
    Fixture f;
    Value* p = f.ctx->createPromise();
    EXPECT_EQ(f.ctx->getPrototype(p), f.ctx->promisePrototype());
    // %Promise.prototype% 的原型是 Object.prototype
    EXPECT_EQ(f.ctx->getPrototype(f.ctx->promisePrototype()), f.ctx->objectPrototype());
    // 双向 constructor
    EXPECT_EQ(f.ctx->getProperty(f.ctx->promisePrototype(), "constructor"),
              f.ctx->global()->lookup("Promise"));
}

TEST(PromiseTest, GlobalPromiseIsConstructor) {
    Fixture f;
    Value* promiseFn = f.ctx->global()->lookup("Promise");
    ASSERT_NE(promiseFn, nullptr);
    EXPECT_EQ(promiseFn->type()->kind(), TypeKind::Function);
    EXPECT_EQ(f.ctx->getProperty(promiseFn, "prototype"), f.ctx->promisePrototype());
    EXPECT_EQ(str(f.ctx->getProperty(promiseFn, "name")), "Promise");
}

TEST(PromiseTest, ToStringTagIsPromise) {
    Fixture f;
    Value* p = f.ctx->createPromise();
    Value* tag = f.ctx->getProperty(
        p, f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToStringTag));
    EXPECT_EQ(str(tag), "Promise");
    // Object.prototype.toString 走 @@toStringTag
    Value* toString = f.ctx->getProperty(f.ctx->objectPrototype(), "toString");
    Value* s = f.ctx->call(toString, p, 0, nullptr);
    EXPECT_EQ(str(s), "[object Promise]");
}

// ===== 内部槽初始状态 =====

TEST(PromiseTest, InitiallyPendingWithUndefinedResult) {
    Fixture f;
    Value* p = f.ctx->createPromise();
    PromiseData* d = neo::core::asPromise(p);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->state, PromiseState::Pending);
    ASSERT_NE(d->result, nullptr);
    EXPECT_EQ(d->result->type()->kind(), TypeKind::Undefined);
    EXPECT_TRUE(d->reactions.empty());
    EXPECT_FALSE(d->isHandled);
}

TEST(PromiseTest, AsPromiseRejectsNonPromise) {
    Fixture f;
    EXPECT_EQ(neo::core::asPromise(f.ctx->createObject()), nullptr);
    EXPECT_EQ(neo::core::asPromise(nullptr), nullptr);
}

// ===== 对象行为 =====

TEST(PromiseTest, NotCallableNorConstructor) {
    Fixture f;
    Value* p = f.ctx->createPromise();
    EXPECT_EQ(f.ctx->call(p, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(f.ctx->construct(p, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(PromiseTest, CustomPropertiesWork) {
    Fixture f;
    Value* p = f.ctx->createPromise();
    f.ctx->setProperty(p, "tag", f.ctx->createString("mine"));
    EXPECT_EQ(str(f.ctx->getProperty(p, "tag")), "mine");
}

TEST(PromiseTest, ArithmeticUsesToPrimitive) {
    Fixture f;
    Value* p = f.ctx->createPromise();
    Value* sum = f.ctx->add(p, f.ctx->createString("!"));
    ASSERT_EQ(sum->type()->kind(), TypeKind::String);
    EXPECT_EQ(str(sum), "[object Promise]!");
}

// ===== 构造函数 =====

TEST(PromiseTest, ConstructRequiresCallableExecutor) {
    Fixture f;
    Value* promiseFn = f.ctx->global()->lookup("Promise");
    // 无参
    Value* r0 = f.ctx->construct(promiseFn, 0, nullptr);
    ASSERT_EQ(r0->type()->kind(), TypeKind::Exception);
    // 非函数
    Value* args[] = {f.ctx->createNumber(1)};
    Value* r1 = f.ctx->construct(promiseFn, 1, args);
    ASSERT_EQ(r1->type()->kind(), TypeKind::Exception);
}

TEST(PromiseTest, ConstructWithExecutorCreatesPendingPromise) {
    Fixture f;
    Value* promiseFn = f.ctx->global()->lookup("Promise");
    Value* executor = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 2);
    Value* args[] = {executor};
    Value* p = f.ctx->construct(promiseFn, 1, args);
    ASSERT_EQ(p->type()->kind(), TypeKind::Promise);
    EXPECT_EQ(f.ctx->getPrototype(p), f.ctx->promisePrototype());
    EXPECT_EQ(neo::core::asPromise(p)->state, PromiseState::Pending);
}

TEST(PromiseTest, CallWithoutNewIsTypeError) {
    Fixture f;
    Value* promiseFn = f.ctx->global()->lookup("Promise");
    Value* executor = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 2);
    Value* args[] = {executor};
    Value* r = f.ctx->call(promiseFn, nullptr, 1, args);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    Value* err = static_cast<neo::core::ExceptionData*>(r->data())->child;
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "TypeError");
}

// ===== captureValue =====

TEST(PromiseTest, CaptureValueInjectsIntoClosure) {
    Fixture f;
    Value* payload = f.ctx->createString("captured");
    Value* fn = f.ctx->createNativeFunction(
        [](IContext* /*ctx*/, Value* self, size_t, Value*[]) {
            // native 内通过闭包作用域取回注入的值
            auto* d = static_cast<neo::core::FunctionData*>(self->data());
            return d->closureScope->lookup("__payload");
        }, 0);
    ASSERT_EQ(f.ctx->captureValue(fn, "__payload", payload), fn);
    Value* r = f.ctx->call(fn, fn, 0, nullptr);
    EXPECT_EQ(r, payload);
}

TEST(PromiseTest, CaptureValueRejectsNonFunction) {
    Fixture f;
    Value* r = f.ctx->captureValue(f.ctx->createNumber(1), "x", f.ctx->createNumber(2));
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(PromiseTest, CaptureValueRejectsDuplicateName) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 0);
    ASSERT_EQ(f.ctx->captureValue(fn, "x", f.ctx->createNumber(1)), fn);
    Value* r = f.ctx->captureValue(fn, "x", f.ctx->createNumber(2));
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}
