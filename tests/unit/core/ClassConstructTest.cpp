// ClassConstructTest.cpp - class 构造算法与 super/this/new.target（ADR-047 P1-3）
//
// 用 native 函数模拟 class 构造器/方法，验证：
// - class 构造器只能 new，call 抛 TypeError
// - 基类/派生类构造、newTarget.prototype、new.target
// - 派生类 this 初始化、super() 一次、super 前读 this 抛 ReferenceError
// - 派生类返回原始值 / 未 super 返回 undefined 的错误路径
// - super.prop 读写（跳过 receiver 自有属性、setter/getter receiver）

#include "core/IContext.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using neo::core::FunctionCreateOptions;
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
};

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }

Value* makeClass(IContext* ctx, neo::core::NativeFn fn, Value* proto, Value* superCtor,
                 bool derived) {
    FunctionCreateOptions opts;
    opts.isClassConstructor = true;
    opts.isDerived = derived;
    opts.hasPrototypeProperty = false;
    opts.superConstructor = superCtor;
    Value* cls = ctx->createNativeFunction(fn, 0, {}, opts);

    ctx->defineProperty(cls, "prototype", proto, false, false, false);
    ctx->defineProperty(proto, "constructor", cls, true, false, true);
    if (derived && superCtor != nullptr) {
        Value* superProto = ctx->getProperty(superCtor, ctx->createString("prototype"));
        ctx->setPrototype(proto, superProto);
        ctx->setPrototype(cls, superCtor);
    }
    return cls;
}

Value* baseCtor(IContext* ctx, Value* self, size_t, Value*[]) {
    ctx->defineProperty(self, "x", ctx->createNumber(1), true, true, true);
    ctx->defineProperty(self, "nt", ctx->newTarget(), true, true, true);
    return ctx->createUndefined();
}

Value* g_super = nullptr;

Value* derivedCtor(IContext* ctx, Value*, size_t, Value*[]) {
    Value* obj = ctx->constructSuper(g_super, ctx->newTarget(), 0, nullptr);
    if (neo::core::detail::isErrorValue(obj)) return obj;
    ctx->defineProperty(obj, "y", ctx->createNumber(2), true, true, true);
    return ctx->createUndefined();
}

Value* derivedNoSuper(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createUndefined();
}

Value* derivedTwice(IContext* ctx, Value*, size_t, Value*[]) {
    Value* first = ctx->constructSuper(g_super, ctx->newTarget(), 0, nullptr);
    if (neo::core::detail::isErrorValue(first)) return first;
    return ctx->constructSuper(g_super, ctx->newTarget(), 0, nullptr);
}

Value* derivedPrimitive(IContext* ctx, Value*, size_t, Value*[]) {
    Value* obj = ctx->constructSuper(g_super, ctx->newTarget(), 0, nullptr);
    if (neo::core::detail::isErrorValue(obj)) return obj;
    return ctx->createNumber(3);
}

Value* derivedThisBeforeSuper(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->getThis();
}

Value* g_home = nullptr;

Value* superGetP(IContext* ctx, Value* self, size_t, Value*[]) {
    return ctx->getSuperProperty(g_home, ctx->createString("p"), self);
}

Value* superSetQ(IContext* ctx, Value* self, size_t, Value*[]) {
    return ctx->setSuperProperty(g_home, ctx->createString("q"), ctx->createNumber(2), self);
}

}  // namespace

TEST(ClassConstructTest, ClassConstructorCannotBeCalled) {
    Fixture f;
    Value* proto = f.ctx->createObject();
    Value* cls = makeClass(f.ctx.get(), baseCtor, proto, nullptr, false);

    Value* r = f.ctx->call(cls, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
}

TEST(ClassConstructTest, BaseClassConstructUsesPrototypeAndNewTarget) {
    Fixture f;
    Value* proto = f.ctx->createObject();
    Value* cls = makeClass(f.ctx.get(), baseCtor, proto, nullptr, false);

    Value* inst = f.ctx->construct(cls, 0, nullptr);
    ASSERT_EQ(inst->type()->kind(), TypeKind::Object);
    EXPECT_EQ(f.ctx->getPrototype(inst), proto);
    EXPECT_EQ(numOf(f.ctx->getProperty(inst, f.ctx->createString("x"))), 1.0);
    EXPECT_EQ(f.ctx->getProperty(inst, f.ctx->createString("nt")), cls);
}

TEST(ClassConstructTest, ReflectConstructNewTargetControlsPrototype) {
    Fixture f;
    Value* proto = f.ctx->createObject();
    Value* cls = makeClass(f.ctx.get(), baseCtor, proto, nullptr, false);

    Value* protoD = f.ctx->createObject();
    Value* D = makeClass(f.ctx.get(), baseCtor, protoD, nullptr, false);

    Value* inst = f.ctx->construct(cls, D, 0, nullptr);
    EXPECT_EQ(f.ctx->getPrototype(inst), protoD);
    EXPECT_EQ(f.ctx->getProperty(inst, f.ctx->createString("nt")), D);
}

TEST(ClassConstructTest, DerivedClassCallsSuperAndInitializesThis) {
    Fixture f;
    Value* protoB = f.ctx->createObject();
    Value* B = makeClass(f.ctx.get(), baseCtor, protoB, nullptr, false);
    g_super = B;

    Value* protoD = f.ctx->createObject();
    Value* D = makeClass(f.ctx.get(), derivedCtor, protoD, B, true);

    Value* inst = f.ctx->construct(D, 0, nullptr);
    ASSERT_EQ(inst->type()->kind(), TypeKind::Object);
    EXPECT_EQ(f.ctx->getPrototype(inst), protoD);
    EXPECT_EQ(numOf(f.ctx->getProperty(inst, f.ctx->createString("x"))), 1.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(inst, f.ctx->createString("y"))), 2.0);
    EXPECT_EQ(f.ctx->getProperty(inst, f.ctx->createString("nt")), D);
    g_super = nullptr;
}

