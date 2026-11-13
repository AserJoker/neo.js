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
using frontend::Directive;
using frontend::FunctionDeclaration;
using frontend::FunctionExpression;
using frontend::Identifier;
using frontend::IfStatement;
using frontend::LabeledStatement;
using frontend::Node;
using frontend::NodeId;
using frontend::NodeKind;
using frontend::NodeList;
using frontend::NodePtr;
using frontend::Program;
using frontend::SourceType;
using frontend::SwitchStatement;
using frontend::VariableDeclaration;
using frontend::VariableDeclarator;
using frontend::VariableKind;
using frontend::WithStatement;

bool isVarScope(ShadowScopeKind kind) {
    return kind == ShadowScopeKind::Program ||
           kind == ShadowScopeKind::FunctionBody ||
           kind == ShadowScopeKind::StaticBlock;
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
        case ShadowBindingKind::LexicalFunction:
            return true;
        default:
            return false;
    }
}

// var 与同名 lexical 绑定冲突。
// catch 参数仅在简单 BindingIdentifier 时豁免（Annex B B.3.4）。
bool varConflictsWith(const ShadowScope& scope, ShadowBindingKind kind) {
    if (kind == ShadowBindingKind::CatchParam) {
        return !scope.simpleCatchParameter;
    }
    return isLexicalKind(kind);
}

bool hasUseStrictDirective(const NodeList& body) {
    for (const NodePtr& statement : body) {
        if (!statement || statement->kind() != NodeKind::Directive) break;
        if (static_cast<const Directive&>(*statement).isUseStrict()) return true;
    }
    return false;
}

