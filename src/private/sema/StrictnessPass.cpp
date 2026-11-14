// sema/StrictnessPass.cpp - 严格模式早期错误检查实现

#include "sema/StrictnessPass.h"

#include "sema/BindingNames.h"
#include "sema/Walker.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace neo::sema {

namespace {

using frontend::AssignmentPattern;
using frontend::ArrayPattern;
using frontend::BlockStatement;
using frontend::ClassBody;
using frontend::ClassDeclaration;
using frontend::ClassExpression;
using frontend::Directive;
using frontend::DoWhileStatement;
using frontend::ForInStatement;
using frontend::ForOfStatement;
using frontend::ForStatement;
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
using frontend::NumericLiteral;
using frontend::ObjectPattern;
using frontend::Program;
using frontend::Property;
using frontend::RestElement;
using frontend::SourceRange;
using frontend::SourceType;
using frontend::StringLiteral;
using frontend::UnaryExpression;
using frontend::UnaryOperator;
using frontend::VariableDeclaration;
using frontend::VariableDeclarator;
using frontend::WhileStatement;
using frontend::WithStatement;

bool isStrictReserved(std::string_view name) {
    return name == "implements" || name == "interface" || name == "let" ||
           name == "package" || name == "private" || name == "protected" ||
           name == "public" || name == "static" || name == "yield";
}

bool isEvalOrArguments(std::string_view name) {
    return name == "eval" || name == "arguments";
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
    return hasUseStrictDirective(static_cast<const BlockStatement&>(*body).body);
}

// 14.13.2 IsLabelledFunction：标签链末端是否为 FunctionDeclaration。
bool isLabelledFunction(const Node* statement) {
    while (statement && statement->kind() == NodeKind::LabeledStatement) {
        const auto& labelled = static_cast<const LabeledStatement&>(*statement);
        if (labelled.body &&
            labelled.body->kind() == NodeKind::FunctionDeclaration) {
            return true;
        }
        statement = labelled.body.get();
    }
    return false;
}

void checkBindingName(const BindingName& binding, SemaContext& context,
                      const char* what) {
    if (isEvalOrArguments(binding.name)) {
        context.error(binding.range.start,
                      std::string(what) + " '" + binding.name +
                          "' is not allowed in strict mode");
    }
}

void checkStrictReservedName(std::string_view name, SourceRange range,
                             SemaContext& context) {
    if (isStrictReserved(name)) {
        context.error(range.start, "Unexpected strict mode reserved word '" +
                                       std::string(name) + "'");
    }
}

void checkBindingPattern(Node* pattern, SemaContext& context,
                         const char* what) {
    if (!pattern) return;

    std::vector<BindingName> names;
    bool nonSimple = false;
    collectBindingNames(pattern, names, nonSimple);
    for (const BindingName& binding : names) {
        checkBindingName(binding, context, what);
    }
}

void checkAssignmentTarget(Node* target, SemaContext& context) {
    if (!target) return;

    switch (target->kind()) {
        case NodeKind::Identifier: {
            const auto& identifier = static_cast<Identifier&>(*target);
            if (isEvalOrArguments(identifier.name)) {
                context.error(identifier.range().start,
                              "Assignment to '" + identifier.name +
                                  "' is not allowed in strict mode");
            }
            break;
        }
        case NodeKind::ObjectPattern:
        case NodeKind::ArrayPattern:
        case NodeKind::AssignmentPattern:
        case NodeKind::RestElement: {
            std::vector<BindingName> names;
            bool nonSimple = false;
            collectBindingNames(target, names, nonSimple);
            for (const BindingName& binding : names) {
                if (isEvalOrArguments(binding.name)) {
                    context.error(binding.range.start,
                                  "Assignment to '" + binding.name +
                                      "' is not allowed in strict mode");
                }
            }
            break;
        }
        default:
            // 成员访问等目标允许（obj.eval = 1）。
            break;
    }
}

class StrictnessVisitor final : public IVisitor {
public:
    void enter(Walker& /*walker*/, SemaContext& context,
               Node& node) override {
        switch (node.kind()) {
            case NodeKind::Program: {
                auto& program = static_cast<Program&>(node);
                const bool module = program.sourceType == SourceType::Module;
                push(module || hasUseStrictDirective(program.body));
                break;
            }
            case NodeKind::FunctionDeclaration:
            case NodeKind::FunctionExpression:
            case NodeKind::ArrowFunctionExpression:
                handleFunction(node, context);
                break;
            case NodeKind::ClassDeclaration:
            case NodeKind::ClassExpression:
                handleClass(node, context);
                break;
            case NodeKind::ClassBody:
                push(true);
                break;
            case NodeKind::IfStatement: {
                auto& statement = static_cast<IfStatement&>(node);
                checkIfClause(statement.consequent.get(), context);
                checkIfClause(statement.alternate.get(), context);
                break;
            }
            case NodeKind::WhileStatement: {
                auto& statement = static_cast<WhileStatement&>(node);
                checkLoopBody(statement.body.get(), context);
                break;
            }
            case NodeKind::DoWhileStatement: {
                auto& statement = static_cast<DoWhileStatement&>(node);
                checkLoopBody(statement.body.get(), context);
                break;
            }
            case NodeKind::ForStatement: {
                auto& statement = static_cast<ForStatement&>(node);
                checkLoopBody(statement.body.get(), context);
                break;
            }
            case NodeKind::WithStatement: {
                auto& statement = static_cast<WithStatement&>(node);
                if (strict()) {
                    context.error(node.range().start,
                                  "With statement is not allowed in strict mode");
                }
                if (isLabelledFunction(statement.body.get())) {
                    context.error(node.range().start,
                                  "Labelled function declaration is not allowed "
                                  "as the body of a with statement");
                }
                break;
            }
            case NodeKind::LabeledStatement: {
                auto& statement = static_cast<LabeledStatement&>(node);
                if (strict()) {
                    checkStrictReservedName(statement.label,
                                            statement.range(), context);
                    if (statement.body &&
                        statement.body->kind() ==
                            NodeKind::FunctionDeclaration) {
                        context.error(
                            node.range().start,
                            "In strict mode code, functions can only be "
                            "declared at top level or inside a block");
                    }
                }
                break;
            }
            case NodeKind::BreakStatement: {
                auto& statement = static_cast<frontend::BreakStatement&>(node);
                if (strict() && !statement.label.empty()) {
                    checkStrictReservedName(statement.label,
                                            statement.range(), context);
                }
                break;
            }
            case NodeKind::ContinueStatement: {
                auto& statement =
                    static_cast<frontend::ContinueStatement&>(node);
                if (strict() && !statement.label.empty()) {
                    checkStrictReservedName(statement.label,
                                            statement.range(), context);
                }
                break;
            }
            case NodeKind::NumericLiteral: {
                auto& literal = static_cast<NumericLiteral&>(node);
                if (strict() && literal.legacyLeadingZero) {
                    context.error(literal.range().start,
                                  "Legacy octal literal is not allowed in "
                                  "strict mode");
                }
                break;
            }
            case NodeKind::StringLiteral: {
                auto& literal = static_cast<StringLiteral&>(node);
                if (strict() &&
                    (literal.legacyOctalEscape || literal.legacyEightOrNine)) {
                    context.error(literal.range().start,
                                  "Legacy escape sequence is not allowed in "
                                  "strict mode");
                }
                break;
            }
            case NodeKind::Directive: {
                auto& directive = static_cast<Directive&>(node);
                if (strict() && (directive.legacyOctalEscape ||
                                 directive.legacyEightOrNine)) {
                    context.error(directive.range().start,
                                  "Legacy escape sequence is not allowed in "
                                  "strict mode");
                }
                break;
            }
            case NodeKind::UnaryExpression:
                handleUnary(node, context);
                break;
            case NodeKind::AssignmentExpression: {
                if (strict()) {
                    auto& assignment =
                        static_cast<frontend::AssignmentExpression&>(node);
                    checkAssignmentTarget(assignment.left.get(), context);
                }
                break;
            }
            case NodeKind::UpdateExpression: {
                if (strict()) {
                    auto& update =
                        static_cast<frontend::UpdateExpression&>(node);
                    checkAssignmentTarget(update.argument.get(), context);
                }
                break;
            }
            case NodeKind::ForInStatement: {
                auto& statement = static_cast<ForInStatement&>(node);
                checkLoopBody(statement.body.get(), context);
                if (strict()) {
                    if (statement.left &&
                        statement.left->kind() !=
                            NodeKind::VariableDeclaration) {
                        checkAssignmentTarget(statement.left.get(), context);
                    }
                }
                break;
            }
            case NodeKind::ForOfStatement: {
                auto& statement = static_cast<ForOfStatement&>(node);
                checkLoopBody(statement.body.get(), context);
                if (strict()) {
                    if (statement.left &&
                        statement.left->kind() !=
                            NodeKind::VariableDeclaration) {
                        checkAssignmentTarget(statement.left.get(), context);
                    }
                }
                break;
            }
            case NodeKind::VariableDeclarator: {
                if (strict()) {
                    auto& declarator = static_cast<VariableDeclarator&>(node);
                    checkBindingPattern(declarator.id.get(), context,
                                        "Binding name");
                }
                break;
            }
            case NodeKind::CatchClause: {
                if (strict()) {
                    auto& clause = static_cast<frontend::CatchClause&>(node);
                    checkBindingPattern(clause.param.get(), context,
                                        "Catch parameter");
                }
                break;
            }
            case NodeKind::ImportSpecifier: {
                auto& specifier = static_cast<frontend::ImportSpecifier&>(node);
                markNonReference(specifier.imported.get());
                if (strict()) {
                    checkBindingPattern(specifier.local.get(), context,
                                        "Import binding");
                }
                break;
            }
            case NodeKind::ImportDefaultSpecifier: {
                auto& specifier =
                    static_cast<frontend::ImportDefaultSpecifier&>(node);
                if (strict()) {
                    checkBindingPattern(specifier.local.get(), context,
                                        "Import binding");
                }
                break;
            }
            case NodeKind::ImportNamespaceSpecifier: {
                auto& specifier =
                    static_cast<frontend::ImportNamespaceSpecifier&>(node);
                if (strict()) {
                    checkBindingPattern(specifier.local.get(), context,
                                        "Import binding");
                }
                break;
            }
            case NodeKind::ExportSpecifier: {
                auto& specifier =
                    static_cast<frontend::ExportSpecifier&>(node);
                markNonReference(specifier.exported.get());
                break;
            }
            case NodeKind::ExportAllDeclaration: {
                auto& declaration =
                    static_cast<frontend::ExportAllDeclaration&>(node);
                markNonReference(declaration.exported.get());
                break;
            }
            case NodeKind::ImportAttribute: {
                auto& attribute =
                    static_cast<frontend::ImportAttribute&>(node);
                markNonReference(attribute.key.get());
                break;
            }
            case NodeKind::MetaProperty: {
                auto& meta = static_cast<frontend::MetaProperty&>(node);
                markNonReference(meta.meta.get());
                markNonReference(meta.property.get());
                break;
            }
            case NodeKind::MemberExpression: {
                auto& member = static_cast<frontend::MemberExpression&>(node);
                if (!member.computed) {
                    markNonReference(member.property.get());
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
            case NodeKind::MethodDefinition: {
                auto& method =
                    static_cast<frontend::MethodDefinition&>(node);
                if (!method.computed) {
                    markNonReference(method.key.get());
                }
                break;
            }
            case NodeKind::PropertyDefinition: {
                auto& property =
                    static_cast<frontend::PropertyDefinition&>(node);
                if (!property.computed) {
                    markNonReference(property.key.get());
                }
                break;
            }
            case NodeKind::Identifier:
                handleIdentifier(node, context);
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
            case NodeKind::ClassBody:
                pop();
                break;
            default:
                break;
        }
    }

private:
    // 14.6.1 / B.3.3：if 子句允许直接 FunctionDeclaration 仅限非严格代码；
    // 标签链末端为函数声明时一律报错（IsLabelledFunction）。
    void checkIfClause(Node* clause, SemaContext& context) {
        if (!clause) return;
        if (isLabelledFunction(clause)) {
            context.error(clause->range().start,
                          "Labelled function declaration is not allowed as the "
                          "body of an if statement");
            return;
        }
        if (strict() && clause->kind() == NodeKind::FunctionDeclaration) {
            context.error(clause->range().start,
                          "In strict mode code, functions can only be declared "
                          "at top level or inside a block");
        }
    }

    // 循环体 / with 体必须是 Statement，FunctionDeclaration 不是 Statement。
    void checkLoopBody(Node* body, SemaContext& context) {
        if (!body) return;
        if (isLabelledFunction(body)) {
            context.error(body->range().start,
                          "Labelled function declaration is not allowed as the "
                          "body of an iteration statement");
            return;
        }
        if (body->kind() == NodeKind::FunctionDeclaration) {
            context.error(body->range().start,
                          "In non-strict mode code, functions can only be "
                          "declared at top level, inside a block, or as the "
                          "body of an if statement");
        }
    }

    bool strict() const {
        return !strictStack_.empty() && strictStack_.back();
    }

    void push(bool value) { strictStack_.push_back(value); }

    void pop() {
        if (!strictStack_.empty()) strictStack_.pop_back();
    }

    void markNonReference(Node* node) {
        if (node) nonReference_.insert(node->id());
    }

    void handleIdentifier(Node& node, SemaContext& context) {
        if (!strict()) return;
        if (nonReference_.count(node.id()) != 0) return;

        const auto& identifier = static_cast<Identifier&>(node);
        checkStrictReservedName(identifier.name, identifier.range(), context);
    }

    void handleUnary(Node& node, SemaContext& context) {
        if (!strict()) return;

        auto& unary = static_cast<UnaryExpression&>(node);
        if (unary.op != UnaryOperator::Delete || !unary.argument) return;
        if (unary.argument->kind() != NodeKind::Identifier) return;

        const auto& identifier =
            static_cast<const Identifier&>(*unary.argument);
        // 严格保留字由 handleIdentifier 统一报错，避免同一位置重复诊断。
        if (isStrictReserved(identifier.name)) return;

        context.error(identifier.range().start,
                      "Delete of an unqualified identifier is not allowed in "
                      "strict mode");
    }

    void handleFunction(Node& node, SemaContext& context) {
        Node* id = nullptr;
        NodeList* params = nullptr;
        Node* body = nullptr;

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
                break;
            }
            case NodeKind::ArrowFunctionExpression: {
                auto& function =
                    static_cast<frontend::ArrowFunctionExpression&>(node);
                params = &function.params;
                body = function.body.get();
                break;
            }
            default:
                break;
        }

        const bool bodyStrict = hasUseStrictDirective(body);
        const bool functionStrict = strict() || bodyStrict;

        if (id && id->kind() == NodeKind::Identifier && functionStrict) {
            const auto& identifier = static_cast<const Identifier&>(*id);
            if (isEvalOrArguments(identifier.name)) {
                context.error(identifier.range().start,
                              "Function name '" + identifier.name +
                                  "' is not allowed in strict mode");
            }
        }

        std::vector<BindingName> names;
        bool nonSimple = false;
        if (params) {
            for (NodePtr& param : *params) {
                collectBindingNames(param.get(), names, nonSimple);
            }
        }

        std::unordered_set<std::string> seen;
        for (const BindingName& binding : names) {
            if (!seen.insert(binding.name).second &&
                (functionStrict || nonSimple)) {
                context.error(binding.range.start,
                              "Duplicate parameter name '" + binding.name +
                                  "'");
            }
            if (functionStrict) {
                checkBindingName(binding, context, "Parameter name");
            }
        }

        if (bodyStrict && nonSimple) {
            context.error(node.range().start,
                          "Illegal 'use strict' directive in function with "
                          "non-simple parameter list");
        }

        push(functionStrict);
    }

