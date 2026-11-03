// ArithmeticPipelineTest.cpp - Context 端到端集成测试
//
// 端到端验证"分配 JS 值 → 作用域跟踪 → 算术运算 → toString → 自动 GC 回收"
// 全链路真实依赖（MallocAllocator + MarkSweepGC + Scope 树 + Context + IType 行为方法），
// 不 mock，验证各模块协作的正确性（ADR-030 §阶段4/5 验收）。
//
// 覆盖场景：
//   1. 各类型 Value 创建后经 Scope 跟踪，运算结果正确
//   2. 混合类型运算（number/string/boolean/null/undefined）经 IType 分派端到端
//   3. toString 各类型经 IType 分派端到端
//   4. Exception/Fatal 透传端到端（异常即值，不参与计算）
//   5. 多次 push/pop scope 后 GC 回收残留 Value（无泄漏）
//
// 关联 ADR: 016（Type 体系行为方法）、017（Fatal 透传）、006（异常即值）、
//          027（集成测试）、029（生命周期）、030（M2 阶段4/5）

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <new>
#include <string>

namespace {

using neo::core::IContext;
using neo::core::Scope;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::NumberData;
using neo::core::StringData;
using neo::infra::Console;
using neo::infra::IAllocator;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

// 集成测试用 sink：捕获输出，验证 console 端到端
class CaptureSink : public IOutputSink {
public:
    void write(std::string_view s) override { buffer_ += s; }
    void writeLine(std::string_view s) override { buffer_ += s; buffer_ += '\n'; }
    void flush() override {}
    const std::string& buffer() const { return buffer_; }
private:
    std::string buffer_;
};

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }

// 在 alloc 上 placement new 构造 Scope（与 Context 内部一致的模式）
Scope* makeScope(IAllocator* alloc, Scope* parent) {
    void* p = alloc->alloc(sizeof(Scope));
    return new (p) Scope(parent, alloc);
}

}  // namespace

// ============ 场景 1：各类型 Value 创建 → Scope 跟踪 → 运算 ============

TEST(ArithmeticPipelineTest, AllPrimitiveTypesCreatedAndTracked) {
    // 端到端：Context 创建 5 种基础类型 Value，验证 type/data 正确
    auto alloc = createMallocAllocator();
    CaptureSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* u = ctx->createUndefined();
    auto* n = ctx->createNull();
    auto* b = ctx->createBoolean(true);
    auto* num = ctx->createNumber(42.0);
    auto* s = ctx->createString("hello");

    ASSERT_NE(u, nullptr);
    ASSERT_NE(n, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(num, nullptr);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(u->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(n->type()->kind(), TypeKind::Null);
    EXPECT_EQ(b->type()->kind(), TypeKind::Boolean);
    EXPECT_EQ(num->type()->kind(), TypeKind::Number);
    EXPECT_EQ(s->type()->kind(), TypeKind::String);
    EXPECT_EQ(numOf(num), 42.0);
    EXPECT_EQ(strOf(s), "hello");
}

// ============ 场景 2：混合类型算术运算经 IType 分派端到端 ============

TEST(ArithmeticPipelineTest, MixedArithmeticEndToEnd) {
    auto alloc = createMallocAllocator();
    CaptureSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // number + number = number
    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createNumber(1.0), ctx->createNumber(2.0))), 3.0);
    // number + string = string concat（JS + 规则：任一是 string → concat）
    EXPECT_EQ(strOf(ctx->add(ctx->createNumber(1.0), ctx->createString("2"))), "12");
    // string + number = string concat（保持原左右顺序）
    EXPECT_EQ(strOf(ctx->add(ctx->createString("a"), ctx->createNumber(1))), "a1");
    // boolean + number = number（ToNumber(true)=1）
    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createBoolean(true), ctx->createNumber(1.0))), 2.0);
    // null + number = number（ToNumber(null)=+0）
    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createNull(), ctx->createNumber(5.0))), 5.0);
    // undefined + number = NaN（ToNumber(undefined)=NaN）
    EXPECT_TRUE(std::isnan(numOf(ctx->add(ctx->createUndefined(), ctx->createNumber(5.0)))));

    // sub/mul/div 全走 ToNumber（无 concat 分支）
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(ctx->createNumber(10.0), ctx->createString("3"))), 7.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->mul(ctx->createBoolean(true), ctx->createString("4"))), 4.0);
    // 12 / null = 12 / 0 = Infinity（JS 除零规则）
    EXPECT_TRUE(std::isinf(numOf(ctx->div(ctx->createNumber(12.0), ctx->createNull()))));
    // 0 / 0 = NaN
    EXPECT_TRUE(std::isnan(numOf(ctx->div(ctx->createNull(), ctx->createNull()))));
}