TEST(ClassConstructTest, DerivedNoSuperReturningUndefinedThrows) {
    Fixture f;
    Value* protoB = f.ctx->createObject();
    Value* B = makeClass(f.ctx.get(), baseCtor, protoB, nullptr, false);
    g_super = B;
    Value* protoD = f.ctx->createObject();
    Value* D = makeClass(f.ctx.get(), derivedNoSuper, protoD, B, true);

    Value* r = f.ctx->construct(D, 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    g_super = nullptr;
}

TEST(ClassConstructTest, DerivedSuperTwiceThrows) {
    Fixture f;
    Value* protoB = f.ctx->createObject();
    Value* B = makeClass(f.ctx.get(), baseCtor, protoB, nullptr, false);
    g_super = B;
    Value* protoD = f.ctx->createObject();
    Value* D = makeClass(f.ctx.get(), derivedTwice, protoD, B, true);

    Value* r = f.ctx->construct(D, 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    g_super = nullptr;
}

TEST(ClassConstructTest, DerivedReturningPrimitiveThrows) {
    Fixture f;
    Value* protoB = f.ctx->createObject();
    Value* B = makeClass(f.ctx.get(), baseCtor, protoB, nullptr, false);
    g_super = B;
    Value* protoD = f.ctx->createObject();
    Value* D = makeClass(f.ctx.get(), derivedPrimitive, protoD, B, true);

    Value* r = f.ctx->construct(D, 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    g_super = nullptr;
}

TEST(ClassConstructTest, ThisBeforeSuperThrows) {
    Fixture f;
    Value* protoB = f.ctx->createObject();
    Value* B = makeClass(f.ctx.get(), baseCtor, protoB, nullptr, false);
    g_super = B;
    Value* protoD = f.ctx->createObject();
    Value* D = makeClass(f.ctx.get(), derivedThisBeforeSuper, protoD, B, true);

    Value* r = f.ctx->construct(D, 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Exception);
    g_super = nullptr;
}

TEST(ClassConstructTest, SuperGetSkipsReceiverOwnProperty) {
    Fixture f;
    Value* protoB = f.ctx->createObject();
    f.ctx->defineProperty(protoB, "p", f.ctx->createNumber(5), true, true, true);
    Value* B = makeClass(f.ctx.get(), baseCtor, protoB, nullptr, false);

    Value* protoD = f.ctx->createObject();
    Value* D = makeClass(f.ctx.get(), derivedCtor, protoD, B, true);
    g_super = B;
    g_home = protoD;

    FunctionCreateOptions mopts;
    mopts.constructible = false;
    mopts.hasPrototypeProperty = false;
    mopts.homeObject = protoD;
    Value* method = f.ctx->createNativeFunction(superGetP, 0, {}, mopts);
    f.ctx->defineProperty(protoD, "m", method, true, false, true);

    Value* inst = f.ctx->construct(D, 0, nullptr);
    // 自有 p 遮蔽原型，但 super.p 必须从 homeObject 的原型开始查找。
    f.ctx->defineProperty(inst, "p", f.ctx->createNumber(9), true, true, true);
    Value* r = f.ctx->call(method, inst, 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r), 5.0);

    g_super = nullptr;
    g_home = nullptr;
}

TEST(ClassConstructTest, SuperSetCreatesOwnPropertyOnReceiver) {
    Fixture f;
    Value* protoB = f.ctx->createObject();
    f.ctx->defineProperty(protoB, "q", f.ctx->createNumber(1), true, true, true);
    Value* B = makeClass(f.ctx.get(), baseCtor, protoB, nullptr, false);

    Value* protoD = f.ctx->createObject();
    Value* D = makeClass(f.ctx.get(), derivedCtor, protoD, B, true);
    g_super = B;
    g_home = protoD;

    FunctionCreateOptions mopts;
    mopts.constructible = false;
    mopts.hasPrototypeProperty = false;
    mopts.homeObject = protoD;
    Value* method = f.ctx->createNativeFunction(superSetQ, 0, {}, mopts);
    f.ctx->defineProperty(protoD, "m", method, true, false, true);

    Value* inst = f.ctx->construct(D, 0, nullptr);
    Value* r = f.ctx->call(method, inst, 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r), 2.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(inst, f.ctx->createString("q"))), 2.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(protoB, f.ctx->createString("q"))), 1.0);

    g_super = nullptr;
    g_home = nullptr;
}
