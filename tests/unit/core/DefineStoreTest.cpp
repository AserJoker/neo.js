// DefineStoreTest.cpp - define/store/clone 单元测试
//
// 验证 ADR-030 阶段 5 变量定义与赋值语义：
// - define：仅当前 scope 定义，重复定义返回 Exception
// - store：沿链查找+clone 替换 type+data，未定义返回 Exception（严格模式）
// - clone：各 Data 子类 clone 正确性
// - Exception/Fatal 透传
// - Value 本体不变（replaceMeta 原地替换）
//
// 关联 ADR: 030 阶段 5、015（Value meta+data）、017（透传）、006（异常即值）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IData.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/Boolean.h"
#include "core/types/Null.h"
#include "core/types/Undefined.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "infra/IAllocator.h"
#include "infra/Console.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <new>

namespace {

using neo::core::IContext;
using neo::core::IGCEngine;
using neo::core::IData;
using neo::core::Scope;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::BooleanData;
using neo::core::NullData;
using neo::core::UndefinedData;
using neo::core::ExceptionData;
using neo::core::FatalData;
using neo::core::getUndefinedType;
using neo::core::getStringType;
using neo::core::getNumberType;
using neo::core::getBooleanType;
using neo::core::getNullType;
using neo::core::getExceptionType;
using neo::core::getFatalType;
using neo::infra::IAllocator;
using neo::infra::Console;
using neo::infra::MallocAllocator;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

Scope* makeScope(IAllocator* alloc, Scope* parent) {
    void* p = alloc->alloc(sizeof(Scope));
    return new (p) Scope(parent, alloc);
}

}  // namespace

// ===== define 测试 =====

TEST(DefineStoreTest, DefineBindsToCurrentScope) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* v = ctx->createNumber(42.0);
    ctx->define("x", v);

    EXPECT_EQ(ctx->current()->lookup("x")->type()->kind(), TypeKind::Number);
    // define clone 了 data，slot 是新 Value，不是 v 本身
    EXPECT_NE(ctx->current()->lookup("x"), v);
    // 但内容相同
    auto* d = static_cast<NumberData*>(ctx->current()->lookup("x")->data());
    EXPECT_DOUBLE_EQ(d->value, 42.0);
}

TEST(DefineStoreTest, DefineReturnsSlotValue) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* v = ctx->createNumber(42.0);
    auto* result = ctx->define("x", v);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Number);
    EXPECT_EQ(result, ctx->current()->lookup("x"));
}

TEST(DefineStoreTest, DefineDuplicateReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* v1 = ctx->createNumber(1.0);
    ctx->define("x", v1);

    auto* v2 = ctx->createNumber(2.0);
    auto* result = ctx->define("x", v2);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
    // 原 x 未被修改
    auto* d = static_cast<NumberData*>(ctx->current()->lookup("x")->data());
    EXPECT_DOUBLE_EQ(d->value, 1.0);
}

TEST(DefineStoreTest, DefineOnlyAffectsCurrentScope) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* v = ctx->createNumber(1.0);
    ctx->define("x", v);
    EXPECT_NE(ctx->current()->lookup("x"), nullptr);

    // push 子 scope
    Scope* s = makeScope(alloc.get(), ctx->current());
    ctx->current()->addChild(s);
    ctx->setCurrentScope(s);

    auto* v2 = ctx->createNumber(2.0);
    ctx->define("y", v2);
    // 子 scope 有 y，父 scope 没有
    EXPECT_NE(ctx->current()->lookup("y"), nullptr);
    ctx->popScope();
    EXPECT_EQ(ctx->root()->lookup("y"), nullptr);
}

TEST(DefineStoreTest, DefineWithExceptionSourcePassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* errChild = ctx->createString("error");
    auto* exc = ctx->createException(errChild);
    auto* result = ctx->define("x", exc);

    EXPECT_EQ(result, exc);  // 透传
    EXPECT_EQ(ctx->current()->lookup("x"), nullptr);  // 未定义
}

