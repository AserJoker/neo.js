// context_test.cpp - Context 门面单元测试
//
// 验证：
// - Context 创建后 global/root/current 三层 scope 就绪
// - global 是根（parent=nullptr），root 的 parent=global，current 初始=root
// - setCurrentScope/popScope 管理 current（parent 链即调用栈），pop 销毁并回到 parent
// - Context 持有 GC 所有权（内部创建），析构先销毁 global_scope 再销毁 GC
// - 注入资源 allocator/console 不被销毁（外部持有）
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面）、012（Scope 树）、030 阶段 4

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
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
using neo::core::TypeKind;
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
    ctx->setCurrentScope(s1);
    EXPECT_EQ(ctx->current(), s1);

    Scope* s2 = makeScope(alloc.get(), ctx->current());
    ctx->current()->addChild(s2);
    ctx->setCurrentScope(s2);
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
        ctx->setCurrentScope(s);
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

// —— Context create* 便捷方法测试 ——

TEST(ContextTest, CreateUndefinedHasCorrectType) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    size_t before = ctx->current()->valueCount();
    auto* v = ctx->createUndefined();
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(ctx->current()->valueCount(), before + 1);  // 已加入 current scope 跟踪
}

TEST(ContextTest, CreateNullHasCorrectType) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* v = ctx->createNull();
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->type()->kind(), TypeKind::Null);
}

TEST(ContextTest, CreateBooleanHasCorrectTypeAndValue) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    size_t before = ctx->current()->valueCount();
    auto* t = ctx->createBoolean(true);
    auto* f = ctx->createBoolean(false);
    ASSERT_NE(t, nullptr);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(t->type()->kind(), TypeKind::Boolean);
    EXPECT_EQ(f->type()->kind(), TypeKind::Boolean);
    // 两个 Value 都加入 current scope
    EXPECT_EQ(ctx->current()->valueCount(), before + 2);
}

TEST(ContextTest, CreateNumberHasCorrectType) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* v = ctx->createNumber(42.0);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->type()->kind(), TypeKind::Number);
}

TEST(ContextTest, CreateStringHasCorrectType) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* v = ctx->createString("hello");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->type()->kind(), TypeKind::String);
}

TEST(ContextTest, CreateWithNameBindsToCurrentScope) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* v = ctx->createNumber(42.0, "answer");
    ASSERT_NE(v, nullptr);
    // bind 后可通过 lookup 查到
    EXPECT_EQ(ctx->current()->lookup("answer"), v);
    // 沿 parent 链也能查到（current=root）
    EXPECT_EQ(ctx->root()->lookupChain("answer"), v);
}

TEST(ContextTest, CreateWithoutNameDoesNotBind) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    size_t before = ctx->current()->valueCount();
    auto* v = ctx->createNumber(42.0);
    ASSERT_NE(v, nullptr);
    // 无名字，lookup 返回 nullptr
    EXPECT_EQ(ctx->current()->lookup("x"), nullptr);
    // 但 Value 仍被 current scope 跟踪（scopeRef++）
    EXPECT_EQ(ctx->current()->valueCount(), before + 1);
    EXPECT_EQ(v->scopeRef(), 1);
}

TEST(ContextTest, CreateInPushedScopeTracksCorrectly) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Scope* s = makeScope(alloc.get(), ctx->current());
    ctx->current()->addChild(s);
    ctx->setCurrentScope(s);

    auto* v = ctx->createString("inner", "x");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(ctx->current()->lookup("x"), v);
    EXPECT_EQ(v->scopeRef(), 1);

    // pop 后 s 销毁，v 的 scopeRef 归 0
    ctx->popScope();
    EXPECT_EQ(v->scopeRef(), 0);
}

TEST(ContextTest, CreatedValuesReachableByGcWhileScoped) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* v = ctx->createNumber(1.0);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->scopeRef(), 1);

    // GC collect 后 v 仍存活（scopeRef != 0）
    size_t before = ctx->gc()->valueCount();
    ctx->gc()->collect();
    // collect 不回收任何存活 Value（v + OOM 哨兵 + well-known symbols + Symbol 包装类）
    EXPECT_EQ(ctx->gc()->valueCount(), before);
    EXPECT_EQ(v->type()->kind(), TypeKind::Number);
}

// ===== OOM 哨兵（ADR-031）=====

TEST(OomTest, OomValueExistsAndIsFatal) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* oom = ctx->oomValue();
    ASSERT_NE(oom, nullptr);
    EXPECT_EQ(oom->type()->kind(), TypeKind::Fatal);
}

TEST(OomTest, OomValueMountedOnGlobal) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* oom = ctx->oomValue();
    // 挂 global：scopeRef >= 1，且可按名查找
    EXPECT_GE(oom->scopeRef(), 1);
    EXPECT_EQ(ctx->global()->lookup("__oom__"), oom);
}

TEST(OomTest, OomValueSurvivesGcCollect) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->gc()->collect();
    // 哨兵挂 global，collect 后仍存活
    EXPECT_EQ(ctx->oomValue()->scopeRef(), 1);
    EXPECT_EQ(ctx->global()->lookup("__oom__"), ctx->oomValue());
}

TEST(OomTest, CreateReturnsOomSentinelWhenExhausted) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 把配额压到当前已用量，后续 alloc 全部失败
    alloc->setLimit(alloc->used());

    Value* v = ctx->createNumber(42.0);
    EXPECT_EQ(v, ctx->oomValue());           // 返回哨兵单例
    EXPECT_EQ(v->type()->kind(), TypeKind::Fatal);
}

TEST(OomTest, CreateStringReturnsOomWhenExhausted) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    alloc->setLimit(alloc->used());
    EXPECT_EQ(ctx->createString("x"), ctx->oomValue());
    EXPECT_EQ(ctx->createUndefined(), ctx->oomValue());
    EXPECT_EQ(ctx->createNull(), ctx->oomValue());
    EXPECT_EQ(ctx->createBoolean(true), ctx->oomValue());
}

TEST(OomTest, OomSentinelIsSingleton) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    alloc->setLimit(alloc->used());
    Value* a = ctx->createNumber(1.0);
    Value* b = ctx->createString("y");
    EXPECT_EQ(a, b);                 // 同一哨兵实例
    EXPECT_EQ(a, ctx->oomValue());
}

TEST(OomTest, NormalCreateNotOom) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* v = ctx->createNumber(1.0);
    EXPECT_NE(v, ctx->oomValue());
    EXPECT_EQ(v->type()->kind(), TypeKind::Number);
}
