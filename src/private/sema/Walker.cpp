// sema/Walker.cpp - AST 遍历与 visitor 分派实现

#include "sema/Walker.h"

#include <cassert>
#include <utility>

namespace neo::sema {

using frontend::Node;
using frontend::NodeKind;
using frontend::NodeList;
using frontend::NodePtr;

void Walker::add(std::unique_ptr<IVisitor> visitor,
                 std::initializer_list<NodeKind> kinds) {
    IVisitor* raw = visitor.get();
    owned_.push_back(std::move(visitor));

    for (NodeKind kind : kinds) {
        if (kind == NodeKind::Count) continue;
        visitors_[static_cast<std::size_t>(kind)].push_back(raw);
    }
}

void Walker::add(std::unique_ptr<IVisitor> visitor, NodeKind kind) {
    add(std::move(visitor), {kind});
}

void Walker::addAll(std::unique_ptr<IVisitor> visitor) {
    IVisitor* raw = visitor.get();
    owned_.push_back(std::move(visitor));

    for (std::size_t i = 0; i + 1 < kKindCount; ++i) {
        visitors_[i].push_back(raw);
    }
}

std::size_t Walker::visitorCount(NodeKind kind) const {
    if (kind == NodeKind::Count) return 0;
    return visitors_[static_cast<std::size_t>(kind)].size();
}

void Walker::walk(Node& root, SemaContext& context) {
    dispatchEnter(root, context);
    walkChildren(root, context);
    dispatchLeave(root, context);
}

void Walker::walkNode(NodePtr& node, SemaContext& context) {
    if (!node) return;
    dispatchEnter(*node, context);
    walkChildren(*node, context);
    dispatchLeave(*node, context);
}

void Walker::walkList(NodeList& list, SemaContext& context) {
    for (NodePtr& child : list) {
        walkNode(child, context);
    }
}

void Walker::dispatchEnter(Node& node, SemaContext& context) {
    for (IVisitor* visitor : visitors_[static_cast<std::size_t>(node.kind())]) {
        visitor->enter(*this, context, node);
    }
}

void Walker::dispatchLeave(Node& node, SemaContext& context) {
    for (IVisitor* visitor : visitors_[static_cast<std::size_t>(node.kind())]) {
        visitor->leave(*this, context, node);
    }
}

void Walker::walkChildren(Node& node, SemaContext& context) {
    switch (node.kind()) {
        case NodeKind::Program: {
            auto& n = static_cast<frontend::Program&>(node);
            walkList(n.body, context);
            break;
        }
        case NodeKind::BlockStatement: {
            auto& n = static_cast<frontend::BlockStatement&>(node);
            walkList(n.body, context);
            break;
        }
        case NodeKind::EmptyStatement:
            break;
        case NodeKind::ExpressionStatement: {
            auto& n = static_cast<frontend::ExpressionStatement&>(node);
            walkNode(n.expression, context);
            break;
        }
        case NodeKind::Directive:
            break;
        case NodeKind::VariableDeclaration: {
            auto& n = static_cast<frontend::VariableDeclaration&>(node);
            walkList(n.declarations, context);
            break;
        }
        case NodeKind::VariableDeclarator: {
            auto& n = static_cast<frontend::VariableDeclarator&>(node);
            walkNode(n.id, context);
            walkNode(n.init, context);
            break;
        }
        case NodeKind::IfStatement: {
            auto& n = static_cast<frontend::IfStatement&>(node);
            walkNode(n.test, context);
            walkNode(n.consequent, context);
            walkNode(n.alternate, context);
            break;
        }
        case NodeKind::WhileStatement: {
            auto& n = static_cast<frontend::WhileStatement&>(node);
            walkNode(n.test, context);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::DoWhileStatement: {
            auto& n = static_cast<frontend::DoWhileStatement&>(node);
            walkNode(n.body, context);
            walkNode(n.test, context);
            break;
        }
        case NodeKind::ForStatement: {
            auto& n = static_cast<frontend::ForStatement&>(node);
            walkNode(n.init, context);
            walkNode(n.test, context);
            walkNode(n.update, context);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::ForInStatement: {
            auto& n = static_cast<frontend::ForInStatement&>(node);
            walkNode(n.left, context);
            walkNode(n.right, context);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::ForOfStatement: {
            auto& n = static_cast<frontend::ForOfStatement&>(node);
            walkNode(n.left, context);
            walkNode(n.right, context);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::ReturnStatement: {
            auto& n = static_cast<frontend::ReturnStatement&>(node);
            walkNode(n.argument, context);
            break;
        }
        case NodeKind::ThrowStatement: {
            auto& n = static_cast<frontend::ThrowStatement&>(node);
            walkNode(n.argument, context);
            break;
        }
        case NodeKind::BreakStatement:
            break;
        case NodeKind::ContinueStatement:
            break;
        case NodeKind::TryStatement: {
            auto& n = static_cast<frontend::TryStatement&>(node);
            walkNode(n.block, context);
            walkNode(n.handler, context);
            walkNode(n.finalizer, context);
            break;
        }
        case NodeKind::CatchClause: {
            auto& n = static_cast<frontend::CatchClause&>(node);
            walkNode(n.param, context);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::SwitchStatement: {
            auto& n = static_cast<frontend::SwitchStatement&>(node);
            walkNode(n.discriminant, context);
            walkList(n.cases, context);
            break;
        }
        case NodeKind::SwitchCase: {
            auto& n = static_cast<frontend::SwitchCase&>(node);
            walkNode(n.test, context);
            walkList(n.consequent, context);
            break;
        }
        case NodeKind::LabeledStatement: {
            auto& n = static_cast<frontend::LabeledStatement&>(node);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::WithStatement: {
            auto& n = static_cast<frontend::WithStatement&>(node);
            walkNode(n.object, context);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::DebuggerStatement:
            break;
        case NodeKind::FunctionDeclaration: {
            auto& n = static_cast<frontend::FunctionDeclaration&>(node);
            walkNode(n.id, context);
            walkList(n.params, context);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::ClassDeclaration: {
            auto& n = static_cast<frontend::ClassDeclaration&>(node);
            walkNode(n.id, context);
            walkNode(n.superClass, context);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::ClassBody: {
            auto& n = static_cast<frontend::ClassBody&>(node);
            walkList(n.elements, context);
            break;
        }
        case NodeKind::MethodDefinition: {
            auto& n = static_cast<frontend::MethodDefinition&>(node);
            walkNode(n.key, context);
            walkNode(n.value, context);
            break;
        }
        case NodeKind::PropertyDefinition: {
            auto& n = static_cast<frontend::PropertyDefinition&>(node);
            walkNode(n.key, context);
            walkNode(n.value, context);
            break;
        }
        case NodeKind::StaticBlock: {
            auto& n = static_cast<frontend::StaticBlock&>(node);
            walkList(n.body, context);
            break;
        }
        case NodeKind::ImportSpecifier: {
            auto& n = static_cast<frontend::ImportSpecifier&>(node);
            walkNode(n.imported, context);
            walkNode(n.local, context);
            break;
        }
        case NodeKind::ImportDefaultSpecifier: {
            auto& n = static_cast<frontend::ImportDefaultSpecifier&>(node);
            walkNode(n.local, context);
            break;
        }
        case NodeKind::ImportNamespaceSpecifier: {
            auto& n = static_cast<frontend::ImportNamespaceSpecifier&>(node);
            walkNode(n.local, context);
            break;
        }
        case NodeKind::ImportAttribute: {
            auto& n = static_cast<frontend::ImportAttribute&>(node);
            walkNode(n.key, context);
            walkNode(n.value, context);
            break;
        }
        case NodeKind::ImportDeclaration: {
            auto& n = static_cast<frontend::ImportDeclaration&>(node);
            walkList(n.specifiers, context);
            walkNode(n.source, context);
            walkList(n.attributes, context);
            break;
        }
        case NodeKind::ExportSpecifier: {
            auto& n = static_cast<frontend::ExportSpecifier&>(node);
            walkNode(n.local, context);
            walkNode(n.exported, context);
            break;
        }
        case NodeKind::ExportAllDeclaration: {
            auto& n = static_cast<frontend::ExportAllDeclaration&>(node);
            walkNode(n.exported, context);
            walkNode(n.source, context);
            walkList(n.attributes, context);
            break;
        }
        case NodeKind::ExportNamedDeclaration: {
            auto& n = static_cast<frontend::ExportNamedDeclaration&>(node);
            walkNode(n.declaration, context);
            walkList(n.specifiers, context);
            walkNode(n.source, context);
            walkList(n.attributes, context);
            break;
        }
        case NodeKind::ExportDefaultDeclaration: {
            auto& n = static_cast<frontend::ExportDefaultDeclaration&>(node);
            walkNode(n.declaration, context);
            break;
        }
        case NodeKind::Identifier:
            break;
        case NodeKind::NumericLiteral:
            break;
        case NodeKind::BigIntLiteral:
            break;
        case NodeKind::StringLiteral:
            break;
        case NodeKind::BooleanLiteral:
            break;
        case NodeKind::NullLiteral:
            break;
        case NodeKind::RegExpLiteral:
            break;
        case NodeKind::TemplateElement:
            break;
        case NodeKind::TemplateLiteral: {
            auto& n = static_cast<frontend::TemplateLiteral&>(node);
            walkList(n.quasis, context);
            walkList(n.expressions, context);
            break;
        }
        case NodeKind::TaggedTemplateExpression: {
            auto& n = static_cast<frontend::TaggedTemplateExpression&>(node);
            walkNode(n.tag, context);
            walkNode(n.quasi, context);
            break;
        }
        case NodeKind::ThisExpression:
            break;
        case NodeKind::SuperExpression:
            break;
        case NodeKind::FunctionExpression: {
            auto& n = static_cast<frontend::FunctionExpression&>(node);
            walkNode(n.id, context);
            walkList(n.params, context);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::ClassExpression: {
            auto& n = static_cast<frontend::ClassExpression&>(node);
            walkNode(n.id, context);
            walkNode(n.superClass, context);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::PrivateIdentifier:
            break;
        case NodeKind::MemberExpression: {
            auto& n = static_cast<frontend::MemberExpression&>(node);
            walkNode(n.object, context);
            walkNode(n.property, context);
            break;
        }
        case NodeKind::CallExpression: {
            auto& n = static_cast<frontend::CallExpression&>(node);
            walkNode(n.callee, context);
            walkList(n.arguments, context);
            break;
        }
        case NodeKind::ChainExpression: {
            auto& n = static_cast<frontend::ChainExpression&>(node);
            walkNode(n.expression, context);
            break;
        }
        case NodeKind::NewExpression: {
            auto& n = static_cast<frontend::NewExpression&>(node);
            walkNode(n.callee, context);
            walkList(n.arguments, context);
            break;
        }
        case NodeKind::MetaProperty: {
            auto& n = static_cast<frontend::MetaProperty&>(node);
            walkNode(n.meta, context);
            walkNode(n.property, context);
            break;
        }
        case NodeKind::ImportExpression: {
            auto& n = static_cast<frontend::ImportExpression&>(node);
            walkNode(n.source, context);
            walkNode(n.options, context);
            break;
        }
        case NodeKind::AwaitExpression: {
            auto& n = static_cast<frontend::AwaitExpression&>(node);
            walkNode(n.argument, context);
            break;
        }
        case NodeKind::YieldExpression: {
            auto& n = static_cast<frontend::YieldExpression&>(node);
            walkNode(n.argument, context);
            break;
        }
        case NodeKind::UnaryExpression: {
            auto& n = static_cast<frontend::UnaryExpression&>(node);
            walkNode(n.argument, context);
            break;
        }
        case NodeKind::UpdateExpression: {
            auto& n = static_cast<frontend::UpdateExpression&>(node);
            walkNode(n.argument, context);
            break;
        }
        case NodeKind::BinaryExpression: {
            auto& n = static_cast<frontend::BinaryExpression&>(node);
            walkNode(n.left, context);
            walkNode(n.right, context);
            break;
        }
        case NodeKind::LogicalExpression: {
            auto& n = static_cast<frontend::LogicalExpression&>(node);
            walkNode(n.left, context);
            walkNode(n.right, context);
            break;
        }
        case NodeKind::ConditionalExpression: {
            auto& n = static_cast<frontend::ConditionalExpression&>(node);
            walkNode(n.test, context);
            walkNode(n.consequent, context);
            walkNode(n.alternate, context);
            break;
        }
        case NodeKind::AssignmentExpression: {
            auto& n = static_cast<frontend::AssignmentExpression&>(node);
            walkNode(n.left, context);
            walkNode(n.right, context);
            break;
        }
        case NodeKind::SequenceExpression: {
            auto& n = static_cast<frontend::SequenceExpression&>(node);
            walkList(n.expressions, context);
            break;
        }
        case NodeKind::ArrowFunctionExpression: {
            auto& n = static_cast<frontend::ArrowFunctionExpression&>(node);
            walkList(n.params, context);
            walkNode(n.body, context);
            break;
        }
        case NodeKind::ArrayExpression: {
            auto& n = static_cast<frontend::ArrayExpression&>(node);
            walkList(n.elements, context);
            break;
        }
        case NodeKind::ObjectExpression: {
            auto& n = static_cast<frontend::ObjectExpression&>(node);
            walkList(n.properties, context);
            break;
        }
        case NodeKind::Property: {
            auto& n = static_cast<frontend::Property&>(node);
            walkNode(n.key, context);
            walkNode(n.value, context);
            break;
        }
        case NodeKind::SpreadElement: {
            auto& n = static_cast<frontend::SpreadElement&>(node);
            walkNode(n.argument, context);
            break;
        }
        case NodeKind::ObjectPattern: {
            auto& n = static_cast<frontend::ObjectPattern&>(node);
            walkList(n.properties, context);
            break;
        }
        case NodeKind::ArrayPattern: {
            auto& n = static_cast<frontend::ArrayPattern&>(node);
            walkList(n.elements, context);
            break;
        }
        case NodeKind::AssignmentPattern: {
            auto& n = static_cast<frontend::AssignmentPattern&>(node);
            walkNode(n.left, context);
            walkNode(n.right, context);
            break;
        }
        case NodeKind::RestElement: {
            auto& n = static_cast<frontend::RestElement&>(node);
            walkNode(n.argument, context);
            break;
        }
        case NodeKind::Count:
            assert(false && "NodeKind::Count is not a real node");
            break;
    }
}

}  // namespace neo::sema
