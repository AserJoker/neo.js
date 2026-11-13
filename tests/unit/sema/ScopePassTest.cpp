// tests/unit/sema/ScopePassTest.cpp - 作用域树与变量提升测试

#include "frontend/Parser.h"
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

using neo::frontend::Node;
using neo::frontend::NodeKind;
using neo::frontend::ParseResult;
using neo::frontend::Parser;
using neo::sema::AnnotationStore;
using neo::sema::IVisitor;
using neo::sema::ScopeGraph;
using neo::sema::SemaContext;
using neo::sema::SemaError;
using neo::sema::ShadowBinding;
using neo::sema::ShadowBindingKind;
using neo::sema::ShadowScope;
using neo::sema::ShadowScopeKind;
using neo::sema::Walker;
using neo::sema::runScopePass;

struct ScopeAnalysis {
    ParseResult parsed;
    ScopeGraph graph;
    std::vector<SemaError> errors;
};

ScopeAnalysis analyze(std::string_view source) {
    Parser parser(source);
    ScopeAnalysis result;
    result.parsed = parser.parseProgram();
    if (!result.parsed.program) return result;

    SemaContext context;
    result.graph = runScopePass(*result.parsed.program, context);
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

const ShadowBinding* findBinding(ShadowScope* scope, std::string_view name) {
    if (!scope) return nullptr;
    const auto it = scope->bindings.find(std::string(name));
    return it == scope->bindings.end() ? nullptr : &it->second;
}

}  // namespace

// ---------------------------------------------------------------------------
// 作用域树
// ---------------------------------------------------------------------------

TEST(ScopePassTest, ProgramCollectsDeclarations) {
    ScopeAnalysis result =
        analyze("var a; let b; const c = 1; function f() {} class D {}");

    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());
    ASSERT_NE(result.graph.root, nullptr);
    EXPECT_EQ(result.graph.root->kind, ShadowScopeKind::Program);

    const ShadowBinding* a = findBinding(result.graph.root.get(), "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->kind, ShadowBindingKind::Var);
    EXPECT_TRUE(a->initialized);

    const ShadowBinding* b = findBinding(result.graph.root.get(), "b");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->kind, ShadowBindingKind::Let);
    EXPECT_FALSE(b->initialized);
    EXPECT_TRUE(b->mutableBinding);

    const ShadowBinding* c = findBinding(result.graph.root.get(), "c");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->kind, ShadowBindingKind::Const);
    EXPECT_FALSE(c->initialized);
    EXPECT_FALSE(c->mutableBinding);

    const ShadowBinding* f = findBinding(result.graph.root.get(), "f");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->kind, ShadowBindingKind::Function);

    const ShadowBinding* d = findBinding(result.graph.root.get(), "D");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->kind, ShadowBindingKind::Class);
    EXPECT_FALSE(d->initialized);
}

