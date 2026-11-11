// DynamicScopeTest.cpp - 运行时闭包链（dynamic scope）引擎原语
//
// 验证：
// - dynamic 作用域中定义的函数自动进入 Environment 模式，捕获定义点环境链
// - eval 在函数定义之后注入的绑定仍能被闭包看到（under-capture 回退）
// - 逃逸闭包 pin 住已 pop 的作用域，GC 后变量仍可读
// - 闭包不可达后，autoDispose 作用域随 FunctionData 析构回收
// - dynamic 标记沿 parent 链继承；非 dynamic 作用域仍走静态 slot 模式
// - setFunctionCapturesEnvironment 显式切换 Environment 模式

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using neo::core::IContext;
using neo::core::IGCEngine;
using neo::core::NumberData;
using neo::core::Scope;
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

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }

// 模拟函数体读取自由变量 x：沿 current 链查找（execScope → closureScope → 环境链）。
Value* readX(IContext* ctx, Value*, size_t, Value*[]) {
    Value* v = ctx->current()->lookupChain("x");
    return v != nullptr ? v : ctx->createUndefined();
}

// 把函数锚定到 root，模拟逃逸闭包被外部持有。
void anchor(IContext* ctx, Value* fn, std::string_view name = "__escaped") {
    ctx->root()->add(fn);
    ctx->root()->bind(name, fn);
}

// —— eval 模拟 ——
// 真实 VM 会把 eval 的源码编译成独立字节码，并在**调用方作用域**执行。
// 这里用 native 函数代替那段编译产物：VM 在直接 eval 调用点把 caller scope
// 交给它（真实实现由 invoke 的调用点特化传入，测试用静态变量模拟）。
Scope* g_evalCallerScope = nullptr;

// 模拟编译产物：
//   var x = 42;
//   function g() { return x; }
// 并返回 g（逃逸闭包）。
Value* simulatedEval(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    // 参数是待执行的源码文本；native 代替其编译结果。
    if (argc == 0 || argv[0]->type()->kind() != neo::core::TypeKind::String) {
        return ctx->createUndefined();
    }
    Scope* caller = g_evalCallerScope;
    if (caller == nullptr) return ctx->createUndefined();

    // 直接 eval 污染的是调用方作用域：标记 dynamic（真实 VM 在编译期已知）。
    caller->markDynamic();

    // 在调用方作用域执行编译产物（临时把 current 切到 caller）。
    Scope* saved = ctx->current();
    ctx->setCurrentScope(caller);
    if (caller->bindingKind("x") == Scope::BindingKind::None) {
        (void)caller->define("x", ctx->createNumber(42));
    }
    Value* g = ctx->createNativeFunction(readX, 0);
    (void)caller->define("g", g);
    ctx->setCurrentScope(saved);
    return g;
}

// 模拟编译产物：
//   y = y + 1;
//   var x = y;
Value* simulatedEvalIncrement(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    if (argc == 0 || argv[0]->type()->kind() != neo::core::TypeKind::String) {
        return ctx->createUndefined();
    }
    Scope* caller = g_evalCallerScope;
    if (caller == nullptr) return ctx->createUndefined();
    caller->markDynamic();

    Scope* saved = ctx->current();
    ctx->setCurrentScope(caller);
    Value* y = caller->lookup("y");
    Value* sum = ctx->add(y, ctx->createNumber(1));
    (void)ctx->store("y", sum);
    // var x = y：复制值而非共享槽位。
    (void)ctx->define("x", caller->lookup("y"));
    ctx->setCurrentScope(saved);
    return ctx->createUndefined();
}

// 模拟 VM 的直接 eval 调用点：把 caller scope 交给 eval 编译产物。
Value* invokeEval(IContext* ctx, Value* evalFn, Scope* caller, Value* code) {
    g_evalCallerScope = caller;
    Value* r = ctx->call(evalFn, ctx->createUndefined(), 1, &code);
    g_evalCallerScope = nullptr;
    return r;
}

}  // namespace

// dynamic 作用域中定义的函数自动捕获环境链；eval 在定义之后注入的 x 仍可见。
TEST(DynamicScopeTest, DynamicScopeClosureSeesBindingInjectedAfterDefinition) {
    Fixture f;
    f.ctx->pushNewScope(f.ctx->current());
    Scope* def = f.ctx->current();
    def->markDynamic();

    Value* fn = f.ctx->createNativeFunction(readX, 0);
    ASSERT_EQ(fn->type()->kind(), neo::core::TypeKind::Function);
    EXPECT_TRUE(static_cast<neo::core::FunctionData*>(fn->data())->capturesEnvironment);

    // 模拟 eval 在函数定义之后注入 var x。
    def->define("x", f.ctx->createNumber(42));

    f.ctx->popScope();
    anchor(f.ctx.get(), fn);

    Value* r = f.ctx->call(fn, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), neo::core::TypeKind::Number);
    EXPECT_EQ(numOf(r), 42.0);
}

