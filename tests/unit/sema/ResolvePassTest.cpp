// tests/unit/sema/ResolvePassTest.cpp - 标识符解析与闭包穿透捕获测试

#include "frontend/Parser.h"
#include "sema/AnnotationStore.h"
#include "sema/ResolvePass.h"
#include "sema/ScopePass.h"
#include "sema/SemaContext.h"
#include "sema/Walker.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using neo::frontend::Identifier;
using neo::frontend::Node;
using neo::frontend::NodeKind;
using neo::frontend::ParseResult;
using neo::frontend::Parser;
using neo::frontend::ParserOptions;
using neo::frontend::SourceType;
using neo::sema::AnnotationStore;
using neo::sema::BindingLocation;
using neo::sema::FunctionCaptures;
using neo::sema::FunctionInfo;
using neo::sema::IdentifierBinding;
using neo::sema::IVisitor;
using neo::sema::ScopeGraph;
using neo::sema::SemaContext;
using neo::sema::SemaError;
using neo::sema::ShadowBindingKind;
using neo::sema::ShadowScopeKind;
using neo::sema::Walker;
using neo::sema::runResolvePass;
using neo::sema::runScopePass;

struct ResolveAnalysis {
    ParseResult parsed;
    ScopeGraph graph;
    AnnotationStore annotations;
    std::vector<SemaError> errors;
};

ResolveAnalysis analyze(std::string_view source,
                        SourceType sourceType = SourceType::Script) {
    Parser parser(source, ParserOptions{sourceType});
    ResolveAnalysis result;
    result.parsed = parser.parseProgram();
    if (!result.parsed.program) return result;

    SemaContext context;
    result.graph = runScopePass(*result.parsed.program, context);
    runResolvePass(*result.parsed.program, result.graph, context,
                   result.annotations);
    result.errors = context.errors();
    return result;
}

class CollectVisitor final : public IVisitor {
public:
    explicit CollectVisitor(NodeKind kind) : kind_(kind) {}

    void enter(Walker& /*walker*/, SemaContext& /*context*/,
               Node& node) override {
        if (node.kind() == kind_) nodes_.push_back(&node);
    }

    std::vector<Node*> take() { return std::move(nodes_); }

private:
    NodeKind kind_;
    std::vector<Node*> nodes_;
};

std::vector<Node*> collect(Node& root, NodeKind kind) {
    AnnotationStore annotations;
    SemaContext context;
    Walker walker(annotations);
    auto visitor = std::make_unique<CollectVisitor>(kind);
    CollectVisitor* raw = visitor.get();
    walker.addAll(std::move(visitor));
    walker.walk(root, context);
    return raw->take();
}

const IdentifierBinding* bindingOf(ResolveAnalysis& analysis, Node* node) {
    return analysis.annotations.get<IdentifierBinding>(node->id());
}

std::vector<Node*> references(ResolveAnalysis& analysis,
                              std::string_view name) {
    std::vector<Node*> result;
    for (Node* node : collect(*analysis.parsed.program, NodeKind::Identifier)) {
        const auto& identifier = static_cast<Identifier&>(*node);
        if (identifier.name != name) continue;
        if (bindingOf(analysis, node) != nullptr) result.push_back(node);
    }
    return result;
}

std::vector<std::string> captures(ResolveAnalysis& analysis, Node* owner) {
    const auto* captures =
        analysis.annotations.get<FunctionCaptures>(owner->id());
    if (!captures) return {};
    return captures->names;
}

}  // namespace

// ---------------------------------------------------------------------------
// 同函数解析
// ---------------------------------------------------------------------------

TEST(ResolvePassTest, LocalVariable) {
    ResolveAnalysis result = analyze("var a; a;");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "a");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Local);
    ASSERT_NE(binding->binding, nullptr);
    EXPECT_EQ(binding->binding->kind, ShadowBindingKind::Var);
}

