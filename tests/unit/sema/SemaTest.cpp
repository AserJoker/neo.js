// tests/unit/sema/SemaTest.cpp - Sema 入口流水线测试

#include "frontend/Parser.h"
#include "sema/AnnotatePass.h"
#include "sema/ResolvePass.h"
#include "sema/Sema.h"

#include <gtest/gtest.h>

#include <string_view>

namespace {

using neo::frontend::Node;
using neo::frontend::NodeKind;
using neo::frontend::ParseResult;
using neo::frontend::Parser;
using neo::frontend::ParserOptions;
using neo::frontend::Program;
using neo::frontend::SourceType;
using neo::sema::FunctionInfo;
using neo::sema::SemaResult;
using neo::sema::runSema;

struct Analysis {
    ParseResult parsed;
    SemaResult sema;
};

Analysis analyze(std::string_view source,
                 SourceType sourceType = SourceType::Script) {
    Parser parser(source, ParserOptions{sourceType});
    Analysis result;
    result.parsed = parser.parseProgram();
    if (!result.parsed.program) return result;
    result.sema = runSema(*result.parsed.program);
    return result;
}

}  // namespace

TEST(SemaTest, RunsAllPassesAndProducesAnnotations) {
    Analysis result = analyze("var a; function f() { return a; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.sema.ok());

    const auto& program = static_cast<const Program&>(*result.parsed.program);
    const Node& function = *program.body[1];
    EXPECT_EQ(function.kind(), NodeKind::FunctionDeclaration);

    const FunctionInfo* info =
        result.sema.annotations.get<FunctionInfo>(function.id());
    ASSERT_NE(info, nullptr);
    EXPECT_FALSE(info->isArrow);
}

TEST(SemaTest, ReportsStrictnessErrors) {
    Analysis result = analyze("\"use strict\"; with (o) { }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.sema.ok());
}

TEST(SemaTest, ReportsScopeErrors) {
    Analysis result = analyze("let x; let x;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.sema.ok());
}

TEST(SemaTest, ReportsControlFlowErrors) {
    Analysis result = analyze("return 1;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.sema.ok());
}

TEST(SemaTest, ReportsModuleErrors) {
    Analysis result = analyze("export { x };", SourceType::Module);
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.sema.ok());
}

TEST(SemaTest, CleanProgramHasNoErrors) {
    Analysis result = analyze("function f(a) { return a + 1; } f(1);");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.sema.ok());
}