// 逃逸闭包 pin 住已 pop 的作用域：GC 后注入值仍可读。
TEST(DynamicScopeTest, EscapedClosureKeepsPoppedScopeAliveAcrossGc) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();

    f.ctx->pushNewScope(f.ctx->current());
    Scope* def = f.ctx->current();
    def->markDynamic();
    Value* fn = f.ctx->createNativeFunction(readX, 0);
    def->define("x", f.ctx->createNumber(7));
    f.ctx->popScope();

    EXPECT_TRUE(def->autoDispose());
    EXPECT_GT(def->captureRef(), 0u);

    anchor(f.ctx.get(), fn);
    gc->collect();

    Value* r = f.ctx->call(fn, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), neo::core::TypeKind::Number);
    EXPECT_EQ(numOf(r), 7.0);
}

// dynamic 标记沿 parent 链继承：内层普通作用域定义的函数也捕获环境链。
TEST(DynamicScopeTest, DynamicFlagIsInheritedByChildScopes) {
    Fixture f;
    f.ctx->pushNewScope(f.ctx->current());
    f.ctx->current()->markDynamic();
    f.ctx->pushNewScope(f.ctx->current());  // 内层未显式标记

    Value* fn = f.ctx->createNativeFunction(readX, 0);
    EXPECT_TRUE(static_cast<neo::core::FunctionData*>(fn->data())->capturesEnvironment);

    // x 定义在外层 dynamic 作用域，内层 pop 后仍应可见。
    f.ctx->current()->parent()->define("x", f.ctx->createNumber(11));
    f.ctx->popScope();
    f.ctx->popScope();
    anchor(f.ctx.get(), fn);

    Value* r = f.ctx->call(fn, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), neo::core::TypeKind::Number);
    EXPECT_EQ(numOf(r), 11.0);
}

// 非 dynamic 作用域保持静态 slot 模式：pop 后局部变量不可见。
TEST(DynamicScopeTest, NonDynamicScopeDoesNotCaptureEnvironment) {
    Fixture f;
    f.ctx->pushNewScope(f.ctx->current());
    Scope* def = f.ctx->current();
    Value* fn = f.ctx->createNativeFunction(readX, 0);
    EXPECT_FALSE(static_cast<neo::core::FunctionData*>(fn->data())->capturesEnvironment);
    def->define("x", f.ctx->createNumber(3));
    f.ctx->popScope();
    anchor(f.ctx.get(), fn);

    Value* r = f.ctx->call(fn, f.ctx->createUndefined(), 0, nullptr);
    EXPECT_EQ(r->type()->kind(), neo::core::TypeKind::Undefined);
}

// 显式原语：setFunctionCapturesEnvironment 把函数切到 Environment 模式。
TEST(DynamicScopeTest, ExplicitEnvironmentCapturePrimitive) {
    Fixture f;
    f.ctx->pushNewScope(f.ctx->current());
    Scope* def = f.ctx->current();
    Value* fn = f.ctx->createNativeFunction(readX, 0);
    f.ctx->setFunctionCapturesEnvironment(fn);
    EXPECT_TRUE(static_cast<neo::core::FunctionData*>(fn->data())->capturesEnvironment);

    def->define("x", f.ctx->createNumber(99));
    f.ctx->popScope();
    anchor(f.ctx.get(), fn);

    Value* r = f.ctx->call(fn, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), neo::core::TypeKind::Number);
    EXPECT_EQ(numOf(r), 99.0);
}

// 闭包不可达后：autoDispose 作用域随 FunctionData 析构回收，作用域树恢复原状。
TEST(DynamicScopeTest, UnreachableEnvironmentClosureReclaimsPinnedScope) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();
    size_t baselineValues = gc->valueCount();
    size_t baselineChildren = f.ctx->root()->childCount();

    {
        f.ctx->pushNewScope(f.ctx->current());
        f.ctx->current()->markDynamic();
        Value* fn = f.ctx->createNativeFunction(readX, 0);
        f.ctx->current()->define("x", f.ctx->createNumber(5));
        f.ctx->popScope();
        // fn 不再从 global 树可达
        (void)fn;
    }

    gc->collect();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), baselineValues);
    EXPECT_EQ(f.ctx->root()->childCount(), baselineChildren);
}

