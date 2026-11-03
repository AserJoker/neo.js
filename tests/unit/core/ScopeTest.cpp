// scope_test.cpp - Scope 跟踪与 scope_ref 联动单元测试
//
// Scope 是堆对象（placement new + IAllocator），析构函数递归销毁子树。
// 测试用 make_scope helper 分配，destroy_scope 调析构 + free。
// 根 scope 析构时递归销毁整棵树（无需手动逐个 destroy）。
//
// 关联 ADR: 012（Scope 树 + scope_ref）、029（生命周期）、030 阶段 2

#include "core/IGCEngine.h"
#include "core/Scope.h"
#include "infra/IAllocator.h"

#include "TestData.h"

#include <gtest/gtest.h>

#include <new>

namespace {

using neo::core::IGCEngine;
using neo::core::Scope;
using neo::core::Value;
using neo::core::test::NumberData;
using neo::core::test::dummy_type;
using neo::infra::IAllocator;
using neo::infra::create_malloc_allocator;

// 在 alloc 上 placement new 构造 Scope（parent 可为 nullptr = global）
Scope* make_scope(IAllocator* alloc, Scope* parent) {
    void* p = alloc->alloc(sizeof(Scope));
    return new (p) Scope(parent, alloc);
}

// 调析构 + free（placement new 对象的标准释放）
// 注意：根 scope 析构会递归销毁所有子，故只需 destroy 根即可。
// 若单独 destroy 中间节点，析构会从父摘除自己 + 递归销毁子树，调用方不需手动摘除。
void destroy_scope(IAllocator* alloc, Scope* s) {
    s->~Scope();
    alloc->free(s);
}

}  // namespace

// —— 基础 Value 跟踪 ——

TEST(ScopeTest, AddIncrementsScopeRef) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    EXPECT_EQ(v->scope_ref(), 0);
    Scope* s = make_scope(alloc.get(), nullptr);
    s->add(v);
    EXPECT_EQ(v->scope_ref(), 1);
    s->add(v);
    EXPECT_EQ(v->scope_ref(), 2);
    EXPECT_EQ(s->value_count(), 2u);
    destroy_scope(alloc.get(), s);  // 析构让 scope_ref 归 0
    EXPECT_EQ(v->scope_ref(), 0);
    gc->collect();
}

TEST(ScopeTest, DestroyDecrementsScopeRef) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    {
        Scope* s = make_scope(alloc.get(), nullptr);
        s->add(v);
        EXPECT_EQ(v->scope_ref(), 1);
        destroy_scope(alloc.get(), s);
        EXPECT_EQ(v->scope_ref(), 0);
    }
    EXPECT_EQ(gc->value_count(), 1u);
    gc->collect();
    EXPECT_EQ(gc->value_count(), 0u);
}

TEST(ScopeTest, BindAndLookup) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* v1 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* v2 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    Scope* s = make_scope(alloc.get(), nullptr);
    s->add(v1);
    s->add(v2);
    s->bind("x", v1);
    s->bind("y", v2);
    EXPECT_EQ(s->lookup("x"), v1);
    EXPECT_EQ(s->lookup("y"), v2);
    EXPECT_EQ(s->lookup("missing"), nullptr);
    EXPECT_EQ(s->named_count(), 2u);
    destroy_scope(alloc.get(), s);
    gc->collect();
}

TEST(ScopeTest, BindOverwrites) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* v1 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* v2 = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    Scope* s = make_scope(alloc.get(), nullptr);
    s->add(v1);
    s->add(v2);
    s->bind("x", v1);
    s->bind("x", v2);
    EXPECT_EQ(s->lookup("x"), v2);
    EXPECT_EQ(s->named_count(), 1u);
    destroy_scope(alloc.get(), s);
    gc->collect();
}

TEST(ScopeTest, DestroyClearsContainers) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    Scope* s = make_scope(alloc.get(), nullptr);
    s->add(v);
    s->bind("x", v);
    destroy_scope(alloc.get(), s);
    EXPECT_EQ(v->scope_ref(), 0);
}

TEST(ScopeTest, MultipleValuesDestroy) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    std::vector<Value*> vs;
    for (int i = 0; i < 5; ++i) {
        vs.push_back(gc->create_value(dummy_type(), gc->alloc_data<NumberData>(i)));
    }
    {
        Scope* s = make_scope(alloc.get(), nullptr);
        for (auto* v : vs) s->add(v);
        for (auto* v : vs) EXPECT_EQ(v->scope_ref(), 1);
        destroy_scope(alloc.get(), s);
    }
    for (auto* v : vs) EXPECT_EQ(v->scope_ref(), 0);
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
    Scope* s = make_scope(alloc.get(), nullptr);
    s->add(v1);
    s->add(v2);
    int count = 0;
    s->for_each_value([&count](Value*) { ++count; });
    EXPECT_EQ(count, 2);
    destroy_scope(alloc.get(), s);
    gc->collect();
}

