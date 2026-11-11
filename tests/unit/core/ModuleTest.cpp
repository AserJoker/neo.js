// ModuleTest.cpp - ES Module 基础设施单元测试
//
// 验证：
// - 模块以绝对路径为唯一 id，惰性创建（getModule）
// - 每个模块借用一棵挂在 global 下的 root scope + 原型为 null 的导出对象
// - import = 惰性加载 + 取 Module + 取导出对象
// - loader 只执行一次，且在模块 root scope 下执行
// - 循环依赖返回当前（部分）导出对象
// - loader 失败原样透传错误值，且模块保持未加载（可重试）
// - 导出对象是模块命名空间 exotic 对象：原型 null、@@toStringTag="Module"
// - 导出属性是活绑定：store 原地更新后导入方读到最新值，且不 clone

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Module.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/WellKnownSymbol.h"
#include "core/types/Array.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>
#include "TestHelpers.h"

#include <memory>

namespace {

using neo::core::ArrayData;
using neo::core::FunctionData;
using neo::core::IContext;
using neo::core::ModuleInfo;
using neo::core::NumberData;
using neo::core::Scope;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::WellKnownSymbol;
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

// —— 模块命名空间 / 活绑定 ——
Value* g_exportedSlot = nullptr;
Value* g_exportResult = nullptr;
Value* g_importedValue = nullptr;
Value* g_importResult = nullptr;
Value* g_storeResult = nullptr;

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
std::string strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }

// 先导出再 store：属性持有槽位 Value*，store 原地替换 meta+data。
Value* loaderLiveBinding(IContext* ctx, Value* exports) {
    ctx->define("x", ctx->createNumber(1));
    g_exportedSlot = ctx->current()->lookup("x");
    g_exportResult = ctx->exportBinding(exports, "x", "x");
    ctx->store("x", ctx->createNumber(2));
    return ctx->createUndefined();
}

Value* loaderExportMissing(IContext* ctx, Value* exports) {
    g_exportResult = ctx->exportBinding(exports, "y", "missing");
    return ctx->createUndefined();
}

Value* loaderExportToObject(IContext* ctx, Value* /*exports*/) {
    Value* obj = ctx->createObject();
    g_exportResult = ctx->exportBinding(obj, "y", "y");
    return ctx->createUndefined();
}

// —— 导入绑定（间接绑定 / 只读）——
Value* loaderExporterStatic(IContext* ctx, Value* exports) {
    ctx->define("x", ctx->createNumber(1));
    ctx->exportBinding(exports, "x", "x");
    return ctx->createUndefined();
}

Value* loaderExporterLive(IContext* ctx, Value* exports) {
    ctx->define("x", ctx->createNumber(1));
    ctx->exportBinding(exports, "x", "x");
    // 让 importer 先建立绑定，再更新导出槽位。
    ctx->importModule("/impLive.js");
    ctx->store("x", ctx->createNumber(2));
    return ctx->createUndefined();
}

Value* loaderImporterLive(IContext* ctx, Value* /*exports*/) {
    Value* ns = ctx->importModule("/expLive.js");
    g_importedValue = ctx->bindImport("x", ns, "x");
    return ctx->createUndefined();
}

Value* loaderImporterReadOnly(IContext* ctx, Value* /*exports*/) {
    Value* ns = ctx->importModule("/expStatic.js");
    g_importResult = ctx->bindImport("x", ns, "x");
    g_storeResult = ctx->store("x", ctx->createNumber(99));
    return ctx->createUndefined();
}

Value* loaderExporterLate(IContext* ctx, Value* exports) {
    // importer 先绑定，此时 x 尚未导出。
    ctx->importModule("/impLate.js");
    ctx->define("x", ctx->createNumber(7));
    ctx->exportBinding(exports, "x", "x");
    return ctx->createUndefined();
}

Value* loaderImporterLate(IContext* ctx, Value* /*exports*/) {
    Value* ns = ctx->importModule("/expLate.js");
    g_importResult = ctx->bindImport("x", ns, "x");
    return ctx->createUndefined();
}

