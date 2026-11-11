// ProxyTest.cpp - Proxy exotic 对象（ADR-048）
//
// 验证：
// - get/set/has/delete/defineProperty/ownKeys/getOwnPropertyDescriptor traps
// - getPrototypeOf/setPrototypeOf/apply/construct traps
// - trap 缺失时回退 target 的普通对象算法
// - 原型链上的 Proxy 触发 get/set/has
// - Proxy.revocable 撤销后所有内部方法抛 TypeError
// - 可调用/可构造代理与 Array.isArray 泛化
// - 非对象 target/handler 被拒绝
// - 可扩展性 trap 与描述符/ownKeys/get/set/has/delete/原型不变量
// - Object.getOwnPropertyDescriptor / isExtensible / preventExtensions

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Object.h"
#include "core/types/Proxy.h"
#include "core/types/Boolean.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>
#include "TestHelpers.h"

#include <memory>
#include <string>
#include <vector>

namespace {

using neo::core::BooleanData;
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
bool boolOf(Value* v) { return static_cast<BooleanData*>(v->data())->value; }
std::string strOf(Value* v) { return static_cast<neo::core::StringData*>(v->data())->value; }

// —— trap 记录 ——
Value* g_self = nullptr;
Value* g_arg0 = nullptr;
Value* g_arg1 = nullptr;
Value* g_arg2 = nullptr;
Value* g_arg3 = nullptr;
size_t g_argc = 0;

Value* recordArgs(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    g_self = self;
    g_argc = argc;
    g_arg0 = argc > 0 ? argv[0] : nullptr;
    g_arg1 = argc > 1 ? argv[1] : nullptr;
    g_arg2 = argc > 2 ? argv[2] : nullptr;
    g_arg3 = argc > 3 ? argv[3] : nullptr;
    return ctx->createUndefined();
}

Value* getTen(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    recordArgs(ctx, self, argc, argv);
    return ctx->createNumber(10);
}

Value* returnTrue(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    recordArgs(ctx, self, argc, argv);
    return ctx->createBoolean(true);
}

Value* returnFalse(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    recordArgs(ctx, self, argc, argv);
    return ctx->createBoolean(false);
}

Value* returnNinetyNine(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    recordArgs(ctx, self, argc, argv);
    return ctx->createNumber(99);
}

Value* addOne(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createNumber(1);
    return ctx->createNumber(numOf(argv[0]) + 1);
}

Value* makeResult(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    recordArgs(ctx, self, argc, argv);
    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "constructed", ctx->createBoolean(true), true, true, true);
    return obj;
}

Value* ownKeysXY(IContext* ctx, Value*, size_t, Value*[]) {
    Value* arr = ctx->createArray();
    ctx->defineProperty(arr, "0", ctx->createString("x"), true, true, true);
    ctx->defineProperty(arr, "1", ctx->createString("y"), true, true, true);
    return arr;
}

Value* descriptorForX(IContext* ctx, Value*, size_t, Value*[]) {
    Value* desc = ctx->createObject();
    ctx->defineProperty(desc, "value", ctx->createNumber(7), true, true, true);
    ctx->defineProperty(desc, "writable", ctx->createBoolean(true), true, true, true);
    ctx->defineProperty(desc, "enumerable", ctx->createBoolean(true), true, true, true);
    ctx->defineProperty(desc, "configurable", ctx->createBoolean(true), true, true, true);
    return desc;
}

Value* nonConfigurableDescriptorForX(IContext* ctx, Value*, size_t, Value*[]) {
    Value* desc = ctx->createObject();
    ctx->defineProperty(desc, "value", ctx->createNumber(7), true, true, true);
    ctx->defineProperty(desc, "writable", ctx->createBoolean(true), true, true, true);
    ctx->defineProperty(desc, "enumerable", ctx->createBoolean(true), true, true, true);
    ctx->defineProperty(desc, "configurable", ctx->createBoolean(false), true, true, true);
    return desc;
}

Value* throwError(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createException(ctx->createString("boom"));
}

Value* defineTrue(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    recordArgs(ctx, self, argc, argv);
    return ctx->createBoolean(true);
}

void defineNative(IContext* ctx, Value* obj, const char* name, neo::core::NativeFn fn) {
    Value* f = ctx->createNativeFunction(fn, 0);
    ctx->defineProperty(obj, name, f, true, true, true);
}

