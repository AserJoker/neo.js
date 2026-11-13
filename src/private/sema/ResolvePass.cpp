// sema/ResolvePass.cpp - 标识符解析与闭包穿透捕获实现

#include "sema/ResolvePass.h"

#include "sema/Walker.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace neo::sema {

namespace {

using frontend::ArrayPattern;
using frontend::ArrowFunctionExpression;
using frontend::AssignmentPattern;
using frontend::CallExpression;
using frontend::CatchClause;
using frontend::ClassDeclaration;
using frontend::ClassExpression;
using frontend::ExportAllDeclaration;
using frontend::ExportNamedDeclaration;
using frontend::ExportSpecifier;
using frontend::FunctionDeclaration;
using frontend::FunctionExpression;
using frontend::Identifier;
using frontend::ImportAttribute;
using frontend::ImportDefaultSpecifier;
using frontend::ImportNamespaceSpecifier;
using frontend::ImportSpecifier;
using frontend::MemberExpression;
using frontend::MetaProperty;
using frontend::MethodDefinition;
using frontend::Node;
using frontend::NodeId;
using frontend::NodeKind;
using frontend::NodeList;
using frontend::NodePtr;
using frontend::ObjectPattern;
using frontend::Program;
using frontend::Property;
using frontend::PropertyDefinition;
using frontend::RestElement;
using frontend::SourceType;
using frontend::VariableDeclarator;

class ResolveVisitor final : public IVisitor {
public:
    ResolveVisitor(Program& program, ScopeGraph& graph,
                   AnnotationStore& annotations)
        : graph_(graph), annotations_(annotations), programId_(program.id()),
          module_(program.sourceType == SourceType::Module) {
        buildScopeOwners();
    }

    void enter(Walker& /*walker*/, SemaContext& /*context*/,
               Node& node) override {
        // 字段初始化器在构造期执行，是独立捕获边界；但计算属性 key 在类
        // 定义期求值，不属于该边界，因此只在 value 子树激活。
        const auto field = fieldOwnerForValue_.find(node.id());
        if (field != fieldOwnerForValue_.end()) {
            pushOwner(field->second);
        }

        switch (node.kind()) {
            case NodeKind::Program:
                pushOwner(node.id());
                break;
            case NodeKind::FunctionDeclaration:
            case NodeKind::FunctionExpression:
            case NodeKind::ArrowFunctionExpression:
                markFunctionBindings(node);
                pushOwner(node.id());
                break;
            case NodeKind::StaticBlock:
                // static block 体按规范是独立函数对象，构成捕获边界。
                pushOwner(node.id());
                break;
            case NodeKind::PropertyDefinition: {
                auto& definition = static_cast<PropertyDefinition&>(node);
                if (!definition.computed) {
                    markNonReference(definition.key.get());
                }
                if (definition.value) {
                    fieldOwnerForValue_[definition.value->id()] = node.id();
                }
                break;
            }
            case NodeKind::MethodDefinition: {
                auto& definition = static_cast<MethodDefinition&>(node);
                if (!definition.computed) {
                    markNonReference(definition.key.get());
                }
                break;
            }
            case NodeKind::Property: {
                auto& property = static_cast<Property&>(node);
                if (!property.computed) {
                    markNonReference(property.key.get());
                }
                break;
            }
            case NodeKind::MemberExpression: {
                auto& member = static_cast<MemberExpression&>(node);
                if (!member.computed) {
                    markNonReference(member.property.get());
                }
                break;
            }
            case NodeKind::VariableDeclarator: {
                auto& declarator = static_cast<VariableDeclarator&>(node);
                markBindingPattern(declarator.id.get());
                break;
            }
            case NodeKind::CatchClause: {
                auto& clause = static_cast<CatchClause&>(node);
                markBindingPattern(clause.param.get());
                break;
            }
            case NodeKind::ClassDeclaration: {
                auto& declaration = static_cast<ClassDeclaration&>(node);
                markNonReference(declaration.id.get());
                break;
            }
            case NodeKind::ClassExpression: {
                auto& expression = static_cast<ClassExpression&>(node);
                markNonReference(expression.id.get());
                break;
            }
            case NodeKind::ImportSpecifier: {
                auto& specifier = static_cast<ImportSpecifier&>(node);
                markNonReference(specifier.imported.get());
                markNonReference(specifier.local.get());
                break;
            }
            case NodeKind::ImportDefaultSpecifier: {
                auto& specifier =
                    static_cast<ImportDefaultSpecifier&>(node);
                markNonReference(specifier.local.get());
                break;
            }
            case NodeKind::ImportNamespaceSpecifier: {
                auto& specifier =
                    static_cast<ImportNamespaceSpecifier&>(node);
                markNonReference(specifier.local.get());
                break;
            }
            case NodeKind::ImportAttribute: {
                auto& attribute = static_cast<ImportAttribute&>(node);
                markNonReference(attribute.key.get());
                break;
            }
            case NodeKind::ExportSpecifier: {
                auto& specifier = static_cast<ExportSpecifier&>(node);
                markNonReference(specifier.exported.get());
                break;
            }
            case NodeKind::ExportNamedDeclaration: {
                auto& declaration =
                    static_cast<ExportNamedDeclaration&>(node);
                // export { x } from "m"：local 指向源模块导出名，不是本地引用。
                if (declaration.source) {
                    for (NodePtr& specifier : declaration.specifiers) {
                        if (!specifier ||
                            specifier->kind() != NodeKind::ExportSpecifier) {
                            continue;
                        }
                        auto& entry =
                            static_cast<ExportSpecifier&>(*specifier);
                        markNonReference(entry.local.get());
                    }
                }
                break;
            }
            case NodeKind::ExportAllDeclaration: {
                auto& declaration = static_cast<ExportAllDeclaration&>(node);
                markNonReference(declaration.exported.get());
                break;
            }
            case NodeKind::MetaProperty: {
                auto& property = static_cast<MetaProperty&>(node);
                markNonReference(property.meta.get());
                markNonReference(property.property.get());
                break;
            }
            case NodeKind::CallExpression:
                checkDirectEval(node);
                break;
            case NodeKind::Identifier:
                resolve(static_cast<Identifier&>(node));
                break;
            default:
                break;
        }
    }