TEST(DefineStoreTest, DefineWithFatalSourcePassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* fatal = ctx->createFatal("OOM");
    auto* result = ctx->define("x", fatal);

    EXPECT_EQ(result, fatal);  // 透传
    EXPECT_EQ(ctx->current()->lookup("x"), nullptr);  // 未定义
}

// ===== store 测试 =====

TEST(DefineStoreTest, StoreUpdatesExistingVariable) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("x", ctx->createNumber(1.0));
    Value* slot = ctx->current()->lookup("x");

    auto* result = ctx->store("x", ctx->createNumber(99.0));

    EXPECT_EQ(result, slot);  // store 返回 target（Value 本体不变）
    auto* d = static_cast<NumberData*>(slot->data());
    EXPECT_DOUBLE_EQ(d->value, 99.0);
}

TEST(DefineStoreTest, StoreReplacesDataNotValue) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("x", ctx->createNumber(1.0));
    Value* slotBefore = ctx->current()->lookup("x");

    ctx->store("x", ctx->createNumber(2.0));
    Value* slotAfter = ctx->current()->lookup("x");

    EXPECT_EQ(slotBefore, slotAfter);  // Value 指针不变（replaceMeta 原地替换）
}

TEST(DefineStoreTest, StoreCrossType) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("x", ctx->createNumber(42.0));
    EXPECT_EQ(ctx->current()->lookup("x")->type()->kind(), TypeKind::Number);

    ctx->store("x", ctx->createString("hello"));
    Value* slot = ctx->current()->lookup("x");
    EXPECT_EQ(slot->type()->kind(), TypeKind::String);  // type 已替换
    auto* d = static_cast<StringData*>(slot->data());
    EXPECT_EQ(d->value, "hello");  // data 已替换
}

TEST(DefineStoreTest, StoreFindsInParentScope) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("x", ctx->createNumber(1.0));

    // push 子 scope
    Scope* s = makeScope(alloc.get(), ctx->current());
    ctx->current()->addChild(s);
    ctx->setCurrentScope(s);

    // 子 scope store 修改父 scope 的 x
    ctx->store("x", ctx->createNumber(42.0));
    auto* d = static_cast<NumberData*>(ctx->root()->lookup("x")->data());
    EXPECT_DOUBLE_EQ(d->value, 42.0);

    ctx->popScope();
}

TEST(DefineStoreTest, StoreUndefinedReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* result = ctx->store("nonexistent", ctx->createNumber(1.0));

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

TEST(DefineStoreTest, StoreWithExceptionSourcePassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("x", ctx->createNumber(1.0));
    auto* errChild = ctx->createString("err");
    auto* exc = ctx->createException(errChild);

    auto* result = ctx->store("x", exc);

    EXPECT_EQ(result, exc);  // 透传
    // x 未被修改
    auto* d = static_cast<NumberData*>(ctx->current()->lookup("x")->data());
    EXPECT_DOUBLE_EQ(d->value, 1.0);
}

TEST(DefineStoreTest, StoreWithFatalSourcePassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("x", ctx->createNumber(1.0));
    auto* fatal = ctx->createFatal("crash");

    auto* result = ctx->store("x", fatal);

    EXPECT_EQ(result, fatal);  // 透传
    auto* d = static_cast<NumberData*>(ctx->current()->lookup("x")->data());
    EXPECT_DOUBLE_EQ(d->value, 1.0);
}

TEST(DefineStoreTest, StoreSelfAssignment) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("x", ctx->createNumber(42.0));
    Value* slot = ctx->current()->lookup("x");

    // x = x 自赋值，不应崩溃
    ctx->store("x", slot);
    auto* d = static_cast<NumberData*>(slot->data());
    EXPECT_DOUBLE_EQ(d->value, 42.0);
}

// ===== clone 测试（各 Data 子类）=====

