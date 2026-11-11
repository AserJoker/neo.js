// FunctionEnvironmentTest.cpp - 函数环境 this/new.target 专用槽（ADR-047 P1-1）
//
// 验证：
// - this/new.target 是函数环境专用槽，不参与普通命名查找
// - 箭头函数（无 own 槽）沿 parent 链继承外层 this/new.target
// - invoke 在调用期间绑定 this/new.target，返回/Fatal 后恢复调用方函数环境
// - 嵌套调用不串函数环境

#include "core/IContext.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using neo::core::IContext;
using neo::core::NumberData;
using neo::core::Scope;
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

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }

Value* returnThis(IContext* ctx, Value*, size_t, Value*[]) {
    Scope* env = ctx->currentFunctionEnv();
    return env != nullptr && env->thisValue() != nullptr ? env->thisValue()
                                                         : ctx->createUndefined();
}

Value* returnNewTarget(IContext* ctx, Value*, size_t, Value*[]) {
    Scope* env = ctx->currentFunctionEnv();
    return env != nullptr && env->newTarget() != nullptr ? env->newTarget()
                                                         : ctx->createUndefined();
}

Value* g_inner = nullptr;

Value* returnInnerThis(IContext* ctx, Value*, size_t, Value*[]) {
    Scope* env = ctx->currentFunctionEnv();
    return env != nullptr && env->thisValue() != nullptr ? env->thisValue()
                                                         : ctx->createUndefined();
}

// 调用 inner 后检查函数环境是否恢复为 outer，再返回 outer 的 this。
Value* callInnerThenReturnThis(IContext* ctx, Value*, size_t, Value*[]) {
    Scope* env = ctx->currentFunctionEnv();
    Value* r = ctx->call(g_inner, ctx->createNumber(7), 0, nullptr);
    (void)r;
    if (ctx->currentFunctionEnv() != env) return ctx->createUndefined();
    return env != nullptr && env->thisValue() != nullptr ? env->thisValue()
                                                         : ctx->createUndefined();
}

Value* returnFatal(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createFatal("boom");
}

}  // namespace

TEST(FunctionEnvironmentTest, ThisSlotIsNotNamedBinding) {
    Fixture f;
    f.ctx->pushNewScope(f.ctx->current());
    Scope* s = f.ctx->current();
    Value* v = f.ctx->createNumber(42);

    s->setThis(v);
    EXPECT_TRUE(s->hasOwnThis());
    EXPECT_EQ(s->thisValue(), v);
    EXPECT_EQ(s->lookupThis(), v);
    // this 不是普通名字，不进入 namedValues_。
    EXPECT_EQ(s->lookup("this"), nullptr);
    EXPECT_EQ(s->lookupChain("this"), nullptr);

    f.ctx->popScope();
}

TEST(FunctionEnvironmentTest, NewTargetSlotIsNotNamedBinding) {
    Fixture f;
    f.ctx->pushNewScope(f.ctx->current());
    Scope* s = f.ctx->current();
    Value* fn = f.ctx->createNativeFunction(returnThis, 0);

    s->setNewTarget(fn);
    EXPECT_TRUE(s->hasOwnNewTarget());
    EXPECT_EQ(s->newTarget(), fn);
    EXPECT_EQ(s->lookupNewTarget(), fn);
    EXPECT_EQ(s->lookup("new.target"), nullptr);
    EXPECT_EQ(s->lookupChain("new.target"), nullptr);

    f.ctx->popScope();
}

TEST(FunctionEnvironmentTest, ArrowInheritsOuterThisAndNewTarget) {
    Fixture f;
    f.ctx->pushNewScope(f.ctx->current());
    Scope* outer = f.ctx->current();
    Value* thisValue = f.ctx->createNumber(1);
    Value* newTarget = f.ctx->createNativeFunction(returnThis, 0);
    outer->setThis(thisValue);
    outer->setNewTarget(newTarget);

    f.ctx->pushNewScope(outer);
    Scope* inner = f.ctx->current();
    EXPECT_FALSE(inner->hasOwnThis());
    EXPECT_FALSE(inner->hasOwnNewTarget());
    EXPECT_EQ(inner->lookupThis(), thisValue);
    EXPECT_EQ(inner->lookupNewTarget(), newTarget);

    // 自己一旦有槽，就遮蔽外层。
    Value* ownThis = f.ctx->createNumber(2);
    inner->setThis(ownThis);
    EXPECT_EQ(inner->lookupThis(), ownThis);
    EXPECT_EQ(outer->lookupThis(), thisValue);

    f.ctx->popScope();
    f.ctx->popScope();
}

TEST(FunctionEnvironmentTest, InvokeBindsThisAndRestoresEnvironment) {
    Fixture f;
    Scope* saved = f.ctx->currentFunctionEnv();
    Value* fn = f.ctx->createNativeFunction(returnThis, 0);

    Value* r = f.ctx->call(fn, f.ctx->createNumber(42), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r), 42.0);
    EXPECT_EQ(f.ctx->currentFunctionEnv(), saved);
}

TEST(FunctionEnvironmentTest, InvokeBindsUndefinedNewTarget) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(returnNewTarget, 0);

    Value* r = f.ctx->call(fn, f.ctx->createUndefined(), 0, nullptr);
    EXPECT_EQ(r->type()->kind(), TypeKind::Undefined);
}

TEST(FunctionEnvironmentTest, NestedInvokeRestoresOuterFunctionEnv) {
    Fixture f;
    g_inner = f.ctx->createNativeFunction(returnInnerThis, 0);
    Value* outer = f.ctx->createNativeFunction(callInnerThenReturnThis, 0);

    Value* r = f.ctx->call(outer, f.ctx->createNumber(11), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r), 11.0);
    g_inner = nullptr;
}

TEST(FunctionEnvironmentTest, FatalRestoresCallerFunctionEnv) {
    Fixture f;
    Scope* saved = f.ctx->currentFunctionEnv();
    Value* fn = f.ctx->createNativeFunction(returnFatal, 0);

    Value* r = f.ctx->call(fn, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Fatal);
    EXPECT_EQ(f.ctx->currentFunctionEnv(), saved);
}
