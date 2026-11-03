// scope_test.cpp - Scope 跟踪与 scope_ref 联动单元测试
//
// 验证 add 后 scope_ref++、on_exit 后 scope_ref-- 归 0、bind/lookup 命名绑定、
// 与 GC collect 联动（on_exit 后 scope_ref=0 的 Value 被回收）。
//
// 关联 ADR: 012（Scope 树 + scope_ref）、029（生命周期）

#include "core/gc.h"
#include "core/scope.h"
#include "infra/allocator.h"

#include "test_data.h"

#include <gtest/gtest.h>

namespace {

using neo::core::GCEngine;
using neo::core::Scope;
using neo::core::Value;
using neo::core::test::NumberData;
using neo::core::test::dummy_type;
using neo::infra::create_malloc_allocator;

}  // namespace

TEST(ScopeTest, AddIncrementsScopeRef) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    EXPECT_EQ(v->scope_ref(), 0);
    Scope s;
    s.add(v);
    EXPECT_EQ(v->scope_ref(), 1);
    s.add(v);  // 同一 Value 多次 add，scope_ref 递增多次
    EXPECT_EQ(v->scope_ref(), 2);
    EXPECT_EQ(s.value_count(), 2u);
    // 清理：on_exit 让 scope_ref 归 0，collect 回收，避免 gc 析构 panic
    s.on_exit();
    gc->collect();
}

TEST(ScopeTest, OnExitDecrementsScopeRef) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    {
        Scope s;
        s.add(v);
        EXPECT_EQ(v->scope_ref(), 1);
        // 作用域退出
        s.on_exit();
        EXPECT_EQ(v->scope_ref(), 0);
    }
    // on_exit 后 scope_ref=0，GC collect 应回收
    EXPECT_EQ(gc->value_count(), 1u);
    gc->collect();
    EXPECT_EQ(gc->value_count(), 0u);
}

TEST(ScopeTest, BindAndLookup) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* v1 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* v2 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    Scope s;
    s.add(v1);
    s.add(v2);
    s.bind("x", v1);
    s.bind("y", v2);
    EXPECT_EQ(s.lookup("x"), v1);
    EXPECT_EQ(s.lookup("y"), v2);
    EXPECT_EQ(s.lookup("missing"), nullptr);
    EXPECT_EQ(s.named_count(), 2u);
    s.on_exit();
    gc->collect();
}

TEST(ScopeTest, BindOverwrites) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* v1 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* v2 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    Scope s;
    s.add(v1);
    s.add(v2);
    s.bind("x", v1);
    s.bind("x", v2);  // 覆盖
    EXPECT_EQ(s.lookup("x"), v2);
    EXPECT_EQ(s.named_count(), 1u);
    s.on_exit();
    gc->collect();
}

TEST(ScopeTest, OnExitClearsContainers) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    Scope s;
    s.add(v);
    s.bind("x", v);
    s.on_exit();
    EXPECT_EQ(s.value_count(), 0u);
    EXPECT_EQ(s.named_count(), 0u);
    EXPECT_EQ(v->scope_ref(), 0);
}

TEST(ScopeTest, MultipleValuesOnExit) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    std::vector<Value*> vs;
    for (int i = 0; i < 5; ++i) {
        vs.push_back(gc->create_value(dummy_type(), gc->alloc_data<NumberData>(i)));
    }
    {
        Scope s;
        for (auto* v : vs) s.add(v);
        for (auto* v : vs) EXPECT_EQ(v->scope_ref(), 1);
        s.on_exit();
    }
    for (auto* v : vs) EXPECT_EQ(v->scope_ref(), 0);
    // 全部 scope_ref=0，collect 后全回收
    EXPECT_EQ(gc->value_count(), 5u);
    gc->collect();
    EXPECT_EQ(gc->value_count(), 0u);
    EXPECT_EQ(gc->collected_count(), 5u);
}

TEST(ScopeTest, ForEachValue) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* v1 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* v2 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    Scope s;
    s.add(v1);
    s.add(v2);
    int count = 0;
    s.for_each_value([&count](Value*) { ++count; });
    EXPECT_EQ(count, 2);
    s.on_exit();
    gc->collect();
}

// —— Scope 树结构测试 ——

