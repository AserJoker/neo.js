// PrimitiveWrapperPipelineTest.cpp - 原始值包装对象端到端集成测试
//
// 端到端验证"全局构造函数 → call/new 语义分叉 → 包装对象 → 原型方法 →
// 运算拆包 → Object.prototype.toString → GC 回收"全链路（真实依赖不 mock）。
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、027（集成测试）、034（原型-构造函数）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Boolean.h"
#include "core/types/Number.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using neo::core::IContext;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::wrapperPrimitiveValue;
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

Value* globalOf(IContext* ctx, const char* name) { return ctx->global()->lookup(name); }

Value* callMethod(IContext* ctx, Value* self, const char* name, size_t argc, Value* argv[]) {
    return ctx->call(ctx->getProperty(self, name), self, argc, argv);
}

}  // namespace

// ============ 场景 1：三个包装构造函数全部挂载 ============

TEST(PrimitiveWrapperPipelineTest, AllWrapperConstructorsRegistered) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    for (const char* name : {"Boolean", "Number", "String", "Object", "Function", "Array",
                             "Symbol"}) {
        Value* fn = globalOf(ctx.get(), name);
        ASSERT_NE(fn, nullptr) << name;
        EXPECT_EQ(fn->type()->kind(), TypeKind::Function) << name;
        EXPECT_EQ(ctx->getPrototype(fn), ctx->functionPrototype()) << name;
    }
}

// ============ 场景 2：call / new 语义分叉 ============

TEST(PrimitiveWrapperPipelineTest, CallVsNewForAllWrappers) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    struct Case {
        const char* ctorName;
        Value* arg;
        TypeKind primitiveKind;
        Value* expectedProto;
    };
    Value* cases[3][4] = {
        {nullptr, ctx->createNumber(1), nullptr, ctx->booleanPrototype()},
        {nullptr, ctx->createString("7"), nullptr, ctx->numberPrototype()},
        {nullptr, ctx->createNumber(7), nullptr, ctx->stringPrototype()},
    };
    const char* names[3] = {"Boolean", "Number", "String"};
    const TypeKind kinds[3] = {TypeKind::Boolean, TypeKind::Number, TypeKind::String};

    for (int i = 0; i < 3; ++i) {
        Value* fn = globalOf(ctx.get(), names[i]);
        Value* args[1] = {cases[i][1]};

        // call → 原始值
        Value* called = ctx->call(fn, nullptr, 1, args);
        EXPECT_EQ(called->type()->kind(), kinds[i]) << names[i];

        // new → 包装对象
        Value* constructed = ctx->construct(fn, 1, args);
        ASSERT_EQ(constructed->type()->kind(), TypeKind::PrimitiveWrapper) << names[i];
        EXPECT_EQ(wrapperPrimitiveValue(constructed)->type()->kind(), kinds[i]) << names[i];
        EXPECT_EQ(ctx->getPrototype(constructed), cases[i][3]) << names[i];
    }
}

// ============ 场景 3：包装对象参与运算 ============

TEST(PrimitiveWrapperPipelineTest, WrapperArithmeticEndToEnd) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* numberFn = globalOf(ctx.get(), "Number");
    Value* args[1] = {ctx->createNumber(10)};
    Value* ten = ctx->construct(numberFn, 1, args);

    EXPECT_DOUBLE_EQ(numOf(ctx->add(ten, ctx->createNumber(5))), 15.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ten, ctx->createNumber(4))), 6.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->mul(ten, ctx->createNumber(3))), 30.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->div(ten, ctx->createNumber(4))), 2.5);
}

// ============ 场景 4：字符串原始值属性访问（ToObject）============

TEST(PrimitiveWrapperPipelineTest, StringPrimitiveMethodsEndToEnd) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* s = ctx->createString("hello");
    EXPECT_EQ(numOf(ctx->getProperty(s, "length")), 5.0);
    EXPECT_EQ(strOf(ctx->getProperty(s, "1")), "e");
    // s.toString() 经 ToObject → String.prototype.toString
    EXPECT_EQ(strOf(callMethod(ctx.get(), s, "toString", 0, nullptr)), "hello");
    EXPECT_EQ(strOf(callMethod(ctx.get(), s, "valueOf", 0, nullptr)), "hello");
}

// ============ 场景 5：Object.prototype.toString 的 tag ============

TEST(PrimitiveWrapperPipelineTest, ToStringTagsEndToEnd) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* toStringFn = ctx->getProperty(ctx->objectPrototype(), "toString");
    Value* numberFn = globalOf(ctx.get(), "Number");
    Value* args[1] = {ctx->createNumber(1)};
    Value* wrapper = ctx->construct(numberFn, 1, args);

    EXPECT_EQ(strOf(ctx->call(toStringFn, wrapper, 0, nullptr)), "[object Number]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createObject(), 0, nullptr)),
              "[object Object]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createArray(), 0, nullptr)),
              "[object Array]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createNumber(1), 0, nullptr)),
              "[object Number]");
}

// ============ 场景 6：GC 保活与无泄漏 ============

TEST(PrimitiveWrapperPipelineTest, WrappersSurviveGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* stringFn = globalOf(ctx.get(), "String");
    Value* args[1] = {ctx->createString("persist")};
    Value* wrapper = ctx->construct(stringFn, 1, args);
    ASSERT_EQ(wrapper->type()->kind(), TypeKind::PrimitiveWrapper);

    // 大量临时包装对象触发 GC，持久包装对象仍可读
    for (int i = 0; i < 200; ++i) {
        Value* tmp[1] = {ctx->createNumber(i)};
        ctx->construct(globalOf(ctx.get(), "Number"), 1, tmp);
    }
    ctx->gc()->collect();
    EXPECT_EQ(strOf(wrapperPrimitiveValue(wrapper)), "persist");
    EXPECT_EQ(numOf(ctx->getProperty(wrapper, "length")), 7.0);
    // Context 析构时 GC 回收全部 Value，ASan 验证无泄漏
}
