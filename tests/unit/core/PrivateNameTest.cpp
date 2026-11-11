// PrivateNameTest.cpp - class 私有字段/方法/访问器（ADR-047 P2）
//
// 验证：
// - PrivateName 身份：同名不同 class 互不相等；description 只用于错误消息
// - 私有字段按声明顺序初始化、可读写、反射不可见
// - 私有方法/访问器：实例只持 brand，函数从 class 定义表取
// - 静态私有字段/方法/访问器挂在 class 构造器自身
// - brand 缺失时读写抛 TypeError
// - 内建实例（Array）天然可承载私有槽
// - PrivateName 引用计数：class 被回收后实例仍能访问私有字段

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/PrivateName.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using neo::core::ArrayData;
using neo::core::FunctionCreateOptions;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::PrivateName;
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

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }

Value* noop(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createUndefined(); }

Value* makeClass(IContext* ctx) {
    FunctionCreateOptions opts;
    opts.isClassConstructor = true;
    opts.hasPrototypeProperty = false;
    return ctx->createNativeFunction(noop, 0, {}, opts);
}

Value* initTen(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createNumber(10); }
Value* initTwenty(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createNumber(20); }

Value* methodAnswer(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createNumber(42);
}

Value* getterValue(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createNumber(7); }

Value* setterStore(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createUndefined();
    // 通过私有字段名写回由测试直接校验，这里只记录到普通属性便于断言。
    return ctx->defineProperty(self, "stored", argv[0], true, true, true);
}

Value* staticValue(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createNumber(99); }

}  // namespace

TEST(PrivateNameTest, IdentityIsPerEvaluationNotDescription) {
    Fixture f;
    IContext* ctx = f.ctx.get();
    PrivateName* a = ctx->createPrivateName("x");
    PrivateName* b = ctx->createPrivateName("x");
    EXPECT_NE(a, b);

    Value* classA = makeClass(ctx);
    Value* classB = makeClass(ctx);
    ctx->addPrivateField(classA, a, ctx->createNativeFunction(initTen, 0));
    ctx->addPrivateField(classB, b, ctx->createNativeFunction(initTwenty, 0));

    Value* instA = ctx->createInstance();
    Value* instB = ctx->createInstance();
    ASSERT_EQ(ctx->initializePrivateElements(instA, classA)->type()->kind(),
              TypeKind::Undefined);
    ASSERT_EQ(ctx->initializePrivateElements(instB, classB)->type()->kind(),
              TypeKind::Undefined);

    EXPECT_TRUE(ctx->hasPrivate(instA, a));
    EXPECT_FALSE(ctx->hasPrivate(instA, b));
    EXPECT_TRUE(ctx->hasPrivate(instB, b));
    EXPECT_FALSE(ctx->hasPrivate(instB, a));
    EXPECT_EQ(numOf(ctx->getPrivate(instA, a)), 10.0);
    EXPECT_EQ(numOf(ctx->getPrivate(instB, b)), 20.0);

    ctx->releasePrivateName(a);
    ctx->releasePrivateName(b);
}

