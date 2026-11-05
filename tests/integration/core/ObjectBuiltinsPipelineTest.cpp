// ObjectBuiltinsPipelineTest.cpp - 对象/函数内建端到端集成测试
//
// 端到端验证 Object 静态方法 + 枚举接口 + Function.prototype.apply/bind +
// @@toStringTag 在真实 Context/GC/Scope 下协作（真实依赖不 mock）。
//
// 关联 ADR: 018（对象模型）、020（标准库组织）、034（原型-构造函数）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using neo::core::ArrayData;
using neo::core::BooleanData;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::ObjectData;
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

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }
bool boolOf(Value* v) { return static_cast<BooleanData*>(v->data())->value; }

Value* globalOf(IContext* ctx, const char* name) { return ctx->global()->lookup(name); }

Value* callStatic(IContext* ctx, const char* name, size_t argc, Value* argv[]) {
    Value* ctor = globalOf(ctx, "Object");
    return ctx->call(ctx->getProperty(ctor, name), ctor, argc, argv);
}

Value* elemAt(IContext* ctx, Value* arr, size_t i) {
    return ctx->getProperty(arr, ctx->createString(std::to_string(i)));
}

// 把数组元素拼成 "k;" 便于整体比较
std::string dumpKeys(Value* arr) {
    std::string out;
    auto* d = static_cast<ArrayData*>(arr->data());
    for (uint32_t i = 0; i < d->length(); ++i) {
        Value* v = d->elementAt(i);
        if (v == nullptr) continue;
        out += strOf(v);
        out += ';';
    }
    return out;
}

}  // namespace

// ============ 场景 1：Object.keys/values/entries 与枚举顺序 ============

TEST(ObjectBuiltinsPipelineTest, EnumerationOrderEndToEnd) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "b", ctx->createNumber(2));
    ctx->setProperty(obj, "10", ctx->createNumber(10));
    ctx->setProperty(obj, "a", ctx->createNumber(1));
    ctx->setProperty(obj, "2", ctx->createNumber(2));

    Value* args[1] = {obj};
    EXPECT_EQ(dumpKeys(callStatic(ctx.get(), "keys", 1, args)), "2;10;b;a;");

    Value* values = callStatic(ctx.get(), "values", 1, args);
    ASSERT_EQ(values->type()->kind(), TypeKind::Array);
    EXPECT_DOUBLE_EQ(numOf(elemAt(ctx.get(), values, 0)), 2.0);
    EXPECT_DOUBLE_EQ(numOf(elemAt(ctx.get(), values, 1)), 10.0);
    EXPECT_DOUBLE_EQ(numOf(elemAt(ctx.get(), values, 2)), 2.0);
    EXPECT_DOUBLE_EQ(numOf(elemAt(ctx.get(), values, 3)), 1.0);

    Value* entries = callStatic(ctx.get(), "entries", 1, args);
    Value* first = elemAt(ctx.get(), entries, 0);
    EXPECT_EQ(strOf(elemAt(ctx.get(), first, 0)), "2");
    EXPECT_DOUBLE_EQ(numOf(elemAt(ctx.get(), first, 1)), 2.0);
}

// ============ 场景 2：Object.assign + Object.create 组合 ============

TEST(ObjectBuiltinsPipelineTest, CreateAssignInheritanceChain) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // proto 有共享方法 area
    Value* proto = ctx->createObject();
    Value* area = ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t, Value*[]) {
            Value* w = c->getProperty(self, "w");
            Value* h = c->getProperty(self, "h");
            return c->mul(w, h);
        }, 0);
    ctx->defineProperty(proto, "area", area);

    Value* createArgs[1] = {proto};
    Value* shape = callStatic(ctx.get(), "create", 1, createArgs);
    ASSERT_EQ(shape->type()->kind(), TypeKind::Object);

    Value* src = ctx->createObject();
    ctx->setProperty(src, "w", ctx->createNumber(3));
    ctx->setProperty(src, "h", ctx->createNumber(4));
    Value* assignArgs[2] = {shape, src};
    EXPECT_EQ(callStatic(ctx.get(), "assign", 2, assignArgs), shape);

    // 继承来的 area 通过原型链访问，且 this 为 shape
    Value* fn = ctx->getProperty(shape, "area");
    ASSERT_EQ(fn->type()->kind(), TypeKind::Function);
    EXPECT_DOUBLE_EQ(numOf(ctx->call(fn, shape, 0, nullptr)), 12.0);

    // keys 只含自有属性
    Value* keysArgs[1] = {shape};
    EXPECT_EQ(dumpKeys(callStatic(ctx.get(), "keys", 1, keysArgs)), "w;h;");
}

