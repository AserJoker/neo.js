// tests/unit/sema/ModulePassTest.cpp - 模块绑定与导出早期错误测试

#include "frontend/Parser.h"
#include "sema/AnnotationStore.h"
#include "sema/ModulePass.h"
#include "sema/ResolvePass.h"
#include "sema/ScopePass.h"
#include "sema/SemaContext.h"

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

namespace {

using neo::frontend::ParseResult;
using neo::frontend::Parser;
using neo::frontend::ParserOptions;
using neo::frontend::SourceType;
using neo::sema::AnnotationStore;
using neo::sema::ScopeGraph;
using neo::sema::SemaContext;
using neo::sema::SemaError;
using neo::sema::runModulePass;
using neo::sema::runResolvePass;
using neo::sema::runScopePass;

struct Analysis {
    ParseResult parsed;
    std::vector<SemaError> errors;
};

Analysis analyze(std::string_view source) {
    Parser parser(source, ParserOptions{SourceType::Module});
    Analysis result;
    result.parsed = parser.parseProgram();
    if (!result.parsed.program) return result;

    SemaContext context;
    AnnotationStore annotations;
    ScopeGraph graph = runScopePass(*result.parsed.program, context);
    runResolvePass(*result.parsed.program, graph, context, annotations);
    runModulePass(*result.parsed.program, context, annotations);
    result.errors = context.errors();
    return result;
}

Analysis analyzeAsScript(std::string_view source) {
    Parser parser(source, ParserOptions{SourceType::Script});
    Analysis result;
    result.parsed = parser.parseProgram();
    if (!result.parsed.program) return result;

    SemaContext context;
    AnnotationStore annotations;
    ScopeGraph graph = runScopePass(*result.parsed.program, context);
    runResolvePass(*result.parsed.program, graph, context, annotations);
    runModulePass(*result.parsed.program, context, annotations);
    result.errors = context.errors();
    return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// 本地导出绑定
// ---------------------------------------------------------------------------

TEST(ModulePassTest, ExportDeclaredNameIsAllowed) {
    Analysis result = analyze("var x; export { x };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, ExportDeclaredLaterIsAllowed) {
    Analysis result = analyze("export { x }; var x;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, ExportUndeclaredNameIsError) {
    Analysis result = analyze("export { x };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, ExportBlockLetIsError) {
    Analysis result = analyze("{ let x; } export { x };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, ExportBlockVarIsAllowed) {
    Analysis result = analyze("{ var x; } export { x };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, ExportForLetIsError) {
    Analysis result = analyze("for (let x;;) { } export { x };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, ExportForVarIsAllowed) {
    Analysis result = analyze("for (var x;;) { } export { x };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, ExportBlockFunctionInModuleIsError) {
    Analysis result = analyze("{ function x() { } } export { x };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, ExportImportBindingIsAllowed) {
    Analysis result = analyze("import { x } from \"m\"; export { x };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, ExportClassIsAllowed) {
    Analysis result = analyze("export class C { }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, ExportClassAndSpecifierDuplicateIsError) {
    Analysis result = analyze("export class C { } export { C };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, ExportFromSkipsLocalCheck) {
    Analysis result = analyze("export { x } from \"m\";");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, ExportStringLocalWithoutFromIsError) {
    Analysis result = analyze("export { \"x\" as y };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, ExportStringLocalWithFromIsAllowed) {
    Analysis result = analyze("export { \"x\" as y } from \"m\";");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, ExportStringExportedNameIsAllowed) {
    Analysis result = analyze("var x; export { x as \"y\" };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// 重复导出
// ---------------------------------------------------------------------------

TEST(ModulePassTest, DuplicateExportSpecifierIsError) {
    Analysis result = analyze("var x; export { x }; export { x };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, DuplicateExportAliasIsError) {
    Analysis result =
        analyze("var x, z; export { x as y }; export { z as y };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, DifferentExportAliasesAreAllowed) {
    Analysis result =
        analyze("var x; export { x as y }; export { x as z };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, DuplicateDefaultIsError) {
    Analysis result = analyze("export default 1; export default 2;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, DefaultAndAliasDefaultIsError) {
    Analysis result =
        analyze("var x; export { x as default }; export default 1;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, DeclarationAndSpecifierDuplicateIsError) {
    Analysis result = analyze("export var x; export { x };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, DuplicateExportVarNamesIsError) {
    Analysis result = analyze("export var x, x;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, ExportStarHasNoExportedNames) {
    Analysis result =
        analyze("export * from \"a\"; export * from \"b\";");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, DuplicateExportStarAsIsError) {
    Analysis result =
        analyze("export * as ns from \"a\"; export * as ns from \"b\";");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, ExportStarAsAndAliasDuplicateIsError) {
    Analysis result = analyze(
        "export * as ns from \"a\"; export { x as ns } from \"b\";");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, DefaultFunctionAndNamedExportIsAllowed) {
    Analysis result =
        analyze("export default function f() { } export { f };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, DefaultClassAndNamedExportIsAllowed) {
    Analysis result =
        analyze("export default class C { } export { C };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, AnonymousDefaultFunctionIsAllowed) {
    Analysis result = analyze("export default function() { };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, ExportDestructuredNames) {
    Analysis result =
        analyze("var obj; export const { a, b } = obj;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// sourceType 约束
// ---------------------------------------------------------------------------

TEST(ModulePassTest, ImportMetaInModuleIsAllowed) {
    Analysis result = analyze("export {}; import.meta;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ModulePassTest, ImportMetaInScriptIsError) {
    Analysis result = analyzeAsScript("import.meta;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, ExportDeclarationInScriptIsError) {
    Analysis result = analyzeAsScript("export {};");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, ImportDeclarationInScriptIsError) {
    Analysis result = analyzeAsScript("import \"m\";");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ModulePassTest, StringImportedNameIsAllowed) {
    Analysis result = analyze("import { \"x\" as y } from \"m\"; y;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}