// —— Scope 树结构 ——

TEST(ScopeTreeTest, ParentChildLink) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    Scope* root = make_scope(alloc.get(), nullptr);
    Scope* child1 = make_scope(alloc.get(), root);
    Scope* child2 = make_scope(alloc.get(), root);
    root->add_child(child1);
    root->add_child(child2);
    EXPECT_EQ(root->parent(), nullptr);
    EXPECT_EQ(child1->parent(), root);
    EXPECT_EQ(child2->parent(), root);
    EXPECT_EQ(root->child_count(), 2u);
    EXPECT_EQ(child1->child_count(), 0u);
    // 根析构递归销毁 child1/child2
    destroy_scope(alloc.get(), root);
    gc->collect();
}

TEST(ScopeTreeTest, DestroyRecursivelyReleasesChildren) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* root_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* child_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    auto* grand_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(3.0));

    Scope* root = make_scope(alloc.get(), nullptr);
    Scope* child = make_scope(alloc.get(), root);
    Scope* grandchild = make_scope(alloc.get(), child);
    root->add_child(child);
    child->add_child(grandchild);

    root->add(root_v);
    child->add(child_v);
    grandchild->add(grand_v);

    EXPECT_EQ(root_v->scope_ref(), 1);
    EXPECT_EQ(child_v->scope_ref(), 1);
    EXPECT_EQ(grand_v->scope_ref(), 1);

    // 根析构递归：grandchild → child → root，逐层 scope_ref 归 0
    destroy_scope(alloc.get(), root);

    EXPECT_EQ(root_v->scope_ref(), 0);
    EXPECT_EQ(child_v->scope_ref(), 0);
    EXPECT_EQ(grand_v->scope_ref(), 0);

    EXPECT_EQ(gc->value_count(), 3u);
    gc->collect();
    EXPECT_EQ(gc->value_count(), 0u);
}

TEST(ScopeTreeTest, MidNodeDestroyRemovesSelfFromParent) {
    // 中间节点析构时从父 children_ 摘除自己——验证无悬垂指针
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    Scope* root = make_scope(alloc.get(), nullptr);
    Scope* child = make_scope(alloc.get(), root);
    root->add_child(child);
    EXPECT_EQ(root->child_count(), 1u);

    destroy_scope(alloc.get(), child);  // child 析构从 root 摘除自己
    EXPECT_EQ(root->child_count(), 0u); // root 不悬垂

    destroy_scope(alloc.get(), root);
    gc->collect();
}

TEST(ScopeTreeTest, RemoveChildIsIdempotent) {
    auto alloc = create_malloc_allocator();
    Scope* root = make_scope(alloc.get(), nullptr);
    Scope* child = make_scope(alloc.get(), root);
    root->add_child(child);
    root->remove_child(child);
    EXPECT_EQ(root->child_count(), 0u);
    root->remove_child(child);  // 再次 remove 不崩溃
    EXPECT_EQ(root->child_count(), 0u);
    // child 未被父持有，手动 destroy
    destroy_scope(alloc.get(), child);
    destroy_scope(alloc.get(), root);
}

TEST(ScopeTreeTest, LookupChainFindsInParent) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* global_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* root_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));

    Scope* global = make_scope(alloc.get(), nullptr);
    Scope* root = make_scope(alloc.get(), global);
    global->add_child(root);

    global->add(global_v);
    global->bind("pi", global_v);
    root->add(root_v);
    root->bind("x", root_v);

    EXPECT_EQ(root->lookup_chain("x"), root_v);
    EXPECT_EQ(root->lookup_chain("pi"), global_v);
    EXPECT_EQ(global->lookup_chain("x"), nullptr);
    EXPECT_EQ(root->lookup_chain("missing"), nullptr);

    destroy_scope(alloc.get(), global);  // 递归销毁 root
    gc->collect();
}