    void leave(Walker& /*walker*/, SemaContext& /*context*/,
               Node& node) override {
        switch (node.kind()) {
            case NodeKind::Program:
            case NodeKind::FunctionDeclaration:
            case NodeKind::FunctionExpression:
            case NodeKind::ArrowFunctionExpression:
            case NodeKind::StaticBlock:
                popOwner();
                break;
            default:
                break;
        }

        if (fieldOwnerForValue_.count(node.id()) != 0) {
            popOwner();
        }
    }

    void finish() {
        for (auto& entry : captures_) {
            if (entry.first == programId_) continue;
            std::vector<std::string> names(entry.second.begin(),
                                           entry.second.end());
            std::sort(names.begin(), names.end());
            annotations_.emplace<FunctionCaptures>(entry.first).names =
                std::move(names);
        }
    }

private:
    void buildScopeOwners() {
        if (!graph_.root) return;
        assignScopeOwner(graph_.root.get(), programId_);
    }

    void assignScopeOwner(ShadowScope* scope, NodeId owner) {
        if (scope->kind == ShadowScopeKind::FunctionClosure ||
            scope->kind == ShadowScopeKind::StaticBlock) {
            const auto it = graph_.closureOwners.find(scope);
            if (it != graph_.closureOwners.end()) {
                owner = it->second;
            }
        }
        scopeOwner_[scope] = owner;
        for (auto& child : scope->children) {
            assignScopeOwner(child.get(), owner);
        }
    }

    void pushOwner(NodeId owner) { ownerStack_.push_back(owner); }

    void popOwner() {
        assert(!ownerStack_.empty());
        ownerStack_.pop_back();
    }

    void markNonReference(Node* node) {
        if (node && node->kind() == NodeKind::Identifier) {
            nonReference_.insert(node->id());
        }
    }

    void markFunctionBindings(Node& node) {
        Node* id = nullptr;
        NodeList* params = nullptr;
        switch (node.kind()) {
            case NodeKind::FunctionDeclaration: {
                auto& function = static_cast<FunctionDeclaration&>(node);
                id = function.id.get();
                params = &function.params;
                break;
            }
            case NodeKind::FunctionExpression: {
                auto& function = static_cast<FunctionExpression&>(node);
                id = function.id.get();
                params = &function.params;
                break;
            }
            case NodeKind::ArrowFunctionExpression: {
                auto& function = static_cast<ArrowFunctionExpression&>(node);
                params = &function.params;
                break;
            }
            default:
                return;
        }

        markNonReference(id);
        if (params) {
            for (NodePtr& param : *params) {
                markBindingPattern(param.get());
            }
        }
    }

