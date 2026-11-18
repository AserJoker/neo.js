// ModuleCompilerTest.cpp - 模块编译与链接端到端测试（ADR-059/060/061）
//
// 验证：
// - 模块三段式字节码：instantiatePc（建槽+函数定义）/ evaluatePc（逻辑）
// - import/export 槽位绑定、活绑定、re-export、export *、export * as ns
// - 循环依赖的实例化/求值分离
// - Module 持有 Code，Context::importModule 驱动 instantiate → link → evaluate

#include "compiler/Compiler.h"
#include "core/IContext.h"
#include "core/Module.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "frontend/Parser.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"
#include "sema/Sema.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using neo::compiler::Compiler;
using neo::compiler::CompilerOptions;
using neo::core::IContext;
using neo::core::ModuleCompilation;
using neo::core::NumberData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::detail::isErrorValue;
using neo::frontend::Parser;
using neo::frontend::ParserOptions;
using neo::frontend::Program;
using neo::frontend::SourceType;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;
using neo::sema::SemaResult;
using neo::sema::runSema;

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

bool addModule(Fixture& f, const std::string& id, const std::string& source) {
    Parser parser(source, ParserOptions{SourceType::Module});
    auto parsed = parser.parseProgram();
    EXPECT_TRUE(parsed.ok());
    SemaResult sema = runSema(*parsed.program);
    EXPECT_TRUE(sema.ok());

    Compiler compiler(static_cast<const Program&>(*parsed.program), sema,
                      CompilerOptions{id});
    auto result = compiler.compile();
    EXPECT_TRUE(result.ok());

    ModuleCompilation compilation;
    compilation.code = std::move(result.code);
    compilation.declarePc = result.declarePc;
    compilation.instantiatePc = result.instantiatePc;
    compilation.evaluatePc = result.evaluatePc;
    compilation.imports = std::move(result.imports);
    compilation.exports = std::move(result.exports);
    return f.ctx->setModuleCompilation(id, std::move(compilation));
}

Value* get(IContext* ctx, Value* obj, const char* name) {
    return ctx->getProperty(obj, ctx->createString(name));
}

double number(Value* value) {
    EXPECT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::Number);
    return static_cast<NumberData*>(value->data())->value;
}

std::string errorText(IContext* ctx, Value* error) {
    if (error == nullptr || error->type()->kind() != TypeKind::Exception) {
        return "<not exception>";
    }
    auto* data = static_cast<neo::core::ExceptionData*>(error->data());
    Value* message = ctx->getProperty(data->child, ctx->createString("message"));
    if (message != nullptr && message->type()->kind() == TypeKind::String) {
        return static_cast<neo::core::StringData*>(message->data())->value;
    }
    return "<no message>";
}

bool has(IContext* ctx, Value* obj, const char* name) {
    Value* result = ctx->hasProperty(obj, ctx->createString(name));
    return result != nullptr && result->type()->kind() == TypeKind::Boolean &&
           static_cast<neo::core::BooleanData*>(result->data())->value;
}

}  // namespace

TEST(ModuleCompilerTest, NamedExportAndImport) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js", "export const answer = 42;"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import { answer } from './a.js';\n"
                          "export const doubled = answer * 2;"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "doubled")), 84.0);
}

TEST(ModuleCompilerTest, ImportMetaUrl) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "export const url = import.meta.url;\n"
                          "export const same = import.meta === import.meta;"));

    Value* ns = f.ctx->importModule("/a.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    Value* url = get(f.ctx.get(), ns, "url");
    ASSERT_NE(url, nullptr);
    ASSERT_EQ(url->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<neo::core::StringData*>(url->data())->value, "/a.js");
    Value* same = get(f.ctx.get(), ns, "same");
    ASSERT_NE(same, nullptr);
    ASSERT_EQ(same->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(static_cast<neo::core::BooleanData*>(same->data())->value);
}

TEST(ModuleCompilerTest, ImportMetaIsPerModule) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js", "export const meta = import.meta;"));
    ASSERT_TRUE(addModule(f, "/b.js", "export const meta = import.meta;"));
    ASSERT_TRUE(addModule(f, "/c.js",
                          "import { meta as a } from './a.js';\n"
                          "import { meta as b } from './b.js';\n"
                          "export const distinct = a !== b;\n"
                          "export const url = b.url;"));

    Value* ns = f.ctx->importModule("/c.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    Value* distinct = get(f.ctx.get(), ns, "distinct");
    ASSERT_NE(distinct, nullptr);
    ASSERT_EQ(distinct->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(static_cast<neo::core::BooleanData*>(distinct->data())->value);
    Value* url = get(f.ctx.get(), ns, "url");
    ASSERT_NE(url, nullptr);
    ASSERT_EQ(url->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<neo::core::StringData*>(url->data())->value, "/b.js");
}

void drainMicrotasks(IContext* ctx) {
    while (ctx->hasRunnableTask()) (void)ctx->runOneTask();
}

TEST(ModuleCompilerTest, DynamicImportResolvesNamespace) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js", "export const answer = 42;"));
    ASSERT_TRUE(addModule(
        f, "/b.js",
        "export let result = 0;"
        "import('./a.js').then(function (m) { result = m.answer; });"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    drainMicrotasks(f.ctx.get());
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "result")), 42.0);
}

