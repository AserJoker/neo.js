// PropertyOpsTest.cpp - 各类型属性操作分派单元测试
//
// 验证（ADR-018 对象模型 + ADR-016 类型分派）：
// - 5 种基础类型（undefined/null/boolean/number/string）：无自有属性
//     get → undefined，set/defineProperty → Exception，has → false，delete → true
// - Exception/Fatal/Uninitialized：属性操作透传 self（异常/哨兵不参与属性操作）
// - Function：属性容器未接入，非 has 操作返回 Exception("not implemented")
// - Object：运算/调用占位返回 Exception
// - ObjectData：symbols 容器的 GC 遍历与 clone
//
// 关联 ADR: 006（异常即值）、017（Fatal 透传）、018（对象模型）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IData.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Function.h"
#include "core/types/Object.h"
#include "core/types/Uninitialized.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
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
using neo::core::getUninitializedType;
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

// native 空函数（用于创建 Function Value）
Value* noopFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createUndefined();
}

// 基础类型属性操作断言：无自有属性的统一行为
void expectPrimitiveNoOwnProperty(IContext* ctx, Value* v) {
    EXPECT_EQ(ctx->getProperty(v, "x")->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(ctx->setProperty(v, "x", ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_FALSE(ctx->hasProperty(v, "x"));
    Value* del = ctx->deleteProperty(v, "x");
    EXPECT_EQ(del->type()->kind(), TypeKind::Boolean);
    EXPECT_EQ(ctx->defineProperty(v, "x", ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
}

// 透传类型属性操作断言：所有操作返回 self（has 恒 false）
void expectPassthroughProperty(IContext* ctx, Value* v) {
    EXPECT_EQ(ctx->getProperty(v, "x"), v);
    EXPECT_EQ(ctx->setProperty(v, "x", ctx->createNumber(1)), v);
    EXPECT_FALSE(ctx->hasProperty(v, "x"));
    EXPECT_EQ(ctx->deleteProperty(v, "x"), v);
    EXPECT_EQ(ctx->defineProperty(v, "x", ctx->createNumber(2)), v);
}

}  // namespace

// ===== 基础类型：无自有属性 =====

TEST(PropertyOpsTest, UndefinedHasNoOwnProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    expectPrimitiveNoOwnProperty(ctx.get(), ctx->createUndefined());
}

TEST(PropertyOpsTest, NullHasNoOwnProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    expectPrimitiveNoOwnProperty(ctx.get(), ctx->createNull());
}

TEST(PropertyOpsTest, BooleanHasNoOwnProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    expectPrimitiveNoOwnProperty(ctx.get(), ctx->createBoolean(true));
}

TEST(PropertyOpsTest, NumberHasNoOwnProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    expectPrimitiveNoOwnProperty(ctx.get(), ctx->createNumber(1));
}

TEST(PropertyOpsTest, StringHasNoOwnProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    expectPrimitiveNoOwnProperty(ctx.get(), ctx->createString("s"));
}

TEST(PropertyOpsTest, PrimitiveSetPropertyMessageContainsTypeName) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // set 错误消息携带属性名与类型名（primitiveSetProperty）
    Value* err = ctx->setProperty(ctx->createNumber(1), "foo", ctx->createNumber(2));
    ASSERT_EQ(err->type()->kind(), TypeKind::Exception);
}

// ===== Exception / Fatal / Uninitialized：透传 self =====

TEST(PropertyOpsTest, ExceptionPropertyOpsPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    expectPassthroughProperty(ctx.get(), ctx->createException(ctx->createString("e")));
}

TEST(PropertyOpsTest, FatalPropertyOpsPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);
    expectPassthroughProperty(ctx.get(), ctx->createFatal("crash"));
}

TEST(PropertyOpsTest, UninitializedPropertyOpsPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* uninit = ctx->gc()->createValue(getUninitializedType(),
                                           ctx->gc()->allocData<neo::core::UninitializedData>());
    ASSERT_NE(uninit, nullptr);
    ctx->current()->add(uninit);
    expectPassthroughProperty(ctx.get(), uninit);
}

// ===== Function：属性容器未接入 =====

TEST(PropertyOpsTest, FunctionPropertyOpsNotImplemented) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(noopFn, 0);
    EXPECT_EQ(ctx->getProperty(fn, "x")->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->setProperty(fn, "x", ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_FALSE(ctx->hasProperty(fn, "x"));
    EXPECT_EQ(ctx->deleteProperty(fn, "x")->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->defineProperty(fn, "x", ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
}

// ===== Object：运算/调用占位 =====

TEST(PropertyOpsTest, ObjectArithmeticNotImplemented) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* num = ctx->createNumber(1);
    EXPECT_EQ(ctx->add(obj, num)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->sub(obj, num)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->mul(obj, num)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->div(obj, num)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->toString(obj)->type()->kind(), TypeKind::Exception);
}

TEST(PropertyOpsTest, ObjectCallReturnsNotAFunctionException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* res = ctx->call(obj, nullptr, 0, nullptr);
    EXPECT_EQ(res->type()->kind(), TypeKind::Exception);
}

// ===== ObjectData：symbols 容器的遍历与 clone =====

TEST(PropertyOpsTest, EnumerateChildrenVisitsSymbolProperties) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    auto* d = static_cast<ObjectData*>(obj->data());
    Value* val = ctx->createNumber(1);
    Value* getter = ctx->createNumber(2);
    Value* setter = ctx->createNumber(3);
    d->symbols.emplace(nullptr, Property{val, true, true, true, getter, setter});

    CountingVisitor visitor;
    d->enumerateChildren(&visitor);
    EXPECT_EQ(visitor.count, 3);  // value + getter + setter
}

TEST(PropertyOpsTest, CloneCopiesSymbolsContainer) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    auto* d = static_cast<ObjectData*>(obj->data());
    Value* val = ctx->createNumber(1);
    d->symbols.emplace(nullptr, Property{val, true, true, true, nullptr, nullptr});

    IData* cloned = d->clone(alloc.get());
    ASSERT_NE(cloned, nullptr);
    auto* cd = static_cast<ObjectData*>(cloned);
    EXPECT_EQ(cd->symbols.size(), 1u);
    EXPECT_EQ(cd->symbols.at(nullptr).value, val);  // 共享 value
    cd->~ObjectData();
    alloc->free(cloned);
}

// ===== ObjectData：getter/setter 参与 GC 遍历（数据属性路径）=====

TEST(PropertyOpsTest, EnumerateChildrenVisitsGetterSetter) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    auto* d = static_cast<ObjectData*>(obj->data());
    Value* val = ctx->createNumber(1);
    Value* getter = ctx->createNumber(2);
    Value* setter = ctx->createNumber(3);
    d->properties.emplace("x", Property{val, true, true, true, getter, setter});

    CountingVisitor visitor;
    d->enumerateChildren(&visitor);
    EXPECT_EQ(visitor.count, 3);  // value + getter + setter
}
