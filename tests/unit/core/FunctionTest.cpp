// FunctionTest.cpp - Function 类型与 native 调用单元测试
//
// 验证（ADR-021/022）：
// - FunctionType 单例、kind/name
// - createNativeFunction：closureScope 是 root 子作用域、rootScope 借用 root
// - call：native 调用、参数传递、this 绑定
// - minArgc 校验（不足返回 Exception）
// - 非函数类型 call 返回 Exception("xxx is not a function")
// - Function 其他行为返回 Exception("not implement")
// - Exception/Fatal/Uninitialized 的 call 透传
// - FunctionData clone
//
// 关联 ADR: 005（native 等价）、013（闭包）、021（Function 结构）、022（调用）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IData.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "infra/IAllocator.h"
#include "infra/Console.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

namespace {

using neo::core::IContext;
using neo::core::IData;
using neo::core::NativeFn;
using neo::core::Scope;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::FunctionData;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::getFunctionType;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

// 测试用 native 函数：返回参数个数
Value* argcFn(IContext* ctx, Value* /*self*/, size_t argc, Value* /*argv*/[]) {
    return ctx->createNumber(static_cast<double>(argc));
}

// 测试用 native 函数：返回第一个参数（无参返回 undefined）
Value* firstArgFn(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return argc > 0 ? argv[0] : ctx->createUndefined();
}

// 测试用 native 函数：返回 this（无 this 返回 undefined）
Value* thisFn(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return self != nullptr ? self : ctx->createUndefined();
}

// 测试用 native 函数：返回 Exception
Value* throwingFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createException(ctx->createString("boom"));
}

// 测试用 native 函数：沿当前作用域链查找 x（验证闭包捕获在调用时可见）
Value* lookupXFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    Value* v = ctx->current()->lookupChain("x");
    return v != nullptr ? v : ctx->createUndefined();
}

}  // namespace

// ===== 类型基础 =====

TEST(FunctionTest, KindAndName) {
    EXPECT_EQ(getFunctionType()->kind(), TypeKind::Function);
    EXPECT_STREQ(getFunctionType()->name(), "function");
}

TEST(FunctionTest, SingletonIdentity) {
    EXPECT_EQ(getFunctionType(), getFunctionType());
}

// ===== 创建 =====

TEST(FunctionTest, CreateNativeFunction) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->type()->kind(), TypeKind::Function);
}

TEST(FunctionTest, DefaultNameIsAnonymous) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    // 函数名是 "name" 属性，不是 data 字段
    Value* name = ctx->getProperty(fn, "name");
    ASSERT_EQ(name->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(name->data())->value, "<anonymous>");
}

TEST(FunctionTest, SetFunctionName) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    EXPECT_EQ(static_cast<StringData*>(ctx->getProperty(fn, "name")->data())->value,
              "<anonymous>");

    ctx->setFunctionName(fn, "myFunc");
    EXPECT_EQ(static_cast<StringData*>(ctx->getProperty(fn, "name")->data())->value,
              "myFunc");
}

TEST(FunctionTest, SetFunctionNameIgnoresNonFunction) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 非函数类型忽略 setFunctionName（不崩溃）
    Value* num = ctx->createNumber(1);
    ctx->setFunctionName(num, "x");
    EXPECT_EQ(num->type()->kind(), TypeKind::Number);
}

TEST(FunctionTest, ClosureScopeIsIsolated) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    auto* d = static_cast<FunctionData*>(fn->data());

    EXPECT_EQ(d->rootScope, ctx->root());          // rootScope 借用 root
    ASSERT_NE(d->closureScope, nullptr);
    // 闭包作用域是孤立的（ADR-036）：不挂在 root 下，不是 GC 根
    EXPECT_EQ(d->closureScope->parent(), nullptr);
    EXPECT_EQ(ctx->root()->children().count(d->closureScope), 0u);
}

TEST(FunctionTest, CreateWithBindNameBinds) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 第 3 参数是绑定名
    Value* fn = ctx->createNativeFunction(argcFn, 0, "f");
    EXPECT_EQ(ctx->current()->lookup("f"), fn);
    // 创建时函数名缺省 <anonymous>
    EXPECT_EQ(static_cast<StringData*>(ctx->getProperty(fn, "name")->data())->value,
              "<anonymous>");
}