TEST(ScopeTreeTest, ParentChildLink) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    Scope root;
    Scope child1(&root);
    Scope child2(&root);
    root.add_child(&child1);
    root.add_child(&child2);
    EXPECT_EQ(root.parent(), nullptr);
    EXPECT_EQ(child1.parent(), &root);
    EXPECT_EQ(child2.parent(), &root);
    EXPECT_EQ(root.child_count(), 2u);
    EXPECT_EQ(child1.child_count(), 0u);
    // 清理：root.on_exit 递归释放 children
    root.on_exit();
    EXPECT_EQ(root.child_count(), 0u);
    gc->collect();
}

TEST(ScopeTreeTest, OnExitRecursivelyReleasesChildren) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* root_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* child_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    auto* grand_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(3.0));

    Scope root;
    Scope child(&root);
    Scope grandchild(&child);
    root.add_child(&child);
    child.add_child(&grandchild);

    root.add(root_v);
    child.add(child_v);
    grandchild.add(grand_v);

    EXPECT_EQ(root_v->scope_ref(), 1);
    EXPECT_EQ(child_v->scope_ref(), 1);
    EXPECT_EQ(grand_v->scope_ref(), 1);

    // root.on_exit 递归：grandchild → child → root，逐层 scope_ref 归 0
    root.on_exit();

    EXPECT_EQ(root_v->scope_ref(), 0);
    EXPECT_EQ(child_v->scope_ref(), 0);
    EXPECT_EQ(grand_v->scope_ref(), 0);
    EXPECT_EQ(root.child_count(), 0u);

    // 全部 scope_ref=0，collect 后全回收
    EXPECT_EQ(gc->value_count(), 3u);
    gc->collect();
    EXPECT_EQ(gc->value_count(), 0u);
}

TEST(ScopeTreeTest, OnExitRemovesSelfFromParent) {
    // 子作用域 on_exit 时从父的 children_ 摘除自己——验证无悬垂指针
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    Scope root;
    Scope child(&root);
    root.add_child(&child);
    EXPECT_EQ(root.child_count(), 1u);

    child.on_exit();  // 子主动退出：从 root 摘除自己
    EXPECT_EQ(root.child_count(), 0u);  // root 不再持有 child
    // root 仍可用，无悬垂
    EXPECT_EQ(root.child_count(), 0u);
    root.on_exit();
    gc->collect();
}

TEST(ScopeTreeTest, RemoveChildIsIdempotent) {
    Scope root;
    Scope child(&root);
    root.add_child(&child);
    root.remove_child(&child);
    EXPECT_EQ(root.child_count(), 0u);
    // 再次 remove 不崩溃、无操作
    root.remove_child(&child);
    EXPECT_EQ(root.child_count(), 0u);
}

TEST(ScopeTreeTest, LookupChainFindsInParent) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* global_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* root_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));

    Scope global;
    Scope root(&global);
    global.add_child(&root);

    global.add(global_v);
    global.bind("pi", global_v);
    root.add(root_v);
    root.bind("x", root_v);

    // root 能查到自己的 x
    EXPECT_EQ(root.lookup_chain("x"), root_v);
    // root 沿 parent 链查到 global 的 pi
    EXPECT_EQ(root.lookup_chain("pi"), global_v);
    // global 查不到 root 的 x（不向下查找）
    EXPECT_EQ(global.lookup_chain("x"), nullptr);
    // 都查不到的变量
    EXPECT_EQ(root.lookup_chain("missing"), nullptr);

    root.on_exit();   // root 从 global 摘除 + 释放自己
    global.on_exit();
    gc->collect();
}

TEST(ScopeTreeTest, LookupChainThreeLevels) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* gv = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* rv = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    auto* bv = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(3.0));

    Scope global;
    Scope root(&global);
    Scope body(&root);
    global.add_child(&root);
    root.add_child(&body);

    global.add(gv);
    global.bind("g", gv);
    root.add(rv);
    root.bind("r", rv);
    body.add(bv);
    body.bind("b", bv);

    // body 沿链查到每一层
    EXPECT_EQ(body.lookup_chain("b"), bv);
    EXPECT_EQ(body.lookup_chain("r"), rv);
    EXPECT_EQ(body.lookup_chain("g"), gv);

    // 仅 global.on_exit 应递归释放 root 和 body
    global.on_exit();
    EXPECT_EQ(gv->scope_ref(), 0);
    EXPECT_EQ(rv->scope_ref(), 0);
    EXPECT_EQ(bv->scope_ref(), 0);
    gc->collect();
}
