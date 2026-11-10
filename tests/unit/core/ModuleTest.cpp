// ModuleTest.cpp - ES Module 基础设施单元测试
//
// 验证：
// - 模块以绝对路径为唯一 id，惰性创建（getModule）
// - 每个模块借用一棵挂在 global 下的 root scope + 原型为 null 的导出对象
// - import = 惰性加载 + 取 Module + 取导出对象
// - loader 只执行一次，且在模块 root scope 下执行
// - 循环依赖返回当前（部分）导出对象
// - loader 失败原样透传错误值，且模块保持未加载（可重试）

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Module.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using neo::core::IContext;
using neo::core::ModuleInfo;
using neo::core::Scope;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::detail::isErrorValue;
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

// —— loader 共享状态（ModuleLoader 是裸函数指针，无法携带 C++ 状态）——
int g_calls = 0;
Scope* g_seenCurrent = nullptr;
Scope* g_seenRoot = nullptr;
Value* g_aFromB = nullptr;
Value* g_bFromA = nullptr;
Value* g_importTarget = nullptr;
Value* g_nestedTarget = nullptr;

Value* loaderAnswer(IContext* ctx, Value* exports) {
    ++g_calls;
    ctx->defineProperty(exports, "answer", ctx->createNumber(42));
    return ctx->createUndefined();
}

Value* loaderScope(IContext* ctx, Value* exports) {
    g_seenCurrent = ctx->current();
    g_seenRoot = ctx->root();
    ctx->defineProperty(exports, "x", ctx->createNumber(1));
    return ctx->createUndefined();
}

Value* loaderFailOnce(IContext* ctx, Value* exports) {
    ++g_calls;
    if (g_calls == 1) {
        return ctx->createException(ctx->createString("boom"));
    }
    ctx->defineProperty(exports, "ok", ctx->createBoolean(true));
    return ctx->createUndefined();
}

Value* loaderA(IContext* ctx, Value* exports) {
    ctx->defineProperty(exports, "a", ctx->createNumber(1));
    g_bFromA = ctx->importModule("/b.js");
    return ctx->createUndefined();
}

Value* loaderB(IContext* ctx, Value* exports) {
    ctx->defineProperty(exports, "b", ctx->createNumber(2));
    g_aFromB = ctx->importModule("/a.js");
    return ctx->createUndefined();
}

// 相对 import：以当前加载模块的 dirname 为基准。
Value* loaderImportDotSlash(IContext* ctx, Value* exports) {
    g_importTarget = ctx->importModule("./b.js");
    ctx->defineProperty(exports, "a", ctx->createNumber(1));
    return ctx->createUndefined();
}

Value* loaderImportParent(IContext* ctx, Value* /*exports*/) {
    g_importTarget = ctx->importModule("../b.js");
    return ctx->createUndefined();
}

Value* loaderImportNested(IContext* ctx, Value* /*exports*/) {
    g_nestedTarget = ctx->importModule("./c.js");
    return ctx->createUndefined();
}

Value* loaderImportAbsolute(IContext* ctx, Value* /*exports*/) {
    g_importTarget = ctx->importModule("/other/b.js");
    return ctx->createUndefined();
}

Value* loaderImportNormalized(IContext* ctx, Value* /*exports*/) {
    g_importTarget = ctx->importModule("./sub/../b.js");
    return ctx->createUndefined();
}

Value* loaderEmpty(IContext* ctx, Value* exports) {
    ctx->defineProperty(exports, "x", ctx->createNumber(1));
    return ctx->createUndefined();
}

TEST(ModuleTest, CreatedLazily) {
    Fixture f;
    EXPECT_FALSE(f.ctx->hasModule("/a.js"));
    ModuleInfo info = f.ctx->getModule("/a.js");
    EXPECT_TRUE(f.ctx->hasModule("/a.js"));
    ASSERT_NE(info.scope, nullptr);
    ASSERT_NE(info.exports, nullptr);
}

