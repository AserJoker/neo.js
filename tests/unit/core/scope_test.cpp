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