bool hasUseStrictDirective(const Node* body) {
    if (!body || body->kind() != NodeKind::BlockStatement) return false;
    return hasUseStrictDirective(
        static_cast<const frontend::BlockStatement&>(*body).body);
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

// Annex B B.3.2：sloppy 块级函数可能额外创建 var 绑定。
// 候选在遍历结束后统一判定，避免依赖声明出现顺序。
struct AnnexBCandidate {
    ShadowScope* blockScope;
    BindingName binding;
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
            case NodeKind::IfStatement:
                enterIf(node);
                break;
            case NodeKind::LabeledStatement:
                enterLabeled(node);
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
            case NodeKind::ClassBody:
            case NodeKind::StaticBlock:
                popScopes(node.id());
                popStrictness();
                break;
            case NodeKind::BlockStatement:
            case NodeKind::ForStatement:
            case NodeKind::ForInStatement:
            case NodeKind::ForOfStatement:
            case NodeKind::CatchClause:
                popScopes(node.id());
                break;
            default:
                // With 的作用域挂在 body 节点上，由 body 的 leave 弹出。
                popScopes(node.id());
                break;
        }
    }

    void finish() { applyAnnexB(); }

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
        module_ = program.sourceType == SourceType::Module;
        pushStrictness(module_ || hasUseStrictDirective(program.body));

        ShadowScope* scope = createScope(ShadowScopeKind::Program, nullptr);
        pushScope(program.id(), scope);
        mapScope(program.id(), scope);
    }

    void enterIf(Node& node) {
        auto& statement = static_cast<IfStatement&>(node);
        markIfClauseFunction(statement.consequent.get());
        markIfClauseFunction(statement.alternate.get());
    }

    // B.3.3：if 子句中的函数声明按“合成 BlockStatement”处理。
    void markIfClauseFunction(Node* clause) {
        if (clause && clause->kind() == NodeKind::FunctionDeclaration) {
            syntheticBlockFunctions_.insert(clause->id());
        }
    }

    void enterLabeled(Node& node) {
        auto& statement = static_cast<LabeledStatement&>(node);
        if (statement.body &&
            statement.body->kind() == NodeKind::FunctionDeclaration) {
            labelledFunctions_.insert(statement.body->id());
        }
    }

    void enterFunctionDeclaration(Node& node) {
        auto& function = static_cast<FunctionDeclaration&>(node);
        const bool hasName =
            function.id && function.id->kind() == NodeKind::Identifier;
        BindingName binding;
        if (hasName) {
            const auto& identifier =
                static_cast<const Identifier&>(*function.id);
            binding = BindingName{identifier.name, identifier.range()};
        }

        if (hasName && labelledFunctions_.count(node.id()) != 0) {
            // 标签函数声明使用 FunctionDeclaration 求值语义：var 型绑定。
            declareVarLikeFunction(binding);
        } else if (syntheticBlockFunctions_.count(node.id()) != 0) {
            ShadowScope* block = createScope(ShadowScopeKind::Block, current());
            pushScope(node.id(), block);
            if (hasName) declareFunction(binding);
        } else if (hasName) {
            declareFunction(binding);
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

        // 严格性：继承外层，或函数体自带 "use strict" 指令。
        pushStrictness(strict() || hasUseStrictDirective(body));

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
        scope->simpleCatchParameter = !nonSimple;
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
        // 类体（含字段初始化器与方法）恒为严格模式。
        pushStrictness(true);
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
        // static block 恒为严格模式，且自身是 var 作用域。
        pushStrictness(true);
        ShadowScope* scope =
            createScope(ShadowScopeKind::StaticBlock, current());
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
        declareVarScoped(binding, ShadowBindingKind::Var);
    }

    void declareVarLikeFunction(const BindingName& binding) {
        declareVarScoped(binding, ShadowBindingKind::Function);
    }

    void declareVarScoped(const BindingName& binding, ShadowBindingKind kind) {
        ShadowScope* target = varScope();

        for (ShadowScope* scope = current(); scope != nullptr;
             scope = scope->parent) {
            const auto it = scope->bindings.find(binding.name);
            if (it != scope->bindings.end() &&
                varConflictsWith(*scope, it->second.kind)) {
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

        declareBinding(target, binding, kind, true, true);
    }

    void declareFunction(const BindingName& binding) {
        ShadowScope* scope = current();
        ShadowBindingKind kind = ShadowBindingKind::LexicalFunction;
        switch (scope->kind) {
            case ShadowScopeKind::Program:
                // Script 顶层函数是 var 型；Module 顶层函数是 lexical 型。
                kind = module_ ? ShadowBindingKind::LexicalFunction
                               : ShadowBindingKind::Function;
                break;
            case ShadowScopeKind::FunctionBody:
            case ShadowScopeKind::StaticBlock:
                kind = ShadowBindingKind::Function;
                break;
            default:
                kind = ShadowBindingKind::LexicalFunction;
                break;
        }

        declareBinding(scope, binding, kind, true, true);

        // Annex B B.3.2 仅适用于 sloppy 的块级函数声明。
        if (kind == ShadowBindingKind::LexicalFunction && !strict()) {
            annexBCandidates_.push_back(AnnexBCandidate{scope, binding});
        }
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

    bool isDuplicateAllowed(ShadowBindingKind existing,
                            ShadowBindingKind incoming) const {
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
        // sloppy 块级重复函数声明合法（Annex B）；strict 下报错。
        if (existing == ShadowBindingKind::LexicalFunction &&
            incoming == ShadowBindingKind::LexicalFunction && !strict()) {
            return true;
        }
        return false;
    }

    bool strict() const {
        return !strictStack_.empty() && strictStack_.back();
    }

    void pushStrictness(bool value) { strictStack_.push_back(value); }

    void popStrictness() {
        if (!strictStack_.empty()) strictStack_.pop_back();
    }

    // Annex B B.3.2：为 sloppy 块级函数补建 var 绑定。
    // 判定等价于“把该函数声明替换成同名 var 声明是否产生早期错误”。
    void applyAnnexB() {
        for (const AnnexBCandidate& candidate : annexBCandidates_) {
            const std::string& name = candidate.binding.name;

            ShadowScope* varScope = nullptr;
            for (ShadowScope* scope = candidate.blockScope; scope != nullptr;
                 scope = scope->parent) {
                if (isVarScope(scope->kind)) {
                    varScope = scope;
                    break;
                }
            }
            if (!varScope) continue;

            if (varScope->kind == ShadowScopeKind::FunctionBody) {
                // 函数代码中名为 arguments 的块级函数不创建 var 绑定。
                if (name == "arguments") continue;
                ShadowScope* args = varScope->parent;
                if (args && args->kind == ShadowScopeKind::FunctionArguments) {
                    const auto param = args->bindings.find(name);
                    if (param != args->bindings.end() &&
                        param->second.kind == ShadowBindingKind::Param) {
                        continue;
                    }
                }
            }

            bool conflict = false;
            for (ShadowScope* scope = candidate.blockScope; scope != nullptr;
                 scope = scope->parent) {
                if (scope == varScope) {
                    const auto it = scope->bindings.find(name);
                    if (it != scope->bindings.end() &&
                        isLexicalKind(it->second.kind)) {
                        conflict = true;
                    }
                    break;
                }
                // 同块已有 var 声明：原代码本身已是早期错误。
                if (scope->varNames.count(name) != 0) {
                    conflict = true;
                    break;
                }
                const auto it = scope->bindings.find(name);
                if (it != scope->bindings.end()) {
                    const ShadowBindingKind kind = it->second.kind;
                    // 同名块级函数（重复声明）不阻止 var 绑定。
                    if (kind == ShadowBindingKind::LexicalFunction) continue;
                    if (isLexicalKind(kind)) {
                        conflict = true;
                        break;
                    }
                }
            }
            if (conflict) continue;

            if (varScope->bindings.find(name) == varScope->bindings.end()) {
                varScope->bindings.emplace(
                    name, ShadowBinding{name, ShadowBindingKind::Var, true,
                                        true, candidate.binding.range});
            }
        }
    }

    ScopeGraph graph_;
    std::vector<ShadowScope*> stack_;
    std::unordered_map<NodeId, std::vector<ShadowScope*>> pushedScopes_;
    std::unordered_set<NodeId> reuseBlocks_;
    std::unordered_map<NodeId, ShadowScope*> bodyScopes_;
    std::unordered_map<NodeId, ShadowScope*> withBodyScopes_;
    std::vector<SwitchContext> switchContexts_;
    std::vector<PendingClass> pendingClasses_;
    std::vector<AnnexBCandidate> annexBCandidates_;
    std::unordered_set<NodeId> syntheticBlockFunctions_;
    std::unordered_set<NodeId> labelledFunctions_;
    std::vector<bool> strictStack_;
    bool module_ = false;
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

    raw->finish();
    return raw->takeGraph();
}

}  // namespace neo::sema
