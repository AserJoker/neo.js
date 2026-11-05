// ArrayPipelineTest.cpp - Array 端到端集成测试
//
// 端到端验证"创建数组 → 索引读写 → 原型方法调用 → 运算/toString → GC 回收"全链路
// 真实依赖（MallocAllocator + MarkSweepGC + Scope 树 + Context + IType 行为方法），
// 不 mock（ADR-027 集成测试）。
//
// 关联 ADR: 018（对象模型 §6）、020（标准库组织）、027（集成测试）、034（原型-构造函数）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

namespace {

using neo::core::ArrayData;
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

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }
ArrayData* dataOf(Value* v) { return static_cast<ArrayData*>(v->data()); }
bool boolOf(Value* v) { return static_cast<BooleanData*>(v->data())->value; }

Value* globalOf(IContext* ctx, const char* name) { return ctx->global()->lookup(name); }

Value* callMethod(IContext* ctx, Value* arr, const char* name, size_t argc, Value* argv[]) {
    return ctx->call(ctx->getProperty(arr, name), arr, argc, argv);
}

}  // namespace

// ============ 场景 1：构造 → 索引 → 遍历 ============

TEST(ArrayPipelineTest, ConstructFillAndIterate) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayFn = globalOf(ctx.get(), "Array");
    Value* arr = ctx->construct(arrayFn, 0, nullptr);
    ASSERT_EQ(arr->type()->kind(), TypeKind::Array);

    for (int i = 0; i < 5; ++i) {
        Value* arg[1] = {ctx->createNumber(i * 10)};
        callMethod(ctx.get(), arr, "push", 1, arg);
    }
    EXPECT_EQ(dataOf(arr)->length(), 5u);
    EXPECT_EQ(strOf(ctx->toString(arr)), "0,10,20,30,40");

    // 逐索引读取
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(numOf(ctx->getProperty(arr, std::to_string(i))), i * 10.0);
    }
}

// ============ 场景 2：稀疏写入 → 迁移 → 截断 ============

TEST(ArrayPipelineTest, SparseWriteMigrationAndTruncate) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createString("a"));
    ctx->setProperty(arr, "1000", ctx->createString("z"));

    EXPECT_TRUE(dataOf(arr)->sparseMode);
    EXPECT_EQ(dataOf(arr)->length(), 1001u);
    EXPECT_EQ(strOf(ctx->getProperty(arr, "0")), "a");
    EXPECT_EQ(strOf(ctx->getProperty(arr, "1000")), "z");

    // 截断到 500：1000 被丢弃
    ctx->setProperty(arr, "length", ctx->createNumber(500));
    EXPECT_EQ(dataOf(arr)->length(), 500u);
    EXPECT_TRUE(ctx->hasProperty(arr, "0"));
    EXPECT_FALSE(ctx->hasProperty(arr, "1000"));
    EXPECT_EQ(ctx->getProperty(arr, "1000")->type()->kind(), TypeKind::Undefined);
}

// ============ 场景 3：原型方法链 ============

TEST(ArrayPipelineTest, PrototypeMethodChain) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    for (int i = 1; i <= 4; ++i) {
        Value* arg[1] = {ctx->createNumber(i)};
        callMethod(ctx.get(), arr, "push", 1, arg);
    }

    // slice(1,3) → [2,3]
    Value* sliceArgs[2] = {ctx->createNumber(1), ctx->createNumber(3)};
    Value* sliced = callMethod(ctx.get(), arr, "slice", 2, sliceArgs);
    EXPECT_EQ(dataOf(sliced)->length(), 2u);
    EXPECT_EQ(strOf(callMethod(ctx.get(), sliced, "join", 0, nullptr)), "2,3");

    // indexOf(3) → 2
    Value* idxArgs[1] = {ctx->createNumber(3)};
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "indexOf", 1, idxArgs)), 2.0);

    // pop → 4，剩余长度 3
    EXPECT_EQ(numOf(callMethod(ctx.get(), arr, "pop", 0, nullptr)), 4.0);
    EXPECT_EQ(dataOf(arr)->length(), 3u);
}

// ============ 场景 4：数组参与运算与字符串拼接 ============

TEST(ArrayPipelineTest, ArithmeticAndConcatenation) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));

    // [1,2] + 3 === "1,23"
    EXPECT_EQ(strOf(ctx->add(arr, ctx->createNumber(3))), "1,23");
    // 3 + [1,2] === "31,2"
    EXPECT_EQ(strOf(ctx->add(ctx->createNumber(3), arr)), "31,2");
    // [10] - 4 === 6
    Value* ten = ctx->createArray();
    ctx->setProperty(ten, "0", ctx->createNumber(10));
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ten, ctx->createNumber(4))), 6.0);
}

// ============ 场景 5：Array.isArray 端到端 ============

TEST(ArrayPipelineTest, IsArrayEndToEnd) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* isArrayFn = ctx->getProperty(globalOf(ctx.get(), "Array"), "isArray");
    Value* arr = ctx->createArray();
    Value* obj = ctx->createObject();
    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 0);

    Value* a1[1] = {arr};
    Value* a2[1] = {obj};
    Value* a3[1] = {fn};
    EXPECT_TRUE(boolOf(ctx->call(isArrayFn, nullptr, 1, a1)));
    EXPECT_FALSE(boolOf(ctx->call(isArrayFn, nullptr, 1, a2)));
    EXPECT_FALSE(boolOf(ctx->call(isArrayFn, nullptr, 1, a3)));
}

// ============ 场景 6：GC 保活与回收（无泄漏）============

TEST(ArrayPipelineTest, ElementsSurviveGcAndScopePopCollects) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 数组持有一批仅在数组内的元素
    Value* arr = ctx->createArray();
    for (int i = 0; i < 10; ++i) {
        ctx->setProperty(arr, std::to_string(i), ctx->createNumber(i));
    }
    ctx->gc()->collect();
    EXPECT_EQ(numOf(ctx->getProperty(arr, "9")), 9.0);

    // push 大量元素触发稀疏迁移，GC 后仍可读
    Value* big = ctx->createArray();
    for (int i = 0; i < 200; ++i) {
        Value* arg[1] = {ctx->createNumber(i)};
        callMethod(ctx.get(), big, "push", 1, arg);
    }
    ctx->gc()->collect();
    EXPECT_EQ(numOf(ctx->getProperty(big, "199")), 199.0);
    // Context 析构时 GC 回收全部 Value，ASan 验证无泄漏
}