TEST(FunctionTest, FunctionNameAndBindNameAreSeparate) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // method 场景：绑定名 foo，函数名 bar（define → store → setname 序列）
    Value* fn = ctx->createNativeFunction(argcFn, 0, "foo");
    ctx->setFunctionName(fn, "bar");
    EXPECT_EQ(static_cast<StringData*>(ctx->getProperty(fn, "name")->data())->value, "bar");
    EXPECT_EQ(ctx->current()->lookup("foo"), fn);
    EXPECT_EQ(ctx->current()->lookup("bar"), nullptr);  // 函数名不参与绑定
}

// ===== 调用 =====

TEST(FunctionTest, CallReturnsNativeResult) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    Value* args[] = {ctx->createNumber(1), ctx->createNumber(2)};
    Value* result = ctx->call(fn, nullptr, 2, args);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(static_cast<NumberData*>(result->data())->value, 2.0);
}

TEST(FunctionTest, CallPassesArguments) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(firstArgFn, 0);
    Value* args[] = {ctx->createString("hello")};
    Value* result = ctx->call(fn, nullptr, 1, args);

    EXPECT_EQ(result, args[0]);
}

TEST(FunctionTest, CallBindsThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(thisFn, 0);
    Value* thisArg = ctx->createNumber(42);
    Value* result = ctx->call(fn, thisArg, 0, nullptr);

    EXPECT_EQ(result, thisArg);
}

TEST(FunctionTest, CallNoArgs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    Value* result = ctx->call(fn, nullptr, 0, nullptr);

    EXPECT_DOUBLE_EQ(static_cast<NumberData*>(result->data())->value, 0.0);
}

TEST(FunctionTest, CallPropagatesException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(throwingFn, 0);
    Value* result = ctx->call(fn, nullptr, 0, nullptr);

    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

// ===== minArgc 校验 =====

TEST(FunctionTest, MinArgcSatisfied) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 2);
    Value* args[] = {ctx->createNumber(1), ctx->createNumber(2)};
    Value* result = ctx->call(fn, nullptr, 2, args);

    EXPECT_EQ(result->type()->kind(), TypeKind::Number);
}

TEST(FunctionTest, MinArgcNotSatisfiedReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 3);
    Value* args[] = {ctx->createNumber(1)};
    Value* result = ctx->call(fn, nullptr, 1, args);

    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

TEST(FunctionTest, MinArgcMoreArgsOk) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 1);
    Value* args[] = {ctx->createNumber(1), ctx->createNumber(2), ctx->createNumber(3)};
    Value* result = ctx->call(fn, nullptr, 3, args);

    EXPECT_DOUBLE_EQ(static_cast<NumberData*>(result->data())->value, 3.0);
}

// ===== 非函数类型调用 =====

