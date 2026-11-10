// FunctionConstructibleTest.cpp - IsConstructor 判定与显式标记（ES2024 §7.2.4）
//
// 验证：
// - native 函数默认可构造；setFunctionConstructible(false) 后 construct 报 TypeError
// - 绑定函数继承 target 的可构造性（§10.4.1.3）
// - 绑定函数可构造时转发给 target：忽略 boundThis、前置 boundArgs
// - 不可构造的绑定函数 construct 报 TypeError（旧实现会调用空 native 指针崩溃）
// - Promise 静态方法 / @@species 用 IsConstructor 而非 isCallable 判定

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Exception.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
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

std::string errorName(IContext* ctx, Value* err) {
    return str(ctx->getProperty(static_cast<neo::core::ExceptionData*>(err->data())->child,
                                "name"));
}

// 记录构造时 this 上的 __mark 属性，并返回 this
Value* recordCtor(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (argc > 0) ctx->setProperty(self, "__first", argv[0]);
    ctx->setProperty(self, "__constructed", ctx->createBoolean(true));
    return ctx->createUndefined();
}

}  // namespace

// ===== 显式可构造性标记 =====

TEST(FunctionConstructibleTest, NativeFunctionIsConstructibleByDefault) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(recordCtor, 0);
    Value* obj = f.ctx->construct(fn, 0, nullptr);
    ASSERT_EQ(obj->type()->kind(), TypeKind::Object);
    EXPECT_TRUE(neo::core::detail::toBoolean(f.ctx->getProperty(obj, "__constructed")));
}

TEST(FunctionConstructibleTest, MarkedNonConstructibleThrowsTypeError) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(recordCtor, 0);
    f.ctx->setFunctionConstructible(fn, false);
    Value* r = f.ctx->construct(fn, 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(errorName(f.ctx.get(), r), "TypeError");
}

TEST(FunctionConstructibleTest, MarkedNonConstructibleStillCallable) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(recordCtor, 0);
    f.ctx->setFunctionConstructible(fn, false);
    // 不可构造不等于不可调用
    Value* r = f.ctx->call(fn, f.ctx->createObject(), 0, nullptr);
    EXPECT_FALSE(neo::core::detail::isErrorValue(r));
}

TEST(FunctionConstructibleTest, SetConstructibleOnNonFunctionIsIgnored) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    f.ctx->setFunctionConstructible(obj, false);  // 不应崩溃
    EXPECT_FALSE(neo::core::detail::isConstructor(obj));
}

// ===== 绑定函数 =====

TEST(FunctionConstructibleTest, BoundFunctionInheritsConstructibility) {
    Fixture f;
    Value* target = f.ctx->createNativeFunction(recordCtor, 0);
    f.ctx->setFunctionConstructible(target, false);
    Value* bound = f.ctx->createBoundFunction(target, f.ctx->createUndefined(), 0, nullptr);
    EXPECT_FALSE(neo::core::detail::isConstructor(bound));
    Value* r = f.ctx->construct(bound, 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(errorName(f.ctx.get(), r), "TypeError");
}

TEST(FunctionConstructibleTest, BoundConstructibleFunctionForwardsToTarget) {
    Fixture f;
    Value* target = f.ctx->createNativeFunction(recordCtor, 0);
    // 绑定 this 与前置实参；construct 应忽略 boundThis、前置 boundArgs
    Value* boundThis = f.ctx->createObject();
    Value* boundArgs[1] = {f.ctx->createNumber(7)};
    Value* bound = f.ctx->createBoundFunction(target, boundThis, 1, boundArgs);

    Value* obj = f.ctx->construct(bound, 0, nullptr);
    ASSERT_EQ(obj->type()->kind(), TypeKind::Object);
    EXPECT_NE(obj, boundThis);  // 新对象，不是 boundThis
    EXPECT_TRUE(neo::core::detail::toBoolean(f.ctx->getProperty(obj, "__constructed")));
    // 前置实参已传入
    Value* first = f.ctx->getProperty(obj, "__first");
    ASSERT_EQ(first->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(static_cast<neo::core::NumberData*>(first->data())->value, 7.0);
}

TEST(FunctionConstructibleTest, NonFunctionBoundTargetIsNotConstructible) {
    Fixture f;
    // createBoundFunction 的 target 契约上是函数，但防御性检查：
    // 非函数 target 时绑定函数不可构造
    Value* target = f.ctx->createNumber(1);
    Value* bound = f.ctx->createBoundFunction(target, f.ctx->createUndefined(), 0, nullptr);
    EXPECT_FALSE(neo::core::detail::isConstructor(bound));
}

// ===== Promise 用 IsConstructor 判定 =====

TEST(FunctionConstructibleTest, PromiseResolveRejectsNonConstructorThis) {
    Fixture f;
    Value* resolveFn = f.ctx->getProperty(f.ctx->global()->lookup("Promise"), "resolve");
    // 不可构造的 native 函数作为 this
    Value* fakeCtor = f.ctx->createNativeFunction(recordCtor, 0);
    f.ctx->setFunctionConstructible(fakeCtor, false);
    Value* args[1] = {f.ctx->createNumber(1)};
    Value* r = f.ctx->call(resolveFn, fakeCtor, 1, args);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(errorName(f.ctx.get(), r), "TypeError");
}

TEST(FunctionConstructibleTest, PromiseThenWithNonConstructibleSpeciesThrows) {
    Fixture f;
    // p.constructor = { [@@species]: nonConstructibleFn }
    Value* nonCtor = f.ctx->createNativeFunction(recordCtor, 0);
    f.ctx->setFunctionConstructible(nonCtor, false);
    Value* species = f.ctx->createObject();
    f.ctx->defineProperty(species,
                          f.ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Species),
                          nonCtor);

    Value* executor = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 2);
    Value* ctorArgs[1] = {executor};
    Value* p = f.ctx->construct(f.ctx->global()->lookup("Promise"), 1, ctorArgs);
    f.ctx->setProperty(p, "constructor", species);

    Value* r = f.ctx->call(f.ctx->getProperty(p, "then"), p, 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(errorName(f.ctx.get(), r), "TypeError");
}

// 回归：旧实现 isCallable(boundFn) 为 true 且 construct 无 bound 分支，
// 会调用 FunctionData::native（nullptr）导致段错误。
TEST(FunctionConstructibleTest, PromiseResolveWithBoundNonConstructibleDoesNotCrash) {
    Fixture f;
    Value* resolveFn = f.ctx->getProperty(f.ctx->global()->lookup("Promise"), "resolve");
    Value* target = f.ctx->createNativeFunction(recordCtor, 0);
    f.ctx->setFunctionConstructible(target, false);
    Value* bound = f.ctx->createBoundFunction(target, f.ctx->createUndefined(), 0, nullptr);

    Value* args[1] = {f.ctx->createNumber(1)};
    Value* r = f.ctx->call(resolveFn, bound, 1, args);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(errorName(f.ctx.get(), r), "TypeError");
}
