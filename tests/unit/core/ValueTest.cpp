// value_test.cpp - Value/Data 引用计数与 scope_ref 单元测试
//
// 验证 Value 构造 data->ref++、析构 data->ref--、scope_ref_inc/dec、marked 位。
// Value 内存走 Allocator + placement new，测试手动构造/析构验证引用计数行为。
//
// 关联 ADR: 015（Value meta+data 引用计数分层）、029（生命周期）

#include "core/value.h"
#include "infra/allocator.h"
#include "infra/impl_allocator.h"

#include "test_data.h"

#include <gtest/gtest.h>

#include <new>

namespace {

using neo::core::Data;
using neo::core::Type;
using neo::core::Value;
using neo::core::test::NumberData;
using neo::core::test::TagData;
using neo::core::test::dummy_type;
using neo::infra::MallocAllocator;

// 在 allocator 分配的内存上构造 Value，析构时显式调 ~Value + free
struct ScopedValue {
    MallocAllocator* alloc;
    Value* v;
    ScopedValue(MallocAllocator* a, Type* t, Data* d) : alloc(a) {
        v = new (alloc->alloc(sizeof(Value))) Value(t, d);
    }
    ~ScopedValue() {
        v->~Value();
        alloc->free(v);
    }
};

TEST(ValueDataTest, ConstructIncrementsDataRef) {
    MallocAllocator alloc;
    auto* d = new NumberData(42.0);
    EXPECT_EQ(d->ref(), 0);
    {
        ScopedValue sv(&alloc, dummy_type(), d);
        EXPECT_EQ(d->ref(), 1);
    }
    // Value 析构后 ref 回 0
    EXPECT_EQ(d->ref(), 0);
    delete d;
}

TEST(ValueDataTest, MultipleValuesShareData) {
    MallocAllocator alloc;
    auto* d = new TagData(7);
    EXPECT_EQ(d->ref(), 0);
    {
        ScopedValue sv1(&alloc, dummy_type(), d);
        EXPECT_EQ(d->ref(), 1);
        ScopedValue sv2(&alloc, dummy_type(), d);
        EXPECT_EQ(d->ref(), 2);
    }
    // 两个 Value 都析构后 ref 回 0
    EXPECT_EQ(d->ref(), 0);
    delete d;
}

TEST(ValueDataTest, NullDataIsSafe) {
    MallocAllocator alloc;
    // data=nullptr 应安全（不递增空指针的 ref）
    ScopedValue sv(&alloc, dummy_type(), nullptr);
    EXPECT_EQ(sv.v->data(), nullptr);
}

TEST(ValueDataTest, ScopeRefStartsAtZero) {
    MallocAllocator alloc;
    auto* d = new NumberData(1.0);
    {
        ScopedValue sv(&alloc, dummy_type(), d);
        EXPECT_EQ(sv.v->scope_ref(), 0);
        sv.v->scope_ref_inc();
        EXPECT_EQ(sv.v->scope_ref(), 1);
        sv.v->scope_ref_inc();
        EXPECT_EQ(sv.v->scope_ref(), 2);
        sv.v->scope_ref_dec();
        EXPECT_EQ(sv.v->scope_ref(), 1);
        sv.v->scope_ref_dec();
        EXPECT_EQ(sv.v->scope_ref(), 0);
    }
    // sv 析构（~Value 调 data->ref--）后才能 delete d
    EXPECT_EQ(d->ref(), 0);
    delete d;
}

TEST(ValueDataTest, MarkedFlag) {
    MallocAllocator alloc;
    auto* d = new NumberData(1.0);
    {
        ScopedValue sv(&alloc, dummy_type(), d);
        EXPECT_FALSE(sv.v->marked());
        sv.v->set_marked(true);
        EXPECT_TRUE(sv.v->marked());
        sv.v->set_marked(false);
        EXPECT_FALSE(sv.v->marked());
    }
    delete d;
}

TEST(ValueDataTest, AccessorsReturnCtorArgs) {
    MallocAllocator alloc;
    auto* d = new NumberData(3.14);
    {
        ScopedValue sv(&alloc, dummy_type(), d);
        EXPECT_EQ(sv.v->type(), dummy_type());
        EXPECT_EQ(sv.v->data(), d);
    }
    delete d;
}

}  // namespace