TEST(FunctionTest, CallNonFunctionReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* num = ctx->createNumber(42);
    Value* result = ctx->call(num, nullptr, 0, nullptr);

    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

TEST(FunctionTest, CallStringReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* result = ctx->call(ctx->createString("x"), nullptr, 0, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

TEST(FunctionTest, CallNullReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* result = ctx->call(ctx->createNull(), nullptr, 0, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

TEST(FunctionTest, CallUndefinedReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* result = ctx->call(ctx->createUndefined(), nullptr, 0, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

TEST(FunctionTest, CallBooleanReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* result = ctx->call(ctx->createBoolean(true), nullptr, 0, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

// ===== Function 运算走 ToPrimitive（ES2024 §13.15.3 / §13.15.4）=====

TEST(FunctionTest, ArithmeticUsesToPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    Value* num = ctx->createNumber(1);

    // Function.prototype.toString → "function () { [native code] }"，故 + 为拼接
    Value* sum = ctx->add(fn, num);
    ASSERT_EQ(sum->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(sum->data())->value,
              "function () { [native code] }1");
    // 算术按 ToNumber(源码字符串) = NaN
    EXPECT_TRUE(std::isnan(static_cast<NumberData*>(ctx->sub(fn, num)->data())->value));
    EXPECT_TRUE(std::isnan(static_cast<NumberData*>(ctx->mul(fn, num)->data())->value));
    EXPECT_TRUE(std::isnan(static_cast<NumberData*>(ctx->div(fn, num)->data())->value));
    // toString 走 ToPrimitive(STRING)
    EXPECT_EQ(static_cast<StringData*>(ctx->toString(fn)->data())->value,
              "function () { [native code] }");
}

// ===== Exception/Fatal/Uninitialized 透传 =====

TEST(FunctionTest, CallExceptionPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* exc = ctx->createException(ctx->createString("e"));
    EXPECT_EQ(ctx->call(exc, nullptr, 0, nullptr), exc);
}

TEST(FunctionTest, CallFatalPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fatal = ctx->createFatal("crash");
    EXPECT_EQ(ctx->call(fatal, nullptr, 0, nullptr), fatal);
}

// ===== clone =====

// ===== invoke 流程：scope 切换/恢复、返回值归属（ADR-022）=====

namespace {

// 记录调用期间观察到的 root/current scope 及其 parent，供断言
// （execScope 调用后即销毁，必须在 native 内部记录其 parent，不能调用后访问）
Scope* gObservedRoot = nullptr;
Scope* gObservedCurrent = nullptr;
Scope* gObservedCurrentParent = nullptr;

Value* observeScopeFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    gObservedRoot = ctx->root();
    gObservedCurrent = ctx->current();
    gObservedCurrentParent = ctx->current()->parent();
    return ctx->createUndefined();
}

}  // namespace

TEST(FunctionTest, CallSwitchesToFunctionRootScope) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    gObservedRoot = nullptr;
    Value* fn = ctx->createNativeFunction(observeScopeFn, 0);
    auto* d = static_cast<FunctionData*>(fn->data());

    ctx->call(fn, nullptr, 0, nullptr);

    // 调用期间 root 切到函数的 rootScope
    EXPECT_EQ(gObservedRoot, d->rootScope);
}

TEST(FunctionTest, CallExecScopeIsChildOfClosure) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    gObservedCurrent = nullptr;
    gObservedCurrentParent = nullptr;
    Value* fn = ctx->createNativeFunction(observeScopeFn, 0);
    auto* d = static_cast<FunctionData*>(fn->data());

    ctx->call(fn, nullptr, 0, nullptr);

    // 调用期间 current 是 closureScope 的子（临时执行作用域）
    ASSERT_NE(gObservedCurrent, nullptr);
    EXPECT_EQ(gObservedCurrentParent, d->closureScope);
}

TEST(FunctionTest, CallRestoresScopeAfterReturn) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Scope* rootBefore = ctx->root();
    Scope* currentBefore = ctx->current();

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    ctx->call(fn, nullptr, 0, nullptr);

    // 调用后 root/current 恢复
    EXPECT_EQ(ctx->root(), rootBefore);
    EXPECT_EQ(ctx->current(), currentBefore);
}

TEST(FunctionTest, CallResultRegisteredInCallerScope) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    Value* result = ctx->call(fn, nullptr, 0, nullptr);

    ASSERT_NE(result, nullptr);
    // 返回值归属调用方 current（restoreValue）：从 global 树可达，GC 不回收
    ctx->gc()->collect();
    EXPECT_EQ(result->type()->kind(), TypeKind::Number);
}

TEST(FunctionTest, CallTempValuesDoNotAccumulateInClosure) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    auto* d = static_cast<FunctionData*>(fn->data());
    size_t closureCountBefore = d->closureScope->valueCount();

    ctx->call(fn, nullptr, 0, nullptr);

    // 临时执行作用域已销毁，closureScope 未累积临时值
    EXPECT_EQ(d->closureScope->valueCount(), closureCountBefore);
}

// ===== 闭包作用域临时挂载（ADR-036）=====

TEST(FunctionTest, ClosureScopeMountedOnlyDuringCall) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(observeScopeFn, 0);
    auto* d = static_cast<FunctionData*>(fn->data());
    EXPECT_EQ(d->closureScope->parent(), nullptr);

    gObservedCurrentParent = nullptr;
    ctx->call(fn, nullptr, 0, nullptr);

    // 调用期间 execScope 的 parent 是 closureScope，且 closureScope 已挂到 rootScope
    EXPECT_EQ(gObservedCurrentParent, d->closureScope);
    // 调用结束后摘回孤立
    EXPECT_EQ(d->closureScope->parent(), nullptr);
    EXPECT_EQ(ctx->root()->children().count(d->closureScope), 0u);
}

namespace {

// 重入：第一次调用里再调自己，观察闭包作用域在内层调用期间仍保持挂载
int gReentrantDepth = 0;
bool gInnerSawMounted = false;
Value* gReentrantFn = nullptr;

Value* reentrantFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    auto* d = static_cast<FunctionData*>(gReentrantFn->data());
    if (gReentrantDepth == 0) {
        ++gReentrantDepth;
        ctx->call(gReentrantFn, nullptr, 0, nullptr);
        --gReentrantDepth;
    } else {
        // 内层调用期间闭包作用域必须仍挂在 rootScope 下
        gInnerSawMounted = d->closureScope->parent() == d->rootScope;
    }
    return ctx->createUndefined();
}

}  // namespace

TEST(FunctionTest, ReentrantCallKeepsClosureMounted) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    gReentrantDepth = 0;
    gInnerSawMounted = false;
    gReentrantFn = ctx->createNativeFunction(reentrantFn, 0);
    auto* d = static_cast<FunctionData*>(gReentrantFn->data());

    ctx->call(gReentrantFn, nullptr, 0, nullptr);

    EXPECT_TRUE(gInnerSawMounted);
    // 最外层退出后才摘除
    EXPECT_EQ(d->closureScope->parent(), nullptr);
}