TEST(ModuleCompilerTest, DynamicImportRelativeToReferrer) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/dir/b.js", "export const value = 7;"));
    ASSERT_TRUE(addModule(
        f, "/dir/a.js",
        "export let result = 0;"
        "import('./b.js').then(function (m) { result = m.value; });"));

    Value* ns = f.ctx->importModule("/dir/a.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    drainMicrotasks(f.ctx.get());
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "result")), 7.0);
}

TEST(ModuleCompilerTest, DynamicImportRejectsWhenSpecifierThrows) {
    Fixture f;
    ASSERT_TRUE(addModule(
        f, "/a.js",
        "export let result = '';"
        "import({ toString: function () { throw new Error('boom'); } })"
        ".catch(function (e) { result = e.message; });"));

    Value* ns = f.ctx->importModule("/a.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    drainMicrotasks(f.ctx.get());
    Value* result = get(f.ctx.get(), ns, "result");
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<neo::core::StringData*>(result->data())->value,
              "boom");
}

TEST(ModuleCompilerTest, DefaultExportAndImport) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js", "export default 7;"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import value from './a.js';\n"
                          "export const result = value + 1;"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "result")), 8.0);
}

TEST(ModuleCompilerTest, NamedExportClass) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "export class A { value() { return 7; } }"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import { A } from './a.js';\n"
                          "export const result = new A().value();"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "result")), 7.0);
}

TEST(ModuleCompilerTest, DefaultNamedClassIsExported) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "export default class A { value() { return 9; } }"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import A from './a.js';\n"
                          "export const result = new A().value();"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "result")), 9.0);
}

TEST(ModuleCompilerTest, DefaultAnonymousClassIsExported) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "export default class { value() { return 3; } }"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import A from './a.js';\n"
                          "export const result = new A().value();"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "result")), 3.0);
}

TEST(ModuleCompilerTest, DefaultNamedClassLocalBinding) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "export default class A {}\n"
                          "export const same = A;"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import A, { same } from './a.js';\n"
                          "export const result = A === same;"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    Value* result = get(f.ctx.get(), ns, "result");
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(static_cast<neo::core::BooleanData*>(result->data())->value);
}

TEST(ModuleCompilerTest, DefaultNamedFunctionIsExported) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "export default function answer() { return 40 + 2; }"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import answer from './a.js';\n"
                          "export const result = answer();"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "result")), 42.0);
}

TEST(ModuleCompilerTest, ImportNamespace) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js", "export const x = 5;"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import * as ns from './a.js';\n"
                          "export const y = ns.x + 1;"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "y")), 6.0);
}

TEST(ModuleCompilerTest, ImportedBindingIsLive) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "export let count = 1;\n"
                          "export function inc() { count = count + 1; }"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import { count, inc } from './a.js';\n"
                          "export { count };\n"
                          "export function bump() { inc(); }"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "count")), 1.0);

    Value* bump = get(f.ctx.get(), ns, "bump");
    ASSERT_FALSE(isErrorValue(bump));
    Value* callResult =
        f.ctx->call(bump, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_FALSE(isErrorValue(callResult));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "count")), 2.0);
}

TEST(ModuleCompilerTest, ReExportNamed) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js", "export const x = 3;"));
    ASSERT_TRUE(addModule(f, "/b.js", "export { x } from './a.js';"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "x")), 3.0);
}

