// ReflectTest.cpp - Reflect 命名空间（ES2024 §28.1）
//
// 验证：
// - 命名空间形状与 @@toStringTag
// - get/set 的 receiver 语义（访问器 this、写入落到 receiver）
// - has / ownKeys / getOwnPropertyDescriptor / deleteProperty
// - defineProperty / setPrototypeOf 返回 Boolean 而非抛错
// - isExtensible / preventExtensions / getPrototypeOf
// - apply / construct（含 newTarget）
// - 非对象 target → TypeError
// - Proxy trap 分派与 receiver 透传

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/WellKnownSymbol.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using neo::core::BooleanData;
using neo::core::IContext;
using neo::core::NumberData;
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
bool boolOf(Value* v) { return static_cast<BooleanData*>(v->data())->value; }
std::string strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }

Value* reflectOf(IContext* ctx) { return ctx->global()->lookup("Reflect"); }

Value* callReflect(IContext* ctx, const char* name, size_t argc, Value* argv[]) {
    Value* ns = reflectOf(ctx);
    Value* fn = ctx->getProperty(ns, name);
    return ctx->call(fn, ns, argc, argv);
}

Value* returnsThis(IContext* /*ctx*/, Value* self, size_t, Value*[]) { return self; }

Value* getTen(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createNumber(10); }

Value* makeArgs(IContext* ctx, std::initializer_list<Value*> items) {
    Value* arr = ctx->createArray();
    auto* d = static_cast<neo::core::ArrayData*>(arr->data());
    uint32_t i = 0;
    for (Value* v : items) d->setElement(i++, v);
    return arr;
}

Value* addOne(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createNumber(1);
    return ctx->createNumber(numOf(argv[0]) + 1);
}

}  // namespace

TEST(ReflectTest, NamespaceShape) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* ns = reflectOf(ctx);
    ASSERT_NE(ns, nullptr);
    EXPECT_EQ(ns->type()->kind(), TypeKind::Object);
    EXPECT_EQ(strOf(ctx->getProperty(ns, ctx->getWellKnownSymbol(
                                           neo::core::WellKnownSymbol::ToStringTag))),
              "Reflect");

    for (const char* name : {"apply", "construct", "defineProperty", "deleteProperty",
                             "get", "getOwnPropertyDescriptor", "getPrototypeOf", "has",
                             "isExtensible", "ownKeys", "preventExtensions", "set",
                             "setPrototypeOf"}) {
        Value* fn = ctx->getProperty(ns, name);
        ASSERT_EQ(fn->type()->kind(), TypeKind::Function) << name;
    }
}

TEST(ReflectTest, GetAndSetUseReceiver) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    // target.x 是访问器：getter 返回 this；Reflect.get 的 receiver 作为 this。
    Value* target = ctx->createObject();
    Value* getter = ctx->createNativeFunction(returnsThis, 0);
    ctx->defineAccessor(target, "x", getter, nullptr, true, true);
    Value* receiver = ctx->createObject();

    Value* getArgs[3] = {target, ctx->createString("x"), receiver};
    EXPECT_EQ(callReflect(ctx, "get", 3, getArgs), receiver);

    // 无 receiver 时默认 target。
    Value* getArgs2[2] = {target, ctx->createString("x")};
    EXPECT_EQ(callReflect(ctx, "get", 2, getArgs2), target);

    // Reflect.set 的 receiver 决定写入位置。
    Value* setArgs[4] = {target, ctx->createString("y"), ctx->createNumber(5), receiver};
    Value* r = callReflect(ctx, "set", 4, setArgs);
    ASSERT_EQ(r->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(r));
    EXPECT_EQ(numOf(ctx->getProperty(receiver, "y")), 5.0);
    EXPECT_EQ(ctx->getProperty(target, "y")->type()->kind(), TypeKind::Undefined);
}

TEST(ReflectTest, PropertyQueriesAndDelete) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createObject();
    ctx->defineProperty(target, "x", ctx->createNumber(1), true, true, true);
    Value* sym = ctx->createSymbol("s");
    ctx->defineProperty(target, sym, ctx->createNumber(2), true, true, true);

    // has
    Value* hasArgs[2] = {target, ctx->createString("x")};
    Value* has = callReflect(ctx, "has", 2, hasArgs);
    ASSERT_EQ(has->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(has));

    // ownKeys：字符串 + symbol
    Value* ownArgs[1] = {target};
    Value* keys = callReflect(ctx, "ownKeys", 1, ownArgs);
    ASSERT_EQ(keys->type()->kind(), TypeKind::Array);
    EXPECT_EQ(numOf(ctx->getProperty(keys, "length")), 2.0);
    EXPECT_EQ(strOf(ctx->getProperty(keys, "0")), "x");
    EXPECT_EQ(ctx->getProperty(keys, "1"), sym);

    // getOwnPropertyDescriptor
    Value* descArgs[2] = {target, ctx->createString("x")};
    Value* desc = callReflect(ctx, "getOwnPropertyDescriptor", 2, descArgs);
    ASSERT_EQ(desc->type()->kind(), TypeKind::Object);
    EXPECT_EQ(numOf(ctx->getProperty(desc, "value")), 1.0);

    // deleteProperty
    Value* delArgs[2] = {target, ctx->createString("x")};
    Value* del = callReflect(ctx, "deleteProperty", 2, delArgs);
    ASSERT_EQ(del->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(del));
    EXPECT_FALSE(boolOf(ctx->hasProperty(target, "x")));
}

