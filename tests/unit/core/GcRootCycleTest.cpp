// GcRootCycleTest.cpp - scopeRef 根环根治（ADR-044）
//
// 旧模型：scopeRef != 0 即 GC 根。闭包作用域跟踪的 Value 若沿 data 引用链
// 回到闭包所属的函数对象，就形成不可回收的根环（正常自引用闭包同样中招）。
//
// 新模型：GC 从 global 作用域树出发标记可达性；孤立闭包作用域只能由持有它的
// 函数对象在标记时 visitScope 递归进来。函数对象从 global 树不可达时，
// 闭包作用域及其捕获值都不被标记，环整体可回收。
//
// 验证：
// - 自引用闭包（函数捕获自身）在函数不可达后被回收
// - 闭包捕获的对象反向引用函数（A → closure → B → A）整体可回收
// - pinRef > 0 的 Value 作为额外根存活
// - 从 global 树可达的闭包（含自引用）不被误回收

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using neo::core::IContext;
using neo::core::IGCEngine;
using neo::core::Scope;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
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

Value* noop(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createUndefined(); }

}  // namespace

// 自引用闭包：函数捕获自身（fn 的 closureScope 绑定 fn）。
// 旧模型下 closureScope 的 values 让 fn 的 scopeRef >= 1，永远不可回收。
TEST(GcRootCycleTest, SelfReferentialClosureIsCollected) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();
    size_t baseline = gc->valueCount();

    {
        // 在临时作用域里创建函数并让它捕获自身
        Scope* s = f.ctx->pushNewScope(f.ctx->current());
        Value* fn = f.ctx->createNativeFunction(noop, 0);
        ASSERT_EQ(f.ctx->captureValue(fn, "__self", fn), fn);
        f.ctx->popScope();  // fn 不再从 global 树可达
        (void)s;
    }

    // 多轮 collect：FunctionData 析构会释放闭包作用域，收敛判定覆盖级联释放
    gc->collect();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), baseline);
}

// 环：函数 → closureScope → 对象 → 函数（对象属性引用函数）。
TEST(GcRootCycleTest, ClosureObjectFunctionCycleIsCollected) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();
    size_t baseline = gc->valueCount();

    {
        f.ctx->pushNewScope(f.ctx->current());
        Value* fn = f.ctx->createNativeFunction(noop, 0);
        Value* obj = f.ctx->createObject();
        // obj.fn = fn（data 引用）
        f.ctx->defineProperty(obj, "fn", fn);
        // fn 的闭包捕获 obj（scope 引用）
        ASSERT_EQ(f.ctx->captureValue(fn, "__obj", obj), fn);
        f.ctx->popScope();
    }

    gc->collect();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), baseline);
}

// 从 global 树可达的自引用闭包不能被误回收。
TEST(GcRootCycleTest, ReachableSelfReferentialClosureSurvives) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();

    Value* fn = f.ctx->createNativeFunction(noop, 0);
    ASSERT_EQ(f.ctx->captureValue(fn, "__self", fn), fn);
    f.ctx->global()->add(fn);  // 挂到 global 树
    f.ctx->global()->bind("__fn", fn);

    gc->collect();
    EXPECT_EQ(f.ctx->global()->lookup("__fn"), fn);
    EXPECT_EQ(fn->type()->kind(), TypeKind::Function);
    // 闭包捕获仍完好
    auto* d = static_cast<neo::core::FunctionData*>(fn->data());
    EXPECT_EQ(d->closureScope->lookup("__self"), fn);
}

// pinRef > 0 的 Value 作为额外根存活（调度器手工钉住）。
TEST(GcRootCycleTest, PinnedValueSurvivesWithoutScopeReachability) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();

    Value* v = f.ctx->createNumber(7);
    {
        // 让它脱离 global 树
        f.ctx->pushNewScope(f.ctx->current());
        f.ctx->popScope();
    }
    v->pinRefInc();
    gc->collect();
    EXPECT_EQ(v->type()->kind(), TypeKind::Number);

    v->pinRefDec();
    gc->collect();
    // 已回收，不可再解引用
}

// 闭包捕获的普通值在函数不可达后一并回收（无环的基本情形）。
TEST(GcRootCycleTest, ClosureCapturedValueCollectedWithFunction) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();
    size_t baseline = gc->valueCount();

    {
        f.ctx->pushNewScope(f.ctx->current());
        Value* fn = f.ctx->createNativeFunction(noop, 0);
        Value* captured = f.ctx->createNumber(42);
        ASSERT_EQ(f.ctx->captureValue(fn, "__x", captured), fn);
        f.ctx->popScope();
    }

    gc->collect();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), baseline);
}