TEST(DefineStoreTest, CloneNumberData) {
    auto alloc = createMallocAllocator();
    NumberData src(123.456);
    IData* cloned = src.clone(alloc.get());

    ASSERT_NE(cloned, nullptr);
    EXPECT_NE(cloned, &src);  // 不同对象
    auto* n = static_cast<NumberData*>(cloned);
    EXPECT_DOUBLE_EQ(n->value, 123.456);
    // clone 不影响原对象 ref
    EXPECT_EQ(src.ref(), 0);
    alloc->free(cloned);
}

TEST(DefineStoreTest, CloneStringData) {
    auto alloc = createMallocAllocator();
    StringData src(std::string("hello world"));
    IData* cloned = src.clone(alloc.get());

    ASSERT_NE(cloned, nullptr);
    auto* s = static_cast<StringData*>(cloned);
    EXPECT_EQ(s->value, "hello world");
    // 修改 clone 不影响原对象
    s->value = "modified";
    EXPECT_EQ(src.value, "hello world");
    alloc->free(cloned);
}

TEST(DefineStoreTest, CloneBooleanData) {
    auto alloc = createMallocAllocator();
    BooleanData src(true);
    IData* cloned = src.clone(alloc.get());

    auto* b = static_cast<BooleanData*>(cloned);
    EXPECT_TRUE(b->value);
    alloc->free(cloned);
}

TEST(DefineStoreTest, CloneNullData) {
    auto alloc = createMallocAllocator();
    NullData src;
    IData* cloned = src.clone(alloc.get());

    ASSERT_NE(cloned, nullptr);
    EXPECT_NE(cloned, &src);
    alloc->free(cloned);
}

TEST(DefineStoreTest, CloneUndefinedData) {
    auto alloc = createMallocAllocator();
    UndefinedData src;
    IData* cloned = src.clone(alloc.get());

    ASSERT_NE(cloned, nullptr);
    EXPECT_NE(cloned, &src);
    alloc->free(cloned);
}

TEST(DefineStoreTest, CloneExceptionData) {
    auto alloc = createMallocAllocator();
    auto gc = neo::core::createGcEngine(alloc.get());
    auto* child = gc->createValue(getUndefinedType(), gc->allocData<UndefinedData>());

    ExceptionData src(child);
    IData* cloned = src.clone(alloc.get());

    ASSERT_NE(cloned, nullptr);
    auto* e = static_cast<ExceptionData*>(cloned);
    EXPECT_EQ(e->child, child);  // 共享 child 指针
    alloc->free(cloned);
    gc->collect();
}

TEST(DefineStoreTest, CloneFatalData) {
    auto alloc = createMallocAllocator();
    FatalData src("crash message");
    IData* cloned = src.clone(alloc.get());

    auto* f = static_cast<FatalData*>(cloned);
    EXPECT_STREQ(f->message, "crash message");
    alloc->free(cloned);
}

// ===== clone 通过 define/store 间接验证 =====

TEST(DefineStoreTest, DefineClonesAllPrimitiveTypes) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("u", ctx->createUndefined());
    ctx->define("n", ctx->createNull());
    ctx->define("b", ctx->createBoolean(true));
    ctx->define("num", ctx->createNumber(3.14));
    ctx->define("str", ctx->createString("test"));

    EXPECT_EQ(ctx->current()->lookup("u")->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(ctx->current()->lookup("n")->type()->kind(), TypeKind::Null);
    EXPECT_EQ(ctx->current()->lookup("b")->type()->kind(), TypeKind::Boolean);
    EXPECT_EQ(ctx->current()->lookup("num")->type()->kind(), TypeKind::Number);
    EXPECT_EQ(ctx->current()->lookup("str")->type()->kind(), TypeKind::String);

    auto* bd = static_cast<BooleanData*>(ctx->current()->lookup("b")->data());
    EXPECT_TRUE(bd->value);
    auto* nd = static_cast<NumberData*>(ctx->current()->lookup("num")->data());
    EXPECT_DOUBLE_EQ(nd->value, 3.14);
    auto* sd = static_cast<StringData*>(ctx->current()->lookup("str")->data());
    EXPECT_EQ(sd->value, "test");
}

