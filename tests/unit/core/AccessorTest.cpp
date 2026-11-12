// AccessorTest.cpp - 访问器属性（getter/setter）单元测试
//
// 验证（ES2024 §10.1.6.3 / §10.1.8.1 / §10.1.9.2）：
// - defineAccessor：getter/setter 至少一个，须为 Function 或 null
// - getProperty 命中 getter → 以 receiver 为 this 调用，返回其结果
// - getter 抛 Exception/Fatal → 透传
// - setProperty 命中 setter → 以 receiver 为 this 调用
// - 只有 getter 无 setter → set 报错
// - 原型链上的访问器：getter/setter 的 this 是 receiver（实例）而非原型
// - 数据属性与访问器属性互转（defineProperty / defineAccessor 覆盖）
// - symbol 键访问器
// - GC：getter/setter 函数被对象持有保活
//
// 关联 ADR: 018（对象模型）、006（异常即值）、017（Fatal 透传）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/TypeBehavior.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

namespace {

using neo::core::IContext;
using neo::core::NumberData;
using neo::core::ObjectData;
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

double numOf(Value* v) {
    return static_cast<NumberData*>(v->data())->value;
}

// 记录 getter/setter 收到的 this
Value* gObservedThis = nullptr;

// getter 返回 42
Value* getter42(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    gObservedThis = self;
    return ctx->createNumber(42);
}

// getter 抛 Exception
Value* throwingGetter(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createException(ctx->createString("getter boom"));
}

// getter 抛 Fatal
Value* fatalGetter(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createFatal("getter fatal");
}

// setter 把收到的值写回 self 的 "_stored" 属性
Value* setterStores(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    gObservedThis = self;
    if (argc > 0) ctx->setProperty(self, "_stored", argv[0]);
    return ctx->createUndefined();
}

// setter 抛 Exception
Value* throwingSetter(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createException(ctx->createString("setter boom"));
}

}  // namespace

// ===== defineAccessor 基础 =====

TEST(AccessorTest, DefineGetterAndRead) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* getter = ctx->createNativeFunction(getter42, 0);
    EXPECT_EQ(ctx->defineAccessor(obj, "x", getter, nullptr), obj);

    Value* v = ctx->getProperty(obj, "x");
    ASSERT_EQ(v->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(v), 42.0);
}

TEST(AccessorTest, GetterReceivesReceiverAsThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* getter = ctx->createNativeFunction(getter42, 0);
    ctx->defineAccessor(obj, "x", getter, nullptr);

    gObservedThis = nullptr;
    ctx->getProperty(obj, "x");
    EXPECT_EQ(gObservedThis, obj);
}

TEST(AccessorTest, DefineSetterAndWrite) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* setter = ctx->createNativeFunction(setterStores, 1);
    ctx->defineAccessor(obj, "x", nullptr, setter);

    Value* val = ctx->createNumber(7);
    EXPECT_EQ(ctx->setProperty(obj, "x", val), val);
    // setter 把值写到了 _stored
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "_stored")), 7.0);
}

TEST(AccessorTest, SetterReceivesReceiverAsThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* setter = ctx->createNativeFunction(setterStores, 1);
    ctx->defineAccessor(obj, "x", nullptr, setter);

    gObservedThis = nullptr;
    ctx->setProperty(obj, "x", ctx->createNumber(1));
    EXPECT_EQ(gObservedThis, obj);
}

TEST(AccessorTest, GetterSetterPair) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* getter = ctx->createNativeFunction(getter42, 0);
    Value* setter = ctx->createNativeFunction(setterStores, 1);
    ctx->defineAccessor(obj, "x", getter, setter);

    // 写走 setter（不改变 getter 的返回值）
    ctx->setProperty(obj, "x", ctx->createNumber(9));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "x")), 42.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "_stored")), 9.0);
}

// ===== 描述符校验 =====

TEST(AccessorTest, RejectsNonFunctionGetter) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    EXPECT_EQ(ctx->defineAccessor(obj, "x", ctx->createNumber(1), nullptr)->type()->kind(),
              TypeKind::Exception);
}

TEST(AccessorTest, RejectsNonFunctionSetter) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    EXPECT_EQ(ctx->defineAccessor(obj, "x", nullptr, ctx->createString("s"))->type()->kind(),
              TypeKind::Exception);
}

TEST(AccessorTest, AcceptsNullGetterAndSetter) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    // null 是合法的（表示无访问器）
    EXPECT_EQ(ctx->defineAccessor(obj, "x", ctx->createNull(), ctx->createNull()), obj);
    // 读取返回 undefined（无 getter）
    EXPECT_EQ(ctx->getProperty(obj, "x")->type()->kind(), TypeKind::Undefined);
}

