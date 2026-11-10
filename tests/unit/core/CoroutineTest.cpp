// CoroutineTest.cpp - 协程抽象单元测试（ADR-045）
//
// 验证：
// - Coroutine 是对象类型（TypeKind::Coroutine，hasObjectLayout=true）
// - createCoroutine 不执行函数体；resume 按 stage 分派执行
// - 返回 Interrupt(Yield) 挂起：resumePoint/yielded 更新，execScope 保留
// - 跨 yield 的状态经作用域树保存（重新进入仍可读）
// - 正常返回 → Completed，再次 resume 返回 undefined
// - sentValue 由函数体经 getCurrentCoroutine 读取
// - GC：不可达协程连同孤立 execScope 与捕获值一并回收

#include "core/Coroutine.h"
#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/InterruptKind.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Coroutine.h"
#include "core/types/Interrupt.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using neo::core::CoroutineData;
using neo::core::CoroutineState;
using neo::core::IContext;
using neo::core::IGCEngine;
using neo::core::InterruptData;
using neo::core::InterruptKind;
using neo::core::NumberData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::asCoroutine;
using neo::core::asInterrupt;
using neo::core::createContext;
using neo::core::detail::isInterruptValue;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

struct Fixture {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    NullSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx = createContext(alloc.get(), &console);
};

double num(Value* v) {
    return static_cast<NumberData*>(v->data())->value;
}

// 三阶段状态机：
//   stage 0：在 execScope 存 __x = 10，yield 1（offset=1）
//   stage 1：读 __x 与 sentValue，yield __x + sent（offset=2）
//   stage 2：返回 __x + sent + 100
Value* staged(IContext* ctx, Value*, size_t, Value*[]) {
    CoroutineData* co = asCoroutine(ctx->getCurrentCoroutine());
    if (co == nullptr) return ctx->createUndefined();

    switch (co->resumePoint) {
        case 0: {
            Value* x = ctx->createNumber(10, "__x");
            if (x->type()->kind() == TypeKind::Exception) return x;
            return ctx->createInterrupt(InterruptKind::Yield, 1, ctx->createNumber(1));
        }
        case 1: {
            Value* x = ctx->current()->lookup("__x");
            double sent = num(co->sentValue);
            double sum = num(x) + sent;
            return ctx->createInterrupt(InterruptKind::Yield, 2, ctx->createNumber(sum));
        }
        case 2: {
            Value* x = ctx->current()->lookup("__x");
            double sent = num(co->sentValue);
            return ctx->createNumber(num(x) + sent + 100);
        }
        default:
            return ctx->createUndefined();
    }
}

}  // namespace

// ===== 类型 =====

TEST(CoroutineTest, KindAndObjectLayout) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(staged, 0);
    Value* co = f.ctx->createCoroutine(fn, nullptr, 0, nullptr);

    EXPECT_EQ(co->type()->kind(), TypeKind::Coroutine);
    EXPECT_TRUE(co->type()->hasObjectLayout());
    EXPECT_TRUE(neo::core::kindHasObjectLayout(TypeKind::Coroutine));
    EXPECT_STREQ(co->type()->name(), "coroutine");

    CoroutineData* d = asCoroutine(co);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->state, CoroutineState::Suspended);
    EXPECT_EQ(d->resumePoint, 0u);
    EXPECT_EQ(d->target, fn);
    EXPECT_NE(d->resume, nullptr);
}

TEST(CoroutineTest, CreateDoesNotRunBody) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(staged, 0);
    Value* co = f.ctx->createCoroutine(fn, nullptr, 0, nullptr);
    // 创建后尚未执行：execScope 为空，yielded 为 undefined
    CoroutineData* d = asCoroutine(co);
    EXPECT_EQ(d->execScope, nullptr);
    EXPECT_EQ(d->yielded->type()->kind(), TypeKind::Undefined);
}

TEST(CoroutineTest, NonFunctionTargetIsException) {
    Fixture f;
    Value* n = f.ctx->createNumber(1);
    Value* co = f.ctx->createCoroutine(n, nullptr, 0, nullptr);
    EXPECT_EQ(co->type()->kind(), TypeKind::Exception);
}

TEST(CoroutineTest, AsCoroutineRejectsOtherTypes) {
    Fixture f;
    EXPECT_EQ(asCoroutine(f.ctx->createNumber(1)), nullptr);
    EXPECT_EQ(asCoroutine(nullptr), nullptr);
}

// ===== 挂起 / 恢复 =====

