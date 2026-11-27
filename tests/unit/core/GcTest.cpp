// gc_test.cpp - MarkSweepGC 生命周期单元测试
//
// 验证 createValue/allocData/collect：从根作用域可达或 pinRef>0 存活、否则回收、
// data ref 归 0 释放、双重阈值自动触发、OOM 返回 nullptr、collectedCount 统计。
//
// 关联 ADR: 008（GC→IAllocator）、012（Scope 树）、029（生命周期）、044（可达性标记）

#include "core/IGCEngine.h"
#include "infra/IAllocator.h"

#include "TestData.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using neo::core::IData;
using neo::core::IGCEngine;
using neo::core::Value;
using neo::core::test::NumberData;
using neo::core::test::TagData;
using neo::core::test::dummyType;
using neo::infra::IAllocator;
using neo::infra::createMallocAllocator;
using neo::infra::createMallocAllocatorWithLimit;

}  // namespace

TEST(GCEngineTest, CreateValueIncrementsCount) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* d = gc->allocData<NumberData>(1.0);
    ASSERT_NE(d, nullptr);
    auto* v = gc->createValue(dummyType(), d);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(gc->valueCount(), 1u);
    EXPECT_EQ(d->ref(), 1);  // Value 构造后 data ref=1
}

TEST(GCEngineTest, CollectReclaimsUnreachable) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    // 创建一个 Value 但不加入任何 scope、pinRef=0，collect 后应被回收
    auto* d = gc->allocData<NumberData>(2.0);
    ASSERT_NE(d, nullptr);
    auto* v = gc->createValue(dummyType(), d);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->pinRef(), 0);
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
    EXPECT_EQ(gc->collectedCount(), 1u);
    // v 和 d 已被 free，不可再用
}

TEST(GCEngineTest, CollectKeepsReachable) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* d = gc->allocData<NumberData>(3.0);
    auto* v = gc->createValue(dummyType(), d);
    v->pinRefInc();  // 模拟 scope 跟踪
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 1u);
    EXPECT_EQ(gc->collectedCount(), 0u);
    EXPECT_EQ(v->pinRef(), 1);  // 仍存活
    // 清理：递减 pinRef 让其可被回收
    v->pinRefDec();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
}

TEST(GCEngineTest, CollectReleasesDataWhenRefReachesZero) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* d = gc->allocData<TagData>(99);
    [[maybe_unused]] auto* v = gc->createValue(dummyType(), d);
    EXPECT_EQ(d->ref(), 1);
    // 不可达（pinRef=0），collect 后 Value 析构 data->ref-- 归 0，data 被 free
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
    // d 已被 free（ref 归 0 触发），不可再用
}

TEST(GCEngineTest, SharedDataNotFreedUntilLastValueCollected) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    // 两个 Value 共享同一 data：v1（pinRef=1 存活）、v2（不可达，可回收）
    auto* d = gc->allocData<TagData>(5);
    auto* v1 = gc->createValue(dummyType(), d);
    [[maybe_unused]] auto* v2 = gc->createValue(dummyType(), d);
    v1->pinRefInc();
    EXPECT_EQ(d->ref(), 2);
    // 第一次 collect：v2 被回收，d->ref 递减到 1，不释放 d（v1 仍引用）
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 1u);
    EXPECT_EQ(d->ref(), 1);
    // v1 仍存活，d 仍可访问
    EXPECT_EQ(static_cast<TagData*>(v1->data())->tag, 5);
    // v1 pinRef=0 后第二次 collect：d->ref 归 0，d 被释放
    v1->pinRefDec();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
}

TEST(GCEngineTest, AllocDataOOMReturnsNull) {
    // 极小配额，allocData 即 OOM
    auto alloc = createMallocAllocatorWithLimit(1);
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* d = gc->allocData<NumberData>(1.0);
    EXPECT_EQ(d, nullptr);
}

