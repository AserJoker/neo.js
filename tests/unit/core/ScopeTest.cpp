// scope_test.cpp - Scope 跟踪与 scopeRef 联动单元测试
//
// Scope 是堆对象（placement new + IAllocator），析构函数递归销毁子树。
// 测试用 makeScope helper 分配，destroyScope 调析构 + free。
// 根 scope 析构时递归销毁整棵树（无需手动逐个 destroy）。
//
// 关联 ADR: 012（Scope 树 + scopeRef）、029（生命周期）、030 阶段 2

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
using neo::core::test::dummyType;
using neo::infra::IAllocator;
using neo::infra::createMallocAllocator;

// 在 alloc 上 placement new 构造 Scope（parent 可为 nullptr = global）
Scope* makeScope(IAllocator* alloc, Scope* parent) {
    void* p = alloc->alloc(sizeof(Scope));
    return new (p) Scope(parent, alloc);
}

// 调析构 + free（placement new 对象的标准释放）
// 注意：根 scope 析构会递归销毁所有子，故只需 destroy 根即可。
// 若单独 destroy 中间节点，析构会从父摘除自己 + 递归销毁子树，调用方不需手动摘除。
void destroyScope(IAllocator* alloc, Scope* s) {
    s->~Scope();
    alloc->free(s);
}

}  // namespace

// —— 基础 Value 跟踪 ——

TEST(ScopeTest, AddIncrementsScopeRef) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    EXPECT_EQ(v->scopeRef(), 0);
    Scope* s = makeScope(alloc.get(), nullptr);
    s->add(v);
    EXPECT_EQ(v->scopeRef(), 1);
    s->add(v);
    EXPECT_EQ(v->scopeRef(), 2);
    EXPECT_EQ(s->valueCount(), 2u);
    destroyScope(alloc.get(), s);  // 析构让 scopeRef 归 0
    EXPECT_EQ(v->scopeRef(), 0);
    gc->collect();
}

TEST(ScopeTest, DestroyDecrementsScopeRef) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    {
        Scope* s = makeScope(alloc.get(), nullptr);
        s->add(v);
        EXPECT_EQ(v->scopeRef(), 1);
        destroyScope(alloc.get(), s);
        EXPECT_EQ(v->scopeRef(), 0);
    }
    EXPECT_EQ(gc->valueCount(), 1u);
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
}

TEST(ScopeTest, BindAndLookup) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v1 = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    auto* v2 = gc->createValue(dummyType(), gc->allocData<NumberData>(2.0));
    Scope* s = makeScope(alloc.get(), nullptr);
    s->add(v1);
    s->add(v2);
    s->bind("x", v1);
    s->bind("y", v2);
    EXPECT_EQ(s->lookup("x"), v1);
    EXPECT_EQ(s->lookup("y"), v2);
    EXPECT_EQ(s->lookup("missing"), nullptr);
    EXPECT_EQ(s->namedCount(), 2u);
    destroyScope(alloc.get(), s);
    gc->collect();
}

TEST(ScopeTest, BindOverwrites) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v1 = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    auto* v2 = gc->createValue(dummyType(), gc->allocData<NumberData>(2.0));
    Scope* s = makeScope(alloc.get(), nullptr);
    s->add(v1);
    s->add(v2);
    s->bind("x", v1);
    s->bind("x", v2);
    EXPECT_EQ(s->lookup("x"), v2);
    EXPECT_EQ(s->namedCount(), 1u);
    destroyScope(alloc.get(), s);
    gc->collect();
}

TEST(ScopeTest, DestroyClearsContainers) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    Scope* s = makeScope(alloc.get(), nullptr);
    s->add(v);
    s->bind("x", v);
    destroyScope(alloc.get(), s);
    EXPECT_EQ(v->scopeRef(), 0);
}

TEST(ScopeTest, MultipleValuesDestroy) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    std::vector<Value*> vs;
    for (int i = 0; i < 5; ++i) {
        vs.push_back(gc->createValue(dummyType(), gc->allocData<NumberData>(i)));
    }
    {
        Scope* s = makeScope(alloc.get(), nullptr);
        for (auto* v : vs) s->add(v);
        for (auto* v : vs) EXPECT_EQ(v->scopeRef(), 1);
        destroyScope(alloc.get(), s);
    }
    for (auto* v : vs) EXPECT_EQ(v->scopeRef(), 0);
    EXPECT_EQ(gc->valueCount(), 5u);
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
    EXPECT_EQ(gc->collectedCount(), 5u);
}

TEST(ScopeTest, ForEachValue) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v1 = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    auto* v2 = gc->createValue(dummyType(), gc->allocData<NumberData>(2.0));
    Scope* s = makeScope(alloc.get(), nullptr);
    s->add(v1);
    s->add(v2);
    int count = 0;
    s->forEachValue([&count](Value*) { ++count; });
    EXPECT_EQ(count, 2);
    destroyScope(alloc.get(), s);
    gc->collect();
}