Value* makeTargetWithX(IContext* ctx) {
    Value* target = ctx->createObject();
    ctx->defineProperty(target, "x", ctx->createNumber(1), true, true, true);
    return target;
}

Value* returnUndefined(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createUndefined();
}

Value* ownKeysEmpty(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createArray();
}

Value* g_stored = nullptr;
Value* returnStored(IContext* ctx, Value*, size_t, Value*[]) {
    (void)ctx;
    return g_stored;
}

Value* initOne(IContext* ctx, Value*, size_t, Value*[]) {
    return ctx->createNumber(1);
}

Value* baseCtorBody(IContext* ctx, Value* self, size_t, Value*[]) {
    return ctx->initializeInstanceElements(self, ctx->currentFunction());
}

Value* derivedCtorBody(IContext* ctx, Value*, size_t, Value*[]) {
    Value* superCtor = ctx->classSuperConstructor(ctx->currentFunction());
    Value* obj = ctx->constructSuper(superCtor, ctx->newTarget(), 0, nullptr);
    if (neo::core::detail::isErrorValue(obj)) return obj;
    return ctx->initializeInstanceElements(obj, ctx->currentFunction());
}

neo::core::PrivateName* g_private = nullptr;
Value* readPrivateSelf(IContext* ctx, Value* self, size_t, Value*[]) {
    return ctx->getPrivate(self, g_private);
}

Value* callObjectStatic(IContext* ctx, const char* name, size_t argc, Value* argv[]) {
    Value* ctor = ctx->global()->lookup("Object");
    Value* fn = ctx->getProperty(ctor, name);
    return ctx->call(fn, ctor, argc, argv);
}

}  // namespace

TEST(ProxyTest, GetSetHasDeleteTraps) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = makeTargetWithX(ctx);
    Value* handler = ctx->createObject();
    defineNative(ctx, handler, "get", getTen);
    defineNative(ctx, handler, "set", returnTrue);
    defineNative(ctx, handler, "has", returnTrue);
    defineNative(ctx, handler, "deleteProperty", returnFalse);

    Value* proxy = ctx->createProxy(target, handler);
    ASSERT_EQ(proxy->type()->kind(), TypeKind::Proxy);

    // get trap：this = handler，参数 (target, key, receiver)
    Value* got = ctx->getProperty(proxy, "x");
    ASSERT_EQ(got->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(got), 10.0);
    EXPECT_EQ(g_self, handler);
    EXPECT_EQ(g_arg0, target);
    EXPECT_EQ(strOf(g_arg1), "x");
    EXPECT_EQ(g_arg2, proxy);

    // set trap：参数 (target, key, value, receiver)
    EXPECT_EQ(numOf(ctx->setProperty(proxy, "y", ctx->createNumber(2))), 2.0);
    EXPECT_EQ(g_argc, 4u);
    EXPECT_EQ(strOf(g_arg1), "y");
    EXPECT_EQ(numOf(g_arg2), 2.0);
    EXPECT_EQ(g_arg3, proxy);

    // has trap
    EXPECT_TRUE(neo::test::hasProperty(ctx, proxy, "z"));
    EXPECT_EQ(g_argc, 2u);

    // delete trap 返回 false
    Value* del = ctx->deleteProperty(proxy, "x");
    ASSERT_EQ(del->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(boolOf(del));
}

TEST(ProxyTest, HasTrapErrorPropagates) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createObject();
    Value* handler = ctx->createObject();
    defineNative(ctx, handler, "has", throwError);
    Value* proxy = ctx->createProxy(target, handler);

    // has trap 抛错必须经 Value* 通道透传，而不是吞成 false。
    EXPECT_EQ(ctx->hasProperty(proxy, "x")->type()->kind(), TypeKind::Exception);

    // 撤销后的 hasProperty 同样返回 Exception。
    revokeProxy(proxy);
    EXPECT_EQ(ctx->hasProperty(proxy, "x")->type()->kind(), TypeKind::Exception);
}