TEST(ResolvePassTest, BlockLetIsLocal) {
    ResolveAnalysis result = analyze("{ let a; a; }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "a");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Local);
    ASSERT_NE(binding->scope, nullptr);
    EXPECT_EQ(binding->scope->kind, ShadowScopeKind::Block);
}

TEST(ResolvePassTest, InnerVarShadowsOuter) {
    ResolveAnalysis result =
        analyze("var a; function f(){ var a; return a; }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "a");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Local);
    ASSERT_NE(binding->scope, nullptr);
    EXPECT_EQ(binding->scope->kind, ShadowScopeKind::FunctionBody);
}

TEST(ResolvePassTest, BindingIdentifiersAreNotReferences) {
    ResolveAnalysis result = analyze("var a; function f(a) { }");
    ASSERT_TRUE(result.parsed.ok());

    EXPECT_TRUE(references(result, "a").empty());
}

TEST(ResolvePassTest, FunctionExpressionNameIsLocal) {
    ResolveAnalysis result = analyze("var f = function g(){ return g; };");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "g");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Local);
    ASSERT_NE(binding->binding, nullptr);
    EXPECT_EQ(binding->binding->kind, ShadowBindingKind::FunctionName);
}

TEST(ResolvePassTest, CatchParamIsLocal) {
    ResolveAnalysis result = analyze("try { } catch (e) { e; }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "e");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Local);
    ASSERT_NE(binding->binding, nullptr);
    EXPECT_EQ(binding->binding->kind, ShadowBindingKind::CatchParam);
}

TEST(ResolvePassTest, ImportBindingIsLocal) {
    ResolveAnalysis result =
        analyze("import { x } from \"m\"; x;", SourceType::Module);
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "x");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Local);
    ASSERT_NE(binding->binding, nullptr);
    EXPECT_EQ(binding->binding->kind, ShadowBindingKind::Import);
}

// ---------------------------------------------------------------------------
// 闭包捕获与穿透
// ---------------------------------------------------------------------------

TEST(ResolvePassTest, InnerFunctionCapturesOuterVar) {
    ResolveAnalysis result = analyze(
        "function f(){ var a; return function(){ return a; }; }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "a");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Captured);

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionDeclaration);
    auto expressions = collect(*result.parsed.program,
                               NodeKind::FunctionExpression);
    ASSERT_EQ(functions.size(), 1u);
    ASSERT_EQ(expressions.size(), 1u);
    EXPECT_EQ(captures(result, functions[0]), std::vector<std::string>{});
    EXPECT_EQ(captures(result, expressions[0]),
              std::vector<std::string>{"a"});
}

TEST(ResolvePassTest, PassthroughCaptureRegistersEveryCrossedFunction) {
    ResolveAnalysis result = analyze(
        "var a; function main(){ function fn(){ return a; } }");
    ASSERT_TRUE(result.parsed.ok());

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 2u);  // main, fn
    EXPECT_EQ(captures(result, functions[0]),
              std::vector<std::string>{"a"});
    EXPECT_EQ(captures(result, functions[1]),
              std::vector<std::string>{"a"});
}

TEST(ResolvePassTest, ArrowFunctionCapturesOuterVar) {
    ResolveAnalysis result =
        analyze("function f(){ var a; return () => a; }");
    ASSERT_TRUE(result.parsed.ok());

    auto arrows = collect(*result.parsed.program,
                          NodeKind::ArrowFunctionExpression);
    ASSERT_EQ(arrows.size(), 1u);
    EXPECT_EQ(captures(result, arrows[0]),
              std::vector<std::string>{"a"});
}

TEST(ResolvePassTest, CapturesAreDeduplicated) {
    ResolveAnalysis result = analyze(
        "function f(){ var a; return function(){ return a + a; }; }");
    ASSERT_TRUE(result.parsed.ok());

    auto expressions = collect(*result.parsed.program,
                               NodeKind::FunctionExpression);
    ASSERT_EQ(expressions.size(), 1u);
    EXPECT_EQ(captures(result, expressions[0]),
              std::vector<std::string>{"a"});
}

