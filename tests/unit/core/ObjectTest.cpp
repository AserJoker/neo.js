// ObjectTest.cpp - Object 类型与属性操作单元测试
//
// 验证（ADR-018 对象模型）：
// - ObjectType 单例、kind/name
// - createObject：空对象（properties/order/symbols 空，prototype=nullptr）
// - get/set/has/delete/defineProperty 五个基础操作
// - order 保序遍历
// - 描述符特性（writable/enumerable/configurable）
// - GC 遍历（prototype + property value 被标记）
// - 非对象类型的属性操作（get→undefined，set→Exception，has→false）
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IData.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Object.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "infra/IAllocator.h"
#include "infra/Console.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

namespace {

using neo::core::IContext;
using neo::core::IData;
using neo::core::IGCVisitor;
using neo::core::ObjectData;
using neo::core::Property;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::getObjectType;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

// 计数访问器：统计 GC 遍历到的子 value 数
class CountingVisitor : public IGCVisitor {
public:
    int count = 0;
    void visit(Value* /*v*/) override { ++count; }
};

}  // namespace

// ===== 类型基础 =====

TEST(ObjectTest, KindAndName) {
    EXPECT_EQ(getObjectType()->kind(), TypeKind::Object);
    EXPECT_STREQ(getObjectType()->name(), "object");
}

TEST(ObjectTest, SingletonIdentity) {
    EXPECT_EQ(getObjectType(), getObjectType());
}

// ===== 创建 =====

TEST(ObjectTest, CreateEmptyObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->type()->kind(), TypeKind::Object);

    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_TRUE(d->properties.empty());
    EXPECT_TRUE(d->order.empty());
    EXPECT_TRUE(d->symbols.empty());
    // 自动挂 Object.prototype（ADR-035 完整原型链）
    EXPECT_EQ(d->prototype, ctx->objectPrototype());
}

TEST(ObjectTest, CreateObjectWithBindName) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject("o");
    EXPECT_EQ(ctx->current()->lookup("o"), obj);
}

// ===== 属性基础操作 =====

TEST(ObjectTest, SetAndGetProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* val = ctx->createNumber(42);

    ctx->setProperty(obj, "x", val);
    EXPECT_EQ(ctx->getProperty(obj, "x"), val);
}

TEST(ObjectTest, GetMissingPropertyReturnsUndefined) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* v = ctx->getProperty(obj, "nope");
    EXPECT_EQ(v->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectTest, HasProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    EXPECT_FALSE(ctx->hasProperty(obj, "x"));

    ctx->setProperty(obj, "x", ctx->createNumber(1));
    EXPECT_TRUE(ctx->hasProperty(obj, "x"));
}

TEST(ObjectTest, DeleteProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "x", ctx->createNumber(1));

    Value* result = ctx->deleteProperty(obj, "x");
    EXPECT_EQ(result->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(ctx->hasProperty(obj, "x"));

    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_TRUE(d->order.empty());  // order 同步移除
}

TEST(ObjectTest, DeleteMissingPropertySucceeds) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* result = ctx->deleteProperty(obj, "nope");
    EXPECT_EQ(result->type()->kind(), TypeKind::Boolean);
}

// ===== order 保序 =====

TEST(ObjectTest, OrderPreservesInsertion) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "b", ctx->createNumber(1));
    ctx->setProperty(obj, "a", ctx->createNumber(2));
    ctx->setProperty(obj, "c", ctx->createNumber(3));

    auto* d = static_cast<ObjectData*>(obj->data());
    ASSERT_EQ(d->order.size(), 3u);
    EXPECT_EQ(d->order[0], "b");
    EXPECT_EQ(d->order[1], "a");
    EXPECT_EQ(d->order[2], "c");
}

TEST(ObjectTest, SettingExistingKeyDoesNotDuplicateOrder) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "x", ctx->createNumber(1));
    ctx->setProperty(obj, "x", ctx->createNumber(2));

    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->order.size(), 1u);
    EXPECT_DOUBLE_EQ(static_cast<NumberData*>(ctx->getProperty(obj, "x")->data())->value, 2.0);
}

// ===== 描述符特性 =====

TEST(ObjectTest, DefinePropertyWithDescriptor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* val = ctx->createNumber(7);
    ctx->defineProperty(obj, "x", val, /*writable=*/false, /*enumerable=*/false,
                        /*configurable=*/false);

    auto* d = static_cast<ObjectData*>(obj->data());
    const Property& p = d->properties.at("x");
    EXPECT_EQ(p.value, val);
    EXPECT_FALSE(p.writable);
    EXPECT_FALSE(p.enumerable);
    EXPECT_FALSE(p.configurable);
}