TEST(FunctionTest, FatalCallUnmountsClosureScope) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createFatal("boom"); }, 0);
    auto* d = static_cast<FunctionData*>(fn->data());
    Value* result = ctx->call(fn, nullptr, 0, nullptr);

    EXPECT_EQ(result->type()->kind(), TypeKind::Fatal);
    // Fatal 虽不恢复现场，但闭包作用域必须摘除（否则 ~Context 时双重销毁）
    EXPECT_EQ(d->closureScope->parent(), nullptr);
}

TEST(FunctionTest, ClosureScopeDestroyedWithFunctionData) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 在子作用域里创建函数并捕获值，退出子作用域后函数不可达
    Scope* s = ctx->pushNewScope(ctx->current());
    ASSERT_NE(s, nullptr);
    ctx->createNumber(42, "x");
    Value* fn = ctx->createNativeFunction(argcFn, 0);
    ASSERT_EQ(ctx->setClosure(fn, "x"), fn);
    ctx->popScope();  // fn 的 Value 不再被任何作用域跟踪

    // fn 从 global 树不可达 → 一轮 GC 回收 fn → FunctionData 析构 → closureScope 销毁。
    // captured 只被 fn 的闭包作用域引用，随之失去可达性，同一轮或下一轮被回收
    // （GC 析构/收敛判定覆盖此情形）。两轮后全部回收。
    ctx->gc()->collect();
    ctx->gc()->collect();
}

TEST(FunctionTest, RepeatedCreateCallDoesNotAccumulate) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    size_t rootChildrenBefore = ctx->root()->childCount();
    // 每轮在子作用域里创建函数、捕获局部值并调用，然后退出子作用域。
    // 闭包作用域孤立化前，它们常驻 root 子树 → 运行期内存持续增长（ADR-036 bug）。
    for (int i = 0; i < 50; ++i) {
        Scope* s = ctx->pushNewScope(ctx->current());
        ASSERT_NE(s, nullptr);
        ctx->createNumber(i, "x");
        Value* fn = ctx->createNativeFunction(lookupXFn, 0);
        ASSERT_EQ(ctx->setClosure(fn, "x"), fn);
        Value* r = ctx->call(fn, nullptr, 0, nullptr);
        ASSERT_EQ(r->type()->kind(), TypeKind::Number);
        ctx->popScope();
        ctx->gc()->collect();
    }
    // 循环结束后不应残留大量 Value（root/global 的常驻内建除外）
    size_t before = ctx->gc()->valueCount();
    for (int i = 0; i < 50; ++i) {
        Scope* s = ctx->pushNewScope(ctx->current());
        ASSERT_NE(s, nullptr);
        ctx->createNumber(i, "x");
        Value* fn = ctx->createNativeFunction(lookupXFn, 0);
        ctx->setClosure(fn, "x");
        ctx->call(fn, nullptr, 0, nullptr);
        ctx->popScope();
        ctx->gc()->collect();
    }
    // 第二批不带来净增长：函数对象、闭包作用域、捕获值都被回收
    EXPECT_EQ(ctx->gc()->valueCount(), before);
    // root 下没有累积的闭包作用域
    EXPECT_EQ(ctx->root()->childCount(), rootChildrenBefore);
}

// ===== 闭包捕获（ADR-013 §2）=====

TEST(FunctionTest, SetClosureCapturesVariable) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* captured = ctx->createNumber(42, "x");
    Value* fn = ctx->createNativeFunction(argcFn, 0);
    auto* d = static_cast<FunctionData*>(fn->data());

    Value* result = ctx->setClosure(fn, "x");

    EXPECT_EQ(result, fn);
    // closureScope 中绑定的是同一 Value（共享，非复制）
    EXPECT_EQ(d->closureScope->lookup("x"), captured);
}

TEST(FunctionTest, SetClosureSharesValueNotCopies) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* captured = ctx->createNumber(1, "x");
    Value* fn = ctx->createNativeFunction(argcFn, 0);
    auto* d = static_cast<FunctionData*>(fn->data());

    ctx->setClosure(fn, "x");

    // 共享同一 Value（不复制）：closureScope 绑定到同一个 Value
    EXPECT_EQ(d->closureScope->lookup("x"), captured);
}