TEST(ResolvePassTest, NestedBlockCapture) {
    ResolveAnalysis result = analyze(
        "function f(){ { let a; return function(){ return a; }; } }");
    ASSERT_TRUE(result.parsed.ok());

    auto expressions = collect(*result.parsed.program,
                               NodeKind::FunctionExpression);
    ASSERT_EQ(expressions.size(), 1u);
    EXPECT_EQ(captures(result, expressions[0]),
              std::vector<std::string>{"a"});
}

TEST(ResolvePassTest, ParameterDefaultCapturesOuter) {
    ResolveAnalysis result =
        analyze("var x; function f(a = x) { }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "x");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Captured);

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);
    EXPECT_EQ(captures(result, functions[0]),
              std::vector<std::string>{"x"});
}

TEST(ResolvePassTest, ParameterDefaultCannotSeeBodyVar) {
    ResolveAnalysis result = analyze("function f(a = b){ var b; }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "b");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Global);

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);
    EXPECT_TRUE(captures(result, functions[0]).empty());
}

TEST(ResolvePassTest, ModuleTopLevelCapture) {
    ResolveAnalysis result =
        analyze("export {}; var a; function f(){ return a; }",
                SourceType::Module);
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "a");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Captured);

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);
    EXPECT_EQ(captures(result, functions[0]),
              std::vector<std::string>{"a"});
}

TEST(ResolvePassTest, WithBodyStillCapturesOuterVar) {
    ResolveAnalysis result = analyze(
        "function f(){ var a; with (o) { return function(){ return a; }; } }");
    ASSERT_TRUE(result.parsed.ok());

    auto expressions = collect(*result.parsed.program,
                               NodeKind::FunctionExpression);
    ASSERT_EQ(expressions.size(), 1u);
    EXPECT_EQ(captures(result, expressions[0]),
              std::vector<std::string>{"a"});
}

// ---------------------------------------------------------------------------
// 未解析引用
// ---------------------------------------------------------------------------

TEST(ResolvePassTest, UnresolvedInScriptIsGlobal) {
    ResolveAnalysis result = analyze("x;");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "x");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Global);
    EXPECT_EQ(binding->binding, nullptr);
}

TEST(ResolvePassTest, UnresolvedInModuleIsUnresolved) {
    ResolveAnalysis result =
        analyze("export {}; x;", SourceType::Module);
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "x");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Unresolved);
    EXPECT_EQ(binding->binding, nullptr);
}

// ---------------------------------------------------------------------------
// 非引用位置
// ---------------------------------------------------------------------------

TEST(ResolvePassTest, PropertyKeyIsNotReference) {
    ResolveAnalysis result = analyze("({ a: 1 });");
    ASSERT_TRUE(result.parsed.ok());

    EXPECT_TRUE(references(result, "a").empty());
}

TEST(ResolvePassTest, ShorthandValueIsReference) {
    ResolveAnalysis result = analyze("var a; ({ a });");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "a");
    ASSERT_EQ(refs.size(), 1u);
    EXPECT_EQ(bindingOf(result, refs[0])->location, BindingLocation::Local);
}

TEST(ResolvePassTest, MemberPropertyIsNotReference) {
    ResolveAnalysis result = analyze("var o; o.a;");
    ASSERT_TRUE(result.parsed.ok());

    EXPECT_EQ(references(result, "o").size(), 1u);
    EXPECT_TRUE(references(result, "a").empty());
}

TEST(ResolvePassTest, ComputedMemberPropertyIsReference) {
    ResolveAnalysis result = analyze("var o; var a; o[a];");
    ASSERT_TRUE(result.parsed.ok());

    EXPECT_EQ(references(result, "a").size(), 1u);
}

TEST(ResolvePassTest, ComputedPatternKeyIsReference) {
    ResolveAnalysis result =
        analyze("var k; function f({ [k]: v }) { }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "k");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Captured);
    EXPECT_TRUE(references(result, "v").empty());
}

TEST(ResolvePassTest, DestructuringAssignmentTargetIsReference) {
    ResolveAnalysis result = analyze("var a; [a] = [1];");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "a");
    ASSERT_EQ(refs.size(), 1u);
    EXPECT_EQ(bindingOf(result, refs[0])->location, BindingLocation::Local);
}