TEST(ModuleTest, SameIdReturnsSameModule) {
    Fixture f;
    ModuleInfo a = f.ctx->getModule("/a.js");
    ModuleInfo b = f.ctx->getModule("/a.js");
    EXPECT_EQ(a.scope, b.scope);
    EXPECT_EQ(a.exports, b.exports);
}

TEST(ModuleTest, DistinctIdsAreDistinctModules) {
    Fixture f;
    ModuleInfo a = f.ctx->getModule("/a.js");
    ModuleInfo b = f.ctx->getModule("/b.js");
    EXPECT_NE(a.scope, b.scope);
    EXPECT_NE(a.exports, b.exports);
}

TEST(ModuleTest, ExportsPrototypeIsNull) {
    Fixture f;
    ModuleInfo info = f.ctx->getModule("/a.js");
    Value* proto = f.ctx->getPrototype(info.exports);
    ASSERT_NE(proto, nullptr);
    EXPECT_EQ(proto->type()->kind(), TypeKind::Null);
}

TEST(ModuleTest, ModuleScopeIsChildOfGlobal) {
    Fixture f;
    ModuleInfo info = f.ctx->getModule("/a.js");
    EXPECT_EQ(info.scope->parent(), f.ctx->global());
}

TEST(ModuleTest, ImportWithoutLoaderReturnsExports) {
    Fixture f;
    ModuleInfo info = f.ctx->getModule("/a.js");
    Value* imported = f.ctx->importModule("/a.js");
    EXPECT_EQ(imported, info.exports);
}

TEST(ModuleTest, ImportRunsLoaderOnceAndDefinesExports) {
    Fixture f;
    g_calls = 0;
    f.ctx->setModuleLoader("/a.js", loaderAnswer);

    Value* first = f.ctx->importModule("/a.js");
    Value* second = f.ctx->importModule("/a.js");

    EXPECT_EQ(first, second);
    EXPECT_EQ(g_calls, 1);
    EXPECT_TRUE(f.ctx->hasProperty(first, "answer"));
}

TEST(ModuleTest, LoaderRunsInModuleScope) {
    Fixture f;
    g_seenCurrent = nullptr;
    g_seenRoot = nullptr;
    ModuleInfo info = f.ctx->getModule("/a.js");
    f.ctx->setModuleLoader("/a.js", loaderScope);

    f.ctx->importModule("/a.js");

    EXPECT_EQ(g_seenCurrent, info.scope);
    EXPECT_EQ(g_seenRoot, info.scope);
    // loader 执行后恢复调用方作用域。
    EXPECT_EQ(f.ctx->current(), f.ctx->root());
}

TEST(ModuleTest, CircularImportReturnsPartialExports) {
    Fixture f;
    g_aFromB = nullptr;
    g_bFromA = nullptr;
    f.ctx->setModuleLoader("/a.js", loaderA);
    f.ctx->setModuleLoader("/b.js", loaderB);

    Value* a = f.ctx->importModule("/a.js");

    ASSERT_NE(g_bFromA, nullptr);
    ASSERT_NE(g_aFromB, nullptr);
    EXPECT_EQ(g_aFromB, a);  // B 看到的 A 是同一个（部分）导出对象
    EXPECT_TRUE(f.ctx->hasProperty(a, "a"));
    EXPECT_TRUE(f.ctx->hasProperty(g_bFromA, "b"));
}

TEST(ModuleTest, LoaderErrorPropagatesAndAllowsRetry) {
    Fixture f;
    g_calls = 0;
    f.ctx->setModuleLoader("/a.js", loaderFailOnce);

    Value* failed = f.ctx->importModule("/a.js");
    ASSERT_NE(failed, nullptr);
    EXPECT_TRUE(isErrorValue(failed));

    Value* retried = f.ctx->importModule("/a.js");
    EXPECT_EQ(g_calls, 2);
    EXPECT_FALSE(isErrorValue(retried));
    EXPECT_TRUE(f.ctx->hasProperty(retried, "ok"));
}