// —— Scope 树结构 ——

TEST(ScopeTreeTest, ParentChildLink) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    Scope* root = makeScope(alloc.get(), nullptr);
    Scope* child1 = makeScope(alloc.get(), root);
    Scope* child2 = makeScope(alloc.get(), root);
    root->addChild(child1);
    root->addChild(child2);
    EXPECT_EQ(root->parent(), nullptr);
    EXPECT_EQ(child1->parent(), root);
    EXPECT_EQ(child2->parent(), root);
    EXPECT_EQ(root->childCount(), 2u);
    EXPECT_EQ(child1->childCount(), 0u);
    // 根析构递归销毁 child1/child2
    destroyScope(alloc.get(), root);
    gc->collect();
}

TEST(ScopeTreeTest, DestroyRecursivelyReleasesChildren) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* root_v = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    auto* child_v = gc->createValue(dummyType(), gc->allocData<NumberData>(2.0));
    auto* grand_v = gc->createValue(dummyType(), gc->allocData<NumberData>(3.0));

    Scope* root = makeScope(alloc.get(), nullptr);
    Scope* child = makeScope(alloc.get(), root);
    Scope* grandchild = makeScope(alloc.get(), child);
    root->addChild(child);
    child->addChild(grandchild);

    root->add(root_v);
    child->add(child_v);
    grandchild->add(grand_v);

    EXPECT_EQ(root_v->scopeRef(), 1);
    EXPECT_EQ(child_v->scopeRef(), 1);
    EXPECT_EQ(grand_v->scopeRef(), 1);

    // 根析构递归：grandchild → child → root，逐层 scopeRef 归 0
    destroyScope(alloc.get(), root);

    EXPECT_EQ(root_v->scopeRef(), 0);
    EXPECT_EQ(child_v->scopeRef(), 0);
    EXPECT_EQ(grand_v->scopeRef(), 0);

    EXPECT_EQ(gc->valueCount(), 3u);
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
}

TEST(ScopeTreeTest, MidNodeDestroyRemovesSelfFromParent) {
    // 中间节点析构时从父 children_ 摘除自己——验证无悬垂指针
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    Scope* root = makeScope(alloc.get(), nullptr);
    Scope* child = makeScope(alloc.get(), root);
    root->addChild(child);
    EXPECT_EQ(root->childCount(), 1u);

    destroyScope(alloc.get(), child);  // child 析构从 root 摘除自己
    EXPECT_EQ(root->childCount(), 0u); // root 不悬垂

    destroyScope(alloc.get(), root);
    gc->collect();
}

TEST(ScopeTreeTest, RemoveChildIsIdempotent) {
    auto alloc = createMallocAllocator();
    Scope* root = makeScope(alloc.get(), nullptr);
    Scope* child = makeScope(alloc.get(), root);
    root->addChild(child);
    root->removeChild(child);
    EXPECT_EQ(root->childCount(), 0u);
    root->removeChild(child);  // 再次 remove 不崩溃
    EXPECT_EQ(root->childCount(), 0u);
    // child 未被父持有，手动 destroy
    destroyScope(alloc.get(), child);
    destroyScope(alloc.get(), root);
}

TEST(ScopeTreeTest, LookupChainFindsInParent) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* global_v = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    auto* root_v = gc->createValue(dummyType(), gc->allocData<NumberData>(2.0));

    Scope* global = makeScope(alloc.get(), nullptr);
    Scope* root = makeScope(alloc.get(), global);
    global->addChild(root);

    global->add(global_v);
    global->bind("pi", global_v);
    root->add(root_v);
    root->bind("x", root_v);

    EXPECT_EQ(root->lookupChain("x"), root_v);
    EXPECT_EQ(root->lookupChain("pi"), global_v);
    EXPECT_EQ(global->lookupChain("x"), nullptr);
    EXPECT_EQ(root->lookupChain("missing"), nullptr);

    destroyScope(alloc.get(), global);  // 递归销毁 root
    gc->collect();
}

TEST(ScopeTreeTest, LookupChainThreeLevels) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* gv = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    auto* rv = gc->createValue(dummyType(), gc->allocData<NumberData>(2.0));
    auto* bv = gc->createValue(dummyType(), gc->allocData<NumberData>(3.0));

    Scope* global = makeScope(alloc.get(), nullptr);
    Scope* root = makeScope(alloc.get(), global);
    Scope* body = makeScope(alloc.get(), root);
    global->addChild(root);
    root->addChild(body);

    global->add(gv);
    global->bind("g", gv);
    root->add(rv);
    root->bind("r", rv);
    body->add(bv);
    body->bind("b", bv);

    EXPECT_EQ(body->lookupChain("b"), bv);
    EXPECT_EQ(body->lookupChain("r"), rv);
    EXPECT_EQ(body->lookupChain("g"), gv);

    destroyScope(alloc.get(), global);  // 递归销毁 root → body
    EXPECT_EQ(gv->scopeRef(), 0);
    EXPECT_EQ(rv->scopeRef(), 0);
    EXPECT_EQ(bv->scopeRef(), 0);
    gc->collect();
}