TEST(DefineStoreTest, StoreCrossTypeMultipleTimes) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("x", ctx->createNumber(1.0));
    EXPECT_EQ(ctx->current()->lookup("x")->type()->kind(), TypeKind::Number);

    ctx->store("x", ctx->createString("a"));
    EXPECT_EQ(ctx->current()->lookup("x")->type()->kind(), TypeKind::String);

    ctx->store("x", ctx->createBoolean(false));
    EXPECT_EQ(ctx->current()->lookup("x")->type()->kind(), TypeKind::Boolean);

    ctx->store("x", ctx->createNull());
    EXPECT_EQ(ctx->current()->lookup("x")->type()->kind(), TypeKind::Null);

    ctx->store("x", ctx->createUndefined());
    EXPECT_EQ(ctx->current()->lookup("x")->type()->kind(), TypeKind::Undefined);
}

// ===== GC 与 store 交互 =====

TEST(DefineStoreTest, StoreOldDataReachableByGcWhileScoped) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("x", ctx->createNumber(1.0));
    Value* slot = ctx->current()->lookup("x");

    // store 替换 data，旧 data ref 归 0，但 Value 仍存活（scopeRef != 0）
    ctx->store("x", ctx->createNumber(2.0));
    ctx->gc()->collect();

    // slot 仍存活且持有新值
    EXPECT_EQ(slot->scopeRef(), 1);
    auto* d = static_cast<NumberData*>(slot->data());
    EXPECT_DOUBLE_EQ(d->value, 2.0);
}

// ===== 作用域标注（scope flags）=====

TEST(ScopeFlagTest, GlobalDefaultsToStrict) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // global 默认标注 strict，current(=root) 沿链继承
    EXPECT_TRUE(ctx->global()->hasFlag("strict"));
    EXPECT_TRUE(ctx->root()->lookupFlagChain("strict"));
    EXPECT_TRUE(ctx->hasFlag("strict"));
}

TEST(ScopeFlagTest, SetFlagOnCurrentScope) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_FALSE(ctx->current()->hasFlag("sloppy"));
    ctx->setFlag("sloppy");
    EXPECT_TRUE(ctx->current()->hasFlag("sloppy"));
    EXPECT_TRUE(ctx->hasFlag("sloppy"));
}

TEST(ScopeFlagTest, FlagInheritedByChildScope) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->setFlag("module_flag");

    Scope* s = makeScope(alloc.get(), ctx->current());
    ctx->current()->addChild(s);
    ctx->setCurrentScope(s);

    // 子 scope 未标注，但沿链继承父的标注
    EXPECT_FALSE(ctx->current()->hasFlag("module_flag"));
    EXPECT_TRUE(ctx->current()->lookupFlagChain("module_flag"));
    EXPECT_TRUE(ctx->hasFlag("module_flag"));

    ctx->popScope();
}

TEST(ScopeFlagTest, ChildFlagDoesNotLeakToParent) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Scope* s = makeScope(alloc.get(), ctx->current());
    ctx->current()->addChild(s);
    ctx->setCurrentScope(s);

    ctx->setFlag("inner_only");
    EXPECT_TRUE(ctx->hasFlag("inner_only"));

    ctx->popScope();
    // 父 scope 不继承子 scope 的标注
    EXPECT_FALSE(ctx->hasFlag("inner_only"));
}

TEST(ScopeFlagTest, ClearFlag) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->current()->setFlag("temp");
    EXPECT_TRUE(ctx->current()->hasFlag("temp"));
    ctx->current()->clearFlag("temp");
    EXPECT_FALSE(ctx->current()->hasFlag("temp"));
}

