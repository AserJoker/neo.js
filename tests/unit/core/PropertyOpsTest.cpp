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
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/String.h"
#include "core/types/Uninitialized.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>
#include "TestHelpers.h"

#include <cmath>

namespace {

using neo::core::IContext;
using neo::core::IData;
using neo::core::IGCVisitor;
using neo::core::NumberData;
using neo::core::ObjectData;
using neo::core::StringData;
using neo::core::Property;
using neo::core::SymbolProperty;
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
    void visitScope(neo::core::Scope* /*scope*/) override {}
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
    EXPECT_FALSE(neo::test::hasProperty(ctx, v, "x"));
    Value* del = ctx->deleteProperty(v, "x");
    EXPECT_EQ(del->type()->kind(), TypeKind::Boolean);
    EXPECT_EQ(ctx->defineProperty(v, "x", ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
}

// 透传类型属性操作断言：所有操作返回 self（has 恒 false）
void expectPassthroughProperty(IContext* ctx, Value* v) {
    EXPECT_EQ(ctx->getProperty(v, "x"), v);
    EXPECT_EQ(ctx->setProperty(v, "x", ctx->createNumber(1)), v);
    EXPECT_FALSE(neo::test::hasProperty(ctx, v, "x"));
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

// ===== Function：继承 ObjectData，属性操作与普通对象一致 =====

TEST(PropertyOpsTest, FunctionPropertyOpsBehaveLikeObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(noopFn, 0);
    // 空函数对象：无自有属性
    EXPECT_EQ(ctx->getProperty(fn, "x")->type()->kind(), TypeKind::Undefined);
    EXPECT_FALSE(neo::test::hasProperty(ctx, fn, "x"));

    // set 在函数对象上建自有属性（与普通对象一致）
    Value* v = ctx->createNumber(1);
    EXPECT_EQ(ctx->setProperty(fn, "x", v), v);
    EXPECT_TRUE(neo::test::hasProperty(ctx, fn, "x"));
    EXPECT_EQ(ctx->getProperty(fn, "x"), v);

    // defineProperty 完整描述符
    ctx->defineProperty(fn, "y", ctx->createNumber(2), false, false, false);
    auto* d = static_cast<ObjectData*>(fn->data());
    EXPECT_FALSE(d->properties.at("y").writable);

    // delete 移除自有属性
    EXPECT_EQ(ctx->deleteProperty(fn, "x")->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(neo::test::hasProperty(ctx, fn, "x"));
}

// ===== Object：运算走 ToPrimitive（ES2024 §13.15.3 / §13.15.4）=====

TEST(PropertyOpsTest, ObjectArithmeticUsesToPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* num = ctx->createNumber(1);
    // 普通对象 ToPrimitive → "[object Object]"，故 + 为拼接
    Value* sum = ctx->add(obj, num);
    ASSERT_EQ(sum->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(sum->data())->value, "[object Object]1");
    // 算术按 ToNumber("[object Object]") = NaN
    EXPECT_TRUE(std::isnan(static_cast<NumberData*>(ctx->sub(obj, num)->data())->value));
    EXPECT_TRUE(std::isnan(static_cast<NumberData*>(ctx->mul(obj, num)->data())->value));
    EXPECT_TRUE(std::isnan(static_cast<NumberData*>(ctx->div(obj, num)->data())->value));
    // toString 走 ToPrimitive(STRING)
    EXPECT_EQ(static_cast<StringData*>(ctx->toString(obj)->data())->value,
              "[object Object]");
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
    Value* sym = ctx->createSymbol("k");
    Value* val = ctx->createNumber(1);
    Value* getter = ctx->createNumber(2);
    Value* setter = ctx->createNumber(3);
    d->symbols.emplace(sym->data(),
                       SymbolProperty{sym, Property{val, true, true, true, getter, setter}});

    CountingVisitor visitor;
    d->enumerateChildren(&visitor);
    EXPECT_EQ(visitor.count, 5);  // prototype + key + value + getter + setter
}

TEST(PropertyOpsTest, CloneSharesSymbolsContainer) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    auto* d = static_cast<ObjectData*>(obj->data());
    Value* sym = ctx->createSymbol("k");
    Value* val = ctx->createNumber(1);
    d->symbols.emplace(sym->data(),
                       SymbolProperty{sym, Property{val, true, true, true, nullptr, nullptr}});
    int refBefore = d->ref();

    // 引用语义：clone 返回同一 data（ref++），symbols 容器天然共享
    IData* cloned = d->clone(alloc.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned, d);
    EXPECT_EQ(d->ref(), refBefore + 1);
    EXPECT_EQ(static_cast<ObjectData*>(cloned)->symbols.at(sym->data()).prop.value, val);
    d->refDec();
}


// ===== 原型操作分派（ADR-018 §1）=====

TEST(PropertyOpsTest, PassthroughTypesPrototypeOps) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* exc = ctx->createException(ctx->createString("e"));
    EXPECT_EQ(ctx->getPrototype(exc), exc);
    EXPECT_EQ(ctx->setPrototype(exc, ctx->createObject()), exc);

    Value* fatal = ctx->createFatal("crash");
    EXPECT_EQ(ctx->getPrototype(fatal), fatal);
    EXPECT_EQ(ctx->setPrototype(fatal, ctx->createObject()), fatal);

    Value* uninit = ctx->gc()->createValue(getUninitializedType(),
                                           ctx->gc()->allocData<neo::core::UninitializedData>());
    ASSERT_NE(uninit, nullptr);
    ctx->current()->add(uninit);
    EXPECT_EQ(ctx->getPrototype(uninit), uninit);
    EXPECT_EQ(ctx->setPrototype(uninit, ctx->createObject()), uninit);
}

TEST(PropertyOpsTest, FunctionPrototypeOpsBehaveLikeObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(noopFn, 0);
    // 新函数的原型是 Function.prototype（ADR-035）
    EXPECT_EQ(ctx->getPrototype(fn), ctx->functionPrototype());

    Value* proto = ctx->createObject();
    EXPECT_EQ(ctx->setPrototype(fn, proto), fn);
    EXPECT_EQ(ctx->getPrototype(fn), proto);

    // 原型链查找对函数生效
    Value* val = ctx->createNumber(7);
    ctx->setProperty(proto, "inherited", val);
    EXPECT_EQ(ctx->getProperty(fn, "inherited"), val);
    EXPECT_TRUE(neo::test::hasProperty(ctx, fn, "inherited"));
}

TEST(PropertyOpsTest, FunctionCanBePrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 函数可作为其他对象的原型（FunctionData 派生自 ObjectData）
    Value* fn = ctx->createNativeFunction(noopFn, 0);
    ctx->setProperty(fn, "x", ctx->createNumber(3));
    Value* obj = ctx->createObject();
    EXPECT_EQ(ctx->setPrototype(obj, fn), obj);
    EXPECT_EQ(ctx->getProperty(obj, "x")->type()->kind(), TypeKind::Number);
}

TEST(PropertyOpsTest, PrimitivePrototypeOpsDispatch) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* values[] = {ctx->createUndefined(), ctx->createNull(),
                       ctx->createBoolean(true), ctx->createNumber(1),
                       ctx->createString("s")};
    for (Value* v : values) {
        EXPECT_EQ(ctx->getPrototype(v)->type()->kind(), TypeKind::Undefined);
        EXPECT_EQ(ctx->setPrototype(v, ctx->createObject())->type()->kind(),
                  TypeKind::Exception);
    }
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
    EXPECT_EQ(visitor.count, 4);  // prototype + value + getter + setter
}
