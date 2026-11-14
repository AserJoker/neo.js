// tests/unit/sema/FunctionInfoTest.cpp - 函数级标注测试（ControlFlowPass 产出）

#include "frontend/Parser.h"
#include "sema/ControlFlowPass.h"
#include "sema/AnnotationStore.h"
#include "sema/ResolvePass.h"
#include "sema/ScopePass.h"
#include "sema/SemaContext.h"
#include "sema/Walker.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <vector>

namespace {

using neo::frontend::Node;
using neo::frontend::NodeKind;
using neo::frontend::ParseResult;
using neo::frontend::Parser;
using neo::frontend::ParserOptions;
using neo::frontend::SourceType;
using neo::sema::AnnotationStore;
using neo::sema::FunctionInfo;
using neo::sema::IVisitor;
using neo::sema::ScopeGraph;
using neo::sema::SemaContext;
using neo::sema::Walker;
using neo::sema::runControlFlowPass;
using neo::sema::runResolvePass;
using neo::sema::runScopePass;

struct Analysis {
    ParseResult parsed;
    AnnotationStore annotations;
};

Analysis analyze(std::string_view source) {
    Parser parser(source, ParserOptions{SourceType::Script});
    Analysis result;
    result.parsed = parser.parseProgram();
    if (!result.parsed.program) return result;

    SemaContext context;
    ScopeGraph graph = runScopePass(*result.parsed.program, context);
    runResolvePass(*result.parsed.program, graph, context, result.annotations);
    runControlFlowPass(*result.parsed.program, context, result.annotations);
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

const FunctionInfo* infoOf(Analysis& analysis, Node* node) {
    return analysis.annotations.get<FunctionInfo>(node->id());
}

}  // namespace

TEST(FunctionInfoTest, FunctionDeclarationFlags) {
    Analysis result = analyze("async function* f() { }");
    ASSERT_TRUE(result.parsed.ok());

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);
    const FunctionInfo* info = infoOf(result, functions[0]);
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->isAsync);
    EXPECT_TRUE(info->isGenerator);
    EXPECT_FALSE(info->isArrow);
    EXPECT_FALSE(info->isMethod);
    EXPECT_FALSE(info->hasDirectEval);
}

TEST(FunctionInfoTest, ArrowFunctionFlags) {
    Analysis result = analyze("const f = async () => 1;");
    ASSERT_TRUE(result.parsed.ok());

    auto arrows = collect(*result.parsed.program,
                          NodeKind::ArrowFunctionExpression);
    ASSERT_EQ(arrows.size(), 1u);
    const FunctionInfo* info = infoOf(result, arrows[0]);
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->isArrow);
    EXPECT_TRUE(info->isAsync);
    EXPECT_FALSE(info->isGenerator);
}

TEST(FunctionInfoTest, ClassMethodFlags) {
    Analysis result = analyze("class C { m() { } get g() { } }");
    ASSERT_TRUE(result.parsed.ok());

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionExpression);
    ASSERT_EQ(functions.size(), 2u);
    for (Node* function : functions) {
        const FunctionInfo* info = infoOf(result, function);
        ASSERT_NE(info, nullptr);
        EXPECT_TRUE(info->isMethod);
        EXPECT_FALSE(info->isConstructor);
    }
}

TEST(FunctionInfoTest, DerivedConstructorFlags) {
    Analysis result = analyze("class C extends B { constructor() { } }");
    ASSERT_TRUE(result.parsed.ok());

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionExpression);
    ASSERT_EQ(functions.size(), 1u);
    const FunctionInfo* info = infoOf(result, functions[0]);
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->isMethod);
    EXPECT_TRUE(info->isConstructor);
    EXPECT_TRUE(info->isDerivedConstructor);
}

TEST(FunctionInfoTest, BaseConstructorIsNotDerived) {
    Analysis result = analyze("class C { constructor() { } }");
    ASSERT_TRUE(result.parsed.ok());

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionExpression);
    ASSERT_EQ(functions.size(), 1u);
    const FunctionInfo* info = infoOf(result, functions[0]);
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->isConstructor);
    EXPECT_FALSE(info->isDerivedConstructor);
}

TEST(FunctionInfoTest, ObjectMethodFlags) {
    Analysis result = analyze("({ m() { } });");
    ASSERT_TRUE(result.parsed.ok());

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionExpression);
    ASSERT_EQ(functions.size(), 1u);
    const FunctionInfo* info = infoOf(result, functions[0]);
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->isMethod);
    EXPECT_FALSE(info->isConstructor);
}

TEST(FunctionInfoTest, StaticBlockFlags) {
    Analysis result = analyze("class C { static { } }");
    ASSERT_TRUE(result.parsed.ok());

    auto blocks = collect(*result.parsed.program, NodeKind::StaticBlock);
    ASSERT_EQ(blocks.size(), 1u);
    const FunctionInfo* info = infoOf(result, blocks[0]);
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->isStaticBlock);
}

TEST(FunctionInfoTest, DirectEvalFlagIsCopied) {
    Analysis result = analyze("function f() { eval(\"x\"); }");
    ASSERT_TRUE(result.parsed.ok());

    auto functions = collect(*result.parsed.program,
                             NodeKind::FunctionDeclaration);
    ASSERT_EQ(functions.size(), 1u);
    const FunctionInfo* info = infoOf(result, functions[0]);
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->hasDirectEval);
}
