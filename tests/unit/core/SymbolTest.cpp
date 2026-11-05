// SymbolTest.cpp - Symbol 数据类型单元测试
//
// 验证（ES2024 §6.1.5 / §20.4）：
// - SymbolType 单例、kind/name
// - createSymbol：description 可空、每次分配唯一 id
// - 相等性：id 相同才相等（同描述的两个 symbol 不相等）
// - toString："Symbol(description)"
// - 运算：add/sub/mul/div 全部返回 Exception（不能隐式转换）
// - call/construct：不是函数/构造函数
// - 属性/原型操作：原始类型无自有属性、无原型
// - clone：复制同 id / 同描述的 data
// - GC：symbol 作为普通 Value 被跟踪
//
// 关联 ADR: 016（Type 体系）、017（类型清单）、018（对象模型 symbols 容器）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IData.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/Symbol.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

namespace {

using neo::core::IContext;
using neo::core::IData;
using neo::core::StringData;
using neo::core::SymbolData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::getSymbolType;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

const SymbolData* symOf(Value* v) {
    return static_cast<SymbolData*>(v->data());
}

std::string strOf(Value* v) {
    return static_cast<StringData*>(v->data())->value;
}

}  // namespace

// ===== 类型基础 =====

TEST(SymbolTest, KindAndName) {
    EXPECT_EQ(getSymbolType()->kind(), TypeKind::Symbol);
    EXPECT_STREQ(getSymbolType()->name(), "symbol");
}

TEST(SymbolTest, SingletonIdentity) {
    EXPECT_EQ(getSymbolType(), getSymbolType());
}

// ===== 创建 =====

TEST(SymbolTest, CreateWithDescription) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("key");
    ASSERT_EQ(sym->type()->kind(), TypeKind::Symbol);
    EXPECT_EQ(symOf(sym)->description, "key");
}

TEST(SymbolTest, CreateWithoutDescription) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol();
    EXPECT_EQ(symOf(sym)->description, "");
}

TEST(SymbolTest, CreateWithBindName) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s", "sym");
    EXPECT_EQ(ctx->current()->lookup("sym"), sym);
}

// ===== 唯一性与相等性 =====

TEST(SymbolTest, SameDescriptionProducesDistinctSymbols) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* a = ctx->createSymbol("x");
    Value* b = ctx->createSymbol("x");
    EXPECT_EQ(symOf(a)->description, symOf(b)->description);
    EXPECT_NE(symOf(a), symOf(b));             // 独立 data
    EXPECT_FALSE(symOf(a)->equals(symOf(b)));  // 不相等
}

TEST(SymbolTest, EqualsByIdentity) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* a = ctx->createSymbol("x");
    const SymbolData* d = symOf(a);
    // 同一 data 指针 → 相等；hash 按指针
    EXPECT_TRUE(d->equals(d));
    EXPECT_EQ(d->hash(), d->hash());
    // 另一个同描述 symbol 的 data → 不相等
    EXPECT_FALSE(d->equals(symOf(ctx->createSymbol("x"))));
}

TEST(SymbolTest, EachCreateAllocatesDistinctData) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* a = ctx->createSymbol("a");
    Value* b = ctx->createSymbol("b");
    EXPECT_NE(symOf(a), symOf(b));
}

// ===== toString =====

TEST(SymbolTest, ToStringWithDescription) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("key");
    Value* s = ctx->toString(sym);
    ASSERT_EQ(s->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(s), "Symbol(key)");
}

TEST(SymbolTest, ToStringWithoutDescription) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol();
    EXPECT_EQ(strOf(ctx->toString(sym)), "Symbol()");
}

// ===== 运算：不能隐式转换 =====

TEST(SymbolTest, ArithmeticReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s");
    Value* num = ctx->createNumber(1);
    EXPECT_EQ(ctx->add(sym, num)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->sub(sym, num)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->mul(sym, num)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->div(sym, num)->type()->kind(), TypeKind::Exception);
}

TEST(SymbolTest, RightOperandSymbolPassthrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 右操作数是 Exception/Fatal 时透传（passthrough 优先于 symbol 转换错误）
    Value* sym = ctx->createSymbol("s");
    Value* exc = ctx->createException(ctx->createString("e"));
    EXPECT_EQ(ctx->add(sym, exc), exc);
    EXPECT_EQ(ctx->sub(sym, exc), exc);
}

// ===== call / construct =====

TEST(SymbolTest, CallIsNotAFunction) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s");
    EXPECT_EQ(ctx->call(sym, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(SymbolTest, ConstructIsNotAConstructor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s");
    EXPECT_EQ(ctx->construct(sym, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

// ===== 属性 / 原型：原始类型无自有属性 =====

TEST(SymbolTest, PropertyOpsBehaveLikePrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s");
    EXPECT_EQ(ctx->getProperty(sym, "x")->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(ctx->setProperty(sym, "x", ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_FALSE(ctx->hasProperty(sym, "x"));
    EXPECT_EQ(ctx->deleteProperty(sym, "x")->type()->kind(), TypeKind::Boolean);
    EXPECT_EQ(ctx->defineProperty(sym, "x", ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
}

TEST(SymbolTest, PrototypeOpsBehaveLikePrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s");
    EXPECT_EQ(ctx->getPrototype(sym)->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(ctx->setPrototype(sym, ctx->createObject())->type()->kind(),
              TypeKind::Exception);
}

// ===== clone =====

TEST(SymbolTest, CloneSharesData) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("key");
    auto* d = const_cast<SymbolData*>(symOf(sym));
    int refBefore = d->ref();

    // 不可变类型共享 data（ADR-019 §4）：clone 返回同一 data，ref++
    IData* cloned = d->clone(alloc.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned, d);
    EXPECT_EQ(d->ref(), refBefore + 1);
    EXPECT_TRUE(d->equals(static_cast<SymbolData*>(cloned)));
    d->refDec();  // 撤销 clone 引入的引用
}

// ===== GC =====

TEST(SymbolTest, SymbolSurvivesGcWhileScoped) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s");
    ctx->gc()->collect();
    EXPECT_EQ(sym->type()->kind(), TypeKind::Symbol);
    EXPECT_EQ(symOf(sym)->description, "s");
}