TEST(PrivateNameTest, FieldReadWriteAndReflectionInvisible) {
    Fixture f;
    IContext* ctx = f.ctx.get();
    Value* cls = makeClass(ctx);
    PrivateName* x = ctx->createPrivateName("x");
    ctx->addPrivateField(cls, x, ctx->createNativeFunction(initTen, 0));

    Value* inst = ctx->createInstance();
    ASSERT_EQ(ctx->initializePrivateElements(inst, cls)->type()->kind(),
              TypeKind::Undefined);
    EXPECT_EQ(numOf(ctx->getPrivate(inst, x)), 10.0);

    Value* twenty = ctx->createNumber(20);
    ASSERT_EQ(ctx->setPrivate(inst, x, twenty)->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(numOf(ctx->getPrivate(inst, x)), 20.0);

    // 反射不可见：普通属性表为空，按字符串键读不到。
    Value* keys = ctx->ownPropertyKeys(inst, false, true);
    ASSERT_EQ(keys->type()->kind(), TypeKind::Array);
    EXPECT_EQ(static_cast<ArrayData*>(keys->data())->length(), 0u);
    EXPECT_EQ(ctx->getProperty(inst, ctx->createString("#x"))->type()->kind(),
              TypeKind::Undefined);

    ctx->releasePrivateName(x);
}

TEST(PrivateNameTest, MissingBrandThrowsTypeError) {
    Fixture f;
    IContext* ctx = f.ctx.get();
    Value* cls = makeClass(ctx);
    PrivateName* x = ctx->createPrivateName("x");
    ctx->addPrivateField(cls, x, ctx->createNativeFunction(initTen, 0));

    Value* other = ctx->createInstance();
    EXPECT_FALSE(ctx->hasPrivate(other, x));
    EXPECT_EQ(ctx->getPrivate(other, x)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->setPrivate(other, x, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);

    ctx->releasePrivateName(x);
}

TEST(PrivateNameTest, MethodAndAccessorUseBrandOnly) {
    Fixture f;
    IContext* ctx = f.ctx.get();
    Value* cls = makeClass(ctx);
    PrivateName* m = ctx->createPrivateName("m");
    PrivateName* acc = ctx->createPrivateName("acc");
    ctx->addPrivateMethod(cls, m, ctx->createNativeFunction(methodAnswer, 0));
    ctx->addPrivateAccessor(cls, acc, ctx->createNativeFunction(getterValue, 0),
                            ctx->createNativeFunction(setterStore, 0));

    Value* inst = ctx->createInstance();
    ASSERT_EQ(ctx->initializePrivateElements(inst, cls)->type()->kind(),
              TypeKind::Undefined);
    EXPECT_TRUE(ctx->hasPrivate(inst, m));
    EXPECT_TRUE(ctx->hasPrivate(inst, acc));
    // brand-only 槽不是字段：直接 getPrivate 报错。
    EXPECT_EQ(ctx->getPrivate(inst, m)->type()->kind(), TypeKind::Exception);

    Value* method = ctx->getPrivateMethod(cls, inst, m);
    ASSERT_EQ(method->type()->kind(), TypeKind::Function);
    Value* r = ctx->call(method, inst, 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r), 42.0);

    Value* getter = ctx->getPrivateGetter(cls, inst, acc);
    ASSERT_EQ(getter->type()->kind(), TypeKind::Function);
    EXPECT_EQ(numOf(ctx->call(getter, inst, 0, nullptr)), 7.0);

    Value* setter = ctx->getPrivateSetter(cls, inst, acc);
    ASSERT_EQ(setter->type()->kind(), TypeKind::Function);
    Value* arg = ctx->createNumber(3);
    Value* sr = ctx->call(setter, inst, 1, &arg);
    ASSERT_FALSE(neo::core::detail::isErrorValue(sr));
    EXPECT_EQ(numOf(ctx->getProperty(inst, ctx->createString("stored"))), 3.0);

    // 无 brand 的对象读私有方法报错。
    Value* other = ctx->createInstance();
    EXPECT_EQ(ctx->getPrivateMethod(cls, other, m)->type()->kind(),
              TypeKind::Exception);

    ctx->releasePrivateName(m);
    ctx->releasePrivateName(acc);
}

TEST(PrivateNameTest, StaticPrivateElementsLiveOnClassConstructor) {
    Fixture f;
    IContext* ctx = f.ctx.get();
    Value* cls = makeClass(ctx);
    PrivateName* sf = ctx->createPrivateName("sf");
    PrivateName* sm = ctx->createPrivateName("sm");
    PrivateName* sa = ctx->createPrivateName("sa");
    ctx->addStaticPrivateField(cls, sf, ctx->createNativeFunction(staticValue, 0));
    ctx->addStaticPrivateMethod(cls, sm, ctx->createNativeFunction(methodAnswer, 0));
    ctx->addStaticPrivateAccessor(cls, sa, ctx->createNativeFunction(getterValue, 0),
                                  nullptr);

    ASSERT_EQ(ctx->initializeStaticPrivateElements(cls)->type()->kind(),
              TypeKind::Undefined);
    EXPECT_EQ(numOf(ctx->getPrivate(cls, sf)), 99.0);

    Value* method = ctx->getPrivateMethod(cls, cls, sm);
    ASSERT_EQ(method->type()->kind(), TypeKind::Function);
    EXPECT_EQ(numOf(ctx->call(method, cls, 0, nullptr)), 42.0);

    Value* getter = ctx->getPrivateGetter(cls, cls, sa);
    ASSERT_EQ(getter->type()->kind(), TypeKind::Function);
    EXPECT_EQ(numOf(ctx->call(getter, cls, 0, nullptr)), 7.0);

    // 静态私有槽不泄漏到实例。
    Value* inst = ctx->createInstance();
    EXPECT_FALSE(ctx->hasPrivate(inst, sf));

    ctx->releasePrivateName(sf);
    ctx->releasePrivateName(sm);
    ctx->releasePrivateName(sa);
}

TEST(PrivateNameTest, BuiltinInstanceCanHoldPrivateSlots) {
    Fixture f;
    IContext* ctx = f.ctx.get();
    Value* cls = makeClass(ctx);
    PrivateName* x = ctx->createPrivateName("x");
    ctx->addPrivateField(cls, x, ctx->createNativeFunction(initTen, 0));

    // ArrayData 继承 InstanceData：class extends Array 的实例天然可承载私有槽。
    Value* arr = ctx->createArray();
    ASSERT_EQ(ctx->initializePrivateElements(arr, cls)->type()->kind(),
              TypeKind::Undefined);
    EXPECT_EQ(numOf(ctx->getPrivate(arr, x)), 10.0);

    ctx->releasePrivateName(x);
}

TEST(PrivateNameTest, PlainObjectCannotHoldPrivateSlots) {
    Fixture f;
    IContext* ctx = f.ctx.get();
    Value* cls = makeClass(ctx);
    PrivateName* x = ctx->createPrivateName("x");
    ctx->addPrivateField(cls, x, ctx->createNativeFunction(initTen, 0));

    Value* obj = ctx->createObject();
    EXPECT_EQ(ctx->initializePrivateElements(obj, cls)->type()->kind(),
              TypeKind::Exception);

    ctx->releasePrivateName(x);
}

TEST(PrivateNameTest, NameSurvivesClassCollectionViaInstanceSlot) {
    Fixture f;
    IContext* ctx = f.ctx.get();
    PrivateName* x = ctx->createPrivateName("x");

    // class 定义在临时作用域中；实例在作用域外创建并初始化。
    ctx->pushNewScope(ctx->current());
    Value* cls = makeClass(ctx);
    ctx->addPrivateField(cls, x, ctx->createNativeFunction(initTen, 0));
    ctx->popScope();

    // pop 后 Value 尚未回收，仍可读取；随后 collect 会销毁 class data。
    Value* inst = ctx->createInstance();
    ASSERT_EQ(ctx->initializePrivateElements(inst, cls)->type()->kind(),
              TypeKind::Undefined);

    // 释放调用方引用：此后 PrivateName 只由实例槽持有。
    ctx->releasePrivateName(x);
    ctx->gc()->collect();

    // class 已不可达，但实例槽仍持有一份引用，私有字段可继续访问。
    EXPECT_TRUE(ctx->hasPrivate(inst, x));
    EXPECT_EQ(numOf(ctx->getPrivate(inst, x)), 10.0);
}