Value* loaderDuplicateImport(IContext* ctx, Value* /*exports*/) {
    ctx->define("x", ctx->createNumber(1));
    Value* ns = ctx->importModule("/expStatic.js");
    g_importResult = ctx->bindImport("x", ns, "x");
    return ctx->createUndefined();
}

Value* loaderBindToObject(IContext* ctx, Value* /*exports*/) {
    Value* obj = ctx->createObject();
    g_importResult = ctx->bindImport("x", obj, "x");
    return ctx->createUndefined();
}

// —— 闭包捕获导入绑定 / re-export ——
Value* g_closure = nullptr;

Value* dummyNative(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createUndefined();
}

Value* loaderExporterClosureLate(IContext* ctx, Value* exports) {
    // importer 先捕获导入绑定，此时 x 尚未导出。
    ctx->importModule("/impClosureLate.js");
    ctx->define("x", ctx->createNumber(7));
    ctx->exportBinding(exports, "x", "x");
    return ctx->createUndefined();
}

Value* loaderImporterClosureLate(IContext* ctx, Value* /*exports*/) {
    Value* ns = ctx->importModule("/expClosureLate.js");
    ctx->bindImport("x", ns, "x");
    g_closure = ctx->createNativeFunction(dummyNative, 0);
    ctx->setClosure(g_closure, "x");
    return ctx->createUndefined();
}

Value* loaderReexporter(IContext* ctx, Value* exports) {
    Value* ns = ctx->importModule("/srcLate.js");
    ctx->bindImport("x", ns, "x");
    // 源导出尚未出现：re-export 仍应保持间接，晚到后可解析。
    g_exportResult = ctx->exportBinding(exports, "x", "x");
    return ctx->createUndefined();
}

Value* loaderSrcLate(IContext* ctx, Value* exports) {
    ctx->importModule("/reexporter.js");
    ctx->define("x", ctx->createNumber(7));
    ctx->exportBinding(exports, "x", "x");
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
    EXPECT_TRUE(neo::test::hasProperty(f.ctx, first, "answer"));
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
    EXPECT_TRUE(neo::test::hasProperty(f.ctx, a, "a"));
    EXPECT_TRUE(neo::test::hasProperty(f.ctx, g_bFromA, "b"));
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
    EXPECT_TRUE(neo::test::hasProperty(f.ctx, retried, "ok"));
}

TEST(ModuleTest, RelativeImportResolvesAgainstCurrentModule) {
    Fixture f;
    g_importTarget = nullptr;
    f.ctx->setModuleLoader("/mods/a.js", loaderImportDotSlash);
    f.ctx->setModuleLoader("/mods/b.js", loaderEmpty);

    Value* a = f.ctx->importModule("/mods/a.js");

    EXPECT_TRUE(f.ctx->hasModule("/mods/b.js"));
    EXPECT_EQ(g_importTarget, f.ctx->getModule("/mods/b.js").exports);
    EXPECT_TRUE(neo::test::hasProperty(f.ctx, a, "a"));
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

TEST(ModuleTest, ExportsAreModuleNamespaceObjects) {
    Fixture f;
    ModuleInfo info = f.ctx->getModule("/a.js");
    EXPECT_EQ(info.exports->type()->kind(), TypeKind::ModuleNamespace);
    EXPECT_TRUE(info.exports->type()->hasObjectLayout());
}

TEST(ModuleTest, NamespaceToStringTagIsModule) {
    Fixture f;
    ModuleInfo info = f.ctx->getModule("/a.js");
    Value* tag = f.ctx->getProperty(
        info.exports, f.ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag));
    ASSERT_NE(tag, nullptr);
    EXPECT_EQ(tag->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(tag), "Module");
}

TEST(ModuleTest, ExportBindingIsLiveAndNotCloned) {
    Fixture f;
    g_exportedSlot = nullptr;
    g_exportResult = nullptr;
    f.ctx->setModuleLoader("/live.js", loaderLiveBinding);

    Value* ns = f.ctx->importModule("/live.js");
    ASSERT_FALSE(isErrorValue(ns));
    ASSERT_FALSE(isErrorValue(g_exportResult));
    ASSERT_NE(g_exportedSlot, nullptr);

    Value* key = f.ctx->createString("x");
    Value* seen = f.ctx->getProperty(ns, key);
    // 属性直接持有模块作用域槽位，而非 clone 出的副本。
    EXPECT_EQ(seen, g_exportedSlot);
    EXPECT_EQ(numOf(seen), 2.0);  // 导出后 store 的最新值
}