TEST(ProxyTest, MissingTrapsFallBackToTarget) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = makeTargetWithX(ctx);
    Value* handler = ctx->createObject();
    Value* proxy = ctx->createProxy(target, handler);

    EXPECT_EQ(numOf(ctx->getProperty(proxy, "x")), 1.0);
    EXPECT_TRUE(neo::test::hasProperty(ctx, proxy, "x"));
    EXPECT_FALSE(neo::test::hasProperty(ctx, proxy, "nope"));
    EXPECT_EQ(numOf(ctx->setProperty(proxy, "x", ctx->createNumber(5))), 5.0);
    EXPECT_EQ(numOf(ctx->getProperty(target, "x")), 5.0);
}

TEST(ProxyTest, OwnKeysAndDescriptorTraps) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createObject();
    Value* handler = ctx->createObject();
    defineNative(ctx, handler, "ownKeys", ownKeysXY);
    defineNative(ctx, handler, "getOwnPropertyDescriptor", descriptorForX);
    Value* proxy = ctx->createProxy(target, handler);

    std::vector<Value*> keys;
    ASSERT_EQ(neo::core::ownPropertyKeys(ctx, proxy, false, false, keys), nullptr);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(strOf(keys[0]), "x");
    EXPECT_EQ(strOf(keys[1]), "y");

    // 可枚举过滤走 getOwnPropertyDescriptor trap（返回 enumerable=true）。
    std::vector<Value*> enumerable;
    ASSERT_EQ(neo::core::ownPropertyKeys(ctx, proxy, true, false, enumerable), nullptr);
    EXPECT_EQ(enumerable.size(), 2u);

    // hasOwnProperty 同样走 getOwnPropertyDescriptor trap。
    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* argv[1] = {ctx->createString("x")};
    Value* r = ctx->call(hasOwn, proxy, 1, argv);
    ASSERT_EQ(r->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(r));
}

TEST(ProxyTest, DefinePropertyTrap) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createObject();
    Value* handler = ctx->createObject();
    defineNative(ctx, handler, "defineProperty", defineTrue);
    Value* proxy = ctx->createProxy(target, handler);

    Value* r = ctx->defineProperty(proxy, "k", ctx->createNumber(7), true, false, true);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r), 7.0);
    EXPECT_EQ(g_argc, 3u);
    EXPECT_EQ(strOf(g_arg1), "k");
    ASSERT_EQ(g_arg2->type()->kind(), TypeKind::Object);
    EXPECT_EQ(numOf(ctx->getProperty(g_arg2, "value")), 7.0);
    EXPECT_FALSE(boolOf(ctx->getProperty(g_arg2, "enumerable")));
}

TEST(ProxyTest, PrototypeTraps) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createObject();
    Value* protoA = ctx->createObject();
    Value* protoB = ctx->createObject();
    ctx->setPrototype(target, protoA);

    Value* handler = ctx->createObject();
    defineNative(ctx, handler, "getPrototypeOf", getTen);
    defineNative(ctx, handler, "setPrototypeOf", returnTrue);
    Value* proxy = ctx->createProxy(target, handler);

    // getPrototypeOf trap 返回非对象 → TypeError（trap 结果校验）。
    EXPECT_EQ(ctx->getPrototype(proxy)->type()->kind(), TypeKind::Exception);

    // setPrototypeOf trap 返回 true → 成功，参数 (target, proto)
    EXPECT_EQ(ctx->setPrototype(proxy, protoB), proxy);
    EXPECT_EQ(g_arg0, target);
    EXPECT_EQ(g_arg1, protoB);

    // 无 trap 时回退 target 的原型。
    Value* plain = ctx->createProxy(target, ctx->createObject());
    EXPECT_EQ(ctx->getPrototype(plain), protoA);
}

TEST(ProxyTest, ApplyAndConstructTraps) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createNativeFunction(addOne, 0);
    Value* handler = ctx->createObject();
    defineNative(ctx, handler, "apply", returnNinetyNine);
    defineNative(ctx, handler, "construct", makeResult);
    Value* proxy = ctx->createProxy(target, handler);

    // apply trap
    Value* r = ctx->call(proxy, ctx->createUndefined(), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r), 99.0);
    EXPECT_EQ(g_argc, 3u);
    EXPECT_EQ(g_arg0, target);
    EXPECT_EQ(g_arg1->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(g_arg2->type()->kind(), TypeKind::Array);

    // construct trap
    Value* obj = ctx->construct(proxy, 0, nullptr);
    ASSERT_EQ(obj->type()->kind(), TypeKind::Object);
    EXPECT_TRUE(boolOf(ctx->getProperty(obj, "constructed")));

    // 无 trap：转发到 target
    Value* plain = ctx->createProxy(target, ctx->createObject());
    Value* r2 = ctx->call(plain, ctx->createUndefined(), 1, &r);
    ASSERT_EQ(r2->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r2), 100.0);
}