    // 绑定模式中的 Identifier 是声明而非引用；默认值右侧仍是引用。
    void markBindingPattern(Node* node) {
        if (!node) return;
        switch (node->kind()) {
            case NodeKind::Identifier:
                nonReference_.insert(node->id());
                break;
            case NodeKind::ObjectPattern: {
                auto& pattern = static_cast<ObjectPattern&>(*node);
                for (NodePtr& property : pattern.properties) {
                    if (!property) continue;
                    if (property->kind() == NodeKind::Property) {
                        auto& entry = static_cast<Property&>(*property);
                        if (!entry.computed) {
                            markNonReference(entry.key.get());
                        }
                        markBindingPattern(entry.value.get());
                    } else if (property->kind() == NodeKind::RestElement) {
                        auto& rest = static_cast<RestElement&>(*property);
                        markBindingPattern(rest.argument.get());
                    }
                }
                break;
            }
            case NodeKind::ArrayPattern: {
                auto& pattern = static_cast<ArrayPattern&>(*node);
                for (NodePtr& element : pattern.elements) {
                    markBindingPattern(element.get());
                }
                break;
            }
            case NodeKind::AssignmentPattern: {
                auto& pattern = static_cast<AssignmentPattern&>(*node);
                markBindingPattern(pattern.left.get());
                break;
            }
            case NodeKind::RestElement: {
                auto& rest = static_cast<RestElement&>(*node);
                markBindingPattern(rest.argument.get());
                break;
            }
            default:
                break;
        }
    }

    void checkDirectEval(Node& node) {
        auto& call = static_cast<CallExpression&>(node);
        if (!call.callee || call.callee->kind() != NodeKind::Identifier) {
            return;
        }
        const auto& callee = static_cast<Identifier&>(*call.callee);
        if (callee.name != "eval") return;

        assert(!ownerStack_.empty());
        annotations_.emplace<FunctionFlags>(ownerStack_.back()).hasDirectEval =
            true;
    }

    void resolve(Identifier& identifier) {
        if (nonReference_.count(identifier.id()) != 0) return;

        const auto nodeScope = graph_.nodeScopes.find(identifier.id());
        if (nodeScope == graph_.nodeScopes.end() || !nodeScope->second) {
            return;
        }

        ShadowScope* scope = nodeScope->second;
        ShadowScope* found = nullptr;
        for (ShadowScope* current = scope; current != nullptr;
             current = current->parent) {
            if (current->bindings.find(identifier.name) !=
                current->bindings.end()) {
                found = current;
                break;
            }
        }

        if (!found) {
            const BindingLocation location =
                module_ ? BindingLocation::Unresolved
                        : BindingLocation::Global;
            annotations_.emplace<IdentifierBinding>(
                identifier.id(), nullptr, nullptr, location);
            return;
        }

        ShadowBinding& binding = found->bindings.at(identifier.name);
        assert(!ownerStack_.empty());
        const NodeId currentOwner = ownerStack_.back();
        const NodeId bindingOwner = scopeOwner(found);
        const BindingLocation location =
            currentOwner == bindingOwner ? BindingLocation::Local
                                         : BindingLocation::Captured;

        annotations_.emplace<IdentifierBinding>(identifier.id(), found,
                                                &binding, location);

        if (location == BindingLocation::Captured) {
            registerCaptures(identifier.name, bindingOwner);
        }
    }

    NodeId scopeOwner(ShadowScope* scope) const {
        const auto it = scopeOwner_.find(scope);
        return it == scopeOwner_.end() ? programId_ : it->second;
    }

    void registerCaptures(const std::string& name, NodeId bindingOwner) {
        for (auto it = ownerStack_.rbegin(); it != ownerStack_.rend(); ++it) {
            if (*it == bindingOwner) break;
            if (*it == programId_) continue;
            captures_[*it].insert(name);
        }
    }

    ScopeGraph& graph_;
    AnnotationStore& annotations_;
    NodeId programId_;
    bool module_;
    std::unordered_map<ShadowScope*, NodeId> scopeOwner_;
    std::unordered_map<NodeId, NodeId> fieldOwnerForValue_;
    std::unordered_set<NodeId> nonReference_;
    std::unordered_map<NodeId, std::unordered_set<std::string>> captures_;
    std::vector<NodeId> ownerStack_;
};

}  // namespace

void runResolvePass(frontend::Node& program, ScopeGraph& graph,
                    SemaContext& context, AnnotationStore& annotations) {
    auto& programNode = static_cast<Program&>(program);
    auto visitor = std::make_unique<ResolveVisitor>(programNode, graph,
                                                    annotations);
    ResolveVisitor* raw = visitor.get();

    Walker walker(annotations);
    walker.addAll(std::move(visitor));
    walker.walk(program, context);

    raw->finish();
}

}  // namespace neo::sema
