// sema/ScopePass.cpp - 作用域树与变量提升实现

#include "sema/ScopePass.h"

#include "sema/BindingNames.h"
#include "sema/Walker.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace neo::sema {

namespace {

using frontend::BlockStatement;
using frontend::CatchClause;
using frontend::ClassBody;
using frontend::ClassDeclaration;
using frontend::ClassExpression;
using frontend::FunctionDeclaration;
using frontend::FunctionExpression;
using frontend::Identifier;
using frontend::Node;
using frontend::NodeId;
using frontend::NodeKind;
using frontend::NodeList;
using frontend::NodePtr;
using frontend::Program;
using frontend::SwitchStatement;
using frontend::VariableDeclaration;
using frontend::VariableDeclarator;
using frontend::VariableKind;
using frontend::WithStatement;

bool isVarScope(ShadowScopeKind kind) {
    return kind == ShadowScopeKind::Program ||
           kind == ShadowScopeKind::FunctionBody;
}

bool isLexicalKind(ShadowBindingKind kind) {
    switch (kind) {
        case ShadowBindingKind::Let:
        case ShadowBindingKind::Const:
        case ShadowBindingKind::Class:
        case ShadowBindingKind::Import:
        case ShadowBindingKind::CatchParam:
        case ShadowBindingKind::FunctionName:
        case ShadowBindingKind::ClassName:
            return true;
        default:
            return false;
    }
}

// var 与 lexical 冲突检查时，catch 参数不构成冲突（catch (e) { var e; } 合法）。
bool isVarConflictKind(ShadowBindingKind kind) {
    return isLexicalKind(kind) && kind != ShadowBindingKind::CatchParam;
}

bool isDuplicateAllowed(ShadowBindingKind existing, ShadowBindingKind incoming) {
    if (existing == ShadowBindingKind::Param &&
        incoming == ShadowBindingKind::Param) {
        // 重复参数的严格性由 StrictnessPass 判定。
        return true;
    }
    if (incoming == ShadowBindingKind::Var &&
        (existing == ShadowBindingKind::Var ||
         existing == ShadowBindingKind::Function)) {
        return true;
    }
    if (incoming == ShadowBindingKind::Function &&
        (existing == ShadowBindingKind::Var ||
         existing == ShadowBindingKind::Function)) {
        return true;
    }
    return false;
}

struct PendingClass {
    bool hasName = false;
    BindingName name;
};

struct SwitchContext {
    NodeId switchId;
    ShadowScope* scope;
    bool pushed = false;
};

class ScopeBuilder final : public IVisitor {
public:
    explicit ScopeBuilder(SemaContext& context) : context_(context) {}

    void enter(Walker& /*walker*/, SemaContext& /*context*/,
               Node& node) override {
        activatePendingScope(node);

        switch (node.kind()) {
            case NodeKind::Program:
                enterProgram(node);
                break;
            case NodeKind::FunctionDeclaration:
                enterFunctionDeclaration(node);
                break;
            case NodeKind::FunctionExpression:
            case NodeKind::ArrowFunctionExpression:
                enterFunction(node);
                break;
            case NodeKind::BlockStatement:
                enterBlock(node);
                break;
            case NodeKind::ForStatement:
            case NodeKind::ForInStatement:
            case NodeKind::ForOfStatement:
                enterFor(node);
                break;
            case NodeKind::CatchClause:
                enterCatch(node);
                break;
            case NodeKind::SwitchStatement:
                enterSwitch(node);
                break;
            case NodeKind::SwitchCase:
                activateSwitchScope();
                break;
            case NodeKind::WithStatement:
                enterWith(node);
                break;
            case NodeKind::ClassDeclaration:
                enterClassDeclaration(node);
                break;
            case NodeKind::ClassExpression:
                enterClassExpression(node);
                break;
            case NodeKind::ClassBody:
                enterClassBody(node);
                break;
            case NodeKind::StaticBlock:
                enterStaticBlock(node);
                break;
            case NodeKind::VariableDeclaration:
                enterVariableDeclaration(node);
                break;
            case NodeKind::ImportSpecifier:
            case NodeKind::ImportDefaultSpecifier:
            case NodeKind::ImportNamespaceSpecifier:
                enterImportSpecifier(node);
                break;
            default:
                break;
        }
    }

    void leave(Walker& /*walker*/, SemaContext& /*context*/,
               Node& node) override {
        switch (node.kind()) {
            case NodeKind::SwitchStatement:
                leaveSwitch(node);
                break;
            case NodeKind::Program:
            case NodeKind::FunctionDeclaration:
            case NodeKind::FunctionExpression:
            case NodeKind::ArrowFunctionExpression:
            case NodeKind::BlockStatement:
            case NodeKind::ForStatement:
            case NodeKind::ForInStatement:
            case NodeKind::ForOfStatement:
            case NodeKind::CatchClause:
            case NodeKind::ClassBody:
            case NodeKind::StaticBlock:
                popScopes(node.id());
                break;
            default:
                // With 的作用域挂在 body 节点上，由 body 的 leave 弹出。
                popScopes(node.id());
                break;
        }
    }

