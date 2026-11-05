// WellKnownSymbolTest.cpp - well-known symbols 与 Symbol 包装类单元测试
//
// 验证（ES2024 §6.1.5.1 / §20.4）：
// - 15 个 well-known symbols 在 Context 创建时生成，始终返回同一 Value
// - description = "Symbol.<name>"，彼此不相等
// - 挂 global 存活（GC 后仍可用）
// - Symbol 包装类注册到 global：Symbol(description) 返回新 symbol
// - Symbol.prototype.toString()
// - Symbol.<well-known> 属性（不可写/不可枚举/不可配置）
// - Symbol.prototype[Symbol.toStringTag] = "Symbol"
//
// 关联 ADR: 020（标准库组织）、016（Type 体系）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/WellKnownSymbol.h"
#include "core/types/Function.h"
#include "core/types/Object.h"
#include "core/types/String.h"
#include "core/types/Symbol.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace {

using neo::core::IContext;
using neo::core::ObjectData;
using neo::core::StringData;
using neo::core::SymbolData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::WellKnownSymbol;
using neo::core::createContext;
using neo::core::kWellKnownSymbolCount;
using neo::core::wellKnownSymbolName;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

std::string strOf(Value* v) {
    return static_cast<StringData*>(v->data())->value;
}

const SymbolData* symOf(Value* v) {
    return static_cast<SymbolData*>(v->data());
}

}  // namespace

// ===== well-known symbols =====

TEST(WellKnownSymbolTest, AllAreSymbolsWithDistinctIdentity) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    std::set<const void*> identities;
    for (size_t i = 0; i < kWellKnownSymbolCount; ++i) {
        Value* sym = ctx->getWellKnownSymbol(static_cast<WellKnownSymbol>(i));
        ASSERT_EQ(sym->type()->kind(), TypeKind::Symbol);
        identities.insert(symOf(sym));
    }
    EXPECT_EQ(identities.size(), kWellKnownSymbolCount);  // 全部互不相同
}

TEST(WellKnownSymbolTest, StableAcrossCalls) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* a = ctx->getWellKnownSymbol(WellKnownSymbol::Iterator);
    Value* b = ctx->getWellKnownSymbol(WellKnownSymbol::Iterator);
    EXPECT_EQ(a, b);  // 同一 Value
}

TEST(WellKnownSymbolTest, DescriptionsMatchNames) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    for (size_t i = 0; i < kWellKnownSymbolCount; ++i) {
        auto which = static_cast<WellKnownSymbol>(i);
        Value* sym = ctx->getWellKnownSymbol(which);
        EXPECT_EQ(symOf(sym)->description,
                  std::string("Symbol.") + wellKnownSymbolName(which));
    }
}

TEST(WellKnownSymbolTest, NamesAreUniqueAndNonEmpty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    std::set<std::string> names;
    for (size_t i = 0; i < kWellKnownSymbolCount; ++i) {
        std::string n = wellKnownSymbolName(static_cast<WellKnownSymbol>(i));
        EXPECT_FALSE(n.empty());
        names.insert(n);
    }
    EXPECT_EQ(names.size(), kWellKnownSymbolCount);
}

TEST(WellKnownSymbolTest, SurviveGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->getWellKnownSymbol(WellKnownSymbol::ToPrimitive);
    ctx->gc()->collect();
    EXPECT_EQ(ctx->getWellKnownSymbol(WellKnownSymbol::ToPrimitive), sym);
    EXPECT_EQ(sym->type()->kind(), TypeKind::Symbol);
}

// ===== Symbol 包装类 =====

TEST(WellKnownSymbolTest, SymbolGlobalIsRegistered) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    ASSERT_NE(symbolFn, nullptr);
    EXPECT_EQ(symbolFn->type()->kind(), TypeKind::Function);
    // 函数名
    EXPECT_EQ(strOf(ctx->getProperty(symbolFn, "name")), "Symbol");
}

