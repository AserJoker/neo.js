// NativeClassTest.cpp - native class 注册与扩展库（ADR-047 扩展库）
//
// 验证：
// - currentFunction 在 native 调用期间指向被调函数，返回后恢复
// - registerClass 创建构造器/prototype/方法/字段/私有元素/静态元素
// - native 构造体通过 currentFunction + initializeInstanceElements 初始化字段
// - 派生类通过 classSuperConstructor + constructSuper 调用父构造器
// - classPrivateName/functionHomeObject 供 native 方法取回 class 元数据
// - class extends null 的 proto 组合
// - native class 经模块 loader 导出、import 取回后可构造

#include "core/IContext.h"
#include "core/Value.h"
#include "core/types/Boolean.h"
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

using neo::core::FunctionData;
using neo::core::IContext;
using neo::core::NativeClassDef;
using neo::core::NativeClassField;
using neo::core::NativeClassMethod;
using neo::core::NativeClassPrivateField;
using neo::core::NativeClassPrivateMethod;
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
Value* initThree(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createNumber(3); }

// 基类构造体：先初始化字段，再执行用户逻辑。
Value* baseCtorBody(IContext* ctx, Value* self, size_t, Value*[]) {
    return ctx->initializeInstanceElements(self, ctx->currentFunction());
}

// 派生类构造体：super() 后初始化本类字段。
Value* derivedCtorBody(IContext* ctx, Value*, size_t, Value*[]) {
    Value* superCtor = ctx->classSuperConstructor(ctx->currentFunction());
    Value* obj = ctx->constructSuper(superCtor, ctx->newTarget(), 0, nullptr);
    if (neo::core::detail::isErrorValue(obj)) return obj;
    return ctx->initializeInstanceElements(obj, ctx->currentFunction());
}

Value* methodSum(IContext* ctx, Value* self, size_t, Value*[]) {
    Value* x = ctx->getProperty(self, "x");
    Value* y = ctx->getProperty(self, "y");
    return ctx->add(x, y);
}

Value* methodHello(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createNumber(100);
}

Value* methodSuperHello(IContext* ctx, Value* self, size_t, Value*[]) {
    Value* home = ctx->functionHomeObject(ctx->currentFunction());
    Value* hello = ctx->getSuperProperty(home, ctx->createString("hello"), self);
    if (neo::core::detail::isErrorValue(hello)) return hello;
    return ctx->call(hello, self, 0, nullptr);
}

Value* methodStatic(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createNumber(7);
}

Value* methodSecret(IContext* ctx, Value* self, size_t, Value*[]) {
    PrivateName* name = ctx->classPrivateName(ctx->currentFunction(), "secret");
    if (name == nullptr) return ctx->createUndefined();
    return ctx->getPrivate(self, name);
}

Value* methodStaticSecret(IContext* ctx, Value* self, size_t, Value*[]) {
    PrivateName* name = ctx->classPrivateName(ctx->currentFunction(), "ssecret");
    if (name == nullptr) return ctx->createUndefined();
    return ctx->getPrivate(self, name);
}

Value* returnsObject(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createObject();
}

Value* currentFnProbe(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->currentFunction();
}

Value* g_seenFunction = nullptr;

Value* captureCurrentFn(IContext* ctx, Value*, size_t, Value*[]) {
    g_seenFunction = ctx->currentFunction();
    return ctx->createUndefined();
}

Value* callInner(IContext* ctx, Value* self, size_t, Value*[]) {
    Value* inner = ctx->createNativeFunction(captureCurrentFn, 0);
    Value* r = ctx->call(inner, self, 0, nullptr);
    if (neo::core::detail::isErrorValue(r)) return r;
    return ctx->currentFunction();
}

Value* g_exportedClass = nullptr;

Value* loaderGeometry(IContext* ctx, Value* exports) {
    NativeClassDef def;
    def.name = "Point";
    def.constructor = baseCtorBody;
    def.fields = {{"x", initOne}, {"y", initTwo}};
    def.methods = {{"sum", methodSum, 0}};
    Value* cls = ctx->registerClass(def);
    if (neo::core::detail::isErrorValue(cls)) return cls;
    g_exportedClass = cls;
    ctx->define("Point", cls);
    return ctx->exportBinding(exports, "Point", "Point");
}

}  // namespace