    ScopeGraph takeGraph() { return std::move(graph_); }

private:
    ShadowScope* current() {
        assert(!stack_.empty());
        return stack_.back();
    }

    ShadowScope* createScope(ShadowScopeKind kind, ShadowScope* parent) {
        auto owned = std::make_unique<ShadowScope>(kind, parent);
        ShadowScope* raw = owned.get();
        if (parent) {
            parent->children.push_back(std::move(owned));
        } else {
            graph_.root = std::move(owned);
        }
        return raw;
    }

    void pushScope(NodeId id, ShadowScope* scope) {
        stack_.push_back(scope);
        pushedScopes_[id].push_back(scope);
    }

    void popScopes(NodeId id) {
        const auto it = pushedScopes_.find(id);
        if (it == pushedScopes_.end()) return;

        for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
            assert(!stack_.empty());
            assert(stack_.back() == *rit);
            stack_.pop_back();
        }
        pushedScopes_.erase(it);
    }

    void mapScope(NodeId id, ShadowScope* scope) { graph_.scopes[id] = scope; }

    void activatePendingScope(Node& node) {
        const auto body = bodyScopes_.find(node.id());
        if (body != bodyScopes_.end()) {
            pushScope(node.id(), body->second);
            mapScope(node.id(), body->second);
            bodyScopes_.erase(body);
        }

        const auto with = withBodyScopes_.find(node.id());
        if (with != withBodyScopes_.end()) {
            pushScope(node.id(), with->second);
            withBodyScopes_.erase(with);
        }
    }

    void activateSwitchScope() {
        if (switchContexts_.empty()) return;
        SwitchContext& context = switchContexts_.back();
        if (context.pushed) return;
        pushScope(context.switchId, context.scope);
        context.pushed = true;
    }

    void enterProgram(Node& node) {
        auto& program = static_cast<Program&>(node);
        ShadowScope* scope = createScope(ShadowScopeKind::Program, nullptr);
        pushScope(program.id(), scope);
        mapScope(program.id(), scope);
    }

    void enterFunctionDeclaration(Node& node) {
        auto& function = static_cast<FunctionDeclaration&>(node);
        if (function.id && function.id->kind() == NodeKind::Identifier) {
            const auto& identifier =
                static_cast<const Identifier&>(*function.id);
            declareFunction(BindingName{identifier.name, identifier.range()});
        }
        enterFunction(node);
    }

    void enterFunction(Node& node) {
        Node* id = nullptr;
        NodeList* params = nullptr;
        Node* body = nullptr;
        bool arrow = false;
        bool expression = false;

        switch (node.kind()) {
            case NodeKind::FunctionDeclaration: {
                auto& function = static_cast<FunctionDeclaration&>(node);
                id = function.id.get();
                params = &function.params;
                body = function.body.get();
                break;
            }
            case NodeKind::FunctionExpression: {
                auto& function = static_cast<FunctionExpression&>(node);
                id = function.id.get();
                params = &function.params;
                body = function.body.get();
                expression = true;
                break;
            }
            case NodeKind::ArrowFunctionExpression: {
                auto& function =
                    static_cast<frontend::ArrowFunctionExpression&>(node);
                params = &function.params;
                body = function.body.get();
                arrow = true;
                expression = true;
                break;
            }
            default:
                return;
        }

        ShadowScope* closure =
            createScope(ShadowScopeKind::FunctionClosure, current());
        ShadowScope* arguments =
            createScope(ShadowScopeKind::FunctionArguments, closure);
        ShadowScope* functionBody =
            createScope(ShadowScopeKind::FunctionBody, arguments);

        graph_.functions[node.id()] = FunctionScopes{closure, arguments,
                                                     functionBody};
        mapScope(node.id(), functionBody);
        // 参数在 arguments 作用域中求值；body 节点进入时再切到 functionBody。
        pushScope(node.id(), arguments);

        if (expression && id && id->kind() == NodeKind::Identifier) {
            const auto& identifier = static_cast<const Identifier&>(*id);
            declareBinding(closure,
                           BindingName{identifier.name, identifier.range()},
                           ShadowBindingKind::FunctionName, false, true);
        }

        if (params) {
            for (NodePtr& param : *params) {
                std::vector<BindingName> names;
                bool nonSimple = false;
                collectBindingNames(param.get(), names, nonSimple);
                for (const BindingName& name : names) {
                    declareBinding(arguments, name, ShadowBindingKind::Param,
                                   true, true);
                }
            }
        }

        if (!arrow && arguments->findLocal("arguments") == nullptr) {
            const auto range = node.range();
            declareBinding(arguments, BindingName{"arguments", range},
                           ShadowBindingKind::Arguments, true, true);
        }

        if (body) {
            bodyScopes_[body->id()] = functionBody;
            if (body->kind() == NodeKind::BlockStatement) {
                reuseBlocks_.insert(body->id());
            }
        }
    }

