// context_test.cpp - Context 门面单元测试
//
// 验证：
// - Context 创建后 global/root/current 三层 scope 就绪
// - global 是根（parent=nullptr），root 的 parent=global，current 初始=root
// - pushScope/popScope 管理 current 栈，pop 销毁弹出的 scope
// - Context 持有 GC 所有权（内部创建），析构先销毁 global_scope 再销毁 GC
// - 注入资源 allocator/console 不被销毁（外部持有）
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、012（Scope 树）、030 阶段 4

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "infra/IAllocator.h"
#include "infra/Console.h"
#include "infra/MallocAllocator.h"

#include "TestData.h"

#include <gtest/gtest.h>

#include <new>

namespace {

using neo::core::IContext;
using neo::core::IGCEngine;
using neo::core::Scope;
using neo::core::Value;
using neo::core::createContext;
using neo::core::test::NumberData;
using neo::core::test::dummyType;
using neo::infra::IAllocator;
using neo::infra::Console;
using neo::infra::MallocAllocator;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

// 测试用 IOutputSink（不输出到 stdout，避免测试噪音）
class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

// 在 alloc 上 placement new 构造 Scope
Scope* makeScope(IAllocator* alloc, Scope* parent) {
    void* p = alloc->alloc(sizeof(Scope));
    return new (p) Scope(parent, alloc);
}

}  // namespace

TEST(ContextTest, ThreeLevelScopeReadyAfterCreation) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);

    auto ctx = createContext(alloc.get(), &console);
    EXPECT_NE(ctx->global(), nullptr);
    EXPECT_NE(ctx->root(), nullptr);
    EXPECT_EQ(ctx->current(), ctx->root());  // current 初始 = root

    EXPECT_EQ(ctx->global()->parent(), nullptr);     // global 是根
    EXPECT_EQ(ctx->root()->parent(), ctx->global()); // root 的 parent = global
    EXPECT_EQ(ctx->global()->childCount(), 1u);     // global 持有 root
    // ctx 析构：销毁 global（递归 root）→ 销毁 GC
}

TEST(ContextTest, ResourcesAccessible) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);

    auto ctx = createContext(alloc.get(), &console);
    EXPECT_EQ(ctx->allocator(), alloc.get());
    EXPECT_NE(ctx->gc(), nullptr);  // GC 由 Context 内部创建
    EXPECT_EQ(ctx->console(), &console);
}

TEST(ContextTest, PushPopScope) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);

    auto ctx = createContext(alloc.get(), &console);
    EXPECT_EQ(ctx->current(), ctx->root());

    Scope* s1 = makeScope(alloc.get(), ctx->current());
    ctx->current()->addChild(s1);
    ctx->pushScope(s1);
    EXPECT_EQ(ctx->current(), s1);

    Scope* s2 = makeScope(alloc.get(), ctx->current());
    ctx->current()->addChild(s2);
    ctx->pushScope(s2);
    EXPECT_EQ(ctx->current(), s2);

    // pop s2：s2 被销毁
    ctx->popScope();
    EXPECT_EQ(ctx->current(), s1);

    // pop s1：s1 被销毁
    ctx->popScope();
    EXPECT_EQ(ctx->current(), ctx->root());
    // ctx 析构：销毁 global（递归 root）→ 销毁 GC
}

TEST(ContextTest, PopRootIsNoop) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);

    auto ctx = createContext(alloc.get(), &console);
    EXPECT_EQ(ctx->current(), ctx->root());

    // pop 栈底 root：无操作，current 仍是 root
    ctx->popScope();
    EXPECT_EQ(ctx->current(), ctx->root());
    EXPECT_EQ(ctx->root()->parent(), ctx->global());
}

TEST(ContextTest, DestroyReleasesScopeTreeThenGC) {
    // Context 析构顺序：先销毁 global_scope（scopeRef 归 0）→ 再销毁 GC（collect 回收 Value）
    // 若顺序反了，GC 先销毁则 Value 泄漏 panic；或 scope 先释放但 GC 已不在则 Value 无法回收。
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);

    // 用 GC 创建 Value，挂到 scope 上，ctx 析构时 scope 释放（scopeRef 归 0），
    // GC 析构时 collect 回收所有 Value（不应 panic）
    {
        auto ctx = createContext(alloc.get(), &console);
        IGCEngine* gc = ctx->gc();

        auto* v1 = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
        auto* v2 = gc->createValue(dummyType(), gc->allocData<NumberData>(2.0));
        auto* v3 = gc->createValue(dummyType(), gc->allocData<NumberData>(3.0));

        ctx->global()->add(v1);
        ctx->root()->add(v2);

        Scope* s = makeScope(alloc.get(), ctx->current());
        ctx->current()->addChild(s);
        ctx->pushScope(s);
        s->add(v3);

        EXPECT_EQ(v1->scopeRef(), 1);
        EXPECT_EQ(v2->scopeRef(), 1);
        EXPECT_EQ(v3->scopeRef(), 1);

        // ctx 析构：global 析构（递归 s → root → global，scopeRef 全归 0）
        //           → GC 析构（collect 回收 v1/v2/v3，无泄漏不 panic）
    }
    // 到这里 ctx 已析构，GC 已销毁，无泄漏 panic 说明顺序正确
}

TEST(ContextTest, DestroyDoesNotDestroyInjectedResources) {
    // allocator/console 是注入资源，Context 析构不应销毁它们
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);

    {
        auto ctx = createContext(alloc.get(), &console);
        // ctx 析构不应销毁 alloc/console
    }

    // alloc 仍可用（未被 Context 销毁）
    void* p = alloc->alloc(64);
    EXPECT_NE(p, nullptr);
    alloc->free(p);
    // console 仍存活（栈对象，函数结束才销毁）
}

TEST(ContextTest, LookupChainAcrossScopes) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);

    auto ctx = createContext(alloc.get(), &console);
    IGCEngine* gc = ctx->gc();
    auto* gv = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    auto* rv = gc->createValue(dummyType(), gc->allocData<NumberData>(2.0));

    ctx->global()->add(gv);
    ctx->global()->bind("g", gv);
    ctx->root()->add(rv);
    ctx->root()->bind("r", rv);

    // root 沿链查到 global 的 g
    EXPECT_EQ(ctx->root()->lookupChain("g"), gv);
    EXPECT_EQ(ctx->root()->lookupChain("r"), rv);
    // global 查不到 root 的 r
    EXPECT_EQ(ctx->global()->lookupChain("r"), nullptr);
    // ctx 析构：销毁 global（递归 root）→ 销毁 GC
}