TEST(ObjectTest, SetNonWritablePropertyReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "x", ctx->createNumber(1), /*writable=*/false);

    Value* result = ctx->setProperty(obj, "x", ctx->createNumber(2));
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
    // 原值不变
    EXPECT_DOUBLE_EQ(static_cast<NumberData*>(ctx->getProperty(obj, "x")->data())->value, 1.0);
}

TEST(ObjectTest, DeleteNonConfigurableReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "x", ctx->createNumber(1), true, true, /*configurable=*/false);

    Value* result = ctx->deleteProperty(obj, "x");
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
    EXPECT_TRUE(ctx->hasProperty(obj, "x"));
}

TEST(ObjectTest, DefineExistingPropertyUpdatesDescriptor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "x", ctx->createNumber(1));
    ctx->defineProperty(obj, "x", ctx->createNumber(2), false, false, false);

    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->order.size(), 1u);  // 不重复
    const Property& p = d->properties.at("x");
    EXPECT_DOUBLE_EQ(static_cast<NumberData*>(p.value->data())->value, 2.0);
    EXPECT_FALSE(p.writable);
}

// ===== 非对象类型属性操作 =====

TEST(ObjectTest, PrimitiveGetPropertyReturnsUndefined) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* v = ctx->getProperty(ctx->createNumber(1), "x");
    EXPECT_EQ(v->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectTest, PrimitiveSetPropertyReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* result = ctx->setProperty(ctx->createNumber(1), "x", ctx->createNumber(2));
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

TEST(ObjectTest, PrimitiveHasPropertyIsFalse) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_FALSE(ctx->hasProperty(ctx->createString("s"), "length"));
}

// ===== GC 遍历 =====

TEST(ObjectTest, EnumerateChildrenVisitsPropertyValues) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "a", ctx->createNumber(1));
    ctx->setProperty(obj, "b", ctx->createNumber(2));

    auto* d = static_cast<ObjectData*>(obj->data());
    CountingVisitor visitor;
    d->enumerateChildren(&visitor);
    EXPECT_EQ(visitor.count, 3);  // 两个属性 value + prototype（Object.prototype）
}

TEST(ObjectTest, EnumerateChildrenVisitsPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* proto = ctx->createObject();
    static_cast<ObjectData*>(obj->data())->prototype = proto;

    CountingVisitor visitor;
    static_cast<ObjectData*>(obj->data())->enumerateChildren(&visitor);
    EXPECT_EQ(visitor.count, 1);  // prototype
}

TEST(ObjectTest, PropertyValuesSurviveGcWhileObjectAlive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    // 属性值不绑定到 scope（仅被对象持有），靠 GC 遍历保活
    Value* val = ctx->createNumber(42);
    ctx->setProperty(obj, "x", val);

    ctx->gc()->collect();
    // obj 在 scope 中 → 被标记 → 遍历到属性 value → val 保活
    EXPECT_EQ(ctx->getProperty(obj, "x"), val);
    EXPECT_DOUBLE_EQ(static_cast<NumberData*>(val->data())->value, 42.0);
}

// ===== clone =====

TEST(ObjectTest, CloneSharesData) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "x", ctx->createNumber(1));
    auto* d = static_cast<ObjectData*>(obj->data());
    int refBefore = d->ref();

    // 对象是引用语义（ADR-019 §2）：clone 不复制容器，ref++ 返回同一 data
    IData* cloned = d->clone(alloc.get());
    EXPECT_EQ(cloned, d);
    EXPECT_EQ(d->ref(), refBefore + 1);
    // 撤销 clone 引入的引用，避免 data 泄漏
    d->refDec();
}

// ===== 原型链（ADR-018 §1）=====

TEST(ObjectTest, NewObjectHasObjectPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 新对象自动挂 Object.prototype（ADR-035）
    Value* obj = ctx->createObject();
    EXPECT_EQ(ctx->getPrototype(obj), ctx->objectPrototype());
}

TEST(ObjectTest, SetAndGetPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* proto = ctx->createObject();
    EXPECT_EQ(ctx->setPrototype(obj, proto), obj);
    EXPECT_EQ(ctx->getPrototype(obj), proto);
}