TEST(ModuleCompilerTest, ExportAllExcludesDefault) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "export const x = 1;\n"
                          "export default 9;"));
    ASSERT_TRUE(addModule(f, "/b.js", "export * from './a.js';"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "x")), 1.0);
    EXPECT_FALSE(has(f.ctx.get(), ns, "default"));
}

TEST(ModuleCompilerTest, ExportNamespaceAsNamed) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js", "export const x = 5;"));
    ASSERT_TRUE(addModule(f, "/b.js", "export * as ns from './a.js';"));

    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns));
    Value* inner = get(f.ctx.get(), ns, "ns");
    ASSERT_FALSE(isErrorValue(inner));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), inner, "x")), 5.0);
}

TEST(ModuleCompilerTest, CircularImportsAreLinkedBeforeEvaluation) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "import { b } from './b.js';\n"
                          "export const a = 1;\n"
                          "export function getB() { return b; }"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import { a } from './a.js';\n"
                          "export const b = 2;\n"
                          "export function getA() { return a; }"));

    Value* a = f.ctx->importModule("/a.js");
    ASSERT_FALSE(isErrorValue(a)) << errorText(f.ctx.get(), a);
    Value* getB = get(f.ctx.get(), a, "getB");
    ASSERT_FALSE(isErrorValue(getB));
    Value* bValue = f.ctx->call(getB, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_FALSE(isErrorValue(bValue));
    EXPECT_DOUBLE_EQ(number(bValue), 2.0);

    Value* b = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(b));
    Value* getA = get(f.ctx.get(), b, "getA");
    ASSERT_FALSE(isErrorValue(getA));
    Value* aValue = f.ctx->call(getA, f.ctx->createUndefined(), 0, nullptr);
    ASSERT_FALSE(isErrorValue(aValue));
    EXPECT_DOUBLE_EQ(number(aValue), 1.0);
}

TEST(ModuleCompilerTest, ModuleHasThreeSectionLayout) {
    Fixture f;
    Parser parser("export const x = 1;", ParserOptions{SourceType::Module});
    auto parsed = parser.parseProgram();
    ASSERT_TRUE(parsed.ok());
    SemaResult sema = runSema(*parsed.program);
    ASSERT_TRUE(sema.ok());

    Compiler compiler(static_cast<const Program&>(*parsed.program), sema);
    auto result = compiler.compile();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.declarePc, 0u);
    EXPECT_LT(result.declarePc, result.instantiatePc);
    EXPECT_LT(result.instantiatePc, result.evaluatePc);
    EXPECT_LT(result.evaluatePc, result.code->bytecode.size());
}

// ===== 模块顶层 await（ADR-064）=====

namespace {

Value* g_dynamicNamespace = nullptr;

Value* captureNamespace(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    g_dynamicNamespace = argc > 0 ? argv[0] : nullptr;
    return ctx->createUndefined();
}

}  // namespace

TEST(ModuleCompilerTest, TopLevelAwaitDefersModuleBody) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/tla.js",
                          "export let value = 0;\n"
                          "await Promise.resolve();\n"
                          "value = 7;\n"));
    Value* ns = f.ctx->importModule("/tla.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    // 模块体在 await 处挂起，赋值尚未执行。
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "value")), 0.0);
    drainMicrotasks(f.ctx.get());
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "value")), 7.0);
}

TEST(ModuleCompilerTest, TopLevelAwaitPropagatesToImporters) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/dep.js",
                          "export const a = await Promise.resolve(2);\n"));
    ASSERT_TRUE(addModule(f, "/main.js",
                          "import { a } from './dep.js';\n"
                          "export const b = a + 1;\n"));
    Value* ns = f.ctx->importModule("/main.js");
    ASSERT_FALSE(isErrorValue(ns));
    drainMicrotasks(f.ctx.get());
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "b")), 3.0);
}

TEST(ModuleCompilerTest, TopLevelAwaitRejectionFailsModule) {
    Fixture f;
    ASSERT_TRUE(addModule(f, "/bad.js",
                          "await Promise.reject(new Error('boom'));\n"
                          "export const x = 1;\n"));
    Value* ns = f.ctx->importModule("/bad.js");
    ASSERT_FALSE(isErrorValue(ns));
    drainMicrotasks(f.ctx.get());
    // 失败被缓存：再次 import 返回错误值。
    Value* err = f.ctx->importModule("/bad.js");
    ASSERT_TRUE(isErrorValue(err));
    EXPECT_EQ(errorText(f.ctx.get(), err), "boom");
}