    void handleClass(Node& node, SemaContext& context) {
        Node* id = nullptr;
        if (node.kind() == NodeKind::ClassDeclaration) {
            id = static_cast<ClassDeclaration&>(node).id.get();
        } else {
            id = static_cast<ClassExpression&>(node).id.get();
        }
        if (!id || id->kind() != NodeKind::Identifier) return;

        // class 名字始终按严格模式检查；标记为非引用，避免外层严格时重复报错。
        markNonReference(id);
        const auto& identifier = static_cast<const Identifier&>(*id);
        if (isEvalOrArguments(identifier.name)) {
            context.error(identifier.range().start,
                          "Class name '" + identifier.name +
                              "' is not allowed in strict mode");
        }
        checkStrictReservedName(identifier.name, identifier.range(), context);
    }

    std::vector<bool> strictStack_;
    std::unordered_set<NodeId> nonReference_;
};

}  // namespace

std::unique_ptr<IVisitor> makeStrictnessVisitor() {
    return std::make_unique<StrictnessVisitor>();
}

void runStrictnessPass(frontend::Node& program, SemaContext& context,
                       AnnotationStore& annotations) {
    Walker walker(annotations);
    walker.addAll(makeStrictnessVisitor());
    walker.walk(program, context);
}

}  // namespace neo::sema