TEST(FunctionTest, SetClosureCapturesFromParentScope) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* captured = ctx->createNumber(7, "outer");

    // 进入子 scope 后创建函数并捕获外层变量
    Scope* s = ctx->pushNewScope(ctx->current());
    ASSERT_NE(s, nullptr);
    Value* fn = ctx->createNativeFunction(argcFn, 0);
    ctx->setClosure(fn, "outer");

    auto* d = static_cast<FunctionData*>(fn->data());
    EXPECT_EQ(d->closureScope->lookup("outer"), captured);
    ctx->popScope();
}

TEST(FunctionTest, SetClosureUndefinedVariableReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 0);
    Value* result = ctx->setClosure(fn, "nonexistent");

    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

TEST(FunctionTest, SetClosureOnNonFunctionReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->createNumber(1, "x");
    Value* num = ctx->createNumber(2);
    Value* result = ctx->setClosure(num, "x");

    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

TEST(FunctionTest, ClosureVariableVisibleDuringCall) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 外层定义 x=99，函数闭包捕获 x，调用时通过作用域链可见
    ctx->createNumber(99, "x");
    Value* fn = ctx->createNativeFunction(lookupXFn, 0);
    ctx->setClosure(fn, "x");

    Value* result = ctx->call(fn, nullptr, 0, nullptr);
    ASSERT_EQ(result->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(static_cast<NumberData*>(result->data())->value, 99.0);
}

// ===== 返回值归属时机 / 栈平衡 / Fatal 透传（ADR-022 §3/§4）=====

namespace {

// native 内部额外 push 两层 scope（模拟"保存现场不立即返回"的栈不平衡）
Value* unbalancedFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    ctx->pushNewScope(ctx->current());
    ctx->pushNewScope(ctx->current());
    return ctx->createNumber(7);
}

// native 返回 Fatal（不可捕获）
Value* fatalFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createFatal("fatal from native");
}

// native 返回被 execScope 跟踪的值（验证 restoreValue 时机）
Value* scopedResultFn(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createNumber(99);  // 归属 execScope
}

}  // namespace

TEST(FunctionTest, CallBalancesUnbalancedNativeStack) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Scope* currentBefore = ctx->current();
    Value* fn = ctx->createNativeFunction(unbalancedFn, 0);
    Value* result = ctx->call(fn, nullptr, 0, nullptr);

    // native 多 push 的两层被循环 pop 平衡，current 恢复
    EXPECT_EQ(ctx->current(), currentBefore);
    EXPECT_DOUBLE_EQ(static_cast<NumberData*>(result->data())->value, 7.0);
}

TEST(FunctionTest, CallFatalSkipsRestoreAndStackBalance) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(fatalFn, 0);
    Value* result = ctx->call(fn, nullptr, 0, nullptr);

    // Fatal 直接透传
    EXPECT_EQ(result->type()->kind(), TypeKind::Fatal);
    // 未恢复现场：root 仍是函数 rootScope（Fatal 交给宿主，不继续执行）
    auto* d = static_cast<FunctionData*>(fn->data());
    EXPECT_EQ(ctx->root(), d->rootScope);
}

TEST(FunctionTest, CallResultSurvivesExecScopeDestroy) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(scopedResultFn, 0);
    Value* result = ctx->call(fn, nullptr, 0, nullptr);

    // 返回值在 pop 前归属调用方 scope，execScope 销毁后仍存活（从 global 树可达）
    ASSERT_NE(result, nullptr);
    ctx->gc()->collect();
    EXPECT_EQ(result->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(static_cast<NumberData*>(result->data())->value, 99.0);
}

TEST(FunctionTest, FunctionDataCloneSharesData) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(argcFn, 2);
    ctx->setFunctionName(fn, "cloned");
    auto* d = static_cast<FunctionData*>(fn->data());
    int refBefore = d->ref();

    // 函数是对象（FunctionData : ObjectData），clone 走引用语义（ADR-019 §2）
    IData* cloned = d->clone(alloc.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned, d);
    EXPECT_EQ(d->ref(), refBefore + 1);
    EXPECT_EQ(d->closureScope, static_cast<FunctionData*>(cloned)->closureScope);
    EXPECT_EQ(static_cast<StringData*>(ctx->getProperty(fn, "name")->data())->value,
              "cloned");
    // 撤销 clone 引入的引用，避免 data 泄漏
    d->refDec();
}
