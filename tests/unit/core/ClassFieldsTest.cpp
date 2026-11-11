// ClassFieldsTest.cpp - class 字段初始化与方法/属性描述符（ADR-047 P1-4）
//
// 验证：
// - initializeInstanceElements 按声明顺序执行 public/private 字段初始化
// - 初始化函数的 this = 实例；异常透传
// - classInitializers 在字段之后执行
// - 私有字段与 public 字段交错时保持源码顺序
// - class 方法不可枚举、无 prototype；class 构造器 prototype 不可写/不可配置

#include "core/IContext.h"
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
#include <vector>

namespace {

using neo::core::ClassData;
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

Value* noop(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createUndefined(); }

Value* makeClass(IContext* ctx) {
    FunctionCreateOptions opts;
    opts.isClassConstructor = true;
    opts.hasPrototypeProperty = false;
    return ctx->createNativeFunction(noop, 0, {}, opts);
}

std::vector<std::string> g_order;

Value* initOne(IContext* ctx, Value*, size_t, Value*[]) {
    g_order.push_back("one");
    return ctx->createNumber(1);
}

Value* initTwo(IContext* ctx, Value*, size_t, Value*[]) {
    g_order.push_back("two");
    return ctx->createNumber(2);
}

Value* initThis(IContext* ctx, Value*, size_t, Value*[]) { return ctx->getThis(); }

Value* initError(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createException(ctx->createUndefined());
}

Value* classInit(IContext* ctx, Value*, size_t, Value*[]) {
    g_order.push_back("class");
    return ctx->createUndefined();
}

Value* initA(IContext* ctx, Value*, size_t, Value*[]) {
    g_order.push_back("a");
    return ctx->createNumber(1);
}

Value* initB(IContext* ctx, Value*, size_t, Value*[]) {
    g_order.push_back("b");
    return ctx->createNumber(2);
}

Value* initC(IContext* ctx, Value*, size_t, Value*[]) {
    g_order.push_back("c");
    return ctx->createNumber(3);
}

}  // namespace

TEST(ClassFieldsTest, FieldsInitializeInRegistrationOrder) {
    Fixture f;
    Value* cls = makeClass(f.ctx.get());
    Value* one = f.ctx->createNativeFunction(initOne, 0);
    Value* two = f.ctx->createNativeFunction(initTwo, 0);
    f.ctx->addInstanceField(cls, f.ctx->createString("a"), one);
    f.ctx->addInstanceField(cls, f.ctx->createString("b"), two);

    Value* inst = f.ctx->createObject();
    g_order.clear();
    Value* r = f.ctx->initializeInstanceElements(inst, cls);
    ASSERT_EQ(r->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(numOf(f.ctx->getProperty(inst, f.ctx->createString("a"))), 1.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(inst, f.ctx->createString("b"))), 2.0);
    ASSERT_EQ(g_order.size(), 2u);
    EXPECT_EQ(g_order[0], "one");
    EXPECT_EQ(g_order[1], "two");
}

TEST(ClassFieldsTest, FieldInitializerThisIsInstance) {
    Fixture f;
    Value* cls = makeClass(f.ctx.get());
    Value* init = f.ctx->createNativeFunction(initThis, 0);
    f.ctx->addInstanceField(cls, f.ctx->createString("self"), init);

    Value* inst = f.ctx->createObject();
    ASSERT_EQ(f.ctx->initializeInstanceElements(inst, cls)->type()->kind(),
              TypeKind::Undefined);
    EXPECT_EQ(f.ctx->getProperty(inst, f.ctx->createString("self")), inst);
}

TEST(ClassFieldsTest, FieldInitializerErrorPropagates) {
    Fixture f;
    Value* cls = makeClass(f.ctx.get());
    Value* init = f.ctx->createNativeFunction(initError, 0);
    f.ctx->addInstanceField(cls, f.ctx->createString("bad"), init);

    Value* inst = f.ctx->createObject();
    Value* r = f.ctx->initializeInstanceElements(inst, cls);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
    // 出错字段未定义。
    EXPECT_EQ(f.ctx->getProperty(inst, f.ctx->createString("bad"))->type()->kind(),
              TypeKind::Undefined);
}

TEST(ClassFieldsTest, ClassInitializersRunAfterFields) {
    Fixture f;
    Value* cls = makeClass(f.ctx.get());
    Value* fieldInit = f.ctx->createNativeFunction(initOne, 0);
    Value* clsInit = f.ctx->createNativeFunction(classInit, 0);
    f.ctx->addInstanceField(cls, f.ctx->createString("a"), fieldInit);
    f.ctx->addClassInitializer(cls, clsInit);

    Value* inst = f.ctx->createObject();
    g_order.clear();
    ASSERT_EQ(f.ctx->initializeInstanceElements(inst, cls)->type()->kind(),
              TypeKind::Undefined);
    ASSERT_EQ(g_order.size(), 2u);
    EXPECT_EQ(g_order[0], "one");
    EXPECT_EQ(g_order[1], "class");
}