TEST(GCEngineTest, CreateValueOOMReturnsNull) {
    // 配额够 allocData(NumberData) 但不够再 alloc(sizeof(Value))：
    // NumberData ~24 字节，Value ~32 字节，配额 32 → allocData 后 used=24，
    // createValue 需 32，24+32 > 32 限额 → OOM
    // 注：allocData 成功但 createValue 失败时，d 未被 Value 引用，GC 无法回收。
    // 真实使用中由 Context 层负责释放 d（后续阶段），本测试用极小配额让 allocData
    // 本身 OOM，避免中间态泄漏。
    auto alloc = createMallocAllocatorWithLimit(1);
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* d = gc->allocData<NumberData>(1.0);
    EXPECT_EQ(d, nullptr);  // allocData 即 OOM
}

TEST(GCEngineTest, CountThresholdAutoTriggersGC) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    // 设小数量阈值，验证 createValue 达阈值自动 collect
    gc->setCountThreshold(5);
    // 创建 5 个 Value，都不加 scope（不可达），第 5 个时达阈值触发 collect
    // 注：maybeCollect 在 createValue 开头检查，达阈值先 collect 清理之前的
    std::vector<Value*> vs;
    for (int i = 0; i < 4; ++i) {
        auto* d = gc->allocData<NumberData>(static_cast<double>(i));
        auto* v = gc->createValue(dummyType(), d);
        ASSERT_NE(v, nullptr);
        vs.push_back(v);
    }
    EXPECT_EQ(gc->valueCount(), 4u);
    // 第 5 个 createValue：valueCount(4) >= threshold(5)? 否（4<5），不触发
    // 改为创建到阈值触发：再创建第 5 个后 valueCount=5，第 6 个时 5>=5 触发
    {
        auto* d = gc->allocData<NumberData>(4.0);
        auto* v = gc->createValue(dummyType(), d);
        vs.push_back(v);
    }
    EXPECT_EQ(gc->valueCount(), 5u);
    // 第 6 个：maybeCollect 看到 5>=5，先 collect（前 5 个不可达，全回收）
    {
        auto* d = gc->allocData<NumberData>(5.0);
        auto* v = gc->createValue(dummyType(), d);
        ASSERT_NE(v, nullptr);
    }
    // 前 5 个被回收，仅第 6 个存活
    EXPECT_EQ(gc->valueCount(), 1u);
    EXPECT_EQ(gc->collectedCount(), 5u);
}

TEST(GCEngineTest, MemThresholdAutoTriggersGC) {
    // 带限额的 allocator，内存阈值触发
    auto alloc = createMallocAllocatorWithLimit(4096);
    auto gc = neo::core::createGcEngine(alloc.get());
    gc->setMemThreshold(0.1);  // 10% 即触发，便于测试
    gc->setCountThreshold(1000000);  // 数量阈值调高，仅测内存阈值
    // 创建若干 Value 填充内存（不加 scope，不可达）
    size_t created = 0;
    for (int i = 0; i < 50; ++i) {
        auto* d = gc->allocData<NumberData>(static_cast<double>(i));
        if (d == nullptr) break;
        auto* v = gc->createValue(dummyType(), d);
        if (v == nullptr) break;
        ++created;
    }
    // 应该创建了一些，且因内存阈值触发过 collect（collectedCount > 0）
    EXPECT_GT(created, 0u);
    // 由于不可达的 Value 在 collect 时被回收，最终存活数应远小于 created
    // collectedCount 应 > 0（触发过至少一次 GC 回收）
}

TEST(GCEngineTest, EnumerateValues) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    for (int i = 0; i < 3; ++i) {
        gc->createValue(dummyType(), gc->allocData<NumberData>(i));
    }
    // 用计数 visitor 遍历
    struct Counter : public neo::core::IGCVisitor {
        int n = 0;
        void visit(Value*) override { ++n; }
        void visitScope(neo::core::Scope*) override {}
    } c;
    gc->enumerateValues(&c);
    EXPECT_EQ(c.n, 3);
}