TEST(ScopeFlagTest, NonStrictStoreDefinesGlobal) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 清除 global 的 strict 标注 → 非严格模式
    ctx->global()->clearFlag("strict");
    EXPECT_FALSE(ctx->hasFlag("strict"));

    // 未定义变量 store：非严格模式 → 在 global 定义
    auto* result = ctx->store("implicit", ctx->createNumber(42.0));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Number);
    EXPECT_EQ(result, ctx->global()->lookup("implicit"));

    auto* d = static_cast<NumberData*>(result->data());
    EXPECT_DOUBLE_EQ(d->value, 42.0);
}

TEST(ScopeFlagTest, NonStrictStoreInChildScopeDefinesGlobal) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->global()->clearFlag("strict");

    Scope* s = makeScope(alloc.get(), ctx->current());
    ctx->current()->addChild(s);
    ctx->setCurrentScope(s);

    // 子 scope 中非严格 store → 定义到 global（不是当前 scope）
    auto* result = ctx->store("g", ctx->createNumber(7.0));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result, ctx->global()->lookup("g"));
    EXPECT_EQ(ctx->current()->lookup("g"), nullptr);

    ctx->popScope();
}

TEST(ScopeFlagTest, StrictStoreStillErrors) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 默认严格模式，未定义变量 store → Exception
    auto* result = ctx->store("undeclared", ctx->createNumber(1.0));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->global()->lookup("undeclared"), nullptr);
}

// ===== 可赋值性（mutable）=====

TEST(MutableTest, ValueDefaultsToMutable) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* v = ctx->createNumber(1.0);
    EXPECT_TRUE(v->isMutable());
    EXPECT_TRUE(ctx->isMutable(v));
}

TEST(MutableTest, SetMutableFalse) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* v = ctx->createNumber(1.0);
    ctx->setMutable(v, false);
    EXPECT_FALSE(v->isMutable());
    EXPECT_FALSE(ctx->isMutable(v));

    ctx->setMutable(v, true);
    EXPECT_TRUE(v->isMutable());
}

TEST(MutableTest, StoreOnImmutableReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("c", ctx->createNumber(1.0));
    Value* slot = ctx->current()->lookup("c");
    ctx->setMutable(slot, false);  // 模拟 const 声明

    auto* result = ctx->store("c", ctx->createNumber(99.0));

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
    // 原值未被修改
    auto* d = static_cast<NumberData*>(slot->data());
    EXPECT_DOUBLE_EQ(d->value, 1.0);
}

TEST(MutableTest, StoreOnMutableSucceeds) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("v", ctx->createNumber(1.0));
    Value* slot = ctx->current()->lookup("v");
    EXPECT_TRUE(slot->isMutable());

    auto* result = ctx->store("v", ctx->createNumber(42.0));

    EXPECT_EQ(result, slot);
    auto* d = static_cast<NumberData*>(slot->data());
    EXPECT_DOUBLE_EQ(d->value, 42.0);
}

TEST(MutableTest, ImmutableInParentScopeBlocksChildStore) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("c", ctx->createNumber(1.0));
    ctx->setMutable(ctx->current()->lookup("c"), false);

    Scope* s = makeScope(alloc.get(), ctx->current());
    ctx->current()->addChild(s);
    ctx->setCurrentScope(s);

    // 子 scope store 父 scope 的 const → 仍被拦截
    auto* result = ctx->store("c", ctx->createNumber(2.0));
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);

    ctx->popScope();
    auto* d = static_cast<NumberData*>(ctx->current()->lookup("c")->data());
    EXPECT_DOUBLE_EQ(d->value, 1.0);
}

TEST(MutableTest, ImmutableExceptionPassthroughStillWorks) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    ctx->define("c", ctx->createNumber(1.0));
    ctx->setMutable(ctx->current()->lookup("c"), false);

    // source 是 Exception 时透传优先于 immutable 检查
    auto* exc = ctx->createException(ctx->createString("err"));
    auto* result = ctx->store("c", exc);
    EXPECT_EQ(result, exc);
}