// ===== 只有 getter / 只有 setter =====

TEST(AccessorTest, GetterOnlyRejectsSet) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "x", ctx->createNativeFunction(getter42, 0), nullptr);

    EXPECT_EQ(ctx->setProperty(obj, "x", ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
}

TEST(AccessorTest, SetterOnlyReadsUndefined) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "x", nullptr, ctx->createNativeFunction(setterStores, 1));

    EXPECT_EQ(ctx->getProperty(obj, "x")->type()->kind(), TypeKind::Undefined);
}

// ===== getter 抛错透传 =====

TEST(AccessorTest, GetterExceptionPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "x", ctx->createNativeFunction(throwingGetter, 0), nullptr);
    EXPECT_EQ(ctx->getProperty(obj, "x")->type()->kind(), TypeKind::Exception);
}

TEST(AccessorTest, GetterFatalPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "x", ctx->createNativeFunction(fatalGetter, 0), nullptr);
    EXPECT_EQ(ctx->getProperty(obj, "x")->type()->kind(), TypeKind::Fatal);
}

TEST(AccessorTest, SetterExceptionPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "x", nullptr, ctx->createNativeFunction(throwingSetter, 1));
    EXPECT_EQ(ctx->setProperty(obj, "x", ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
}

// ===== 原型链上的访问器 =====

TEST(AccessorTest, InheritedGetterUsesReceiverAsThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    ctx->defineAccessor(proto, "x", ctx->createNativeFunction(getter42, 0), nullptr);

    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);

    gObservedThis = nullptr;
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "x")), 42.0);
    // this 是实例 obj，不是 proto
    EXPECT_EQ(gObservedThis, obj);
}

TEST(AccessorTest, InheritedSetterUsesReceiverAsThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    ctx->defineAccessor(proto, "x", nullptr, ctx->createNativeFunction(setterStores, 1));

    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);

    gObservedThis = nullptr;
    ctx->setProperty(obj, "x", ctx->createNumber(5));
    // this 是实例；setter 写的 _stored 落在实例上
    EXPECT_EQ(gObservedThis, obj);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "_stored")), 5.0);
    // 实例自身不产生 x 数据属性
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->properties.count("x"), 0u);
}

TEST(AccessorTest, InheritedGetterOnlyRejectsSet) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    ctx->defineAccessor(proto, "x", ctx->createNativeFunction(getter42, 0), nullptr);
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);

    EXPECT_EQ(ctx->setProperty(obj, "x", ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
}

// ===== 数据属性 ↔ 访问器属性互转 =====

TEST(AccessorTest, DefinePropertyOverridesAccessor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "x", ctx->createNativeFunction(getter42, 0), nullptr);
    // 改成数据属性
    Value* val = ctx->createNumber(1);
    ctx->defineProperty(obj, "x", val);
    EXPECT_EQ(ctx->getProperty(obj, "x"), val);

    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->properties.at("x").getter, nullptr);
}

TEST(AccessorTest, DefineAccessorOverridesDataProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "x", ctx->createNumber(1));
    ctx->defineAccessor(obj, "x", ctx->createNativeFunction(getter42, 0), nullptr);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "x")), 42.0);

    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->properties.at("x").value, nullptr);
    EXPECT_EQ(d->order.size(), 1u);  // 不重复追加
}

// ===== symbol 键访问器 =====

TEST(AccessorTest, SymbolKeyAccessor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->defineAccessor(obj, sym, ctx->createNativeFunction(getter42, 0),
                        ctx->createNativeFunction(setterStores, 1));

    gObservedThis = nullptr;
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, sym)), 42.0);
    EXPECT_EQ(gObservedThis, obj);

    ctx->setProperty(obj, sym, ctx->createNumber(3));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "_stored")), 3.0);
}

// ===== 描述符特性 =====

TEST(AccessorTest, AccessorDescriptorFlags) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* getter = ctx->createNativeFunction(getter42, 0);
    ctx->defineAccessor(obj, "x", getter, nullptr, /*enumerable=*/true,
                        /*configurable=*/false);

    auto* d = static_cast<ObjectData*>(obj->data());
    const auto& p = d->properties.at("x");
    EXPECT_EQ(p.getter, getter);
    EXPECT_EQ(p.setter, nullptr);
    EXPECT_EQ(p.value, nullptr);
    EXPECT_TRUE(p.enumerable);
    EXPECT_FALSE(p.configurable);
    // 不可配置 → [[Delete]] 返回 false
    Value* del = ctx->deleteProperty(obj, "x");
    ASSERT_EQ(del->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(neo::core::detail::toBoolean(del));
}