TEST(ModuleTest, RelativeImportResolvesAgainstCurrentModule) {
    Fixture f;
    g_importTarget = nullptr;
    f.ctx->setModuleLoader("/mods/a.js", loaderImportDotSlash);
    f.ctx->setModuleLoader("/mods/b.js", loaderEmpty);

    Value* a = f.ctx->importModule("/mods/a.js");

    EXPECT_TRUE(f.ctx->hasModule("/mods/b.js"));
    EXPECT_EQ(g_importTarget, f.ctx->getModule("/mods/b.js").exports);
    EXPECT_TRUE(f.ctx->hasProperty(a, "a"));
}

TEST(ModuleTest, ParentRelativeImportResolvesAgainstDirname) {
    Fixture f;
    g_importTarget = nullptr;
    f.ctx->setModuleLoader("/mods/sub/a.js", loaderImportParent);
    f.ctx->setModuleLoader("/mods/b.js", loaderEmpty);

    f.ctx->importModule("/mods/sub/a.js");

    EXPECT_TRUE(f.ctx->hasModule("/mods/b.js"));
    EXPECT_EQ(g_importTarget, f.ctx->getModule("/mods/b.js").exports);
}

TEST(ModuleTest, NestedRelativeImportsUseEachModuleDirname) {
    Fixture f;
    g_importTarget = nullptr;
    g_nestedTarget = nullptr;
    f.ctx->setModuleLoader("/mods/a.js", loaderImportDotSlash);  // ./b.js
    f.ctx->setModuleLoader("/mods/b.js", loaderImportNested);    // ./c.js
    f.ctx->setModuleLoader("/mods/c.js", loaderEmpty);

    f.ctx->importModule("/mods/a.js");

    EXPECT_TRUE(f.ctx->hasModule("/mods/c.js"));
    EXPECT_EQ(g_nestedTarget, f.ctx->getModule("/mods/c.js").exports);
}

TEST(ModuleTest, WindowsDriveRelativeImport) {
    Fixture f;
    g_importTarget = nullptr;
    f.ctx->setModuleLoader("C:/mods/a.js", loaderImportDotSlash);
    f.ctx->setModuleLoader("C:/mods/b.js", loaderEmpty);

    f.ctx->importModule("C:/mods/a.js");

    EXPECT_TRUE(f.ctx->hasModule("C:/mods/b.js"));
    EXPECT_EQ(g_importTarget, f.ctx->getModule("C:/mods/b.js").exports);
}

TEST(ModuleTest, AbsoluteImportIsNotRebased) {
    Fixture f;
    g_importTarget = nullptr;
    f.ctx->setModuleLoader("/mods/a.js", loaderImportAbsolute);
    f.ctx->setModuleLoader("/other/b.js", loaderEmpty);

    f.ctx->importModule("/mods/a.js");

    EXPECT_TRUE(f.ctx->hasModule("/other/b.js"));
    EXPECT_FALSE(f.ctx->hasModule("/mods/other/b.js"));
    EXPECT_EQ(g_importTarget, f.ctx->getModule("/other/b.js").exports);
}

TEST(ModuleTest, RelativeImportNormalizesDotSegments) {
    Fixture f;
    g_importTarget = nullptr;
    f.ctx->setModuleLoader("/mods/a.js", loaderImportNormalized);
    f.ctx->setModuleLoader("/mods/b.js", loaderEmpty);

    f.ctx->importModule("/mods/a.js");

    EXPECT_TRUE(f.ctx->hasModule("/mods/b.js"));
    EXPECT_EQ(g_importTarget, f.ctx->getModule("/mods/b.js").exports);
}

TEST(ModuleTest, SetLoaderCreatesModule) {
    Fixture f;
    EXPECT_FALSE(f.ctx->hasModule("/a.js"));
    f.ctx->setModuleLoader("/a.js", loaderAnswer);
    EXPECT_TRUE(f.ctx->hasModule("/a.js"));
}

}  // namespace