TEST(ProxyTest, CallableAndConstructorAdaptation) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createNativeFunction(addOne, 0);
    Value* proxy = ctx->createProxy(target, ctx->createObject());
    EXPECT_TRUE(neo::core::detail::isCallable(proxy));
    EXPECT_TRUE(neo::core::detail::isConstructor(proxy));

    // Function.prototype.call 泛化到可调用代理。
    Value* call = ctx->getProperty(ctx->functionPrototype(), "call");
    Value* arg = ctx->createNumber(4);
    Value* argv[2] = {ctx->createUndefined(), arg};
    Value* r = ctx->call(call, proxy, 2, argv);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r), 5.0);

    // Array.isArray 递归检查 target。
    Value* arr = ctx->createArray();
    Value* arrProxy = ctx->createProxy(arr, ctx->createObject());
    Value* arrayCtor = ctx->global()->lookup("Array");
    Value* isArray = ctx->getProperty(arrayCtor, "isArray");
    Value* aargv[1] = {arrProxy};
    Value* isArr = ctx->call(isArray, arrayCtor, 1, aargv);
    ASSERT_EQ(isArr->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(isArr));
}

TEST(ProxyTest, PrototypeChainDispatch) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createObject();
    Value* handler = ctx->createObject();
    defineNative(ctx, handler, "get", getTen);
    defineNative(ctx, handler, "set", returnTrue);
    defineNative(ctx, handler, "has", returnTrue);
    Value* proxy = ctx->createProxy(target, handler);

    // obj 的原型是 proxy：读取/写入/`in` 都必须经 proxy trap。
    Value* obj = ctx->createObject();
    ASSERT_EQ(ctx->setPrototype(obj, proxy), obj);

    EXPECT_EQ(numOf(ctx->getProperty(obj, "p")), 10.0);
    EXPECT_EQ(g_arg2, obj);  // receiver 是 obj，不是 proxy

    EXPECT_EQ(numOf(ctx->setProperty(obj, "q", ctx->createNumber(3))), 3.0);
    EXPECT_EQ(g_arg3, obj);

    EXPECT_TRUE(neo::test::hasProperty(ctx, obj, "r"));
}

TEST(ProxyTest, Revocable) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* proxyCtor = ctx->global()->lookup("Proxy");
    ASSERT_NE(proxyCtor, nullptr);
    Value* revocable = ctx->getProperty(proxyCtor, "revocable");
    ASSERT_TRUE(neo::core::detail::isCallable(revocable));

    Value* target = makeTargetWithX(ctx);
    Value* handler = ctx->createObject();
    Value* argv[2] = {target, handler};
    Value* result = ctx->call(revocable, proxyCtor, 2, argv);
    ASSERT_EQ(result->type()->kind(), TypeKind::Object);

    Value* proxy = ctx->getProperty(result, "proxy");
    Value* revoke = ctx->getProperty(result, "revoke");
    ASSERT_EQ(proxy->type()->kind(), TypeKind::Proxy);
    ASSERT_TRUE(neo::core::detail::isCallable(revoke));

    EXPECT_EQ(numOf(ctx->getProperty(proxy, "x")), 1.0);

    Value* r = ctx->call(revoke, ctx->createUndefined(), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Undefined);

    // 撤销后所有内部方法抛 TypeError。
    EXPECT_EQ(ctx->getProperty(proxy, "x")->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->setProperty(proxy, "x", ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->getPrototype(proxy)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->deleteProperty(proxy, "x")->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->ownPropertyKeys(proxy, false, true)->type()->kind(),
              TypeKind::Exception);
}