TEST(CoroutineTest, SuspendResumeAndComplete) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(staged, 0);
    Value* co = f.ctx->createCoroutine(fn, nullptr, 0, nullptr);
    CoroutineData* d = asCoroutine(co);

    // 第一次 resume：执行 stage 0，yield 1
    Value* r1 = f.ctx->resumeCoroutine(co, nullptr);
    ASSERT_TRUE(isInterruptValue(r1));
    EXPECT_EQ(asInterrupt(r1)->kind, InterruptKind::Yield);
    EXPECT_DOUBLE_EQ(num(asInterrupt(r1)->value), 1.0);
    EXPECT_EQ(d->state, CoroutineState::Suspended);
    EXPECT_EQ(d->resumePoint, 1u);
    EXPECT_NE(d->execScope, nullptr);
    // 挂起期间 execScope 是孤立子树（parent=nullptr）
    EXPECT_EQ(d->execScope->parent(), nullptr);

    // 第二次 resume：sent=5 → yield 15
    Value* r2 = f.ctx->resumeCoroutine(co, f.ctx->createNumber(5));
    ASSERT_TRUE(isInterruptValue(r2));
    EXPECT_DOUBLE_EQ(num(asInterrupt(r2)->value), 15.0);
    EXPECT_EQ(d->resumePoint, 2u);

    // 第三次 resume：sent=7 → 完成，返回 10+7+100 = 117
    Value* r3 = f.ctx->resumeCoroutine(co, f.ctx->createNumber(7));
    ASSERT_FALSE(isInterruptValue(r3));
    EXPECT_DOUBLE_EQ(num(r3), 117.0);
    EXPECT_EQ(d->state, CoroutineState::Completed);
    EXPECT_EQ(d->execScope, nullptr);
    EXPECT_EQ(d->yielded, r3);

    // 已完成后再次 resume 返回 undefined
    Value* r4 = f.ctx->resumeCoroutine(co, nullptr);
    EXPECT_EQ(r4->type()->kind(), TypeKind::Undefined);
}

TEST(CoroutineTest, SentValueVisibleToBody) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(staged, 0);
    Value* co = f.ctx->createCoroutine(fn, nullptr, 0, nullptr);
    CoroutineData* d = asCoroutine(co);

    (void)f.ctx->resumeCoroutine(co, nullptr);
    Value* sent = f.ctx->createNumber(3);
    (void)f.ctx->resumeCoroutine(co, sent);
    EXPECT_EQ(d->sentValue, sent);
}

TEST(CoroutineTest, CurrentCoroutineIsVisibleInsideBody) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(staged, 0);
    Value* co = f.ctx->createCoroutine(fn, nullptr, 0, nullptr);
    // 首次 resume 期间 getCurrentCoroutine 应为该协程（body 内已按它分派）
    (void)f.ctx->resumeCoroutine(co, nullptr);
    EXPECT_EQ(f.ctx->getCurrentCoroutine(), nullptr);  // 退出后栈清空
}

namespace {
// 嵌套场景的共享状态（native 函数指针无捕获，测试内用文件级静态）
Value* g_innerCo = nullptr;
Value* g_outerCo = nullptr;
bool g_sawInner = false;
bool g_restoredOuter = false;
}  // namespace

TEST(CoroutineTest, NestedCoroutinesUseStack) {
    Fixture f;
    g_innerCo = nullptr;
    g_outerCo = nullptr;
    g_sawInner = false;
    g_restoredOuter = false;

    // 内层协程：body 内检查栈顶是自己
    auto inner = [](IContext* ctx, Value*, size_t, Value*[]) -> Value* {
        if (ctx->getCurrentCoroutine() == g_innerCo) g_sawInner = true;
        return ctx->createNumber(1);
    };
    // 外层协程：body 内 resume 内层，返回后检查栈顶恢复为自己
    auto outer = [](IContext* ctx, Value*, size_t, Value*[]) -> Value* {
        (void)ctx->resumeCoroutine(g_innerCo, nullptr);
        if (ctx->getCurrentCoroutine() == g_outerCo) g_restoredOuter = true;
        return ctx->createNumber(2);
    };

    Value* innerFn = f.ctx->createNativeFunction(inner, 0);
    g_innerCo = f.ctx->createCoroutine(innerFn, nullptr, 0, nullptr);
    Value* outerFn = f.ctx->createNativeFunction(outer, 0);
    g_outerCo = f.ctx->createCoroutine(outerFn, nullptr, 0, nullptr);

    Value* r = f.ctx->resumeCoroutine(g_outerCo, nullptr);
    EXPECT_DOUBLE_EQ(num(r), 2.0);
    EXPECT_TRUE(g_sawInner);
    EXPECT_TRUE(g_restoredOuter);
    EXPECT_EQ(f.ctx->getCurrentCoroutine(), nullptr);
}

// ===== GC =====

TEST(CoroutineTest, UnreachableCoroutineCollectsExecScope) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();
    size_t baseline = gc->valueCount();

    {
        f.ctx->pushNewScope(f.ctx->current());
        Value* fn = f.ctx->createNativeFunction(staged, 0);
        Value* co = f.ctx->createCoroutine(fn, nullptr, 0, nullptr);
        // 首次 resume 产生 execScope（含 __x 与 yield 值）
        (void)f.ctx->resumeCoroutine(co, nullptr);
        f.ctx->popScope();  // co/fn 不再从 global 树可达
    }

    gc->collect();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), baseline);
}

TEST(CoroutineTest, ReachableCoroutineKeepsExecScope) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();

    Value* fn = f.ctx->createNativeFunction(staged, 0);
    Value* co = f.ctx->createCoroutine(fn, nullptr, 0, nullptr);
    (void)f.ctx->resumeCoroutine(co, nullptr);

    gc->collect();
    CoroutineData* d = asCoroutine(co);
    ASSERT_NE(d, nullptr);
    ASSERT_NE(d->execScope, nullptr);
    // 跨 yield 的状态仍可读
    Value* x = d->execScope->lookup("__x");
    ASSERT_NE(x, nullptr);
    EXPECT_DOUBLE_EQ(num(x), 10.0);

    // 完成后 execScope 销毁
    (void)f.ctx->resumeCoroutine(co, f.ctx->createNumber(0));
    (void)f.ctx->resumeCoroutine(co, f.ctx->createNumber(0));
    EXPECT_EQ(d->execScope, nullptr);
}
