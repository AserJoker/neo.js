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