TEST(NativeClassTest, CurrentFunctionTracksCalleeAndRestores) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* fn = ctx->createNativeFunction(currentFnProbe, 0);
    Value* r = ctx->call(fn, ctx->createUndefined(), 0, nullptr);
    EXPECT_EQ(r, fn);
    EXPECT_EQ(ctx->currentFunction(), nullptr);

    // 嵌套调用：内层返回后，外层 currentFunction 恢复为外层函数。
    Value* outer = ctx->createNativeFunction(callInner, 0);
    Value* r2 = ctx->call(outer, ctx->createUndefined(), 0, nullptr);
    EXPECT_EQ(r2, outer);
    EXPECT_EQ(ctx->currentFunction(), nullptr);
}

TEST(NativeClassTest, RegisterBaseClassWithFieldsMethodsAndPrivate) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    PrivateName* secret = ctx->createPrivateName("secret");
    PrivateName* ssecret = ctx->createPrivateName("ssecret");

    NativeClassDef def;
    def.name = "Point";
    def.constructor = baseCtorBody;
    def.fields = {{"x", initOne}, {"y", initTwo}};
    def.methods = {{"sum", methodSum, 0}};
    def.staticMethods = {{"origin", methodStatic, 0}};
    def.privateFields = {{secret, initThree}};
    def.privateMethods = {{secret, methodSecret}};
    def.staticPrivateFields = {{ssecret, initThree}};
    def.staticPrivateMethods = {{ssecret, methodStaticSecret}};

    Value* cls = ctx->registerClass(def);
    ASSERT_EQ(cls->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->classSuperConstructor(cls), nullptr);

    // 原型/描述符
    Value* proto = ctx->getProperty(cls, "prototype");
    ASSERT_EQ(proto->type()->kind(), TypeKind::Object);
    EXPECT_EQ(ctx->getProperty(proto, "constructor"), cls);
    Value* sum = ctx->getProperty(proto, "sum");
    EXPECT_EQ(ctx->functionHomeObject(sum), proto);

    // 私有名查询（构造器与成员两种入口）
    EXPECT_EQ(ctx->classPrivateName(cls, "secret"), secret);
    EXPECT_EQ(ctx->classPrivateName(sum, "secret"), secret);

    Value* inst = ctx->construct(cls, 0, nullptr);
    ASSERT_EQ(inst->type()->kind(), TypeKind::Object);
    EXPECT_EQ(numOf(ctx->getProperty(inst, "x")), 1.0);
    EXPECT_EQ(numOf(ctx->getProperty(inst, "y")), 2.0);
    EXPECT_EQ(numOf(ctx->call(sum, inst, 0, nullptr)), 3.0);
    EXPECT_EQ(numOf(ctx->getPrivate(inst, secret)), 3.0);

    // 私有方法：方法体通过 currentFunction 找回 class 元数据。
    Value* pm = ctx->getPrivateMethod(cls, inst, secret);
    ASSERT_EQ(pm->type()->kind(), TypeKind::Function);
    EXPECT_EQ(numOf(ctx->call(pm, inst, 0, nullptr)), 3.0);

    // 静态方法/静态私有
    Value* origin = ctx->getProperty(cls, "origin");
    EXPECT_EQ(numOf(ctx->call(origin, cls, 0, nullptr)), 7.0);
    EXPECT_EQ(numOf(ctx->getPrivate(cls, ssecret)), 3.0);
    Value* spm = ctx->getPrivateMethod(cls, cls, ssecret);
    ASSERT_EQ(spm->type()->kind(), TypeKind::Function);
    EXPECT_EQ(numOf(ctx->call(spm, cls, 0, nullptr)), 3.0);

    ctx->releasePrivateName(secret);
    ctx->releasePrivateName(ssecret);
}