TEST(WellKnownSymbolTest, SymbolCallCreatesNewSymbol) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* desc = ctx->createString("foo");
    Value* args[1] = {desc};
    Value* sym = ctx->call(symbolFn, nullptr, 1, args);

    ASSERT_EQ(sym->type()->kind(), TypeKind::Symbol);
    EXPECT_EQ(symOf(sym)->description, "foo");

    // 每次调用都是新 symbol
    Value* sym2 = ctx->call(symbolFn, nullptr, 1, args);
    EXPECT_NE(symOf(sym), symOf(sym2));
}

TEST(WellKnownSymbolTest, SymbolCallWithoutDescription) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* sym = ctx->call(symbolFn, nullptr, 0, nullptr);
    ASSERT_EQ(sym->type()->kind(), TypeKind::Symbol);
    EXPECT_EQ(symOf(sym)->description, "");
}

TEST(WellKnownSymbolTest, SymbolCallCoercesDescription) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* args[1] = {ctx->createNumber(42)};
    Value* sym = ctx->call(symbolFn, nullptr, 1, args);
    EXPECT_EQ(symOf(sym)->description, "42");
}

TEST(WellKnownSymbolTest, SymbolCallWithUndefinedDescriptionIsEmpty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* args[1] = {ctx->createUndefined()};
    Value* sym = ctx->call(symbolFn, nullptr, 1, args);
    EXPECT_EQ(symOf(sym)->description, "");
}

TEST(WellKnownSymbolTest, PrototypeToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* proto = ctx->getProperty(symbolFn, "prototype");
    Value* toStringFn = ctx->getProperty(proto, "toString");
    ASSERT_EQ(toStringFn->type()->kind(), TypeKind::Function);

    Value* sym = ctx->call(symbolFn, nullptr, 0, nullptr);
    Value* res = ctx->call(toStringFn, sym, 0, nullptr);
    ASSERT_EQ(res->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(res), "Symbol()");
}

TEST(WellKnownSymbolTest, PrototypeToStringRejectsNonSymbolThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* proto = ctx->getProperty(symbolFn, "prototype");
    Value* toStringFn = ctx->getProperty(proto, "toString");

    Value* res = ctx->call(toStringFn, ctx->createNumber(1), 0, nullptr);
    EXPECT_EQ(res->type()->kind(), TypeKind::Exception);
}

TEST(WellKnownSymbolTest, WellKnownSymbolsAreConstructorProperties) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    for (size_t i = 0; i < kWellKnownSymbolCount; ++i) {
        auto which = static_cast<WellKnownSymbol>(i);
        Value* got = ctx->getProperty(symbolFn, wellKnownSymbolName(which));
        EXPECT_EQ(got, ctx->getWellKnownSymbol(which));
    }
}

TEST(WellKnownSymbolTest, WellKnownPropertiesAreReadOnly) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    auto* d = static_cast<ObjectData*>(symbolFn->data());
    const auto& p = d->properties.at("iterator");
    EXPECT_FALSE(p.writable);
    EXPECT_FALSE(p.enumerable);
    EXPECT_FALSE(p.configurable);
    // 普通 set 报错
    EXPECT_EQ(ctx->setProperty(symbolFn, "iterator", ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
}

TEST(WellKnownSymbolTest, PrototypeToStringTagIsSymbol) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    Value* proto = ctx->getProperty(symbolFn, "prototype");
    Value* tag = ctx->getProperty(proto, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag));
    ASSERT_EQ(tag->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(tag), "Symbol");
}

TEST(WellKnownSymbolTest, SymbolFunctionSurvivesGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* symbolFn = ctx->global()->lookup("Symbol");
    ctx->gc()->collect();
    EXPECT_EQ(ctx->global()->lookup("Symbol"), symbolFn);
    EXPECT_EQ(symbolFn->type()->kind(), TypeKind::Function);
}