TEST(ProxyTest, ConstructorRejectsNonObject) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* proxyCtor = ctx->global()->lookup("Proxy");
    ASSERT_NE(proxyCtor, nullptr);

    Value* handler = ctx->createObject();
    Value* argv[2] = {ctx->createNumber(1), handler};
    Value* r = ctx->construct(proxyCtor, 2, argv);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);

    Value* target = ctx->createObject();
    Value* argv2[2] = {target, ctx->createNumber(2)};
    Value* r2 = ctx->construct(proxyCtor, 2, argv2);
    EXPECT_EQ(r2->type()->kind(), TypeKind::Exception);

    // 直接调用（无 new）也拒绝。
    Value* argv3[2] = {target, handler};
    Value* r3 = ctx->call(proxyCtor, ctx->createUndefined(), 2, argv3);
    EXPECT_EQ(r3->type()->kind(), TypeKind::Exception);
}

TEST(ProxyTest, ObjectDescriptorAndExtensibilityApi) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* obj = makeTargetWithX(ctx);
    Value* key = ctx->createString("x");

    // getOwnPropertyDescriptor：完整描述符对象。
    Value* argv[2] = {obj, key};
    Value* desc = callObjectStatic(ctx, "getOwnPropertyDescriptor", 2, argv);
    ASSERT_EQ(desc->type()->kind(), TypeKind::Object);
    EXPECT_EQ(numOf(ctx->getProperty(desc, "value")), 1.0);
    EXPECT_TRUE(boolOf(ctx->getProperty(desc, "writable")));
    EXPECT_TRUE(boolOf(ctx->getProperty(desc, "enumerable")));
    EXPECT_TRUE(boolOf(ctx->getProperty(desc, "configurable")));

    // 不存在的属性 → undefined。
    Value* missingArg[2] = {obj, ctx->createString("nope")};
    EXPECT_EQ(callObjectStatic(ctx, "getOwnPropertyDescriptor", 2, missingArg)->type()->kind(),
              TypeKind::Undefined);

    // isExtensible / preventExtensions。
    Value* extArg[1] = {obj};
    EXPECT_TRUE(boolOf(callObjectStatic(ctx, "isExtensible", 1, extArg)));
    EXPECT_EQ(callObjectStatic(ctx, "preventExtensions", 1, extArg), obj);
    EXPECT_FALSE(boolOf(callObjectStatic(ctx, "isExtensible", 1, extArg)));

    // 不可扩展对象：新增属性失败，已有可写属性仍可更新。
    EXPECT_EQ(ctx->defineProperty(obj, "y", ctx->createNumber(2), true, true, true)
                  ->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(numOf(ctx->setProperty(obj, "x", ctx->createNumber(9))), 9.0);
    EXPECT_EQ(numOf(ctx->getProperty(obj, "x")), 9.0);
}

TEST(ProxyTest, ExtensibilityTrapsAndInvariants) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createObject();
    Value* handler = ctx->createObject();
    defineNative(ctx, handler, "isExtensible", returnFalse);
    defineNative(ctx, handler, "preventExtensions", returnTrue);
    Value* proxy = ctx->createProxy(target, handler);

    // isExtensible trap 与 target 实际可扩展性不一致 → TypeError。
    EXPECT_EQ(proxy->type()->isExtensible(ctx, proxy)->type()->kind(), TypeKind::Exception);

    // preventExtensions trap 返回 true 但 target 仍可扩展 → TypeError。
    EXPECT_EQ(proxy->type()->preventExtensions(ctx, proxy)->type()->kind(),
              TypeKind::Exception);

    // target 真正不可扩展后，两个 trap 结果一致 → 成功。
    ASSERT_EQ(target->type()->preventExtensions(ctx, target)->type()->kind(),
              TypeKind::Boolean);
    Value* ext = proxy->type()->isExtensible(ctx, proxy);
    ASSERT_EQ(ext->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(boolOf(ext));
    Value* prev = proxy->type()->preventExtensions(ctx, proxy);
    ASSERT_EQ(prev->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(prev));
}

