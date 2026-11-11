// ClassFieldsTest.cpp - class 字段初始化与方法/属性描述符（ADR-047 P1-4）
//
// 验证：
// - initializeInstanceFields 按注册顺序执行字段初始化并定义自有属性
// - 初始化函数的 this = 实例；异常透传
// - classInitializers 在字段之后执行
// - 私有字段本阶段显式报错
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
    Value* r = f.ctx->initializeInstanceFields(inst, cls);
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
    ASSERT_EQ(f.ctx->initializeInstanceFields(inst, cls)->type()->kind(),
              TypeKind::Undefined);
    EXPECT_EQ(f.ctx->getProperty(inst, f.ctx->createString("self")), inst);
}

TEST(ClassFieldsTest, FieldInitializerErrorPropagates) {
    Fixture f;
    Value* cls = makeClass(f.ctx.get());
    Value* init = f.ctx->createNativeFunction(initError, 0);
    f.ctx->addInstanceField(cls, f.ctx->createString("bad"), init);

    Value* inst = f.ctx->createObject();
    Value* r = f.ctx->initializeInstanceFields(inst, cls);
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
    ASSERT_EQ(f.ctx->initializeInstanceFields(inst, cls)->type()->kind(),
              TypeKind::Undefined);
    ASSERT_EQ(g_order.size(), 2u);
    EXPECT_EQ(g_order[0], "one");
    EXPECT_EQ(g_order[1], "class");
}

TEST(ClassFieldsTest, PrivateFieldIsExplicitlyUnimplemented) {
    Fixture f;
    Value* cls = makeClass(f.ctx.get());
    f.ctx->addInstanceField(cls, f.ctx->createString("#x"), nullptr, true);

    Value* inst = f.ctx->createObject();
    Value* r = f.ctx->initializeInstanceFields(inst, cls);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
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