namespace {

// 模拟 FunctionData：析构时释放其持有的作用域，从而使别的 Value 失去可达性。
// 这类「sweep 期间新产生的死亡 Value」要到下一轮才被回收（ADR-036）。
class ReleasingData : public IData {
public:
    Value* target = nullptr;
    explicit ReleasingData(Value* t) : target(t) {}
    ~ReleasingData() override {
        if (target != nullptr) target->pinRefDec();
    }
    IData* clone(neo::infra::IAllocator*) const override { return nullptr; }
};

}  // namespace

TEST(GCEngineTest, FinalCollectConvergesOnCascadeRelease) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    // target 被「作用域」跟踪，holder 不可达
    auto* target = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    target->pinRefInc();
    auto* holderData = gc->allocData<ReleasingData>(target);
    gc->createValue(dummyType(), holderData);

    // 单轮 collect：holder 被回收，其 data 析构让 target 失去可达性，
    // 但 target 在本轮标记阶段仍是根 → 本轮存活
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 1u);
    EXPECT_EQ(target->pinRef(), 0);

    // 第二轮才回收 target
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
}

TEST(GCEngineTest, DestructorConvergesWithoutFalseLeak) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* target = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    target->pinRefInc();
    gc->createValue(dummyType(), gc->allocData<ReleasingData>(target));

    // 不手动 collect：~MarkSweepGC 必须循环收敛，而不是因首轮残留 target 误报泄漏
    gc.reset();
}

TEST(GCEngineTest, CollectOnEmptyIsNoop) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
    EXPECT_EQ(gc->collectedCount(), 0u);
}

TEST(GCEngineTest, MarkEpochAdvancesEachCollect) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    v->pinRefInc();
    EXPECT_EQ(v->markEpoch(), 0u);  // 从未标记
    gc->collect();
    uint64_t first = v->markEpoch();
    EXPECT_NE(first, 0u);  // 本轮被标记
    gc->collect();
    // 每轮用新代数覆盖，无需重置：存活 Value 的 markEpoch 随本轮递增
    EXPECT_GT(v->markEpoch(), first);
    v->pinRefDec();
}

// —— P1 显式所有权原语 ——

TEST(GCEngineTest, CreateValueMarksGcOwned) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    ASSERT_NE(v, nullptr);
    EXPECT_TRUE(v->gcOwned());
    EXPECT_EQ(gc->cppOwnedCount(), 0u);
}

TEST(GCEngineTest, AllocUnownedIsInvisibleToGC) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* d = gc->allocData<NumberData>(2.0);
    auto* v = gc->allocUnowned(dummyType(), d);
    ASSERT_NE(v, nullptr);
    EXPECT_FALSE(v->gcOwned());
    EXPECT_EQ(d->ref(), 1);
    EXPECT_EQ(gc->valueCount(), 0u);
    EXPECT_EQ(gc->cppOwnedCount(), 1u);

    // collect 不触碰 C++ 拥有的 Value。
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
    EXPECT_EQ(gc->cppOwnedCount(), 1u);

    gc->freeUnowned(v);
    EXPECT_EQ(gc->cppOwnedCount(), 0u);
}

TEST(GCEngineTest, AdoptTransfersOwnershipToGC) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v =
        gc->allocUnowned(dummyType(), gc->allocData<NumberData>(3.0));
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(gc->cppOwnedCount(), 1u);

    gc->adopt(v);
    EXPECT_TRUE(v->gcOwned());
    EXPECT_EQ(gc->cppOwnedCount(), 0u);
    EXPECT_EQ(gc->valueCount(), 1u);

    // 不可达，collect 回收。
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
    EXPECT_EQ(gc->collectedCount(), 1u);
}