// ============ 场景 3：toString 各类型经 IType 分派端到端 ============

TEST(ArithmeticPipelineTest, ToStringAllTypesEndToEnd) {
    auto alloc = createMallocAllocator();
    CaptureSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(strOf(ctx->toString(ctx->createUndefined())), "undefined");
    EXPECT_EQ(strOf(ctx->toString(ctx->createNull())), "null");
    EXPECT_EQ(strOf(ctx->toString(ctx->createBoolean(true))), "true");
    EXPECT_EQ(strOf(ctx->toString(ctx->createBoolean(false))), "false");
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(42.0))), "42");
    EXPECT_EQ(strOf(ctx->toString(ctx->createNumber(3.14))), "3.14");
    EXPECT_EQ(strOf(ctx->toString(ctx->createString("hello"))), "hello");
}

// ============ 场景 4：Exception/Fatal 透传端到端（异常即值）============

TEST(ArithmeticPipelineTest, ExceptionFatalPassthroughEndToEnd) {
    auto alloc = createMallocAllocator();
    CaptureSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* exc = ctx->createException(ctx->createUndefined());
    auto* fatal = ctx->createFatal("OOM");

    // Exception 不参与计算，透传返回自身
    EXPECT_EQ(ctx->add(exc, ctx->createNumber(1.0)), exc);
    EXPECT_EQ(ctx->sub(ctx->createNumber(1.0), exc), exc);
    // Fatal 优先于 Exception（ADR-017 §4）
    EXPECT_EQ(ctx->add(exc, fatal), fatal);
    EXPECT_EQ(ctx->add(fatal, exc), fatal);
    // Fatal 透传
    EXPECT_EQ(ctx->add(fatal, ctx->createNumber(1.0)), fatal);
    // toString 也透传
    EXPECT_EQ(ctx->toString(exc), exc);
    EXPECT_EQ(ctx->toString(fatal), fatal);
}

// ============ 场景 5：多 scope push/pop + GC 自动回收（无泄漏）============

TEST(ArithmeticPipelineTest, ScopeLifecycleAndGcCollection) {
    // 端到端：多次 push/pop scope 创建 Value，pop 后 Value 应被 GC 回收。
    // 配合 ASan 验证无泄漏（析构链路：Scope~ → Value scopeRef 归 0 → GC collect → free）。
    auto alloc = createMallocAllocator();
    CaptureSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 在 root scope 创建一些 Value
    auto* root_num = ctx->createNumber(1.0);
    (void)root_num;

    // push 多层 scope，每层创建 Value 并运算
    Scope* scopes[3];
    for (int i = 0; i < 3; ++i) {
        scopes[i] = makeScope(alloc.get(), ctx->current());
        ctx->pushScope(scopes[i]);
        // 每层创建 Value（被 current scope 跟踪）
        auto* v = ctx->createNumber(static_cast<double>(i));
        auto* r = ctx->add(v, ctx->createNumber(1.0));
        ASSERT_NE(r, nullptr);
    }
    EXPECT_EQ(ctx->current(), scopes[2]);

    // 逐层 pop：popScope 销毁 scope（~Scope 递归子树），Value scopeRef 归 0
    ctx->popScope();
    EXPECT_EQ(ctx->current(), scopes[1]);
    ctx->popScope();
    EXPECT_EQ(ctx->current(), scopes[0]);
    ctx->popScope();
    EXPECT_EQ(ctx->current(), ctx->root());

    // 大量运算产生中间 Value（经 GC maybeCollect 回收）
    for (int i = 0; i < 100; ++i) {
        auto* r = ctx->add(ctx->createNumber(static_cast<double>(i)), ctx->createNumber(1.0));
        ASSERT_NE(r, nullptr);
    }
    // Context 析构时 GC 回收所有 Value，ASan 验证无泄漏
}