TEST(ResolvePassTest, ExportLocalIsReference) {
    ResolveAnalysis result =
        analyze("export {}; var a; export { a };", SourceType::Module);
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "a");
    ASSERT_EQ(refs.size(), 1u);
    EXPECT_EQ(bindingOf(result, refs[0])->location, BindingLocation::Local);
}

// ---------------------------------------------------------------------------
// 类：方法 / 字段初始化器捕获边界
// ---------------------------------------------------------------------------

TEST(ResolvePassTest, ClassMethodCapturesClassName) {
    ResolveAnalysis result = analyze("class C { m(){ return C; } }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "C");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Captured);

    auto methods = collect(*result.parsed.program,
                           NodeKind::FunctionExpression);
    ASSERT_EQ(methods.size(), 1u);
    EXPECT_EQ(captures(result, methods[0]),
              std::vector<std::string>{"C"});
}

TEST(ResolvePassTest, FieldInitializerCapturesOuterVar) {
    ResolveAnalysis result = analyze("var x; class C { y = x; }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "x");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Captured);

    auto fields = collect(*result.parsed.program,
                          NodeKind::PropertyDefinition);
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(captures(result, fields[0]),
              std::vector<std::string>{"x"});
}

TEST(ResolvePassTest, ComputedFieldKeyIsOutsideInitializer) {
    ResolveAnalysis result = analyze("var x; class C { [x] = 1; }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "x");
    ASSERT_EQ(refs.size(), 1u);
    EXPECT_EQ(bindingOf(result, refs[0])->location, BindingLocation::Local);

    auto fields = collect(*result.parsed.program,
                          NodeKind::PropertyDefinition);
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_TRUE(captures(result, fields[0]).empty());
}

TEST(ResolvePassTest, StaticBlockIsCaptureBoundary) {
    ResolveAnalysis result =
        analyze("function f(){ var a; class C { static { a; } } }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "a");
    ASSERT_EQ(refs.size(), 1u);
    EXPECT_EQ(bindingOf(result, refs[0])->location, BindingLocation::Captured);

    auto blocks = collect(*result.parsed.program, NodeKind::StaticBlock);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(captures(result, blocks[0]),
              std::vector<std::string>{"a"});

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);
    EXPECT_TRUE(captures(result, functions[0]).empty());
}

TEST(ResolvePassTest, StaticBlockOwnVarIsLocal) {
    ResolveAnalysis result =
        analyze("class C { static { var a; a; } }");
    ASSERT_TRUE(result.parsed.ok());

    auto refs = references(result, "a");
    ASSERT_EQ(refs.size(), 1u);
    const auto* binding = bindingOf(result, refs[0]);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->location, BindingLocation::Local);
    ASSERT_NE(binding->scope, nullptr);
    EXPECT_EQ(binding->scope->kind, ShadowScopeKind::StaticBlock);
}

TEST(ResolvePassTest, FunctionInStaticBlockCapturesBlockVar) {
    ResolveAnalysis result = analyze(
        "class C { static { var a; function g(){ return a; } } }");
    ASSERT_TRUE(result.parsed.ok());

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);
    EXPECT_EQ(captures(result, functions[0]),
              std::vector<std::string>{"a"});
}

// ---------------------------------------------------------------------------
// 直接 eval 标记
// ---------------------------------------------------------------------------

TEST(ResolvePassTest, DirectEvalMarksEnclosingFunction) {
    ResolveAnalysis result = analyze("function f(){ eval(\"1\"); }");
    ASSERT_TRUE(result.parsed.ok());

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);
    const auto* info =
        result.annotations.get<FunctionInfo>(functions[0]->id());
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->hasDirectEval);
}

TEST(ResolvePassTest, IndirectEvalDoesNotMarkFunction) {
    ResolveAnalysis result = analyze("function f(){ var e = eval; e(\"1\"); }");
    ASSERT_TRUE(result.parsed.ok());

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);
    const auto* info =
        result.annotations.get<FunctionInfo>(functions[0]->id());
    EXPECT_EQ(info, nullptr);
}