TEST(ReflectTest, DefinePropertyReturnsBoolean) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createObject();
    Value* desc = ctx->createObject();
    ctx->setProperty(desc, "value", ctx->createNumber(3));
    ctx->setProperty(desc, "configurable", ctx->createBoolean(false));
    Value* args[3] = {target, ctx->createString("x"), desc};
    Value* r = callReflect(ctx, "defineProperty", 3, args);
    ASSERT_EQ(r->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(r));
    EXPECT_EQ(numOf(ctx->getProperty(target, "x")), 3.0);

    // 不可配置属性不兼容 → false（而非抛错）。
    Value* badDesc = ctx->createObject();
    ctx->setProperty(badDesc, "value", ctx->createNumber(4));
    Value* badArgs[3] = {target, ctx->createString("x"), badDesc};
    Value* r2 = callReflect(ctx, "defineProperty", 3, badArgs);
    ASSERT_EQ(r2->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(boolOf(r2));
    EXPECT_EQ(numOf(ctx->getProperty(target, "x")), 3.0);

    // 不可扩展对象新增属性 → false。
    Value* extArgs[1] = {target};
    callReflect(ctx, "preventExtensions", 1, extArgs);
    Value* desc2 = ctx->createObject();
    ctx->setProperty(desc2, "value", ctx->createNumber(1));
    Value* args2[3] = {target, ctx->createString("y"), desc2};
    Value* r3 = callReflect(ctx, "defineProperty", 3, args2);
    ASSERT_EQ(r3->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(boolOf(r3));
}

TEST(ReflectTest, PrototypeAndExtensibility) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createObject();
    Value* proto = ctx->createObject();
    Value* setProtoArgs[2] = {target, proto};
    Value* r = callReflect(ctx, "setPrototypeOf", 2, setProtoArgs);
    ASSERT_EQ(r->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(r));

    Value* getProtoArgs[1] = {target};
    EXPECT_EQ(callReflect(ctx, "getPrototypeOf", 1, getProtoArgs), proto);

    // 非对象且非 null 的原型 → TypeError。
    Value* badProtoArgs[2] = {target, ctx->createNumber(1)};
    EXPECT_EQ(callReflect(ctx, "setPrototypeOf", 2, badProtoArgs)->type()->kind(),
              TypeKind::Exception);

    Value* extArgs[1] = {target};
    Value* ext = callReflect(ctx, "isExtensible", 1, extArgs);
    ASSERT_EQ(ext->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(ext));
    Value* prev = callReflect(ctx, "preventExtensions", 1, extArgs);
    ASSERT_EQ(prev->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(prev));
    EXPECT_FALSE(boolOf(callReflect(ctx, "isExtensible", 1, extArgs)));
}

TEST(ReflectTest, ApplyAndConstruct) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* fn = ctx->createNativeFunction(addOne, 0);
    Value* argsList = makeArgs(ctx, {ctx->createNumber(4)});
    Value* applyArgs[3] = {fn, ctx->createUndefined(), argsList};
    Value* r = callReflect(ctx, "apply", 3, applyArgs);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r), 5.0);

    // construct：newTarget 决定实例原型。
    Value* ctor = ctx->createNativeFunction(addOne, 0);
    Value* proto = ctx->createObject();
    ctx->defineProperty(ctor, "prototype", proto, true, false, false);
    Value* newTarget = ctx->createNativeFunction(addOne, 0);
    Value* newProto = ctx->createObject();
    ctx->defineProperty(newTarget, "prototype", newProto, true, false, false);
    Value* emptyArgs = makeArgs(ctx, {});
    Value* constructArgs[3] = {ctor, emptyArgs, newTarget};
    Value* obj = callReflect(ctx, "construct", 3, constructArgs);
    ASSERT_EQ(obj->type()->kind(), TypeKind::Object);
    EXPECT_EQ(ctx->getPrototype(obj), newProto);

    // 非构造器 → TypeError。
    Value* badArgs[2] = {ctx->createObject(), emptyArgs};
    EXPECT_EQ(callReflect(ctx, "construct", 2, badArgs)->type()->kind(),
              TypeKind::Exception);
}

TEST(ReflectTest, RequiresObjectTarget) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* prim = ctx->createNumber(1);
    Value* key = ctx->createString("x");
    Value* args[3] = {prim, key, prim};
    for (const char* name : {"get", "has", "ownKeys", "getOwnPropertyDescriptor",
                             "getPrototypeOf", "isExtensible", "preventExtensions",
                             "deleteProperty", "defineProperty", "set",
                             "setPrototypeOf"}) {
        EXPECT_EQ(callReflect(ctx, name, 3, args)->type()->kind(), TypeKind::Exception)
            << name;
    }
}

TEST(ReflectTest, DispatchesProxyTrapsWithReceiver) {
    Fixture f;
    IContext* ctx = f.ctx.get();

    Value* target = ctx->createObject();
    Value* handler = ctx->createObject();
    Value* getTrap = ctx->createNativeFunction(getTen, 0);
    ctx->defineProperty(handler, "get", getTrap, true, true, true);
    Value* proxy = ctx->createProxy(target, handler);

    Value* receiver = ctx->createObject();
    Value* args[3] = {proxy, ctx->createString("x"), receiver};
    Value* r = callReflect(ctx, "get", 3, args);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(r), 10.0);

    // has trap 缺失 → 回退 target 的普通算法。
    Value* hasArgs[2] = {proxy, ctx->createString("x")};
    Value* has = callReflect(ctx, "has", 2, hasArgs);
    ASSERT_EQ(has->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(boolOf(has));
}
