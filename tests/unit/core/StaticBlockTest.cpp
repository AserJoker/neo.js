// StaticBlockTest.cpp - class static block 引擎原语（ADR-047 §10）
//
// 验证：
// - runStaticBlock 以 this = class 构造器、new.target = undefined 调用
// - static block 内 super.prop 走 homeObject = class 构造器
// - static block 内可通过 currentFunction 解析私有名
// - static block 按调用位置看到已初始化的静态私有字段，未初始化的不可见
// - 块内错误原样透传
// - 非 class 上执行 static block 报错

#include "core/IContext.h"
#include "core/Value.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
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
using neo::core::NativeClassDef;
using neo::core::NumberData;
using neo::core::PrivateName;
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

Value* noop(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createUndefined(); }
Value* initOne(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createNumber(1); }
Value* initTwo(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createNumber(2); }

Value* staticFoo(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createNumber(42); }

Value* g_blockThis = nullptr;
Value* g_blockNewTarget = nullptr;
Value* g_blockPrivate = nullptr;

Value* blockSuperAndPrivate(IContext* ctx, Value* self, size_t, Value*[]) {
    g_blockThis = self;
    g_blockNewTarget = ctx->newTarget();

    PrivateName* a = ctx->classPrivateName(ctx->currentFunction(), "a");
    if (a == nullptr) return ctx->createException(ctx->createString("missing #a"));
    g_blockPrivate = ctx->getPrivate(self, a);
    if (neo::core::detail::isErrorValue(g_blockPrivate)) return g_blockPrivate;

    Value* home = ctx->functionHomeObject(ctx->currentFunction());
    Value* foo = ctx->getSuperProperty(home, ctx->createString("foo"), self);
    if (neo::core::detail::isErrorValue(foo)) return foo;
    return ctx->call(foo, self, 0, nullptr);
}

Value* blockReadA(IContext* ctx, Value* self, size_t, Value*[]) {
    PrivateName* a = ctx->classPrivateName(ctx->currentFunction(), "a");
    if (a == nullptr) return ctx->createException(ctx->createString("missing #a"));
    return ctx->getPrivate(self, a);
}

Value* blockReadB(IContext* ctx, Value* self, size_t, Value*[]) {
    PrivateName* b = ctx->classPrivateName(ctx->currentFunction(), "b");
    if (b == nullptr) return ctx->createException(ctx->createString("missing #b"));
    return ctx->getPrivate(self, b);
}

Value* blockError(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createException(ctx->createString("boom"));
}

}  // namespace

TEST(StaticBlockTest, ThisSuperAndPrivateNames) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    NativeClassDef base;
    base.name = "Base";
    base.staticMethods = {{"foo", staticFoo, 0}};
    Value* baseCtor = ctx->registerClass(base);
    ASSERT_EQ(baseCtor->type()->kind(), TypeKind::Function);

    PrivateName* a = ctx->createPrivateName("a");
    NativeClassDef derived;
    derived.name = "Derived";
    derived.constructor = noop;
    derived.superCtor = baseCtor;
    derived.staticPrivateFields = {{a, initOne}};
    Value* derivedCtor = ctx->registerClass(derived);
    ASSERT_EQ(derivedCtor->type()->kind(), TypeKind::Function);

    g_blockThis = nullptr;
    g_blockNewTarget = ctx->createUndefined();
    g_blockPrivate = nullptr;
    Value* r = ctx->runStaticBlock(derivedCtor, blockSuperAndPrivate);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r), 42.0);

    EXPECT_EQ(g_blockThis, derivedCtor);          // this = class 构造器
    EXPECT_EQ(g_blockNewTarget, nullptr);         // 普通调用，new.target 为空
    ASSERT_NE(g_blockPrivate, nullptr);
    EXPECT_EQ(numOf(g_blockPrivate), 1.0);        // 私有静态字段可见

    ctx->releasePrivateName(a);
}

TEST(StaticBlockTest, OrderingWithStaticPrivateFields) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    NativeClassDef def;
    def.name = "C";
    Value* cls = ctx->registerClass(def);
    ASSERT_EQ(cls->type()->kind(), TypeKind::Function);

    PrivateName* a = ctx->createPrivateName("a");
    PrivateName* b = ctx->createPrivateName("b");
    ctx->addStaticPrivateField(cls, a, ctx->createNativeFunction(initOne, 0));
    ctx->addStaticPrivateField(cls, b, ctx->createNativeFunction(initTwo, 0));

    // a 在块之前初始化：可见。
    ASSERT_EQ(ctx->initializeStaticPrivateField(cls, a)->type()->kind(),
              TypeKind::Undefined);
    Value* ra = ctx->runStaticBlock(cls, blockReadA);
    ASSERT_EQ(ra->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(ra), 1.0);

    // b 尚未初始化：不可见（TypeError）。
    Value* rb = ctx->runStaticBlock(cls, blockReadB);
    EXPECT_EQ(rb->type()->kind(), TypeKind::Exception);

    // 在声明位置初始化后可见。
    ASSERT_EQ(ctx->initializeStaticPrivateField(cls, b)->type()->kind(),
              TypeKind::Undefined);
    Value* rb2 = ctx->runStaticBlock(cls, blockReadB);
    ASSERT_EQ(rb2->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(rb2), 2.0);

    ctx->releasePrivateName(a);
    ctx->releasePrivateName(b);
}

TEST(StaticBlockTest, ErrorPropagates) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    NativeClassDef def;
    def.name = "C";
    Value* cls = ctx->registerClass(def);
    ASSERT_EQ(cls->type()->kind(), TypeKind::Function);

    Value* r = ctx->runStaticBlock(cls, blockError);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(StaticBlockTest, NonClassReceiverRejected) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* obj = ctx->createObject();
    Value* r = ctx->runStaticBlock(obj, noop);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
}