TEST(NativeClassTest, RegisterDerivedClassCallsSuperAndInherits) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    NativeClassDef base;
    base.name = "Base";
    base.constructor = baseCtorBody;
    base.fields = {{"baseField", initOne}};
    base.methods = {{"hello", methodHello, 0}};
    Value* baseCtor = ctx->registerClass(base);
    ASSERT_EQ(baseCtor->type()->kind(), TypeKind::Function);

    NativeClassDef derived;
    derived.name = "Derived";
    derived.constructor = derivedCtorBody;
    derived.superCtor = baseCtor;
    derived.fields = {{"derivedField", initTwo}};
    derived.methods = {{"superHello", methodSuperHello, 0}};
    Value* derivedCtor = ctx->registerClass(derived);
    ASSERT_EQ(derivedCtor->type()->kind(), TypeKind::Function);

    EXPECT_EQ(ctx->classSuperConstructor(derivedCtor), baseCtor);
    EXPECT_EQ(ctx->getPrototype(derivedCtor), baseCtor);
    Value* derivedProto = ctx->getProperty(derivedCtor, "prototype");
    EXPECT_EQ(ctx->getPrototype(derivedProto),
              ctx->getProperty(baseCtor, "prototype"));

    Value* inst = ctx->construct(derivedCtor, 0, nullptr);
    ASSERT_EQ(inst->type()->kind(), TypeKind::Object);
    EXPECT_EQ(ctx->getPrototype(inst), derivedProto);
    EXPECT_EQ(numOf(ctx->getProperty(inst, "baseField")), 1.0);
    EXPECT_EQ(numOf(ctx->getProperty(inst, "derivedField")), 2.0);

    Value* superHello = ctx->getProperty(inst, "superHello");
    EXPECT_EQ(numOf(ctx->call(superHello, inst, 0, nullptr)), 100.0);

    Value* isBase = ctx->instanceOf(inst, baseCtor);
    ASSERT_EQ(isBase->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(static_cast<neo::core::BooleanData*>(isBase->data())->value);
}

TEST(NativeClassTest, ExtendsNullPrototypeChain) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    NativeClassDef def;
    def.name = "NullBase";
    def.superCtor = ctx->createNull();
    def.constructor = returnsObject;  // 不调用 super()，直接返回对象
    Value* cls = ctx->registerClass(def);
    ASSERT_EQ(cls->type()->kind(), TypeKind::Function);

    Value* proto = ctx->getProperty(cls, "prototype");
    EXPECT_EQ(ctx->getPrototype(proto)->type()->kind(), TypeKind::Null);

    Value* dummy = ctx->createNativeFunction(noop, 0);
    EXPECT_EQ(ctx->getPrototype(cls), ctx->getPrototype(dummy));

    Value* inst = ctx->construct(cls, 0, nullptr);
    EXPECT_EQ(inst->type()->kind(), TypeKind::Object);

    // 派生类未提供构造器时无法自动 super()，注册即报错。
    NativeClassDef bad;
    bad.name = "Bad";
    bad.superCtor = ctx->createNull();
    EXPECT_EQ(ctx->registerClass(bad)->type()->kind(), TypeKind::Exception);
}

TEST(NativeClassTest, NativeClassExportedThroughModuleLoader) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    g_exportedClass = nullptr;
    ctx->setModuleLoader("/lib/geometry.js", loaderGeometry);
    Value* ns = ctx->importModule("/lib/geometry.js");
    ASSERT_EQ(ns->type()->kind(), TypeKind::ModuleNamespace);

    Value* Point = ctx->getProperty(ns, "Point");
    ASSERT_EQ(Point->type()->kind(), TypeKind::Function);
    // define 会 clone 到新槽位；对象 data 引用语义共享，故比较 data。
    EXPECT_EQ(Point->data(), g_exportedClass->data());

    Value* inst = ctx->construct(Point, 0, nullptr);
    ASSERT_EQ(inst->type()->kind(), TypeKind::Object);
    EXPECT_EQ(numOf(ctx->getProperty(inst, "x")), 1.0);
    EXPECT_EQ(numOf(ctx->getProperty(inst, "y")), 2.0);
    Value* sum = ctx->getProperty(inst, "sum");
    EXPECT_EQ(numOf(ctx->call(sum, inst, 0, nullptr)), 3.0);
}