// 端到端：native 模拟 eval("var x = 42; function g(){ return x; }")。
// - eval 之前定义的函数 before 能看到 eval 注入的 x（under-capture 回退）
// - eval 定义的 g 逃逸，caller pop 后跨 GC 仍能读 x
TEST(DynamicScopeTest, SimulatedEvalInjectsIntoCallerAndEscapingClosure) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();

    // 全局 eval 函数：Slot 模式，不 pin 调用方链。
    Value* evalFn = f.ctx->createNativeFunction(simulatedEval, 1);

    // 模拟函数调用帧：caller 是函数体作用域，Sema 已知其中含直接 eval。
    f.ctx->pushNewScope(f.ctx->current());
    Scope* caller = f.ctx->current();
    caller->markDynamic();

    // eval 之前定义的闭包：静态捕获不可能知道稍后注入的 x。
    Value* before = f.ctx->createNativeFunction(readX, 0);
    caller->define("before", before);
    anchor(f.ctx.get(), before, "__before");

    Value* code = f.ctx->createString("var x = 42; function g() { return x; }");
    Value* g = invokeEval(f.ctx.get(), evalFn, caller, code);
    ASSERT_EQ(g->type()->kind(), neo::core::TypeKind::Function);
    anchor(f.ctx.get(), g, "__g");

    // eval 注入后，eval 之前定义的闭包也能看到 x。
    Value* r = f.ctx->call(before, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), neo::core::TypeKind::Number);
    EXPECT_EQ(numOf(r), 42.0);

    // caller pop 后，逃逸闭包 g 仍能读 x；GC 后仍存活。
    f.ctx->popScope();
    EXPECT_TRUE(caller->autoDispose());
    gc->collect();
    r = f.ctx->call(g, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_EQ(r->type()->kind(), neo::core::TypeKind::Number);
    EXPECT_EQ(numOf(r), 42.0);
}

// 端到端：eval 读取并改写调用方已有绑定，注入的新绑定反映改写结果。
TEST(DynamicScopeTest, SimulatedEvalReadsAndWritesCallerBinding) {
    Fixture f;

    Value* evalFn = f.ctx->createNativeFunction(simulatedEvalIncrement, 1);

    f.ctx->pushNewScope(f.ctx->current());
    Scope* caller = f.ctx->current();
    caller->markDynamic();
    caller->define("y", f.ctx->createNumber(10));

    Value* code = f.ctx->createString("y = y + 1; var x = y;");
    (void)invokeEval(f.ctx.get(), evalFn, caller, code);

    // eval 改写的是调用方槽位本身。
    Value* y = caller->lookup("y");
    ASSERT_EQ(y->type()->kind(), neo::core::TypeKind::Number);
    EXPECT_EQ(numOf(y), 11.0);
    Value* x = caller->lookup("x");
    ASSERT_EQ(x->type()->kind(), neo::core::TypeKind::Number);
    EXPECT_EQ(numOf(x), 11.0);
}

// 端到端：eval 注入的绑定在 caller pop 后由逃逸闭包保活；闭包不可达后整体回收。
TEST(DynamicScopeTest, SimulatedEvalEnvironmentIsReclaimedWhenClosureDies) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();
    size_t baselineValues = gc->valueCount();
    size_t baselineChildren = f.ctx->root()->childCount();

    {
        // evalFn 放在临时作用域：pop 后它与自动创建的 prototype 一起不可达。
        f.ctx->pushNewScope(f.ctx->current());
        Value* evalFn = f.ctx->createNativeFunction(simulatedEval, 1);
        f.ctx->popScope();

        f.ctx->pushNewScope(f.ctx->current());
        Scope* caller = f.ctx->current();
        caller->markDynamic();
        Value* code = f.ctx->createString("var x = 42; function g() { return x; }");
        Value* g = invokeEval(f.ctx.get(), evalFn, caller, code);
        (void)g;
        f.ctx->popScope();
        // g 随 caller 一起不可达
    }

    gc->collect();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), baselineValues);
    EXPECT_EQ(f.ctx->root()->childCount(), baselineChildren);
}

// 多层作用域同时 pin：闭包析构自底向上解 pin，逐层回收孤儿。
TEST(DynamicScopeTest, NestedPinnedScopesAreReclaimedBottomUp) {
    Fixture f;
    IGCEngine* gc = f.ctx->gc();
    size_t baselineValues = gc->valueCount();
    size_t baselineChildren = f.ctx->root()->childCount();

    {
        f.ctx->pushNewScope(f.ctx->current());
        f.ctx->current()->markDynamic();
        f.ctx->pushNewScope(f.ctx->current());
        Value* fn = f.ctx->createNativeFunction(readX, 0);
        f.ctx->current()->define("x", f.ctx->createNumber(1));
        f.ctx->popScope();
        f.ctx->popScope();
        (void)fn;
    }

    gc->collect();
    gc->collect();
    EXPECT_EQ(gc->valueCount(), baselineValues);
    EXPECT_EQ(f.ctx->root()->childCount(), baselineChildren);
}
