// ClassEndToEndTest.cpp - class 核心端到端（ADR-047 P1-5）
//
// 用引擎原语模拟 VM 的 class 定义与实例化：
// - 创建 class 构造器/prototype、链接原型链、定义方法/字段/源码
// - 基类/派生类构造、super()、字段初始化顺序
// - instanceof / typeof / Function.prototype.toString / bind 构造
// - GC 后实例、原型、方法、字段仍可用

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/OperatorBehavior.h"
#include "core/SourceText.h"
#include "core/Value.h"
#include "core/types/Boolean.h"
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

using neo::core::BooleanData;
using neo::core::FunctionCreateOptions;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::SourceText;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::createSourceText;
using neo::core::releaseSourceText;
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
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }
bool boolOf(Value* v) { return static_cast<BooleanData*>(v->data())->value; }

struct SimClass {
    Value* ctor = nullptr;
    Value* proto = nullptr;
};

SimClass defineClass(IContext* ctx, std::string_view name, neo::core::NativeFn ctorFn,
                     Value* superCtor, std::string_view source) {
    Value* proto = ctx->createObject();

    FunctionCreateOptions opts;
    opts.isClassConstructor = true;
    opts.isDerived = superCtor != nullptr;
    opts.hasPrototypeProperty = false;
    opts.superConstructor = superCtor;
    Value* ctor = ctx->createNativeFunction(ctorFn, 0, {}, opts);

    ctx->defineProperty(ctor, "prototype", proto, false, false, false);
    ctx->defineProperty(proto, "constructor", ctor, true, false, true);
    if (superCtor != nullptr) {
        Value* superProto = ctx->getProperty(superCtor, ctx->createString("prototype"));
        ctx->setPrototype(proto, superProto);
        ctx->setPrototype(ctor, superCtor);
    }
    ctx->setFunctionName(ctor, name);

    SourceText* text = createSourceText(std::string(source));
    ctx->setFunctionSource(ctor, {text, 0, static_cast<uint32_t>(text->text.size())});
    releaseSourceText(text);

    return {ctor, proto};
}

Value* defineMethod(IContext* ctx, Value* proto, std::string_view name,
                    neo::core::NativeFn fn, Value* homeObject) {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    opts.homeObject = homeObject;
    Value* method = ctx->createNativeFunction(fn, 0, {}, opts);
    ctx->defineProperty(proto, ctx->createString(name), method, true, false, true);
    return method;
}

Value* g_baseCtor = nullptr;
Value* g_derivedCtor = nullptr;
Value* g_derivedProto = nullptr;

Value* initBaseField(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createNumber(10);
}
Value* initDerivedField(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createNumber(20);
}

Value* baseCtor(IContext* ctx, Value* self, size_t, Value*[]) {
    return ctx->initializeInstanceFields(self, g_baseCtor);
}

Value* derivedCtor(IContext* ctx, Value*, size_t, Value*[]) {
    Value* obj = ctx->constructSuper(g_baseCtor, ctx->newTarget(), 0, nullptr);
    if (neo::core::detail::isErrorValue(obj)) return obj;
    return ctx->initializeInstanceFields(obj, g_derivedCtor);
}

Value* baseHello(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createString("base");
}

Value* derivedHello(IContext* ctx, Value* self, size_t, Value*[]) {
    Value* fn = ctx->getSuperProperty(g_derivedProto, ctx->createString("hello"), self);
    if (neo::core::detail::isErrorValue(fn)) return fn;
    return ctx->call(fn, self, 0, nullptr);
}

Value* toStringOf(IContext* ctx, Value* fn) {
    Value* toString = ctx->getProperty(fn, ctx->createString("toString"));
    return ctx->call(toString, fn, 0, nullptr);
}

}  // namespace

TEST(ClassEndToEndTest, DerivedClassWithFieldsMethodsSuperAndGc) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    SimClass base = defineClass(ctx, "Base", baseCtor, nullptr,
                                "class Base { hello() {} }");
    g_baseCtor = base.ctor;
    ctx->addInstanceField(base.ctor, ctx->createString("baseField"),
                          ctx->createNativeFunction(initBaseField, 0));
    defineMethod(ctx, base.proto, "hello", baseHello, base.proto);

    SimClass derived = defineClass(ctx, "Derived", derivedCtor, base.ctor,
                                   "class Derived extends Base {}");
    g_derivedCtor = derived.ctor;
    g_derivedProto = derived.proto;
    ctx->addInstanceField(derived.ctor, ctx->createString("derivedField"),
                          ctx->createNativeFunction(initDerivedField, 0));
    defineMethod(ctx, derived.proto, "hello", derivedHello, derived.proto);

    Value* inst = ctx->construct(derived.ctor, 0, nullptr);
    ASSERT_EQ(inst->type()->kind(), TypeKind::Object);
    EXPECT_EQ(ctx->getPrototype(inst), derived.proto);
    EXPECT_EQ(numOf(ctx->getProperty(inst, ctx->createString("baseField"))), 10.0);
    EXPECT_EQ(numOf(ctx->getProperty(inst, ctx->createString("derivedField"))), 20.0);

    // instanceof / typeof
    Value* isBase = ctx->instanceOf(inst, base.ctor);
    ASSERT_EQ(isBase->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(isBase));
    Value* isDerived = ctx->instanceOf(inst, derived.ctor);
    ASSERT_EQ(isDerived->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(isDerived));
    EXPECT_STREQ(neo::core::detail::typeofName(derived.ctor), "function");

    // 方法调用：derivedHello -> super.hello -> "base"
    Value* hello = ctx->getProperty(inst, ctx->createString("hello"));
    Value* r = ctx->call(hello, inst, 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(r), "base");

    // Function.prototype.toString 使用 class 源码切片
    Value* text = toStringOf(ctx, derived.ctor);
    ASSERT_EQ(text->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(text), "class Derived extends Base {}");

    // bind 后构造：newTarget 替换为 target，实例仍用 derived.prototype
    Value* bindFn = ctx->getProperty(derived.ctor, ctx->createString("bind"));
    Value* bindArg = ctx->createUndefined();
    Value* bound = ctx->call(bindFn, derived.ctor, 1, &bindArg);
    ASSERT_EQ(bound->type()->kind(), TypeKind::Function);
    Value* boundInst = ctx->construct(bound, 0, nullptr);
    ASSERT_EQ(boundInst->type()->kind(), TypeKind::Object);
    EXPECT_EQ(ctx->getPrototype(boundInst), derived.proto);
    EXPECT_EQ(numOf(ctx->getProperty(boundInst, ctx->createString("derivedField"))), 20.0);

    // GC 后实例、原型、方法、字段仍可用
    ctx->gc()->collect();
    EXPECT_EQ(ctx->getPrototype(inst), derived.proto);
    EXPECT_EQ(numOf(ctx->getProperty(inst, ctx->createString("baseField"))), 10.0);
    Value* r2 = ctx->call(ctx->getProperty(inst, ctx->createString("hello")), inst, 0,
                          nullptr);
    ASSERT_EQ(r2->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(r2), "base");

    g_baseCtor = nullptr;
    g_derivedCtor = nullptr;
    g_derivedProto = nullptr;
}