TEST(ClassFieldsTest, PrivateFieldIsRegisteredSeparately) {
    Fixture f;
    Value* cls = makeClass(f.ctx.get());
    neo::core::PrivateName* name = f.ctx->createPrivateName("x");
    f.ctx->addPrivateField(cls, name, f.ctx->createNativeFunction(initOne, 0));

    Value* inst = f.ctx->createObject();
    ASSERT_EQ(f.ctx->initializeInstanceElements(inst, cls)->type()->kind(),
              TypeKind::Undefined);
    EXPECT_TRUE(f.ctx->hasPrivate(inst, name));
    EXPECT_EQ(numOf(f.ctx->getPrivate(inst, name)), 1.0);
    // 私有字段不进入普通属性表（反射不可见）。
    EXPECT_EQ(f.ctx->ownPropertyKeys(inst, false, true)->type()->kind(),
              TypeKind::Array);
    auto* keys = static_cast<neo::core::ArrayData*>(
        f.ctx->ownPropertyKeys(inst, false, true)->data());
    EXPECT_EQ(keys->length(), 0u);

    f.ctx->releasePrivateName(name);
}

TEST(ClassFieldsTest, PublicAndPrivateFieldsInitializeInDeclarationOrder) {
    Fixture f;
    Value* cls = makeClass(f.ctx.get());
    neo::core::PrivateName* b = f.ctx->createPrivateName("b");
    // 声明顺序：public a; #b; public c。
    f.ctx->addInstanceField(cls, f.ctx->createString("a"),
                            f.ctx->createNativeFunction(initA, 0));
    f.ctx->addPrivateField(cls, b, f.ctx->createNativeFunction(initB, 0));
    f.ctx->addInstanceField(cls, f.ctx->createString("c"),
                            f.ctx->createNativeFunction(initC, 0));

    Value* inst = f.ctx->createObject();
    g_order.clear();
    ASSERT_EQ(f.ctx->initializeInstanceElements(inst, cls)->type()->kind(),
              TypeKind::Undefined);
    ASSERT_EQ(g_order.size(), 3u);
    EXPECT_EQ(g_order[0], "a");
    EXPECT_EQ(g_order[1], "b");
    EXPECT_EQ(g_order[2], "c");
    EXPECT_EQ(numOf(f.ctx->getProperty(inst, f.ctx->createString("a"))), 1.0);
    EXPECT_EQ(numOf(f.ctx->getPrivate(inst, b)), 2.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(inst, f.ctx->createString("c"))), 3.0);

    f.ctx->releasePrivateName(b);
}

TEST(ClassFieldsTest, SetFunctionClassConstructorUpdatesMetadata) {
    Fixture f;
    Value* cls = makeClass(f.ctx.get());
    Value* superCtor = f.ctx->createNativeFunction(noop, 0);

    f.ctx->setFunctionClassConstructor(cls, true, superCtor);
    auto* cd = static_cast<ClassData*>(cls->data());
    EXPECT_TRUE(cd->isDerived);
    EXPECT_EQ(cd->superConstructor, superCtor);
}

TEST(ClassFieldsTest, MethodIsNonEnumerableAndHasNoPrototype) {
    Fixture f;
    Value* proto = f.ctx->createObject();
    FunctionCreateOptions mopts;
    mopts.constructible = false;
    mopts.hasPrototypeProperty = false;
    Value* method = f.ctx->createNativeFunction(noop, 0, {}, mopts);
    f.ctx->defineProperty(proto, "m", method, true, false, true);

    Value* allKeys = f.ctx->ownPropertyKeys(proto, false, false);
    Value* enumKeys = f.ctx->ownPropertyKeys(proto, true, false);
    auto* all = static_cast<neo::core::ArrayData*>(allKeys->data());
    auto* en = static_cast<neo::core::ArrayData*>(enumKeys->data());
    EXPECT_EQ(all->length(), 1u);
    EXPECT_EQ(en->length(), 0u);
    EXPECT_FALSE(neo::core::detail::isConstructor(method));
}

TEST(ClassFieldsTest, ClassPrototypePropertyIsNonWritableNonConfigurable) {
    Fixture f;
    Value* cls = makeClass(f.ctx.get());
    Value* proto = f.ctx->createObject();
    f.ctx->defineProperty(cls, "prototype", proto, false, false, false);

    // 默认 strict：写/删不可写、不可配置属性都抛 TypeError。
    Value* w = f.ctx->setProperty(cls, f.ctx->createString("prototype"),
                                  f.ctx->createObject());
    EXPECT_EQ(w->type()->kind(), TypeKind::Exception);
    Value* d = f.ctx->deleteProperty(cls, f.ctx->createString("prototype"));
    EXPECT_EQ(d->type()->kind(), TypeKind::Exception);
}