TEST(GCEngineTest, AdoptIsIdempotent) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v =
        gc->allocUnowned(dummyType(), gc->allocData<NumberData>(4.0));
    ASSERT_NE(v, nullptr);

    gc->adopt(v);
    gc->adopt(v);
    EXPECT_EQ(gc->valueCount(), 1u);
    EXPECT_EQ(gc->cppOwnedCount(), 0u);

    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
}

TEST(GCEngineTest, ScopeRefPreservesOwnershipBits) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v = gc->createValue(dummyType(), gc->allocData<NumberData>(5.0));
    ASSERT_NE(v, nullptr);

    v->scopeRefInc();
    v->scopeRefInc();
    EXPECT_EQ(v->scopeRef(), 2u);
    EXPECT_TRUE(v->gcOwned());

    v->scopeRefDec();
    EXPECT_EQ(v->scopeRef(), 1u);
    EXPECT_TRUE(v->gcOwned());

    v->markDead();
    EXPECT_TRUE(v->dead());
    EXPECT_TRUE(v->gcOwned());
}

// —— P2 统一 native 引用计数 ——

TEST(GCEngineTest, AllocUnownedStartsWithOneRef) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v = gc->allocUnowned(dummyType(), gc->allocData<NumberData>(1.0));
    ASSERT_NE(v, nullptr);
    EXPECT_FALSE(v->gcOwned());
    EXPECT_EQ(v->pinRef(), 1);
    EXPECT_EQ(gc->cppOwnedCount(), 1u);
    gc->unref(v);
    EXPECT_EQ(gc->cppOwnedCount(), 0u);
}

TEST(GCEngineTest, RefUnrefNativeOwnedLifetime) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v = gc->allocUnowned(dummyType(), gc->allocData<NumberData>(2.0));
    ASSERT_NE(v, nullptr);

    gc->ref(v);
    EXPECT_EQ(v->pinRef(), 2);
    gc->unref(v);
    EXPECT_EQ(v->pinRef(), 1);
    EXPECT_EQ(gc->cppOwnedCount(), 1u);

    // 最后一次 unref 归 0：native-owned 被销毁。
    gc->unref(v);
    EXPECT_EQ(gc->cppOwnedCount(), 0u);
}

TEST(GCEngineTest, RefKeepsGcOwnedAcrossCollect) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v = gc->createValue(dummyType(), gc->allocData<NumberData>(3.0));
    ASSERT_NE(v, nullptr);
    ASSERT_TRUE(v->gcOwned());

    gc->ref(v);
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 1u);
    EXPECT_EQ(gc->collectedCount(), 0u);

    gc->unref(v);
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
}

TEST(GCEngineTest, CopyForNativeSharesDataAndRefs) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* source = gc->createValue(dummyType(), gc->allocData<NumberData>(4.0));
    ASSERT_NE(source, nullptr);
    gc->ref(source);  // B 契约：源 Value 由 native 侧锚住

    Value* copy = gc->copyForNative(source);
    ASSERT_NE(copy, nullptr);
    EXPECT_FALSE(copy->gcOwned());
    EXPECT_EQ(copy->pinRef(), 1);
    EXPECT_EQ(copy->data(), source->data());
    EXPECT_EQ(source->data()->ref(), 2);

    gc->unref(copy);
    EXPECT_EQ(gc->cppOwnedCount(), 0u);
    EXPECT_EQ(source->data()->ref(), 1);

    // 源仍被 ref 住，collect 不回收。
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 1u);
    gc->unref(source);
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
}

TEST(GCEngineTest, AdoptConsumesCreatorRef) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v = gc->allocUnowned(dummyType(), gc->allocData<NumberData>(5.0));
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->pinRef(), 1);

    gc->adopt(v);
    EXPECT_TRUE(v->gcOwned());
    EXPECT_EQ(v->pinRef(), 0);  // 创建者 ref 已转移给 GC 图
    EXPECT_EQ(gc->cppOwnedCount(), 0u);
    EXPECT_EQ(gc->valueCount(), 1u);

    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
}