    void enterBlock(Node& node) {
        if (reuseBlocks_.erase(node.id()) != 0) {
            mapScope(node.id(), current());
            return;
        }
        ShadowScope* scope = createScope(ShadowScopeKind::Block, current());
        pushScope(node.id(), scope);
        mapScope(node.id(), scope);
    }

    void enterFor(Node& node) {
        ShadowScope* scope = createScope(ShadowScopeKind::For, current());
        pushScope(node.id(), scope);
        mapScope(node.id(), scope);
    }

    void enterCatch(Node& node) {
        auto& clause = static_cast<CatchClause&>(node);
        ShadowScope* scope = createScope(ShadowScopeKind::Catch, current());
        pushScope(node.id(), scope);
        mapScope(node.id(), scope);

        std::vector<BindingName> names;
        bool nonSimple = false;
        collectBindingNames(clause.param.get(), names, nonSimple);
        for (const BindingName& name : names) {
            declareBinding(scope, name, ShadowBindingKind::CatchParam, true,
                           true);
        }

        if (clause.body && clause.body->kind() == NodeKind::BlockStatement) {
            reuseBlocks_.insert(clause.body->id());
        }
    }

    void enterSwitch(Node& node) {
        auto& statement = static_cast<SwitchStatement&>(node);
        ShadowScope* scope = createScope(ShadowScopeKind::Block, current());
        mapScope(statement.id(), scope);
        switchContexts_.push_back(SwitchContext{statement.id(), scope, false});
    }

    void leaveSwitch(Node& node) {
        if (!switchContexts_.empty()) {
            SwitchContext context = switchContexts_.back();
            switchContexts_.pop_back();
            if (context.pushed) {
                popScopes(context.switchId);
            }
        }
        // switch 自身也可能作为 with 的 body，需要弹出挂在它上面的 With 作用域。
        popScopes(node.id());
    }

    void enterWith(Node& node) {
        auto& statement = static_cast<WithStatement&>(node);
        ShadowScope* scope = createScope(ShadowScopeKind::With, current());
        mapScope(statement.id(), scope);
        if (statement.body) {
            withBodyScopes_[statement.body->id()] = scope;
        }
    }

    void enterClassDeclaration(Node& node) {
        auto& declaration = static_cast<ClassDeclaration&>(node);
        if (declaration.id && declaration.id->kind() == NodeKind::Identifier) {
            const auto& identifier =
                static_cast<const Identifier&>(*declaration.id);
            declareBinding(current(),
                           BindingName{identifier.name, identifier.range()},
                           ShadowBindingKind::Class, true, false);
            pendingClasses_.push_back(PendingClass{
                true, BindingName{identifier.name, identifier.range()}});
        } else {
            pendingClasses_.push_back(PendingClass{});
        }
    }

    void enterClassExpression(Node& node) {
        auto& expression = static_cast<ClassExpression&>(node);
        if (expression.id && expression.id->kind() == NodeKind::Identifier) {
            const auto& identifier =
                static_cast<const Identifier&>(*expression.id);
            pendingClasses_.push_back(PendingClass{
                true, BindingName{identifier.name, identifier.range()}});
        } else {
            pendingClasses_.push_back(PendingClass{});
        }
    }

    void enterClassBody(Node& node) {
        ShadowScope* scope = createScope(ShadowScopeKind::Class, current());
        pushScope(node.id(), scope);
        mapScope(node.id(), scope);

        if (!pendingClasses_.empty()) {
            PendingClass pending = std::move(pendingClasses_.back());
            pendingClasses_.pop_back();
            if (pending.hasName) {
                declareBinding(scope, pending.name,
                               ShadowBindingKind::ClassName, false, true);
            }
        }
    }

    void enterStaticBlock(Node& node) {
        ShadowScope* scope =
            createScope(ShadowScopeKind::FunctionBody, current());
        pushScope(node.id(), scope);
        mapScope(node.id(), scope);
        graph_.functions[node.id()] = FunctionScopes{nullptr, nullptr, scope};
    }

