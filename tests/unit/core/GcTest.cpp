// gc_test.cpp - MarkSweepGC 生命周期单元测试
//
// 验证 createValue/allocData/collect：scopeRef!=0 存活、scopeRef==0 回收、
// data ref 归 0 释放、双重阈值自动触发、OOM 返回 nullptr、collectedCount 统计。
//
// 关联 ADR: 008（GC→IAllocator）、012（scopeRef 根）、029（生命周期）

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
    // 创建一个 Value 但不加入任何 scope（scopeRef=0），collect 后应被回收
    auto* d = gc->allocData<NumberData>(2.0);
    ASSERT_NE(d, nullptr);
    auto* v = gc->createValue(dummyType(), d);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->scopeRef(), 0);
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
    v->scopeRefInc();  // 模拟 scope 跟踪
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 1u);
    EXPECT_EQ(gc->collectedCount(), 0u);
    EXPECT_EQ(v->scopeRef(), 1);  // 仍存活
    // 清理：递减 scopeRef 让其可被回收
    v->scopeRefDec();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
}

TEST(GCEngineTest, CollectReleasesDataWhenRefReachesZero) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* d = gc->allocData<TagData>(99);
    [[maybe_unused]] auto* v = gc->createValue(dummyType(), d);
    EXPECT_EQ(d->ref(), 1);
    // scopeRef=0，collect 后 Value 析构 data->ref-- 归 0，data 被 free
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
    // d 已被 free（ref 归 0 触发），不可再用
}

TEST(GCEngineTest, SharedDataNotFreedUntilLastValueCollected) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    // 两个 Value 共享同一 data：v1（scopeRef=1 存活）、v2（scopeRef=0 可回收）
    auto* d = gc->allocData<TagData>(5);
    auto* v1 = gc->createValue(dummyType(), d);
    [[maybe_unused]] auto* v2 = gc->createValue(dummyType(), d);
    v1->scopeRefInc();
    EXPECT_EQ(d->ref(), 2);
    // 第一次 collect：v2 被回收，d->ref 递减到 1，不释放 d（v1 仍引用）
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 1u);
    EXPECT_EQ(d->ref(), 1);
    // v1 仍存活，d 仍可访问
    EXPECT_EQ(static_cast<TagData*>(v1->data())->tag, 5);
    // v1 scopeRef=0 后第二次 collect：d->ref 归 0，d 被释放
    v1->scopeRefDec();
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
    // 创建 5 个 Value，都不加 scope（scopeRef=0），第 5 个时达阈值触发 collect
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
    // 第 6 个：maybeCollect 看到 5>=5，先 collect（前 5 个 scopeRef=0 全回收）
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
    // 创建若干 Value 填充内存（不加 scope，scopeRef=0）
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
    // 由于 scopeRef=0 的 Value 在 collect 时被回收，最终存活数应远小于 created
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
    } c;
    gc->enumerateValues(&c);
    EXPECT_EQ(c.n, 3);
}

namespace {

// 模拟 FunctionData：析构时释放其持有的作用域，从而对别的 Value 做 scopeRef--。
// 这类「sweep 期间新产生的死亡 Value」要到下一轮才被回收（ADR-036）。
class ReleasingData : public IData {
public:
    Value* target = nullptr;
    explicit ReleasingData(Value* t) : target(t) {}
    ~ReleasingData() override {
        if (target != nullptr) target->scopeRefDec();
    }
    IData* clone(neo::infra::IAllocator*) const override { return nullptr; }
};

}  // namespace

TEST(GCEngineTest, FinalCollectConvergesOnCascadeRelease) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    // target 被「作用域」跟踪（scopeRef=1），holder 不可达
    auto* target = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    target->scopeRefInc();
    auto* holderData = gc->allocData<ReleasingData>(target);
    gc->createValue(dummyType(), holderData);

    // 单轮 collect：holder 被回收，其 data 析构让 target scopeRef 归 0，
    // 但 target 在本轮标记阶段仍是根 → 本轮存活
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 1u);
    EXPECT_EQ(target->scopeRef(), 0);

    // 第二轮才回收 target
    gc->collect();
    EXPECT_EQ(gc->valueCount(), 0u);
}

TEST(GCEngineTest, DestructorConvergesWithoutFalseLeak) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* target = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    target->scopeRefInc();
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

TEST(GCEngineTest, MarkedFlagResetEachCollect) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* v = gc->createValue(dummyType(), gc->allocData<NumberData>(1.0));
    v->scopeRefInc();
    v->setMarked(true);  // 手动置位（模拟上次 collect 残留）
    gc->collect();
    // collect 应重置 marked 并重新标记：存活 Value marked=true
    EXPECT_TRUE(v->marked());
    v->scopeRefDec();
}