TEST(ModuleTest, LiveBindingSurvivesGc) {
    Fixture f;
    g_exportedSlot = nullptr;
    g_exportResult = nullptr;
    f.ctx->setModuleLoader("/live.js", loaderLiveBinding);

    Value* ns = f.ctx->importModule("/live.js");
    ASSERT_FALSE(isErrorValue(ns));

    f.ctx->gc()->collect();

    Value* seen = f.ctx->getProperty(ns, f.ctx->createString("x"));
    EXPECT_EQ(seen, g_exportedSlot);
    EXPECT_EQ(numOf(seen), 2.0);
}

TEST(ModuleTest, ExportBindingRejectsMissingLocal) {
    Fixture f;
    g_exportResult = nullptr;
    f.ctx->setModuleLoader("/missing.js", loaderExportMissing);

    f.ctx->importModule("/missing.js");

    ASSERT_NE(g_exportResult, nullptr);
    EXPECT_TRUE(isErrorValue(g_exportResult));
}

TEST(ModuleTest, ExportBindingRejectsNonNamespaceTarget) {
    Fixture f;
    g_exportResult = nullptr;
    f.ctx->setModuleLoader("/obj.js", loaderExportToObject);

    f.ctx->importModule("/obj.js");

    ASSERT_NE(g_exportResult, nullptr);
    EXPECT_TRUE(isErrorValue(g_exportResult));
}

TEST(ModuleTest, NamespaceRejectsMutation) {
    Fixture f;
    ModuleInfo info = f.ctx->getModule("/a.js");
    Value* key = f.ctx->createString("x");

    EXPECT_TRUE(isErrorValue(f.ctx->setProperty(info.exports, key, f.ctx->createNumber(1))));
    EXPECT_TRUE(isErrorValue(f.ctx->deleteProperty(info.exports, key)));
    EXPECT_TRUE(isErrorValue(
        f.ctx->defineAccessor(info.exports, key, nullptr, nullptr, true, true)));
    EXPECT_TRUE(isErrorValue(f.ctx->setPrototype(info.exports, f.ctx->createObject())));
}

TEST(ModuleTest, ImportBindingResolvesLiveValue) {
    Fixture f;
    g_importedValue = nullptr;
    f.ctx->setModuleLoader("/expLive.js", loaderExporterLive);
    f.ctx->setModuleLoader("/impLive.js", loaderImporterLive);

    Value* ns = f.ctx->importModule("/expLive.js");
    ASSERT_FALSE(isErrorValue(ns));
    ASSERT_NE(g_importedValue, nullptr);
    // importer 绑定后 exporter 又 store 了 2：间接绑定读取时解析，能看到最新值。
    EXPECT_EQ(numOf(g_importedValue), 2.0);
}

TEST(ModuleTest, ImportBindingIsReadOnly) {
    Fixture f;
    g_importResult = nullptr;
    g_storeResult = nullptr;
    f.ctx->setModuleLoader("/expStatic.js", loaderExporterStatic);
    f.ctx->setModuleLoader("/impReadOnly.js", loaderImporterReadOnly);

    f.ctx->importModule("/impReadOnly.js");

    ASSERT_NE(g_importResult, nullptr);
    EXPECT_FALSE(isErrorValue(g_importResult));
    ASSERT_NE(g_storeResult, nullptr);
    EXPECT_TRUE(isErrorValue(g_storeResult));
}

TEST(ModuleTest, ImportBindingSeesLateExport) {
    Fixture f;
    g_importResult = nullptr;
    f.ctx->setModuleLoader("/expLate.js", loaderExporterLate);
    f.ctx->setModuleLoader("/impLate.js", loaderImporterLate);

    f.ctx->importModule("/expLate.js");

    // 绑定时导出尚不存在，返回 undefined；导出完成后间接绑定解析到 7。
    ASSERT_NE(g_importResult, nullptr);
    EXPECT_EQ(g_importResult->type()->kind(), TypeKind::Undefined);
    Value* resolved = f.ctx->getModule("/impLate.js").scope->lookup("x");
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(numOf(resolved), 7.0);
}