TEST(ScopeTreeTest, LookupChainThreeLevels) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* gv = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* rv = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    auto* bv = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(3.0));

    Scope* global = make_scope(alloc.get(), nullptr);
    Scope* root = make_scope(alloc.get(), global);
    Scope* body = make_scope(alloc.get(), root);
    global->add_child(root);
    root->add_child(body);

    global->add(gv);
    global->bind("g", gv);
    root->add(rv);
    root->bind("r", rv);
    body->add(bv);
    body->bind("b", bv);

    EXPECT_EQ(body->lookup_chain("b"), bv);
    EXPECT_EQ(body->lookup_chain("r"), rv);
    EXPECT_EQ(body->lookup_chain("g"), gv);

    destroy_scope(alloc.get(), global);  // 递归销毁 root → body
    EXPECT_EQ(gv->scope_ref(), 0);
    EXPECT_EQ(rv->scope_ref(), 0);
    EXPECT_EQ(bv->scope_ref(), 0);
    gc->collect();
}

// —— 中间节点析构：销毁子树、父不悬垂、兄弟不受影响 ——

TEST(ScopeTreeTest, MidNodeDestroySubtreeAndKeepParentValid) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    // 结构：global → root → mid → leaf1, leaf2
    auto* g_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* r_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    auto* m_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(3.0));
    auto* l1_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(4.0));
    auto* l2_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(5.0));

    Scope* global = make_scope(alloc.get(), nullptr);
    Scope* root = make_scope(alloc.get(), global);
    Scope* mid = make_scope(alloc.get(), root);
    Scope* leaf1 = make_scope(alloc.get(), mid);
    Scope* leaf2 = make_scope(alloc.get(), mid);
    global->add_child(root);
    root->add_child(mid);
    mid->add_child(leaf1);
    mid->add_child(leaf2);

    global->add(g_v);
    root->add(r_v);
    mid->add(m_v);
    leaf1->add(l1_v);
    leaf2->add(l2_v);

    EXPECT_EQ(root->child_count(), 1u);
    EXPECT_EQ(mid->child_count(), 2u);

    // 中间节点 mid 析构：
    //   - mid 从 root 的 children_ 摘除（root 不悬垂）
    //   - mid 的子树 leaf1/leaf2 递归析构 + free 内存
    //   - mid 自己的 values 释放
    destroy_scope(alloc.get(), mid);

    EXPECT_EQ(root->child_count(), 0u);
    EXPECT_EQ(m_v->scope_ref(), 0);
    EXPECT_EQ(l1_v->scope_ref(), 0);
    EXPECT_EQ(l2_v->scope_ref(), 0);
    EXPECT_EQ(r_v->scope_ref(), 1);
    EXPECT_EQ(g_v->scope_ref(), 1);

    EXPECT_EQ(root->parent(), global);

    destroy_scope(alloc.get(), global);  // 递归销毁 root
    gc->collect();
}

TEST(ScopeTreeTest, MidNodeDestroySiblingUnaffected) {
    auto alloc = create_malloc_allocator();
    auto gc = neo::core::create_gc_engine(alloc.get());
    auto* a_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(1.0));
    auto* a_child_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(2.0));
    auto* b_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(3.0));
    auto* b_child_v = gc->create_value(dummy_type(), gc->alloc_data<NumberData>(4.0));

    Scope* root = make_scope(alloc.get(), nullptr);
    Scope* childA = make_scope(alloc.get(), root);
    Scope* childB = make_scope(alloc.get(), root);
    Scope* aLeaf = make_scope(alloc.get(), childA);
    Scope* bLeaf = make_scope(alloc.get(), childB);
    root->add_child(childA);
    root->add_child(childB);
    childA->add_child(aLeaf);
    childB->add_child(bLeaf);

    childA->add(a_v);
    aLeaf->add(a_child_v);
    childB->add(b_v);
    bLeaf->add(b_child_v);

    // childA 析构：销毁自身 + aLeaf，root 和 childB 链不受影响
    destroy_scope(alloc.get(), childA);

    EXPECT_EQ(root->child_count(), 1u);
    EXPECT_EQ(a_v->scope_ref(), 0);
    EXPECT_EQ(a_child_v->scope_ref(), 0);
    EXPECT_EQ(b_v->scope_ref(), 1);
    EXPECT_EQ(b_child_v->scope_ref(), 1);

    // childB 仍可用
    destroy_scope(alloc.get(), root);  // 递归销毁 childB → bLeaf
    EXPECT_EQ(b_v->scope_ref(), 0);
    EXPECT_EQ(b_child_v->scope_ref(), 0);
    gc->collect();
}

TEST(ScopeTreeTest, ChildrenSetDeduplicates) {
    auto alloc = create_malloc_allocator();
    Scope* root = make_scope(alloc.get(), nullptr);
    Scope* child = make_scope(alloc.get(), root);
    root->add_child(child);
    root->add_child(child);
    root->add_child(child);
    EXPECT_EQ(root->child_count(), 1u);
    destroy_scope(alloc.get(), root);  // 递归销毁 child
}