TEST(ProxyTest, DescriptorAndOwnKeysInvariants) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    // target 有不可配置属性 x。
    Value* target = ctx->createObject();
    ctx->defineProperty(target, "x", ctx->createNumber(1), true, false, false);

    // 1. getOwnPropertyDescriptor 隐藏不可配置属性 → TypeError。
    {
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "getOwnPropertyDescriptor", returnUndefined);
        Value* proxy = ctx->createProxy(target, handler);
        Value* argv[2] = {proxy, ctx->createString("x")};
        EXPECT_EQ(callObjectStatic(ctx, "getOwnPropertyDescriptor", 2, argv)->type()->kind(),
                  TypeKind::Exception);
    }

    // 2. ownKeys 遗漏不可配置属性 → TypeError。
    {
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "ownKeys", ownKeysEmpty);
        Value* proxy = ctx->createProxy(target, handler);
        std::vector<Value*> keys;
        EXPECT_EQ(neo::core::ownPropertyKeys(ctx, proxy, false, false, keys)->type()->kind(),
                  TypeKind::Exception);
    }

    // 3. 可扩展 target 上报告新的不可配置属性 → TypeError。
    {
        Value* extTarget = ctx->createObject();
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "getOwnPropertyDescriptor", nonConfigurableDescriptorForX);
        Value* proxy = ctx->createProxy(extTarget, handler);
        Value* argv[2] = {proxy, ctx->createString("x")};
        EXPECT_EQ(callObjectStatic(ctx, "getOwnPropertyDescriptor", 2, argv)->type()->kind(),
                  TypeKind::Exception);
    }
}

TEST(ProxyTest, GetSetHasDeleteInvariants) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    // 不可配置、不可写的数据属性 x=1。
    Value* target = ctx->createObject();
    ctx->defineProperty(target, "x", ctx->createNumber(1), false, false, false);

    // get trap 返回不同值 → TypeError。
    {
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "get", returnNinetyNine);
        Value* proxy = ctx->createProxy(target, handler);
        EXPECT_EQ(ctx->getProperty(proxy, "x")->type()->kind(), TypeKind::Exception);
    }

    // set trap 报告成功写入不同值 → TypeError。
    {
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "set", returnTrue);
        Value* proxy = ctx->createProxy(target, handler);
        EXPECT_EQ(ctx->setProperty(proxy, "x", ctx->createNumber(2))->type()->kind(),
                  TypeKind::Exception);
    }

    // has trap 隐藏不可配置属性 → TypeError；报告存在则允许。
    {
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "has", returnFalse);
        Value* proxy = ctx->createProxy(target, handler);
        EXPECT_EQ(ctx->hasProperty(proxy, "x")->type()->kind(), TypeKind::Exception);
    }
    {
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "has", returnTrue);
        Value* proxy = ctx->createProxy(target, handler);
        Value* r = ctx->hasProperty(proxy, "x");
        ASSERT_EQ(r->type()->kind(), TypeKind::Boolean);
        EXPECT_TRUE(boolOf(r));
    }

    // deleteProperty trap 返回 true 删除不可配置属性 → TypeError。
    {
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "deleteProperty", returnTrue);
        Value* proxy = ctx->createProxy(target, handler);
        EXPECT_EQ(ctx->deleteProperty(proxy, "x")->type()->kind(), TypeKind::Exception);
    }
}

TEST(ProxyTest, DefineAndPrototypeInvariants) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    // defineProperty trap 成功但 target 不可扩展且属性不存在 → TypeError。
    {
        Value* target = ctx->createObject();
        ASSERT_EQ(target->type()->preventExtensions(ctx, target)->type()->kind(),
                  TypeKind::Boolean);
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "defineProperty", defineTrue);
        Value* proxy = ctx->createProxy(target, handler);
        EXPECT_EQ(ctx->defineProperty(proxy, "y", ctx->createNumber(1), true, true, true)
                      ->type()->kind(),
                  TypeKind::Exception);
    }

    // getPrototypeOf trap 在不可扩展 target 上返回不同原型 → TypeError。
    {
        Value* target = ctx->createObject();
        Value* protoA = ctx->createObject();
        Value* protoB = ctx->createObject();
        ctx->setPrototype(target, protoA);
        ASSERT_EQ(target->type()->preventExtensions(ctx, target)->type()->kind(),
                  TypeKind::Boolean);
        g_stored = protoB;
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "getPrototypeOf", returnStored);
        Value* proxy = ctx->createProxy(target, handler);
        EXPECT_EQ(ctx->getPrototype(proxy)->type()->kind(), TypeKind::Exception);
    }

    // setPrototypeOf trap 在不可扩展 target 上改原型 → TypeError。
    {
        Value* target = ctx->createObject();
        Value* protoA = ctx->createObject();
        Value* protoB = ctx->createObject();
        ctx->setPrototype(target, protoA);
        ASSERT_EQ(target->type()->preventExtensions(ctx, target)->type()->kind(),
                  TypeKind::Boolean);
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "setPrototypeOf", returnTrue);
        Value* proxy = ctx->createProxy(target, handler);
        EXPECT_EQ(ctx->setPrototype(proxy, protoB)->type()->kind(), TypeKind::Exception);
    }
}