TEST(ObjectTest, SetPrototypeNullClears) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, ctx->createObject());
    ctx->setPrototype(obj, ctx->createNull());
    EXPECT_EQ(ctx->getPrototype(obj)->type()->kind(), TypeKind::Null);
}

TEST(ObjectTest, SetPrototypeRejectsNonObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    EXPECT_EQ(ctx->setPrototype(obj, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->setPrototype(obj, ctx->createUndefined())->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectTest, SetPrototypeRejectsCycle) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* a = ctx->createObject();
    Value* b = ctx->createObject();
    ctx->setPrototype(b, a);  // b → a
    // a → b 会成环
    EXPECT_EQ(ctx->setPrototype(a, b)->type()->kind(), TypeKind::Exception);
    // 原型未变（仍是 Object.prototype）
    EXPECT_EQ(ctx->getPrototype(a), ctx->objectPrototype());
}

TEST(ObjectTest, SetPrototypeRejectsSelfCycle) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    EXPECT_EQ(ctx->setPrototype(obj, obj)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectTest, GetPropertyFindsInheritedData) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    Value* val = ctx->createNumber(7);
    ctx->setProperty(proto, "x", val);

    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);
    EXPECT_EQ(ctx->getProperty(obj, "x"), val);
}

TEST(ObjectTest, GetPropertyWalksMultiLevelChain) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* grand = ctx->createObject();
    Value* val = ctx->createNumber(9);
    ctx->setProperty(grand, "deep", val);
    Value* mid = ctx->createObject();
    ctx->setPrototype(mid, grand);
    Value* leaf = ctx->createObject();
    ctx->setPrototype(leaf, mid);

    EXPECT_EQ(ctx->getProperty(leaf, "deep"), val);
}

TEST(ObjectTest, OwnPropertyShadowsPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    ctx->setProperty(proto, "x", ctx->createNumber(1));
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);
    Value* own = ctx->createNumber(2);
    ctx->setProperty(obj, "x", own);

    EXPECT_EQ(ctx->getProperty(obj, "x"), own);
}

TEST(ObjectTest, HasPropertyIncludesPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    ctx->setProperty(proto, "x", ctx->createNumber(1));
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);

    EXPECT_TRUE(ctx->hasProperty(obj, "x"));       // 原型链上
    EXPECT_FALSE(ctx->hasProperty(obj, "y"));
}

TEST(ObjectTest, SetCreatesOwnPropertyWhenInheritedWritable) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    ctx->setProperty(proto, "x", ctx->createNumber(1));  // 默认可写
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);

    Value* own = ctx->createNumber(2);
    EXPECT_EQ(ctx->setProperty(obj, "x", own), own);
    // 在自身上新建，原型不受影响
    EXPECT_EQ(ctx->getProperty(obj, "x"), own);
    EXPECT_DOUBLE_EQ(
        static_cast<NumberData*>(ctx->getProperty(proto, "x")->data())->value, 1.0);
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->properties.count("x"), 1u);
}

TEST(ObjectTest, SetRejectsReadOnlyInheritedProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    ctx->defineProperty(proto, "x", ctx->createNumber(1), /*writable=*/false);
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);

    EXPECT_EQ(ctx->setProperty(obj, "x", ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
    // 未在自身上创建
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->properties.count("x"), 0u);
}

TEST(ObjectTest, DeleteDoesNotAffectPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    ctx->setProperty(proto, "x", ctx->createNumber(1));
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);

    // delete 只删自有属性，原型上的 x 保留
    EXPECT_EQ(ctx->deleteProperty(obj, "x")->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(ctx->hasProperty(obj, "x"));
}

TEST(ObjectTest, DefinePropertyOnlyDefinesOwn) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    ctx->setProperty(proto, "x", ctx->createNumber(1));
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);

    ctx->defineProperty(obj, "x", ctx->createNumber(2), false, false, false);
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->properties.count("x"), 1u);
    EXPECT_FALSE(d->properties.at("x").writable);
    // 原型不受影响
    auto* pd = static_cast<ObjectData*>(proto->data());
    EXPECT_TRUE(pd->properties.at("x").writable);
}

TEST(ObjectTest, PrototypeSurvivesGcWhileObjectAlive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* proto = ctx->createObject();
    ctx->setProperty(proto, "x", ctx->createNumber(5));
    ctx->setPrototype(obj, proto);

    ctx->gc()->collect();
    // obj 在 scope → 标记 → 遍历 prototype → proto 保活 → 其属性值也保活
    EXPECT_EQ(ctx->getPrototype(obj), proto);
    EXPECT_EQ(ctx->getProperty(obj, "x")->type()->kind(), TypeKind::Number);
}

