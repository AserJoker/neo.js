// context_test.cpp - Context 门面单元测试
//
// 验证：
// - Context 创建后 global/root/current 三层 scope 就绪
// - global 是根（parent=nullptr），root 的 parent=global，current 初始=root
// - push_scope/pop_scope 管理 current 栈，pop 销毁弹出的 scope
// - 析构先销毁 global_scope（递归销毁整棵树），gc/alloc/console 不被销毁（外部持有）
// - 注入资源访问（allocator/gc/console）正确返回
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
using neo::core::create_gc_engine;
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
    auto gc = create_gc_engine(alloc.get());
    NullSink sink;
    Console console(&sink);

    auto ctx = create_context(alloc.get(), gc.get(), &console);
    EXPECT_NE(ctx->global(), nullptr);
    EXPECT_NE(ctx->root(), nullptr);
    EXPECT_EQ(ctx->current(), ctx->root());  // current 初始 = root

    EXPECT_EQ(ctx->global()->parent(), nullptr);     // global 是根
    EXPECT_EQ(ctx->root()->parent(), ctx->global()); // root 的 parent = global
    EXPECT_EQ(ctx->global()->child_count(), 1u);     // global 持有 root
    // ctx 析构销毁 global（递归 root），gc/alloc/console 外部销毁
}

TEST(ContextTest, InjectedResourcesAccessible) {
    auto alloc = create_malloc_allocator();
    auto gc = create_gc_engine(alloc.get());
    NullSink sink;
    Console console(&sink);

    auto ctx = create_context(alloc.get(), gc.get(), &console);
    EXPECT_EQ(ctx->allocator(), alloc.get());
    EXPECT_EQ(ctx->gc(), gc.get());
    EXPECT_EQ(ctx->console(), &console);
}

TEST(ContextTest, PushPopScope) {
    auto alloc = create_malloc_allocator();
    auto gc = create_gc_engine(alloc.get());
    NullSink sink;
    Console console(&sink);

    auto ctx = create_context(alloc.get(), gc.get(), &console);
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
    // ctx 析构销毁 global（递归 root）
}

TEST(ContextTest, PopRootIsNoop) {
    auto alloc = create_malloc_allocator();
    auto gc = create_gc_engine(alloc.get());
    NullSink sink;
    Console console(&sink);

    auto ctx = create_context(alloc.get(), gc.get(), &console);
    EXPECT_EQ(ctx->current(), ctx->root());

    // pop 栈底 root：无操作，current 仍是 root
    ctx->pop_scope();
    EXPECT_EQ(ctx->current(), ctx->root());
    EXPECT_EQ(ctx->root()->parent(), ctx->global());
}

TEST(ContextTest, DestroyReleasesEntireScopeTree) {
    auto alloc = create_malloc_allocator();
    auto gc = create_gc_engine(alloc.get());
    NullSink sink;
    Console console(&sink);

    auto* v1 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* v2 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    auto* v3 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(3.0));

    {
        auto ctx = create_context(alloc.get(), gc.get(), &console);
        // global 持 v1，root 持 v2，push 一个 scope 持 v3
        ctx->global()->add(v1);
        ctx->root()->add(v2);

        Scope* s = make_scope(alloc.get(), ctx->current());
        ctx->current()->add_child(s);
        ctx->push_scope(s);
        s->add(v3);

        EXPECT_EQ(v1->scope_ref(), 1);
        EXPECT_EQ(v2->scope_ref(), 1);
        EXPECT_EQ(v3->scope_ref(), 1);

        // ctx 析构：销毁 global（递归销毁 s → root → global），所有 scope_ref 归 0
    }

    EXPECT_EQ(v1->scope_ref(), 0);
    EXPECT_EQ(v2->scope_ref(), 0);
    EXPECT_EQ(v3->scope_ref(), 0);
    // gc 仍存活（外部持有），collect 回收所有 Value
    EXPECT_EQ(gc->value_count(), 3u);
    gc->collect();
    EXPECT_EQ(gc->value_count(), 0u);
}

TEST(ContextTest, DestroyDoesNotDestroyInjectedResources) {
    auto alloc = create_malloc_allocator();
    auto gc = create_gc_engine(alloc.get());
    NullSink sink;
    Console console(&sink);

    {
        auto ctx = create_context(alloc.get(), gc.get(), &console);
        // ctx 析构不应销毁 gc/alloc/console
    }

    // gc 仍可用（未被 Context 销毁）
    auto* v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(42.0));
    EXPECT_NE(v, nullptr);
    EXPECT_EQ(gc->value_count(), 1u);
    gc->collect();
    EXPECT_EQ(gc->value_count(), 0u);
    // alloc/console 仍存活（栈对象，函数结束才销毁）
}

TEST(ContextTest, LookupChainAcrossScopes) {
    auto alloc = create_malloc_allocator();
    auto gc = create_gc_engine(alloc.get());
    NullSink sink;
    Console console(&sink);

    auto ctx = create_context(alloc.get(), gc.get(), &console);
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
    // ctx 析构销毁 global（递归 root）
}