TEST(ProxyTest, ClassExtendsProxy) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    // Proxy 构造函数没有 prototype 属性 → class extends Proxy 抛 TypeError。
    {
        neo::core::NativeClassDef def;
        def.name = "Bad";
        def.constructor = derivedCtorBody;
        def.superCtor = ctx->global()->lookup("Proxy");
        Value* c = ctx->registerClass(def);
        EXPECT_EQ(c->type()->kind(), TypeKind::Exception);
    }

    // 包装了合法构造器的 Proxy 可作为 superclass。
    neo::core::NativeClassDef baseDef;
    baseDef.name = "Base";
    baseDef.constructor = baseCtorBody;
    baseDef.fields = {{"baseField", initOne}};
    Value* baseCtor = ctx->registerClass(baseDef);
    ASSERT_EQ(baseCtor->type()->kind(), TypeKind::Function);

    Value* proxy = ctx->createProxy(baseCtor, ctx->createObject());

    neo::core::NativeClassDef derivedDef;
    derivedDef.name = "Derived";
    derivedDef.constructor = derivedCtorBody;
    derivedDef.superCtor = proxy;
    Value* derivedCtor = ctx->registerClass(derivedDef);
    ASSERT_EQ(derivedCtor->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->classSuperConstructor(derivedCtor), proxy);
    EXPECT_EQ(ctx->getPrototype(derivedCtor), proxy);
    EXPECT_EQ(ctx->getPrototype(ctx->getProperty(derivedCtor, "prototype")),
              ctx->getProperty(baseCtor, "prototype"));

    Value* inst = ctx->construct(derivedCtor, 0, nullptr);
    ASSERT_TRUE(neo::core::detail::isObjectKind(inst->type()->kind()));
    EXPECT_EQ(numOf(ctx->getProperty(inst, "baseField")), 1.0);
    EXPECT_TRUE(boolOf(ctx->instanceOf(inst, derivedCtor)));
}

TEST(ProxyTest, ClassSuperConstructTrapReceivesNewTarget) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    neo::core::NativeClassDef baseDef;
    baseDef.name = "Base";
    baseDef.constructor = baseCtorBody;
    Value* baseCtor = ctx->registerClass(baseDef);
    ASSERT_EQ(baseCtor->type()->kind(), TypeKind::Function);

    Value* handler = ctx->createObject();
    defineNative(ctx, handler, "construct", makeResult);
    Value* proxy = ctx->createProxy(baseCtor, handler);

    neo::core::NativeClassDef derivedDef;
    derivedDef.name = "Derived";
    derivedDef.constructor = derivedCtorBody;
    derivedDef.superCtor = proxy;
    Value* derivedCtor = ctx->registerClass(derivedDef);
    ASSERT_EQ(derivedCtor->type()->kind(), TypeKind::Function);

    Value* inst = ctx->construct(derivedCtor, 0, nullptr);
    ASSERT_EQ(inst->type()->kind(), TypeKind::Object);
    EXPECT_TRUE(boolOf(ctx->getProperty(inst, "constructed")));
    // construct trap 的 newTarget 是派生类构造器。
    EXPECT_EQ(g_arg2, derivedCtor);
}

