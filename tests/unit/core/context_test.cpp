// context_test.cpp - Context 门面单元测试
//
// 验证：
// - Context 创建后 global/root/current 三层 scope 就绪
// - global 是根（parent=nullptr），root 的 parent=global，current 初始=root
// - push_scope/pop_scope 管理 current 栈，pop 销毁弹出的 scope
// - Context 持有 GC 所有权（内部创建），析构先销毁 global_scope 再销毁 GC
// - 注入资源 allocator/console 不被销毁（外部持有）
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、012（Scope 树）、030 阶段 4

#include "core/context.h"
#include "core/gc.h"
#include "core/scope.h"
#include "core/value.h"
#include "infra/allocator.h"
#include "infra/console.h"
#include "infra/impl_allocator.h"

#include "test_data.h"

#include <gtest/gtest.h>

#include <new>

namespace {

using neo::core::Context;
using neo::core::GCEngine;
using neo::core::Scope;
using neo::core::Value;
using neo::core::create_context;
using neo::core::test::NumberData;
using neo::core::test::dummy_type;
using neo::infra::Allocator;
using neo::infra::Console;
using neo::infra::MallocAllocator;
using neo::infra::OutputSink;
using neo::infra::create_malloc_allocator;

// 测试用 OutputSink（不输出到 stdout，避免测试噪音）
class NullSink : public OutputSink {
public:
    void write(std::string_view) override {}
    void write_line(std::string_view) override {}
    void flush() override {}
};

// 在 alloc 上 placement new 构造 Scope
Scope* make_scope(Allocator* alloc, Scope* parent) {
    void* p = alloc->alloc(sizeof(Scope));
    return new (p) Scope(parent, alloc);
}

}  // namespace

TEST(ContextTest, ThreeLevelScopeReadyAfterCreation) {
    auto alloc = create_malloc_allocator();
    NullSink sink;
    Console console(&sink);

    auto ctx = create_context(alloc.get(), &console);
    EXPECT_NE(ctx->global(), nullptr);
    EXPECT_NE(ctx->root(), nullptr);
    EXPECT_EQ(ctx->current(), ctx->root());  // current 初始 = root

    EXPECT_EQ(ctx->global()->parent(), nullptr);     // global 是根
    EXPECT_EQ(ctx->root()->parent(), ctx->global()); // root 的 parent = global
    EXPECT_EQ(ctx->global()->child_count(), 1u);     // global 持有 root
    // ctx 析构：销毁 global（递归 root）→ 销毁 GC
}

TEST(ContextTest, ResourcesAccessible) {
    auto alloc = create_malloc_allocator();
    NullSink sink;
    Console console(&sink);

    auto ctx = create_context(alloc.get(), &console);
    EXPECT_EQ(ctx->allocator(), alloc.get());
    EXPECT_NE(ctx->gc(), nullptr);  // GC 由 Context 内部创建
    EXPECT_EQ(ctx->console(), &console);
}

TEST(ContextTest, PushPopScope) {
    auto alloc = create_malloc_allocator();
    NullSink sink;
    Console console(&sink);

    auto ctx = create_context(alloc.get(), &console);
    EXPECT_EQ(ctx->current(), ctx->root());

    Scope* s1 = make_scope(alloc.get(), ctx->current());
    ctx->current()->add_child(s1);
    ctx->push_scope(s1);
    EXPECT_EQ(ctx->current(), s1);

    Scope* s2 = make_scope(alloc.get(), ctx->current());
    ctx->current()->add_child(s2);
    ctx->push_scope(s2);
    EXPECT_EQ(ctx->current(), s2);

    // pop s2：s2 被销毁
    ctx->pop_scope();
    EXPECT_EQ(ctx->current(), s1);

    // pop s1：s1 被销毁
    ctx->pop_scope();
    EXPECT_EQ(ctx->current(), ctx->root());
    // ctx 析构：销毁 global（递归 root）→ 销毁 GC
}

TEST(ContextTest, PopRootIsNoop) {
    auto alloc = create_malloc_allocator();
    NullSink sink;
    Console console(&sink);

    auto ctx = create_context(alloc.get(), &console);
    EXPECT_EQ(ctx->current(), ctx->root());

    // pop 栈底 root：无操作，current 仍是 root
    ctx->pop_scope();
    EXPECT_EQ(ctx->current(), ctx->root());
    EXPECT_EQ(ctx->root()->parent(), ctx->global());
}

TEST(ContextTest, DestroyReleasesScopeTreeThenGC) {
    // Context 析构顺序：先销毁 global_scope（scope_ref 归 0）→ 再销毁 GC（collect 回收 Value）
    // 若顺序反了，GC 先销毁则 Value 泄漏 panic；或 scope 先释放但 GC 已不在则 Value 无法回收。
    auto alloc = create_malloc_allocator();
    NullSink sink;
    Console console(&sink);

    // 用 GC 创建 Value，挂到 scope 上，ctx 析构时 scope 释放（scope_ref 归 0），
    // GC 析构时 collect 回收所有 Value（不应 panic）
    {
        auto ctx = create_context(alloc.get(), &console);
        GCEngine* gc = ctx->gc();

        auto* v1 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
        auto* v2 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
        auto* v3 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(3.0));

        ctx->global()->add(v1);
        ctx->root()->add(v2);

        Scope* s = make_scope(alloc.get(), ctx->current());
        ctx->current()->add_child(s);
        ctx->push_scope(s);
        s->add(v3);

        EXPECT_EQ(v1->scope_ref(), 1);
        EXPECT_EQ(v2->scope_ref(), 1);
        EXPECT_EQ(v3->scope_ref(), 1);

        // ctx 析构：global 析构（递归 s → root → global，scope_ref 全归 0）
        //           → GC 析构（collect 回收 v1/v2/v3，无泄漏不 panic）
    }
    // 到这里 ctx 已析构，GC 已销毁，无泄漏 panic 说明顺序正确
}

TEST(ContextTest, DestroyDoesNotDestroyInjectedResources) {
    // allocator/console 是注入资源，Context 析构不应销毁它们
    auto alloc = create_malloc_allocator();
    NullSink sink;
    Console console(&sink);

    {
        auto ctx = create_context(alloc.get(), &console);
        // ctx 析构不应销毁 alloc/console
    }

    // alloc 仍可用（未被 Context 销毁）
    void* p = alloc->alloc(64);
    EXPECT_NE(p, nullptr);
    alloc->free(p);
    // console 仍存活（栈对象，函数结束才销毁）
}

TEST(ContextTest, LookupChainAcrossScopes) {
    auto alloc = create_malloc_allocator();
    NullSink sink;
    Console console(&sink);

    auto ctx = create_context(alloc.get(), &console);
    GCEngine* gc = ctx->gc();
    auto* gv = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* rv = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));

    ctx->global()->add(gv);
    ctx->global()->bind("g", gv);
    ctx->root()->add(rv);
    ctx->root()->bind("r", rv);

    // root 沿链查到 global 的 g
    EXPECT_EQ(ctx->root()->lookup_chain("g"), gv);
    EXPECT_EQ(ctx->root()->lookup_chain("r"), rv);
    // global 查不到 root 的 r
    EXPECT_EQ(ctx->global()->lookup_chain("r"), nullptr);
    // ctx 析构：销毁 global（递归 root）→ 销毁 GC
}
