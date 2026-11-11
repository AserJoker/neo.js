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

Value* makeResult(IContext* ctx, Value*, size_t, Value*[]) {
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
