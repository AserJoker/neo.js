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
    EXPECT_EQ(d->prototype, nullptr);
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
    EXPECT_EQ(visitor.count, 2);  // 两个属性 value（无 prototype）
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

TEST(ObjectTest, CloneCopiesContainers) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "x", ctx->createNumber(1));
    auto* d = static_cast<ObjectData*>(obj->data());

    IData* cloned = d->clone(alloc.get());
    ASSERT_NE(cloned, nullptr);
    auto* cd = static_cast<ObjectData*>(cloned);
    EXPECT_EQ(cd->properties.size(), 1u);
    EXPECT_EQ(cd->order.size(), 1u);
    EXPECT_EQ(cd->properties.at("x").value, d->properties.at("x").value);  // 共享 value
    // placement new 对象需显式析构（含 std::unordered_map/vector/string 内部缓冲区）
    cd->~ObjectData();
    alloc->free(cloned);
}
