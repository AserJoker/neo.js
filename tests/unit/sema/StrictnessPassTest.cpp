// tests/unit/sema/StrictnessPassTest.cpp - 严格模式早期错误测试

#include "frontend/Parser.h"
#include "sema/AnnotationStore.h"
#include "sema/SemaContext.h"
#include "sema/StrictnessPass.h"

#include <gtest/gtest.h>

#include <string_view>
#include <utility>
#include <vector>

namespace {

using neo::frontend::ParseResult;
using neo::frontend::Parser;
using neo::sema::AnnotationStore;
using neo::sema::SemaContext;
using neo::sema::SemaError;
using neo::sema::runStrictnessPass;

struct Analysis {
    ParseResult parsed;
    std::vector<SemaError> errors;
};

Analysis analyze(std::string_view source) {
    Parser parser(source);
    Analysis result;
    result.parsed = parser.parseProgram();
    if (!result.parsed.program) return result;

    AnnotationStore annotations;
    SemaContext context;
    runStrictnessPass(*result.parsed.program, context, annotations);
    result.errors = context.errors();
    return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// 严格性作用域
// ---------------------------------------------------------------------------

TEST(StrictnessPassTest, SloppyWithIsAllowed) {
    Analysis result = analyze("with (o) { }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(StrictnessPassTest, ScriptUseStrictRejectsWith) {
    Analysis result = analyze("\"use strict\"; with (o) { }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, ModuleIsAlwaysStrict) {
    Analysis result = analyze("export {}; with (o) { }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, FunctionInheritsStrictness) {
    Analysis result = analyze("\"use strict\"; function f() { with (o) { } }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, FunctionOwnUseStrict) {
    Analysis result = analyze("function f() { \"use strict\"; with (o) { } }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, NestedFunctionInheritsStrictness) {
    Analysis result =
        analyze("function f() { \"use strict\"; function g() { with (o) { } } }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, ClassBodyIsStrict) {
    Analysis result = analyze("class A { m() { with (o) { } } }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, StaticBlockIsStrict) {
    Analysis result = analyze("class A { static { with (o) { } } }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, ObjectMethodIsNotStrictByDefault) {
    Analysis result = analyze("({ m() { with (o) { } } });");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(StrictnessPassTest, ObjectMethodOwnUseStrict) {
    Analysis result = analyze("({ m() { \"use strict\"; with (o) { } } });");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// legacy 字面量
// ---------------------------------------------------------------------------

TEST(StrictnessPassTest, LegacyOctalLiteralInStrictMode) {
    Analysis result = analyze("\"use strict\"; var x = 010;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, LegacyOctalLiteralInSloppyMode) {
    Analysis result = analyze("var x = 010;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(StrictnessPassTest, LegacyOctalEscapeInStrictMode) {
    Analysis result = analyze("\"use strict\"; var s = \"\\1\";");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, LegacyOctalEscapeInSloppyMode) {
    Analysis result = analyze("var s = \"\\1\";");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(StrictnessPassTest, LegacyEscapeInDirective) {
    Analysis result = analyze("\"use strict\"; \"\\1\";");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// delete / 赋值目标
// ---------------------------------------------------------------------------

TEST(StrictnessPassTest, DeleteIdentifierInStrictMode) {
    Analysis result = analyze("\"use strict\"; delete x;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, DeleteIdentifierInSloppyMode) {
    Analysis result = analyze("delete x;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(StrictnessPassTest, DeleteMemberIsAllowed) {
    Analysis result = analyze("\"use strict\"; delete obj.x;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(StrictnessPassTest, AssignToEvalInStrictMode) {
    Analysis result = analyze("\"use strict\"; eval = 1;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, AssignToEvalInSloppyMode) {
    Analysis result = analyze("eval = 1;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(StrictnessPassTest, UpdateArgumentsInStrictMode) {
    Analysis result = analyze("\"use strict\"; arguments++;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, DestructuringAssignmentToEval) {
    Analysis result = analyze("\"use strict\"; [eval] = arr;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, ForOfTargetEvalInStrictMode) {
    Analysis result = analyze("\"use strict\"; for (eval of xs) { }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, MemberAssignmentToEvalIsAllowed) {
    Analysis result = analyze("\"use strict\"; obj.eval = 1;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// 绑定名
// ---------------------------------------------------------------------------

TEST(StrictnessPassTest, EvalBindingInStrictMode) {
    Analysis result = analyze("\"use strict\"; var eval = 1;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, EvalBindingInSloppyMode) {
    Analysis result = analyze("var eval = 1;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(StrictnessPassTest, ArgumentsParameterInStrictMode) {
    Analysis result = analyze("\"use strict\"; function f(arguments) { }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, FunctionNameEvalWithOwnUseStrict) {
    Analysis result = analyze("function eval() { \"use strict\"; }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, ImportBindingEvalInModule) {
    Analysis result = analyze("import eval from \"m\";");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, ClassNameEvalIsAlwaysStrict) {
    Analysis result = analyze("class eval { }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, ClassNameReservedIsAlwaysStrict) {
    Analysis result = analyze("class implements { }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// 严格模式保留字
// ---------------------------------------------------------------------------

TEST(StrictnessPassTest, StrictReservedBinding) {
    Analysis result = analyze("\"use strict\"; var implements = 1;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, StrictReservedReference) {
    Analysis result = analyze("\"use strict\"; implements;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, StrictReservedPropertyNameIsAllowed) {
    Analysis result = analyze(
        "\"use strict\"; obj.implements; ({ implements: 1 }); "
        "class A { implements() { } }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(StrictnessPassTest, StrictReservedLabel) {
    Analysis result = analyze("\"use strict\"; let: ;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, StrictReservedBreakLabel) {
    Analysis result = analyze("\"use strict\"; while (x) { break let; }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, OrdinaryLabelIsAllowed) {
    Analysis result = analyze("\"use strict\"; label: ;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(StrictnessPassTest, StrictReservedInSloppyMode) {
    Analysis result = analyze("var implements = 1; implements;");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// 参数
// ---------------------------------------------------------------------------

TEST(StrictnessPassTest, DuplicateParamsInSloppyMode) {
    Analysis result = analyze("function f(a, a) { }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(StrictnessPassTest, DuplicateParamsInStrictMode) {
    Analysis result = analyze("\"use strict\"; function f(a, a) { }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, DuplicateParamsWithNonSimpleList) {
    Analysis result = analyze("function f(a, a = 1) { }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, ClassMethodDuplicateParams) {
    Analysis result = analyze("class A { m(a, a) { } }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, NonSimpleParamsWithUseStrict) {
    Analysis result = analyze("function f(a = 1) { \"use strict\"; }");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(StrictnessPassTest, ArrowNonSimpleParamsWithUseStrict) {
    Analysis result = analyze("(a = 1) => { \"use strict\"; };");

    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}