// —— 中间节点析构：销毁子树、父不悬垂、兄弟不受影响 ——

TEST(ScopeTreeTest, MidNodeDestroySubtreeAndKeepParentValid) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    // 结构：global → root → mid → leaf1, leaf2
    auto* g_v = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    auto* r_v = gc->createValue(dummyType(), gc->allocData<NumberData>(2.0));
    auto* m_v = gc->createValue(dummyType(), gc->allocData<NumberData>(3.0));
    auto* l1_v = gc->createValue(dummyType(), gc->allocData<NumberData>(4.0));
    auto* l2_v = gc->createValue(dummyType(), gc->allocData<NumberData>(5.0));

    Scope* global = makeScope(alloc.get(), nullptr);
    Scope* root = makeScope(alloc.get(), global);
    Scope* mid = makeScope(alloc.get(), root);
    Scope* leaf1 = makeScope(alloc.get(), mid);
    Scope* leaf2 = makeScope(alloc.get(), mid);
    global->addChild(root);
    root->addChild(mid);
    mid->addChild(leaf1);
    mid->addChild(leaf2);

    global->add(g_v);
    root->add(r_v);
    mid->add(m_v);
    leaf1->add(l1_v);
    leaf2->add(l2_v);

    EXPECT_EQ(root->childCount(), 1u);
    EXPECT_EQ(mid->childCount(), 2u);

    // 中间节点 mid 析构：
    //   - mid 从 root 的 children_ 摘除（root 不悬垂）
    //   - mid 的子树 leaf1/leaf2 递归析构 + free 内存
    //   - mid 自己的 values 释放
    destroyScope(alloc.get(), mid);

    EXPECT_EQ(root->childCount(), 0u);
    EXPECT_EQ(m_v->scopeRef(), 0);
    EXPECT_EQ(l1_v->scopeRef(), 0);
    EXPECT_EQ(l2_v->scopeRef(), 0);
    EXPECT_EQ(r_v->scopeRef(), 1);
    EXPECT_EQ(g_v->scopeRef(), 1);

    EXPECT_EQ(root->parent(), global);

    destroyScope(alloc.get(), global);  // 递归销毁 root
    gc->collect();
}

TEST(ScopeTreeTest, MidNodeDestroySiblingUnaffected) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* a_v = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    auto* a_child_v = gc->createValue(dummyType(), gc->allocData<NumberData>(2.0));
    auto* b_v = gc->createValue(dummyType(), gc->allocData<NumberData>(3.0));
    auto* b_child_v = gc->createValue(dummyType(), gc->allocData<NumberData>(4.0));

    Scope* root = makeScope(alloc.get(), nullptr);
    Scope* childA = makeScope(alloc.get(), root);
    Scope* childB = makeScope(alloc.get(), root);
    Scope* aLeaf = makeScope(alloc.get(), childA);
    Scope* bLeaf = makeScope(alloc.get(), childB);
    root->addChild(childA);
    root->addChild(childB);
    childA->addChild(aLeaf);
    childB->addChild(bLeaf);

    childA->add(a_v);
    aLeaf->add(a_child_v);
    childB->add(b_v);
    bLeaf->add(b_child_v);

    // childA 析构：销毁自身 + aLeaf，root 和 childB 链不受影响
    destroyScope(alloc.get(), childA);

    EXPECT_EQ(root->childCount(), 1u);
    EXPECT_EQ(a_v->scopeRef(), 0);
    EXPECT_EQ(a_child_v->scopeRef(), 0);
    EXPECT_EQ(b_v->scopeRef(), 1);
    EXPECT_EQ(b_child_v->scopeRef(), 1);

    // childB 仍可用
    destroyScope(alloc.get(), root);  // 递归销毁 childB → bLeaf
    EXPECT_EQ(b_v->scopeRef(), 0);
    EXPECT_EQ(b_child_v->scopeRef(), 0);
    gc->collect();
}

TEST(ScopeTreeTest, ChildrenSetDeduplicates) {
    auto alloc = createMallocAllocator();
    Scope* root = makeScope(alloc.get(), nullptr);
    Scope* child = makeScope(alloc.get(), root);
    root->addChild(child);
    root->addChild(child);
    root->addChild(child);
    EXPECT_EQ(root->childCount(), 1u);
    destroyScope(alloc.get(), root);  // 递归销毁 child
}