    void enterVariableDeclaration(Node& node) {
        auto& declaration = static_cast<VariableDeclaration&>(node);
        for (NodePtr& declarator : declaration.declarations) {
            if (!declarator) continue;
            auto& entry = static_cast<VariableDeclarator&>(*declarator);

            std::vector<BindingName> names;
            bool nonSimple = false;
            collectBindingNames(entry.id.get(), names, nonSimple);

            for (const BindingName& name : names) {
                switch (declaration.variableKind) {
                    case VariableKind::Var:
                        declareVar(name);
                        break;
                    case VariableKind::Let:
                        declareBinding(current(), name, ShadowBindingKind::Let,
                                       true, false);
                        break;
                    case VariableKind::Const:
                        declareBinding(current(), name,
                                       ShadowBindingKind::Const, false, false);
                        break;
                }
            }
        }
    }

    void enterImportSpecifier(Node& node) {
        Node* local = nullptr;
        switch (node.kind()) {
            case NodeKind::ImportSpecifier:
                local =
                    static_cast<frontend::ImportSpecifier&>(node).local.get();
                break;
            case NodeKind::ImportDefaultSpecifier:
                local = static_cast<frontend::ImportDefaultSpecifier&>(node)
                            .local.get();
                break;
            case NodeKind::ImportNamespaceSpecifier:
                local = static_cast<frontend::ImportNamespaceSpecifier&>(node)
                            .local.get();
                break;
            default:
                return;
        }

        std::vector<BindingName> names;
        bool nonSimple = false;
        collectBindingNames(local, names, nonSimple);
        for (const BindingName& name : names) {
            declareBinding(current(), name, ShadowBindingKind::Import, false,
                           true);
        }
    }

    ShadowScope* varScope() {
        for (ShadowScope* scope = current(); scope != nullptr;
             scope = scope->parent) {
            if (isVarScope(scope->kind)) return scope;
        }
        return current();
    }

    void declareVar(const BindingName& binding) {
        ShadowScope* target = varScope();

        for (ShadowScope* scope = current(); scope != nullptr;
             scope = scope->parent) {
            const auto it = scope->bindings.find(binding.name);
            if (it != scope->bindings.end() &&
                isVarConflictKind(it->second.kind)) {
                duplicateError(binding);
                return;
            }
            if (scope == target) break;
        }

        for (ShadowScope* scope = current(); scope != nullptr;
             scope = scope->parent) {
            scope->varNames.insert(binding.name);
            if (scope == target) break;
        }

        declareBinding(target, binding, ShadowBindingKind::Var, true, true);
    }

    void declareFunction(const BindingName& binding) {
        declareBinding(current(), binding, ShadowBindingKind::Function, true,
                       true);
    }

    void declareBinding(ShadowScope* scope, const BindingName& binding,
                        ShadowBindingKind kind, bool mutableBinding,
                        bool initialized) {
        const auto it = scope->bindings.find(binding.name);
        if (it != scope->bindings.end()) {
            if (isDuplicateAllowed(it->second.kind, kind)) {
                if (kind == ShadowBindingKind::Function) {
                    it->second.kind = ShadowBindingKind::Function;
                    it->second.initialized = true;
                }
                return;
            }
            duplicateError(binding);
            return;
        }

        if (isLexicalKind(kind) &&
            scope->varNames.count(binding.name) != 0) {
            duplicateError(binding);
            return;
        }

        // 函数体顶层的 lexical 声明不能与参数同名。
        if (isLexicalKind(kind) &&
            scope->kind == ShadowScopeKind::FunctionBody && scope->parent &&
            scope->parent->kind == ShadowScopeKind::FunctionArguments) {
            const auto param = scope->parent->bindings.find(binding.name);
            if (param != scope->parent->bindings.end() &&
                param->second.kind == ShadowBindingKind::Param) {
                duplicateError(binding);
                return;
            }
        }

        scope->bindings.emplace(
            binding.name,
            ShadowBinding{binding.name, kind, mutableBinding, initialized,
                          binding.range});
    }

    void duplicateError(const BindingName& binding) {
        context_.error(binding.range.start,
                       "Identifier '" + binding.name +
                           "' has already been declared");
    }

    ScopeGraph graph_;
    std::vector<ShadowScope*> stack_;
    std::unordered_map<NodeId, std::vector<ShadowScope*>> pushedScopes_;
    std::unordered_set<NodeId> reuseBlocks_;
    std::unordered_map<NodeId, ShadowScope*> bodyScopes_;
    std::unordered_map<NodeId, ShadowScope*> withBodyScopes_;
    std::vector<SwitchContext> switchContexts_;
    std::vector<PendingClass> pendingClasses_;
    SemaContext& context_;
};

}  // namespace

ScopeGraph runScopePass(frontend::Node& program, SemaContext& context) {
    // ScopePass 不使用标注；Walker 需要一个局部 AnnotationStore。
    AnnotationStore annotations;
    auto builder = std::make_unique<ScopeBuilder>(context);
    ScopeBuilder* raw = builder.get();

    Walker walker(annotations);
    walker.addAll(std::move(builder));
    walker.walk(program, context);

    return raw->takeGraph();
}

}  // namespace neo::sema