TEST(ModuleCompilerTest, DynamicImportWaitsForTopLevelAwait) {
    Fixture f;
    g_dynamicNamespace = nullptr;
    ASSERT_TRUE(addModule(f, "/dyn.js",
                          "export const v = await Promise.resolve(9);\n"));
    Value* promise = f.ctx->importModuleDynamic(f.ctx->createString("/dyn.js"), "");
    ASSERT_EQ(promise->type()->kind(), TypeKind::Promise);
    Value* thenFn = get(f.ctx.get(), promise, "then");
    Value* callback = f.ctx->createNativeFunction(captureNamespace, 1);
    Value* args[1] = {callback};
    ASSERT_FALSE(isErrorValue(f.ctx->call(thenFn, promise, 1, args)));
    drainMicrotasks(f.ctx.get());
    ASSERT_NE(g_dynamicNamespace, nullptr);
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), g_dynamicNamespace, "v")), 9.0);
}

// —— 循环依赖 + 顶层 await（SCC 求值状态机，ADR-064）——

TEST(ModuleCompilerTest, CyclicTlaWithSyncMemberEvaluates) {
    // A 有 TLA，B 同步：B 先同步求值，A 异步完成。
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "import { b } from './b.js';\n"
                          "export let a = 0;\n"
                          "await Promise.resolve();\n"
                          "a = b + 1;\n"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import { a } from './a.js';\n"
                          "export const b = 1;\n"));
    Value* ns = f.ctx->importModule("/a.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    drainMicrotasks(f.ctx.get());
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "a")), 2.0);
}

TEST(ModuleCompilerTest, CyclicTlaBothMembersAwait) {
    // A、B 都有 TLA：B 先完成，A 等 B 后完成。
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "import { b } from './b.js';\n"
                          "export let a = 0;\n"
                          "await Promise.resolve();\n"
                          "a = b + 1;\n"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import { a } from './a.js';\n"
                          "export const b = await Promise.resolve(10);\n"));
    Value* ns = f.ctx->importModule("/a.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    drainMicrotasks(f.ctx.get());
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "a")), 11.0);
}

TEST(ModuleCompilerTest, ImporterOfNonRootMemberWaitsForCycleRoot) {
    // D 同时 import 环根 A 与非根成员 B：B 的依赖解析为 CycleRoot(A)，
    // D 必须等 A 完成才能求值。
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "import { b } from './b.js';\n"
                          "export let a = 0;\n"
                          "await Promise.resolve();\n"
                          "a = 1;\n"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import { a } from './a.js';\n"
                          "export const b = 2;\n"));
    ASSERT_TRUE(addModule(f, "/d.js",
                          "import { a } from './a.js';\n"
                          "import { b } from './b.js';\n"
                          "export const d = a + b;\n"));
    Value* ns = f.ctx->importModule("/d.js");
    ASSERT_FALSE(isErrorValue(ns)) << errorText(f.ctx.get(), ns);
    drainMicrotasks(f.ctx.get());
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), ns, "d")), 3.0);
}

TEST(ModuleCompilerTest, CyclicTlaRejectionPropagatesToParent) {
    // 环根 A 的顶层 await 拒绝沿 AsyncParentModules 传播到 B。
    Fixture f;
    ASSERT_TRUE(addModule(f, "/a.js",
                          "import { b } from './b.js';\n"
                          "export let a = 0;\n"
                          "await Promise.reject(new Error('cycle boom'));\n"));
    ASSERT_TRUE(addModule(f, "/b.js",
                          "import { a } from './a.js';\n"
                          "export const b = 1;\n"));
    Value* ns = f.ctx->importModule("/b.js");
    ASSERT_FALSE(isErrorValue(ns));
    drainMicrotasks(f.ctx.get());
    Value* err = f.ctx->importModule("/b.js");
    ASSERT_TRUE(isErrorValue(err));
    EXPECT_EQ(errorText(f.ctx.get(), err), "cycle boom");
}