// ===== GC =====

TEST(AccessorTest, GetterSurvivesGcWhileObjectAlive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "x", ctx->createNativeFunction(getter42, 0), nullptr);

    ctx->gc()->collect();
    // getter 只被对象持有，靠 enumerateChildren 保活
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "x")), 42.0);
}

TEST(AccessorTest, SymbolKeyAccessorSurvivesGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->defineAccessor(obj, sym, ctx->createNativeFunction(getter42, 0), nullptr);

    ctx->gc()->collect();
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, sym)), 42.0);
}

// ===== symbol 键访问器的边界分支 =====

TEST(AccessorTest, SymbolKeyGetterOnlyRejectsSet) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->defineAccessor(obj, sym, ctx->createNativeFunction(getter42, 0), nullptr);
    EXPECT_EQ(ctx->setProperty(obj, sym, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
}

TEST(AccessorTest, SymbolKeyReadOnlyDataPropertyRejectsSet) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->defineProperty(obj, sym, ctx->createNumber(1), /*writable=*/false);
    EXPECT_EQ(ctx->setProperty(obj, sym, ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
}

TEST(AccessorTest, InheritedSymbolAccessorUsesReceiver) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->defineAccessor(proto, sym, ctx->createNativeFunction(getter42, 0),
                        ctx->createNativeFunction(setterStores, 1));
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);

    gObservedThis = nullptr;
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, sym)), 42.0);
    EXPECT_EQ(gObservedThis, obj);

    ctx->setProperty(obj, sym, ctx->createNumber(4));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "_stored")), 4.0);
}

TEST(AccessorTest, InheritedSymbolGetterOnlyRejectsSet) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->defineAccessor(proto, sym, ctx->createNativeFunction(getter42, 0), nullptr);
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);
    EXPECT_EQ(ctx->setProperty(obj, sym, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
}

TEST(AccessorTest, InheritedSymbolReadOnlyRejectsSet) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->defineProperty(proto, sym, ctx->createNumber(1), /*writable=*/false);
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);
    EXPECT_EQ(ctx->setProperty(obj, sym, ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
}

TEST(AccessorTest, SymbolKeyNonConfigurableAccessorRejectsDelete) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->defineAccessor(obj, sym, ctx->createNativeFunction(getter42, 0), nullptr,
                        /*enumerable=*/false, /*configurable=*/false);
    Value* del = ctx->deleteProperty(obj, sym);
    ASSERT_EQ(del->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(neo::core::detail::toBoolean(del));
}

TEST(AccessorTest, SymbolKeyDefinePropertyOverridesAccessor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->defineAccessor(obj, sym, ctx->createNativeFunction(getter42, 0), nullptr);
    Value* val = ctx->createNumber(1);
    ctx->defineProperty(obj, sym, val);
    EXPECT_EQ(ctx->getProperty(obj, sym), val);
}

TEST(AccessorTest, SymbolKeyDefineAccessorOverridesExisting) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->setProperty(obj, sym, ctx->createNumber(1));
    ctx->defineAccessor(obj, sym, ctx->createNativeFunction(getter42, 0), nullptr);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, sym)), 42.0);
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->symbolOrder.size(), 1u);  // 不重复追加
}

// ===== Function 上的访问器（委托 ObjectType）=====

TEST(AccessorTest, FunctionSupportsAccessor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(getter42, 0);
    EXPECT_EQ(ctx->defineAccessor(fn, "x", ctx->createNativeFunction(getter42, 0), nullptr),
              fn);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(fn, "x")), 42.0);
}

// ===== 原始类型 defineAccessor =====

TEST(AccessorTest, PrimitiveDefineAccessorRejected) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* getter = ctx->createNativeFunction(getter42, 0);
    Value* values[] = {ctx->createUndefined(), ctx->createNull(), ctx->createBoolean(true),
                       ctx->createNumber(1), ctx->createString("s"), ctx->createSymbol("k")};
    for (Value* v : values) {
        EXPECT_EQ(ctx->defineAccessor(v, "x", getter, nullptr)->type()->kind(),
                  TypeKind::Exception);
    }
}

TEST(AccessorTest, PassthroughTypesDefineAccessor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* exc = ctx->createException(ctx->createString("e"));
    EXPECT_EQ(ctx->defineAccessor(exc, "x", nullptr, nullptr), exc);
    Value* fatal = ctx->createFatal("crash");
    EXPECT_EQ(ctx->defineAccessor(fatal, "x", nullptr, nullptr), fatal);
}