TEST(ModuleTest, ImportBindingSurvivesGc) {
    Fixture f;
    g_importedValue = nullptr;
    f.ctx->setModuleLoader("/expLive.js", loaderExporterLive);
    f.ctx->setModuleLoader("/impLive.js", loaderImporterLive);

    f.ctx->importModule("/expLive.js");
    ASSERT_NE(g_importedValue, nullptr);

    f.ctx->gc()->collect();

    EXPECT_EQ(numOf(g_importedValue), 2.0);
    EXPECT_EQ(f.ctx->getModule("/impLive.js").scope->lookup("x"), g_importedValue);
}

TEST(ModuleTest, ImportBindingRejectsDuplicateDeclaration) {
    Fixture f;
    g_importResult = nullptr;
    f.ctx->setModuleLoader("/expStatic.js", loaderExporterStatic);
    f.ctx->setModuleLoader("/impDup.js", loaderDuplicateImport);

    f.ctx->importModule("/impDup.js");

    ASSERT_NE(g_importResult, nullptr);
    EXPECT_TRUE(isErrorValue(g_importResult));
}

TEST(ModuleTest, ImportBindingRejectsNonNamespaceTarget) {
    Fixture f;
    g_importResult = nullptr;
    f.ctx->setModuleLoader("/impObj.js", loaderBindToObject);

    f.ctx->importModule("/impObj.js");

    ASSERT_NE(g_importResult, nullptr);
    EXPECT_TRUE(isErrorValue(g_importResult));
}

TEST(ModuleTest, ClosureCapturesIndirectBindingForLateExport) {
    Fixture f;
    g_closure = nullptr;
    f.ctx->setModuleLoader("/expClosureLate.js", loaderExporterClosureLate);
    f.ctx->setModuleLoader("/impClosureLate.js", loaderImporterClosureLate);

    f.ctx->importModule("/expClosureLate.js");
    f.ctx->gc()->collect();

    ASSERT_NE(g_closure, nullptr);
    auto* d = static_cast<FunctionData*>(g_closure->data());
    // 捕获的是间接绑定，而非捕获时的 undefined。
    EXPECT_EQ(d->closureScope->bindingKind("x"), Scope::BindingKind::Indirect);
    Value* resolved = d->closureScope->lookup("x");
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(numOf(resolved), 7.0);
}

TEST(ModuleTest, ReexportRemainsIndirectForLateExport) {
    Fixture f;
    g_exportResult = nullptr;
    f.ctx->setModuleLoader("/srcLate.js", loaderSrcLate);
    f.ctx->setModuleLoader("/reexporter.js", loaderReexporter);

    f.ctx->importModule("/srcLate.js");
    f.ctx->gc()->collect();

    ModuleInfo re = f.ctx->getModule("/reexporter.js");
    Value* key = f.ctx->createString("x");
    EXPECT_TRUE(neo::test::hasProperty(f.ctx, re.exports, key));
    Value* resolved = f.ctx->getProperty(re.exports, key);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(numOf(resolved), 7.0);
}

TEST(ModuleTest, ReexportNameIsEnumerable) {
    Fixture f;
    g_exportResult = nullptr;
    f.ctx->setModuleLoader("/srcLate.js", loaderSrcLate);
    f.ctx->setModuleLoader("/reexporter.js", loaderReexporter);

    f.ctx->importModule("/srcLate.js");

    ModuleInfo re = f.ctx->getModule("/reexporter.js");
    Value* keys = f.ctx->ownEnumerableStringKeys(re.exports);
    ASSERT_NE(keys, nullptr);
    auto* d = static_cast<ArrayData*>(keys->data());
    ASSERT_EQ(d->length(), 1u);
    Value* k = d->elementAt(0);
    ASSERT_NE(k, nullptr);
    EXPECT_EQ(strOf(k), "x");
}

}  // namespace