TEST(ObjectTest, PrimitivePrototypeOps) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 非对象：get 返回 undefined，set 返回 Exception
    Value* num = ctx->createNumber(1);
    EXPECT_EQ(ctx->getPrototype(num)->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(ctx->setPrototype(num, ctx->createObject())->type()->kind(),
              TypeKind::Exception);
}

// ===== Symbol 作为属性键（ToPropertyKey，ES2024 §7.1.19）=====

TEST(ObjectTest, SymbolKeySetAndGet) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    Value* val = ctx->createNumber(42);

    ctx->setProperty(obj, sym, val);
    EXPECT_EQ(ctx->getProperty(obj, sym), val);
    EXPECT_TRUE(ctx->hasProperty(obj, sym));

    // 走 symbols 容器，不进 properties
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_TRUE(d->properties.empty());
    EXPECT_EQ(d->symbols.size(), 1u);
}

TEST(ObjectTest, SameDescriptionSymbolsAreDistinctKeys) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* a = ctx->createSymbol("k");
    Value* b = ctx->createSymbol("k");
    ctx->setProperty(obj, a, ctx->createNumber(1));

    // 同描述但不同 symbol → 不同键
    EXPECT_TRUE(ctx->hasProperty(obj, a));
    EXPECT_FALSE(ctx->hasProperty(obj, b));
    EXPECT_EQ(ctx->getProperty(obj, b)->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectTest, SymbolKeyDoesNotCollideWithStringKey) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    Value* strVal = ctx->createNumber(1);
    Value* symVal = ctx->createNumber(2);

    ctx->setProperty(obj, "k", strVal);
    ctx->setProperty(obj, sym, symVal);

    // 字符串键与 symbol 键互不干扰
    EXPECT_EQ(ctx->getProperty(obj, "k"), strVal);
    EXPECT_EQ(ctx->getProperty(obj, sym), symVal);
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->properties.size(), 1u);
    EXPECT_EQ(d->symbols.size(), 1u);
}

TEST(ObjectTest, NonStringPrimitiveKeysCoerceToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* numKey = ctx->createNumber(1);
    Value* val = ctx->createNumber(7);
    ctx->setProperty(obj, numKey, val);

    // 1 和 "1" 是同一个键
    EXPECT_EQ(ctx->getProperty(obj, "1"), val);
    EXPECT_TRUE(ctx->hasProperty(obj, "1"));
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->properties.count("1"), 1u);
}

TEST(ObjectTest, SymbolKeyDelete) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->setProperty(obj, sym, ctx->createNumber(1));

    EXPECT_EQ(ctx->deleteProperty(obj, sym)->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(ctx->hasProperty(obj, sym));
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_TRUE(d->symbols.empty());
    EXPECT_TRUE(d->symbolOrder.empty());
}

TEST(ObjectTest, SymbolKeyDefinePropertyDescriptor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    Value* val = ctx->createNumber(1);
    ctx->defineProperty(obj, sym, val, /*writable=*/false, /*enumerable=*/false,
                        /*configurable=*/false);

    auto* d = static_cast<ObjectData*>(obj->data());
    const auto& sp = d->symbols.at(sym->data());
    EXPECT_EQ(sp.key, sym);
    EXPECT_EQ(sp.prop.value, val);
    EXPECT_FALSE(sp.prop.writable);
    // 不可写 → set 报错
    EXPECT_EQ(ctx->setProperty(obj, sym, ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectTest, SymbolKeyInheritedThroughPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    Value* val = ctx->createNumber(9);
    ctx->setProperty(proto, sym, val);

    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);

    // symbol 键同样沿原型链查找
    EXPECT_EQ(ctx->getProperty(obj, sym), val);
    EXPECT_TRUE(ctx->hasProperty(obj, sym));
}

TEST(ObjectTest, SymbolKeySurvivesGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    Value* val = ctx->createNumber(5);
    ctx->setProperty(obj, sym, val);

    // symbol 键只被对象持有（未绑定 scope），靠 enumerateChildren 保活
    ctx->gc()->collect();
    EXPECT_EQ(ctx->getProperty(obj, sym), val);
    EXPECT_EQ(static_cast<NumberData*>(val->data())->value, 5.0);
}