TEST(ScopePassTest, VarHoistsToFunctionBody) {
    ScopeAnalysis result = analyze("function f() { { var x; } }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> functions =
        collect(*result.parsed.program, NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);

    const auto& scopes = result.graph.functions.at(functions[0]->id());
    ASSERT_NE(scopes.body, nullptr);
    const ShadowBinding* x = findBinding(scopes.body, "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->kind, ShadowBindingKind::Var);

    std::vector<Node*> blocks =
        collect(*result.parsed.program, NodeKind::BlockStatement);
    ASSERT_EQ(blocks.size(), 2u);  // 函数体 + 内层 block
    ShadowScope* inner = result.graph.findScope(blocks[1]->id());
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(findBinding(inner, "x"), nullptr);
}

TEST(ScopePassTest, LetStaysInBlockScope) {
    ScopeAnalysis result = analyze("{ let x; }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> blocks =
        collect(*result.parsed.program, NodeKind::BlockStatement);
    ASSERT_EQ(blocks.size(), 1u);
    ShadowScope* block = result.graph.findScope(blocks[0]->id());
    ASSERT_NE(block, nullptr);
    const ShadowBinding* x = findBinding(block, "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->kind, ShadowBindingKind::Let);
    EXPECT_EQ(findBinding(result.graph.root.get(), "x"), nullptr);
}

TEST(ScopePassTest, ForInitHasOwnScope) {
    ScopeAnalysis result = analyze("for (let i = 0; i < 1; i++) { }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> fors =
        collect(*result.parsed.program, NodeKind::ForStatement);
    ASSERT_EQ(fors.size(), 1u);
    ShadowScope* scope = result.graph.findScope(fors[0]->id());
    ASSERT_NE(scope, nullptr);
    EXPECT_EQ(scope->kind, ShadowScopeKind::For);
    ASSERT_NE(findBinding(scope, "i"), nullptr);
    EXPECT_EQ(findBinding(result.graph.root.get(), "i"), nullptr);
}

TEST(ScopePassTest, CatchScopeReusesBodyBlock) {
    ScopeAnalysis result = analyze("try { } catch (e) { let x; }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> catches =
        collect(*result.parsed.program, NodeKind::CatchClause);
    ASSERT_EQ(catches.size(), 1u);
    ShadowScope* scope = result.graph.findScope(catches[0]->id());
    ASSERT_NE(scope, nullptr);
    EXPECT_EQ(scope->kind, ShadowScopeKind::Catch);
    ASSERT_NE(findBinding(scope, "e"), nullptr);
    ASSERT_NE(findBinding(scope, "x"), nullptr);

    std::vector<Node*> blocks =
        collect(*result.parsed.program, NodeKind::BlockStatement);
    // try block + catch body
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(result.graph.findScope(blocks[1]->id()), scope);
}

TEST(ScopePassTest, SwitchCasesShareBlockScope) {
    ScopeAnalysis result =
        analyze("switch (x) { case 1: let y; case 2: let z; }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> switches =
        collect(*result.parsed.program, NodeKind::SwitchStatement);
    ASSERT_EQ(switches.size(), 1u);
    ShadowScope* scope = result.graph.findScope(switches[0]->id());
    ASSERT_NE(scope, nullptr);
    ASSERT_NE(findBinding(scope, "y"), nullptr);
    ASSERT_NE(findBinding(scope, "z"), nullptr);
    EXPECT_EQ(findBinding(result.graph.root.get(), "y"), nullptr);
}

TEST(ScopePassTest, WithScopeAndBodyBlock) {
    ScopeAnalysis result = analyze("with (o) { let x; }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> withs =
        collect(*result.parsed.program, NodeKind::WithStatement);
    ASSERT_EQ(withs.size(), 1u);
    ShadowScope* with = result.graph.findScope(withs[0]->id());
    ASSERT_NE(with, nullptr);
    EXPECT_EQ(with->kind, ShadowScopeKind::With);

    std::vector<Node*> blocks =
        collect(*result.parsed.program, NodeKind::BlockStatement);
    ASSERT_EQ(blocks.size(), 1u);
    ShadowScope* block = result.graph.findScope(blocks[0]->id());
    ASSERT_NE(block, nullptr);
    ASSERT_NE(findBinding(block, "x"), nullptr);
    EXPECT_EQ(block->parent, with);
}

TEST(ScopePassTest, FunctionScopesSplitArgumentsAndBody) {
    ScopeAnalysis result = analyze("function f(p) { let x; }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> functions =
        collect(*result.parsed.program, NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);
    const auto& scopes = result.graph.functions.at(functions[0]->id());
    ASSERT_NE(scopes.closure, nullptr);
    ASSERT_NE(scopes.arguments, nullptr);
    ASSERT_NE(scopes.body, nullptr);

    ASSERT_NE(findBinding(scopes.arguments, "p"), nullptr);
    ASSERT_NE(findBinding(scopes.body, "x"), nullptr);
    EXPECT_EQ(findBinding(scopes.body, "p"), nullptr);
    EXPECT_EQ(scopes.body->parent, scopes.arguments);
}

TEST(ScopePassTest, StaticBlockHasVarScope) {
    ScopeAnalysis result = analyze("class A { static { var x; } }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> blocks =
        collect(*result.parsed.program, NodeKind::StaticBlock);
    ASSERT_EQ(blocks.size(), 1u);
    ShadowScope* scope = result.graph.findScope(blocks[0]->id());
    ASSERT_NE(scope, nullptr);
    EXPECT_EQ(scope->kind, ShadowScopeKind::StaticBlock);
    ASSERT_NE(findBinding(scope, "x"), nullptr);
    EXPECT_EQ(findBinding(result.graph.root.get(), "x"), nullptr);
}

TEST(ScopePassTest, ClassExpressionNameIsScoped) {
    ScopeAnalysis result = analyze("var C = class D { };");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    ASSERT_NE(findBinding(result.graph.root.get(), "C"), nullptr);
    EXPECT_EQ(findBinding(result.graph.root.get(), "D"), nullptr);

    std::vector<Node*> bodies =
        collect(*result.parsed.program, NodeKind::ClassBody);
    ASSERT_EQ(bodies.size(), 1u);
    ShadowScope* classScope = result.graph.findScope(bodies[0]->id());
    ASSERT_NE(classScope, nullptr);
    ASSERT_NE(findBinding(classScope, "D"), nullptr);
}

TEST(ScopePassTest, FunctionExpressionNameIsScoped) {
    ScopeAnalysis result = analyze("var f = function g() { };");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    ASSERT_NE(findBinding(result.graph.root.get(), "f"), nullptr);
    EXPECT_EQ(findBinding(result.graph.root.get(), "g"), nullptr);

    std::vector<Node*> functions =
        collect(*result.parsed.program, NodeKind::FunctionExpression);
    ASSERT_EQ(functions.size(), 1u);
    const auto& scopes = result.graph.functions.at(functions[0]->id());
    ASSERT_NE(findBinding(scopes.closure, "g"), nullptr);
}

TEST(ScopePassTest, ImplicitArgumentsOnlyForNonArrow) {
    ScopeAnalysis result =
        analyze("function f() { } var g = () => { };");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> functions =
        collect(*result.parsed.program, NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);
    const auto& functionScopes =
        result.graph.functions.at(functions[0]->id());
    ASSERT_NE(findBinding(functionScopes.arguments, "arguments"), nullptr);

    std::vector<Node*> arrows =
        collect(*result.parsed.program, NodeKind::ArrowFunctionExpression);
    ASSERT_EQ(arrows.size(), 1u);
    const auto& arrowScopes = result.graph.functions.at(arrows[0]->id());
    EXPECT_EQ(findBinding(arrowScopes.arguments, "arguments"), nullptr);
}

TEST(ScopePassTest, SwitchDiscriminantIsOutsideCaseScope) {
    ScopeAnalysis result = analyze(
        "switch (function () { return x; }) { case 1: let x; }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> functions =
        collect(*result.parsed.program, NodeKind::FunctionExpression);
    ASSERT_EQ(functions.size(), 1u);
    const auto& scopes = result.graph.functions.at(functions[0]->id());
    EXPECT_EQ(scopes.closure->parent, result.graph.root.get());
}

TEST(ScopePassTest, WithBodySwitchPopsScopes) {
    ScopeAnalysis result = analyze("with (o) switch (x) { case 1: let y; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ScopePassTest, WithBodyFunctionPopsScopes) {
    ScopeAnalysis result = analyze("with (o) function f(a) { let x; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ScopePassTest, WithObjectIsOutsideWithScope) {
    ScopeAnalysis result = analyze(
        "with (function () { return x; }) { let x; }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> functions =
        collect(*result.parsed.program, NodeKind::FunctionExpression);
    ASSERT_EQ(functions.size(), 1u);
    const auto& scopes = result.graph.functions.at(functions[0]->id());
    EXPECT_EQ(scopes.closure->parent, result.graph.root.get());
}

// ---------------------------------------------------------------------------
// 重复声明 / var-lexical 冲突
// ---------------------------------------------------------------------------

TEST(ScopePassTest, DuplicateLetIsError) {
    ScopeAnalysis result = analyze("let x; let x;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, DuplicateVarIsAllowed) {
    ScopeAnalysis result = analyze("var x; var x;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ScopePassTest, VarThenLetIsError) {
    ScopeAnalysis result = analyze("var x; let x;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, LetThenVarIsError) {
    ScopeAnalysis result = analyze("let x; var x;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, NestedBlockVarConflictsWithOuterLet) {
    ScopeAnalysis result = analyze("{ let x; { var x; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, FunctionParamConflictsWithBodyLet) {
    ScopeAnalysis result = analyze("function f(a) { let a; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, FunctionParamAllowsBodyVar) {
    ScopeAnalysis result = analyze("function f(a) { var a; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ScopePassTest, ClassMethodParamConflictsWithBodyLet) {
    ScopeAnalysis result = analyze("class A { m(a) { let a; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, ObjectMethodParamConflictsWithBodyLet) {
    ScopeAnalysis result = analyze("({ m(a) { let a; } });");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, ArrowParamConflictsWithBodyLet) {
    ScopeAnalysis result = analyze("(a) => { let a; };");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, DuplicateImportIsError) {
    ScopeAnalysis result =
        analyze("import { x } from \"a\"; import { x } from \"b\";");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, ImportThenVarIsError) {
    ScopeAnalysis result = analyze("import { x } from \"a\"; var x;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, CatchParamAllowsBodyVar) {
    ScopeAnalysis result = analyze("try { } catch (e) { var e; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ScopePassTest, CatchParamConflictsWithBodyLet) {
    ScopeAnalysis result = analyze("try { } catch (e) { let e; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, ForLetConflictsWithBodyVar) {
    ScopeAnalysis result = analyze("for (let i = 0;;) { var i; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, ForVarAllowsBodyLet) {
    ScopeAnalysis result = analyze("for (var i = 0;;) { let i; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ScopePassTest, DuplicateLetAcrossSwitchCases) {
    ScopeAnalysis result =
        analyze("switch (x) { case 1: let y; case 2: let y; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, BlockFunctionIsLexicalAndGetsAnnexBVar) {
    ScopeAnalysis result = analyze("{ function f() { } }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> blocks =
        collect(*result.parsed.program, NodeKind::BlockStatement);
    ASSERT_GE(blocks.size(), 1u);  // 外层 block + 函数体 block
    ShadowScope* block = result.graph.findScope(blocks[0]->id());
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->kind, ShadowScopeKind::Block);
    const ShadowBinding* local = findBinding(block, "f");
    ASSERT_NE(local, nullptr);
    EXPECT_EQ(local->kind, ShadowBindingKind::LexicalFunction);

    // sloppy 块级函数额外创建 Annex B var 绑定。
    const ShadowBinding* hoisted = findBinding(result.graph.root.get(), "f");
    ASSERT_NE(hoisted, nullptr);
    EXPECT_EQ(hoisted->kind, ShadowBindingKind::Var);
}

TEST(ScopePassTest, StrictBlockFunctionHasNoAnnexBVar) {
    ScopeAnalysis result = analyze("\"use strict\"; { function f() { } }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    EXPECT_EQ(findBinding(result.graph.root.get(), "f"), nullptr);
}

TEST(ScopePassTest, SloppyBlockDuplicateFunctionAllowed) {
    ScopeAnalysis result =
        analyze("{ function f() { } function f() { } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ScopePassTest, StrictBlockDuplicateFunctionIsError) {
    ScopeAnalysis result =
        analyze("\"use strict\"; { function f() { } function f() { } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, BlockFunctionConflictsWithSameBlockVar) {
    ScopeAnalysis result = analyze("{ function f() { } var f; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, BlockFunctionConflictsWithSameBlockLet) {
    ScopeAnalysis result = analyze("{ function f() { } let f; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, AnnexBSkippedWhenOuterLetConflicts) {
    ScopeAnalysis result = analyze("let f; { function f() { } }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    const ShadowBinding* root = findBinding(result.graph.root.get(), "f");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->kind, ShadowBindingKind::Let);
}

TEST(ScopePassTest, AnnexBSkippedForParameterName) {
    ScopeAnalysis result =
        analyze("function g(f) { { function f() { } } }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> functions =
        collect(*result.parsed.program, NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 2u);  // 外层 g + 块级 f
    const auto& scopes = result.graph.functions.at(functions[0]->id());
    EXPECT_EQ(findBinding(scopes.body, "f"), nullptr);
}

TEST(ScopePassTest, AnnexBSkippedForArgumentsName) {
    ScopeAnalysis result =
        analyze("function g() { { function arguments() { } } }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    std::vector<Node*> functions =
        collect(*result.parsed.program, NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 2u);  // 外层 g + 块级 arguments
    const auto& scopes = result.graph.functions.at(functions[0]->id());
    const ShadowBinding* arguments = findBinding(scopes.arguments, "arguments");
    ASSERT_NE(arguments, nullptr);
    EXPECT_EQ(arguments->kind, ShadowBindingKind::Arguments);
    EXPECT_EQ(findBinding(scopes.body, "arguments"), nullptr);
}

TEST(ScopePassTest, AnnexBAppliesToCatchBlockFunction) {
    ScopeAnalysis result =
        analyze("try { throw 1; } catch (e) { function f() { } }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    const ShadowBinding* hoisted = findBinding(result.graph.root.get(), "f");
    ASSERT_NE(hoisted, nullptr);
    EXPECT_EQ(hoisted->kind, ShadowBindingKind::Var);
}

TEST(ScopePassTest, CatchFunctionConflictsWithCatchParam) {
    ScopeAnalysis result = analyze("try { } catch (e) { function e() { } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, DestructuringCatchParamRejectsVar) {
    ScopeAnalysis result = analyze("try { } catch ({ e }) { var e; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, SimpleCatchParamAllowsVarInStrict) {
    ScopeAnalysis result =
        analyze("\"use strict\"; try { } catch (e) { var e; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ScopePassTest, ScriptTopLevelDuplicateFunctionAllowed) {
    ScopeAnalysis result = analyze("function f() { } function f() { }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());

    const ShadowBinding* f = findBinding(result.graph.root.get(), "f");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->kind, ShadowBindingKind::Function);
}

TEST(ScopePassTest, FunctionBodyTopLevelDuplicateFunctionAllowedInStrict) {
    ScopeAnalysis result =
        analyze("function g() { \"use strict\"; function f() { } "
                "function f() { } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ScopePassTest, ModuleTopLevelDuplicateFunctionIsError) {
    ScopeAnalysis result =
        analyze("export {}; function f() { } function f() { }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, ModuleTopLevelFunctionIsLexical) {
    ScopeAnalysis result = analyze("export {}; function f() { }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    const ShadowBinding* f = findBinding(result.graph.root.get(), "f");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->kind, ShadowBindingKind::LexicalFunction);
}

TEST(ScopePassTest, ModuleTopLevelFunctionConflictsWithVar) {
    ScopeAnalysis result = analyze("export {}; function f() { } var f;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, ModuleBlockDuplicateFunctionIsError) {
    ScopeAnalysis result =
        analyze("export {}; { function f() { } function f() { } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, StaticBlockTopLevelDuplicateFunctionAllowed) {
    ScopeAnalysis result =
        analyze("class A { static { function f() { } function f() { } } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ScopePassTest, StaticBlockInnerBlockDuplicateFunctionIsError) {
    ScopeAnalysis result = analyze(
        "class A { static { { function f() { } function f() { } } } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

TEST(ScopePassTest, StaticBlockTopLevelFunctionIsVarLike) {
    ScopeAnalysis result =
        analyze("class A { static { function f() { } var f; } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(ScopePassTest, IfClauseFunctionGetsAnnexBVar) {
    ScopeAnalysis result = analyze("if (true) function f() { }");
    ASSERT_TRUE(result.parsed.ok());
    ASSERT_TRUE(result.errors.empty());

    const ShadowBinding* f = findBinding(result.graph.root.get(), "f");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->kind, ShadowBindingKind::Var);
}

TEST(ScopePassTest, IfClauseFunctionIsNotTopLevelVarLike) {
    // 合成 Block 中的函数与顶层 let 不冲突，且 Annex B var 被抑制。
    ScopeAnalysis result = analyze("if (true) function f() { } let f;");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());

    const ShadowBinding* f = findBinding(result.graph.root.get(), "f");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->kind, ShadowBindingKind::Let);
}

TEST(ScopePassTest, StrictIfClauseFunctionHasNoAnnexBVar) {
    ScopeAnalysis result =
        analyze("\"use strict\"; if (true) function f() { }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());

    EXPECT_EQ(findBinding(result.graph.root.get(), "f"), nullptr);
}

TEST(ScopePassTest, LabelledFunctionIsVarLikeInBlock) {
    ScopeAnalysis result = analyze("{ label: function f() { } }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_TRUE(result.errors.empty());

    const ShadowBinding* f = findBinding(result.graph.root.get(), "f");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->kind, ShadowBindingKind::Function);
}

TEST(ScopePassTest, LabelledFunctionConflictsWithBlockLet) {
    ScopeAnalysis result = analyze("{ label: function f() { } let f; }");
    ASSERT_TRUE(result.parsed.ok());
    EXPECT_FALSE(result.errors.empty());
}