TEST(ProxyTest, PrivateFieldsDoNotForwardThroughProxy) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    g_private = ctx->createPrivateName("x");
    neo::core::NativeClassDef baseDef;
    baseDef.name = "Base";
    baseDef.constructor = baseCtorBody;
    baseDef.privateFields = {{g_private, initOne}};
    baseDef.methods = {{"read", readPrivateSelf, 0}};
    Value* baseCtor = ctx->registerClass(baseDef);
    ASSERT_EQ(baseCtor->type()->kind(), TypeKind::Function);

    Value* inst = ctx->construct(baseCtor, 0, nullptr);
    Value* read = ctx->getProperty(ctx->getProperty(baseCtor, "prototype"), "read");
    ASSERT_EQ(numOf(ctx->call(read, inst, 0, nullptr)), 1.0);

    // Proxy 不转发私有槽：target 已 brand，proxy 自身未 brand → TypeError。
    Value* proxy = ctx->createProxy(inst, ctx->createObject());
    EXPECT_EQ(ctx->call(read, proxy, 0, nullptr)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->getPrivate(proxy, g_private)->type()->kind(), TypeKind::Exception);

    // class extends Proxy：super() 经 construct trap 返回 proxy 时，该 proxy 成为 this，
    // 派生类私有字段初始化在 proxy 自身上（不转发 target）。
    neo::core::PrivateName* derivedPrivate = ctx->createPrivateName("y");
    g_stored = ctx->createProxy(ctx->createObject(), ctx->createObject());
    Value* superHandler = ctx->createObject();
    defineNative(ctx, superHandler, "construct", returnStored);
    neo::core::NativeClassDef derivedDef;
    derivedDef.name = "Derived";
    derivedDef.constructor = derivedCtorBody;
    derivedDef.superCtor = ctx->createProxy(baseCtor, superHandler);
    derivedDef.privateFields = {{derivedPrivate, initOne}};
    Value* derivedCtor = ctx->registerClass(derivedDef);
    ASSERT_EQ(derivedCtor->type()->kind(), TypeKind::Function);
    Value* derivedInst = ctx->construct(derivedCtor, 0, nullptr);
    EXPECT_EQ(derivedInst, g_stored);
    EXPECT_EQ(derivedInst->type()->kind(), TypeKind::Proxy);
    EXPECT_EQ(numOf(ctx->getPrivate(derivedInst, derivedPrivate)), 1.0);
    // Base 的私有字段 brand 在 target 上，proxy 自身不可见。
    EXPECT_EQ(ctx->getPrivate(derivedInst, g_private)->type()->kind(), TypeKind::Exception);
}

TEST(ProxyTest, RevokedAndGcStress) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    // 撤销后 target 仍被持有：GC 后所有内部方法继续抛 TypeError。
    Value* target = makeTargetWithX(ctx);
    Value* handler = ctx->createObject();
    defineNative(ctx, handler, "get", getTen);
    Value* proxy = ctx->createProxy(target, handler);
    revokeProxy(proxy);
    ctx->gc()->collect();
    EXPECT_EQ(ctx->getProperty(proxy, "x")->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->hasProperty(proxy, "x")->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->getPrototype(proxy)->type()->kind(), TypeKind::Exception);

    // 大量短命 proxy：反复创建/丢弃/回收，不应崩溃或泄漏 target。
    for (int round = 0; round < 50; ++round) {
        Value* t = makeTargetWithX(ctx);
        Value* h = ctx->createObject();
        defineNative(ctx, h, "get", getTen);
        Value* p = ctx->createProxy(t, h);
        EXPECT_EQ(numOf(ctx->getProperty(p, "x")), 10.0);
    }
    ctx->gc()->collect();

    // proxy 套 proxy：内层撤销后外层读取透传 TypeError。
    Value* inner = ctx->createProxy(makeTargetWithX(ctx), ctx->createObject());
    Value* outer = ctx->createProxy(inner, ctx->createObject());
    EXPECT_EQ(numOf(ctx->getProperty(outer, "x")), 1.0);
    revokeProxy(inner);
    EXPECT_EQ(ctx->getProperty(outer, "x")->type()->kind(), TypeKind::Exception);
}

TEST(ProxyTest, SurvivesGcAndKeepsTargetAlive) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* proxy = nullptr;
    {
        Value* target = makeTargetWithX(ctx);
        Value* handler = ctx->createObject();
        defineNative(ctx, handler, "get", getTen);
        proxy = ctx->createProxy(target, handler);
    }
    ctx->gc()->collect();
    ASSERT_NE(proxy, nullptr);
    EXPECT_EQ(numOf(ctx->getProperty(proxy, "x")), 10.0);
}
