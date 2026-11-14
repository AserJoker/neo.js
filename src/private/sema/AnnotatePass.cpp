// sema/AnnotatePass.cpp - 函数级标注汇总实现

#include "sema/AnnotatePass.h"

#include "sema/ResolvePass.h"
#include "sema/Walker.h"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace neo::sema {

namespace {

using frontend::ArrowFunctionExpression;
using frontend::ClassDeclaration;
using frontend::ClassExpression;
using frontend::FunctionDeclaration;
using frontend::FunctionExpression;
using frontend::MethodDefinition;
using frontend::MethodKind;
using frontend::Node;
using frontend::NodeId;
using frontend::NodeKind;
using frontend::Property;
using frontend::StaticBlock;

struct MethodInfo {
    MethodKind kind;
    bool derived;
};

class AnnotateVisitor final : public IVisitor {
public:
    explicit AnnotateVisitor(AnnotationStore& annotations)
        : annotations_(annotations) {}

    void enter(Walker& /*walker*/, SemaContext& /*context*/,
               Node& node) override {
        switch (node.kind()) {
            case NodeKind::ClassDeclaration:
            case NodeKind::ClassExpression:
                enterClass(node);
                break;
            case NodeKind::MethodDefinition:
                enterMethod(node);
                break;
            case NodeKind::Property:
                enterObjectMethod(node);
                break;
            case NodeKind::FunctionDeclaration:
                annotateFunctionDeclaration(node);
                break;
            case NodeKind::FunctionExpression:
                annotateFunctionExpression(node);
                break;
            case NodeKind::ArrowFunctionExpression:
                annotateArrow(node);
                break;
            case NodeKind::StaticBlock:
                annotateStaticBlock(node);
                break;
            default:
                break;
        }
    }

    void leave(Walker& /*walker*/, SemaContext& /*context*/,
               Node& node) override {
        if (node.kind() == NodeKind::ClassDeclaration ||
            node.kind() == NodeKind::ClassExpression) {
            classDerived_.pop_back();
        }
    }

private:
    void enterClass(Node& node) {
        const bool derived =
            node.kind() == NodeKind::ClassDeclaration
                ? static_cast<ClassDeclaration&>(node).superClass != nullptr
                : static_cast<ClassExpression&>(node).superClass != nullptr;
        classDerived_.push_back(derived);
    }

    void enterMethod(Node& node) {
        auto& method = static_cast<MethodDefinition&>(node);
        if (!method.value ||
            method.value->kind() != NodeKind::FunctionExpression) {
            return;
        }
        const bool derived =
            !classDerived_.empty() && classDerived_.back();
        methodInfo_[method.value->id()] = MethodInfo{method.methodKind,
                                                     derived};
    }

    void enterObjectMethod(Node& node) {
        auto& property = static_cast<Property&>(node);
        if (!property.method || !property.value ||
            property.value->kind() != NodeKind::FunctionExpression) {
            return;
        }
        methodInfo_[property.value->id()] =
            MethodInfo{MethodKind::Method, false};
    }

    FunctionInfo& functionInfo(NodeId id) {
        return annotations_.emplace<FunctionInfo>(id);
    }

    void copyHasDirectEval(NodeId id, FunctionInfo& info) {
        const FunctionFlags* flags = annotations_.get<FunctionFlags>(id);
        info.hasDirectEval = flags != nullptr && flags->hasDirectEval;
    }

    void annotateFunctionDeclaration(Node& node) {
        auto& function = static_cast<FunctionDeclaration&>(node);
        FunctionInfo& info = functionInfo(node.id());
        info.isAsync = function.async;
        info.isGenerator = function.generator;
        copyHasDirectEval(node.id(), info);
    }

    void annotateFunctionExpression(Node& node) {
        auto& function = static_cast<FunctionExpression&>(node);
        FunctionInfo& info = functionInfo(node.id());
        info.isAsync = function.async;
        info.isGenerator = function.generator;

        const auto method = methodInfo_.find(node.id());
        if (method != methodInfo_.end()) {
            info.isMethod = true;
            info.isConstructor =
                method->second.kind == MethodKind::Constructor;
            info.isDerivedConstructor =
                info.isConstructor && method->second.derived;
        }
        copyHasDirectEval(node.id(), info);
    }

    void annotateArrow(Node& node) {
        auto& function = static_cast<ArrowFunctionExpression&>(node);
        FunctionInfo& info = functionInfo(node.id());
        info.isArrow = true;
        info.isAsync = function.async;
        copyHasDirectEval(node.id(), info);
    }

    void annotateStaticBlock(Node& node) {
        FunctionInfo& info = functionInfo(node.id());
        info.isStaticBlock = true;
        copyHasDirectEval(node.id(), info);
    }

    AnnotationStore& annotations_;
    std::vector<bool> classDerived_;
    std::unordered_map<NodeId, MethodInfo> methodInfo_;
};

}  // namespace

void runAnnotatePass(frontend::Node& program, SemaContext& context,
                     AnnotationStore& annotations) {
    Walker walker(annotations);
    walker.addAll(std::make_unique<AnnotateVisitor>(annotations));
    walker.walk(program, context);
}

}  // namespace neo::sema