// ============ 场景 3：apply/bind 与内建函数协作 ============

TEST(ObjectBuiltinsPipelineTest, ApplyAndBindOnBuiltinFunctions) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // Math 未实现，用 Number 包装构造钩子作为"内建"验证 apply 转发
    Value* numberFn = globalOf(ctx.get(), "Number");
    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    Value* arr = ctx->createArray();
    static_cast<ArrayData*>(arr->data())->setElement(0, ctx->createNumber(7));
    Value* applyArgs[2] = {ctx->createUndefined(), arr};
    Value* r = ctx->call(applyFn, numberFn, 2, applyArgs);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(r), 7.0);

    // bind 固定 this 后调用对象方法
    Value* obj = ctx->createObject();
    Value* getX = ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t, Value*[]) {
            return c->getProperty(self, "x");
        }, 0);
    ctx->defineProperty(obj, "x", ctx->createNumber(9));
    Value* bindFn = ctx->getProperty(ctx->functionPrototype(), "bind");
    Value* bindArgs[1] = {obj};
    Value* bound = ctx->call(bindFn, getX, 1, bindArgs);
    ASSERT_EQ(bound->type()->kind(), TypeKind::Function);
    EXPECT_DOUBLE_EQ(numOf(ctx->call(bound, nullptr, 0, nullptr)), 9.0);
}

// ============ 场景 4：@@toStringTag 与 Object.prototype.toString ============

TEST(ObjectBuiltinsPipelineTest, ToStringTagOverridesDefaultTag) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* toStringFn = ctx->getProperty(ctx->objectPrototype(), "toString");
    Value* obj = ctx->createObject();
    ctx->defineProperty(obj,
                        ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToStringTag),
                        ctx->createString("Widget"), true, false, true);
    EXPECT_EQ(strOf(ctx->call(toStringFn, obj, 0, nullptr)), "[object Widget]");

    // 数组默认 tag
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createArray(), 0, nullptr)),
              "[object Array]");
}

// ============ 场景 5：isPrototypeOf / propertyIsEnumerable 沿链判定 ============

TEST(ObjectBuiltinsPipelineTest, PrototypeQueriesAcrossChain) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* mid = ctx->createObject();
    Value* leaf = ctx->createObject();
    ctx->setPrototype(leaf, mid);

    Value* isProto = ctx->getProperty(ctx->objectPrototype(), "isPrototypeOf");
    Value* args[1] = {leaf};
    EXPECT_TRUE(boolOf(ctx->call(isProto, mid, 1, args)));
    EXPECT_TRUE(boolOf(ctx->call(isProto, ctx->objectPrototype(), 1, args)));
    EXPECT_FALSE(boolOf(ctx->call(isProto, leaf, 1, args)));

    Value* isEnum = ctx->getProperty(ctx->objectPrototype(), "propertyIsEnumerable");
    ctx->setProperty(leaf, "own", ctx->createNumber(1));
    Value* ownArg[1] = {ctx->createString("own")};
    EXPECT_TRUE(boolOf(ctx->call(isEnum, leaf, 1, ownArg)));
    Value* inheritedArg[1] = {ctx->createString("toString")};
    EXPECT_FALSE(boolOf(ctx->call(isEnum, leaf, 1, inheritedArg)));
}

// ============ 场景 6：枚举/绑定对象在 GC 下存活 ============

TEST(ObjectBuiltinsPipelineTest, BuiltinsSurviveGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "kept", ctx->createNumber(5));
    Value* bindFn = ctx->getProperty(ctx->functionPrototype(), "bind");
    Value* getKept = ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t, Value*[]) {
            return c->getProperty(self, "kept");
        }, 0);
    Value* bindArgs[1] = {obj};
    Value* bound = ctx->call(bindFn, getKept, 1, bindArgs);

    // 制造大量垃圾触发回收
    for (int i = 0; i < 300; ++i) {
        Value* tmp = ctx->createObject();
        ctx->setProperty(tmp, "x", ctx->createNumber(i));
        Value* kargs[1] = {tmp};
        callStatic(ctx.get(), "keys", 1, kargs);
    }
    ctx->gc()->collect();

    EXPECT_DOUBLE_EQ(numOf(ctx->call(bound, nullptr, 0, nullptr)), 5.0);
    Value* keysArgs[1] = {obj};
    EXPECT_EQ(dumpKeys(callStatic(ctx.get(), "keys", 1, keysArgs)), "kept;");
    // Context 析构时 GC 回收全部 Value，ASan 验证无泄漏
}
