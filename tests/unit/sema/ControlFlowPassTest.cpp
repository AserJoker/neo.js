// tests/unit/sema/ControlFlowPassTest.cpp - 控制流上下文早期错误测试

#include "frontend/Parser.h"
#include "sema/AnnotationStore.h"
#include "sema/ControlFlowPass.h"
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
using neo::sema::runControlFlowPass;
using neo::sema::runResolvePass;
using neo::sema::runScopePass;

struct Analysis {
    ParseResult parsed;
    std::vector<SemaError> errors;
};

Analysis analyze(std::string_view source,
                 SourceType sourceType = SourceType::Script) {
    Parser parser(source, ParserOptions{sourceType});
    Analysis result;
    result.parsed = parser.parseProgram();
    if (!result.parsed.program) return result;

    SemaContext context;
    AnnotationStore annotations;
    ScopeGraph graph = runScopePass(*result.parsed.program, context);
    runResolvePass(*result.parsed.program, graph, context, annotations);
    runControlFlowPass(*result.parsed.program, context, annotations);
    result.errors = context.errors();
    return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// return
// ---------------------------------------------------------------------------

TEST(ControlFlowPassTest, ReturnAtTopLevelIsError) {
    Analysis result = analyze("return;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, ReturnInFunctionIsAllowed) {
    Analysis result = analyze("function f(){ return; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, ReturnInArrowIsAllowed) {
    Analysis result = analyze("() => { return; };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, ReturnInStaticBlockIsError) {
    Analysis result = analyze("class C { static { return; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// break / continue
// ---------------------------------------------------------------------------

TEST(ControlFlowPassTest, BreakAtTopLevelIsError) {
    Analysis result = analyze("break;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, BreakInLoopIsAllowed) {
    Analysis result = analyze("while (false) { break; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, BreakInSwitchIsAllowed) {
    Analysis result = analyze("switch (1) { case 1: break; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, BreakInFunctionInsideLoopIsError) {
    Analysis result = analyze("while (false) { function f(){ break; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, BreakInStaticBlockInsideLoopIsError) {
    Analysis result =
        analyze("while (false) { class C { static { break; } } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, ContinueAtTopLevelIsError) {
    Analysis result = analyze("continue;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, ContinueInLoopIsAllowed) {
    Analysis result = analyze("while (false) { continue; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, ContinueInSwitchIsError) {
    Analysis result = analyze("switch (1) { case 1: continue; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// 标签
// ---------------------------------------------------------------------------

TEST(ControlFlowPassTest, BreakToLabelIsAllowed) {
    Analysis result = analyze("foo: { break foo; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, BreakToUndefinedLabelIsError) {
    Analysis result = analyze("break foo;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, ContinueToLoopLabelIsAllowed) {
    Analysis result = analyze("foo: while (false) { continue foo; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, ContinueToBlockLabelIsError) {
    Analysis result = analyze("foo: { continue foo; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, ContinueToNestedLoopLabelIsAllowed) {
    Analysis result =
        analyze("foo: bar: while (false) { continue foo; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, ContinueToLabelWrappingBlockIsError) {
    Analysis result =
        analyze("foo: bar: { while (false) { continue foo; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, DuplicateLabelIsError) {
    Analysis result = analyze("foo: foo: while (false) { }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, SequentialSameLabelIsAllowed) {
    Analysis result = analyze("foo: { } foo: { }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, LabelDoesNotCrossFunctionBoundary) {
    Analysis result =
        analyze("foo: while (false) { function f(){ break foo; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, LabelDoesNotCrossStaticBlockBoundary) {
    Analysis result =
        analyze("foo: while (false) { class C { static { break foo; } } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// new.target
// ---------------------------------------------------------------------------

TEST(ControlFlowPassTest, NewTargetAtTopLevelIsError) {
    Analysis result = analyze("new.target;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, NewTargetInFunctionIsAllowed) {
    Analysis result = analyze("function f(){ new.target; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, NewTargetInArrowAtTopLevelIsError) {
    Analysis result = analyze("(() => new.target);");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, NewTargetInArrowInFunctionIsAllowed) {
    Analysis result = analyze("function f(){ return () => new.target; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, NewTargetInMethodIsAllowed) {
    Analysis result = analyze("class C { m(){ new.target; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, NewTargetInStaticBlockIsAllowed) {
    Analysis result = analyze("class C { static { new.target; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, NewTargetInFieldIsAllowed) {
    Analysis result = analyze("class C { x = new.target; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// super
// ---------------------------------------------------------------------------

TEST(ControlFlowPassTest, SuperPropertyInMethodIsAllowed) {
    Analysis result = analyze("class C { m(){ super.x; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperPropertyInObjectMethodIsAllowed) {
    Analysis result = analyze("({ m(){ super.x; } });");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperPropertyInPlainFunctionIsError) {
    Analysis result = analyze("function f(){ super.x; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperPropertyInArrowInMethodIsAllowed) {
    Analysis result = analyze("class C { m(){ return () => super.x; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperPropertyInFunctionInMethodIsError) {
    Analysis result =
        analyze("class C { m(){ function f(){ super.x; } } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperPropertyInStaticBlockIsAllowed) {
    Analysis result = analyze("class C { static { super.x; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperPropertyInFieldIsAllowed) {
    Analysis result = analyze("class C { x = super.x; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperPropertyInComputedKeyAtTopLevelIsError) {
    Analysis result = analyze("class C { [super.x](){} }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperCallInDerivedConstructorIsAllowed) {
    Analysis result =
        analyze("class C extends B { constructor(){ super(); } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperCallInBaseConstructorIsError) {
    Analysis result = analyze("class C { constructor(){ super(); } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperCallInMethodIsError) {
    Analysis result = analyze("class C extends B { m(){ super(); } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperCallInFieldIsError) {
    Analysis result = analyze("class C extends B { x = super(); }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperCallInStaticBlockIsError) {
    Analysis result =
        analyze("class C extends B { static { super(); } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperCallInArrowInDerivedConstructorIsAllowed) {
    Analysis result = analyze(
        "class C extends B { constructor(){ (() => super())(); } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, SuperCallInFunctionInDerivedConstructorIsError) {
    Analysis result = analyze(
        "class C extends B { constructor(){ function f(){ super(); } } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, BareSuperIsError) {
    Analysis result = analyze("class C { m(){ super; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// await / yield / for await
// ---------------------------------------------------------------------------

TEST(ControlFlowPassTest, ForAwaitInAsyncFunctionIsAllowed) {
    Analysis result =
        analyze("async function f(){ for await (const x of y) { } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, ForAwaitInNonAsyncFunctionIsError) {
    Analysis result =
        analyze("function f(){ for await (const x of y) { } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, ForAwaitAtModuleTopLevelIsAllowed) {
    Analysis result =
        analyze("export {}; for await (const x of y) { }",
                SourceType::Module);
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, ForAwaitAtScriptTopLevelIsError) {
    Analysis result = analyze("for await (const x of y) { }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, AwaitInAsyncFunctionIsAllowed) {
    Analysis result = analyze("async function f(){ await 1; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, YieldInGeneratorIsAllowed) {
    Analysis result = analyze("function* f(){ yield 1; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// 类字段 / static block 的 ContainsArguments
// ---------------------------------------------------------------------------

TEST(ControlFlowPassTest, ArgumentsInStaticBlockIsError) {
    Analysis result = analyze("class C { static { arguments; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, ArgumentsInStaticBlockArrowIsError) {
    Analysis result =
        analyze("class C { static { (() => arguments); } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, ArgumentsInStaticBlockFunctionIsAllowed) {
    Analysis result =
        analyze("class C { static { (function(){ arguments; }); } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, ArgumentsInFieldIsError) {
    Analysis result = analyze("class C { x = arguments; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, ArgumentsInFieldArrowIsError) {
    Analysis result = analyze("class C { x = (() => arguments); }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ControlFlowPassTest, ArgumentsInFieldFunctionIsAllowed) {
    Analysis result =
        analyze("class C { x = (function(){ arguments; }); }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, ArgumentsAsPropertyKeyIsAllowed) {
    Analysis result = analyze("class C { x = ({ arguments: 1 }); }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ControlFlowPassTest, ArgumentsInMethodIsAllowed) {
    Analysis result = analyze("class C { m(){ arguments; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}
