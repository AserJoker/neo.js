// sema/ControlFlowPass.cpp - 控制流上下文早期错误实现

#include "sema/ControlFlowPass.h"

#include "sema/ResolvePass.h"
#include "sema/Walker.h"

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

using frontend::ArrowFunctionExpression;
using frontend::AwaitExpression;
using frontend::BreakStatement;
using frontend::CallExpression;
using frontend::ClassDeclaration;
using frontend::ClassExpression;
using frontend::ContinueStatement;
using frontend::DoWhileStatement;
using frontend::ForInStatement;
using frontend::ForOfStatement;
using frontend::ForStatement;
using frontend::FunctionDeclaration;
using frontend::FunctionExpression;
using frontend::Identifier;
using frontend::LabeledStatement;
using frontend::MemberExpression;
using frontend::MetaProperty;
using frontend::MethodDefinition;
using frontend::MethodKind;
using frontend::NewExpression;
using frontend::Node;
using frontend::NodeId;
using frontend::NodeKind;
using frontend::Program;
using frontend::PropertyDefinition;
using frontend::ReturnStatement;
using frontend::SourceType;
using frontend::StaticBlock;
using frontend::SuperExpression;
using frontend::SwitchStatement;
using frontend::WhileStatement;
using frontend::YieldExpression;

bool isIdentifier(const Node* node, std::string_view name) {
    if (!node || node->kind() != NodeKind::Identifier) return false;
    return static_cast<const Identifier&>(*node).name == name;
}

bool isIterationStatement(const Node* node) {
    if (!node) return false;
    switch (node->kind()) {
        case NodeKind::WhileStatement:
        case NodeKind::DoWhileStatement:
        case NodeKind::ForStatement:
        case NodeKind::ForInStatement:
        case NodeKind::ForOfStatement:
            return true;
        case NodeKind::LabeledStatement:
            return isIterationStatement(
                static_cast<const LabeledStatement&>(*node).body.get());
        default:
            return false;
    }
}

struct MethodInfo {
    MethodKind kind;
    bool derived;
};

struct Context {
    bool functionLike = false;
    bool allowsReturn = false;
    bool allowsNewTarget = false;
    bool allowsSuperCall = false;
    bool allowsSuperProperty = false;
    bool allowsArguments = true;
    bool async = false;
    bool generator = false;
};

struct LabelInfo {
    std::string name;
    bool continuable = false;
};

struct SavedState {
    int loopDepth = 0;
    int switchDepth = 0;
    std::size_t labelBase = 0;
};

class ControlFlowVisitor final : public IVisitor {
public:
    explicit ControlFlowVisitor(AnnotationStore& annotations)
        : annotations_(annotations) {}

    void enter(Walker& /*walker*/, SemaContext& context,
               Node& node) override {
        if (fieldValues_.count(node.id()) != 0) {
            pushContext(fieldContext());
        }

        switch (node.kind()) {
            case NodeKind::Program:
                pushContext(programContext(node));
                break;
            case NodeKind::ClassDeclaration:
            case NodeKind::ClassExpression:
                enterClass(node);
                break;
            case NodeKind::MethodDefinition:
                enterMethod(node);
                break;
            case NodeKind::PropertyDefinition:
                enterProperty(node);
                break;
            case NodeKind::Property:
                enterObjectMethod(node);
                break;
            case NodeKind::FunctionDeclaration:
                pushContext(normalFunctionContext(node));
                break;
            case NodeKind::FunctionExpression:
                pushContext(functionExpressionContext(node));
                break;
            case NodeKind::ArrowFunctionExpression:
                pushContext(arrowFunctionContext(node));
                break;
            case NodeKind::StaticBlock:
                pushContext(classElementContext());
                break;
            case NodeKind::WhileStatement:
            case NodeKind::DoWhileStatement:
            case NodeKind::ForStatement:
            case NodeKind::ForInStatement:
                ++loopDepth_;
                break;
            case NodeKind::ForOfStatement:
                ++loopDepth_;
                checkForAwait(node, context);
                break;
            case NodeKind::SwitchStatement:
                ++switchDepth_;
                break;
            case NodeKind::LabeledStatement:
                enterLabel(node, context);
                break;
            case NodeKind::ReturnStatement:
                checkReturn(node, context);
                break;
            case NodeKind::BreakStatement:
                checkBreak(node, context);
                break;
            case NodeKind::ContinueStatement:
                checkContinue(node, context);
                break;
            case NodeKind::MetaProperty:
                checkMetaProperty(node, context);
                break;
            case NodeKind::CallExpression:
                checkCall(node, context);
                break;
            case NodeKind::MemberExpression:
                checkMember(node, context);
                break;
            case NodeKind::NewExpression:
                checkNew(node, context);
                break;
            case NodeKind::SuperExpression:
                checkSuper(node, context);
                break;
            case NodeKind::AwaitExpression:
                checkAwait(node, context);
                break;
            case NodeKind::YieldExpression:
                checkYield(node, context);
                break;
            case NodeKind::Identifier:
                checkArguments(node, context);
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
                popContext();
                break;
            case NodeKind::ClassDeclaration:
            case NodeKind::ClassExpression:
                assert(!classStack_.empty());
                classStack_.pop_back();
                break;
            case NodeKind::WhileStatement:
            case NodeKind::DoWhileStatement:
            case NodeKind::ForStatement:
            case NodeKind::ForInStatement:
            case NodeKind::ForOfStatement:
                --loopDepth_;
                break;
            case NodeKind::SwitchStatement:
                --switchDepth_;
                break;
            case NodeKind::LabeledStatement:
                assert(!labels_.empty());
                labels_.pop_back();
                break;
            default:
                break;
        }

        if (fieldValues_.count(node.id()) != 0) {
            popContext();
        }
    }

private:
    const Context& ctx() const {
        assert(!contexts_.empty());
        return contexts_.back();
    }

    void pushContext(const Context& context) {
        if (context.functionLike) {
            saved_.push_back(SavedState{loopDepth_, switchDepth_, labelBase_});
            loopDepth_ = 0;
            switchDepth_ = 0;
            labelBase_ = labels_.size();
        }
        contexts_.push_back(context);
    }

    void popContext() {
        assert(!contexts_.empty());
        const Context context = contexts_.back();
        contexts_.pop_back();
        if (context.functionLike) {
            assert(!saved_.empty());
            const SavedState state = saved_.back();
            saved_.pop_back();
            loopDepth_ = state.loopDepth;
            switchDepth_ = state.switchDepth;
            labelBase_ = state.labelBase;
        }
    }

    Context programContext(Node& node) {
        auto& program = static_cast<Program&>(node);
        return Context{false,
                       false,
                       false,
                       false,
                       false,
                       true,
                       program.sourceType == SourceType::Module,
                       false};
    }

    Context normalFunctionContext(Node& node) {
        auto& function = static_cast<FunctionDeclaration&>(node);
        return Context{true,  true, true, false, false,
                       true,  function.async, function.generator};
    }

    Context functionExpressionContext(Node& node) {
        auto& function = static_cast<FunctionExpression&>(node);
        const auto method = methodInfo_.find(node.id());
        if (method != methodInfo_.end()) {
            const bool superCall =
                method->second.kind == MethodKind::Constructor &&
                method->second.derived;
            return Context{true,        true,  true, superCall, true,
                           true,        function.async, function.generator};
        }
        return Context{true,  true, true, false, false,
                       true,  function.async, function.generator};
    }

    Context arrowFunctionContext(Node& node) {
        auto& function = static_cast<ArrowFunctionExpression&>(node);
        const Context& parent = ctx();
        return Context{true,
                       true,
                       parent.allowsNewTarget,
                       parent.allowsSuperCall,
                       parent.allowsSuperProperty,
                       parent.allowsArguments,
                       function.async,
                       false};
    }

    Context classElementContext() const {
        return Context{true,  false, true, false, true,
                       false, false, false};
    }

    Context fieldContext() const { return classElementContext(); }

    void enterClass(Node& node) {
        bool derived = false;
        if (node.kind() == NodeKind::ClassDeclaration) {
            derived = static_cast<ClassDeclaration&>(node).superClass != nullptr;
        } else {
            derived = static_cast<ClassExpression&>(node).superClass != nullptr;
        }
        classStack_.push_back(derived);
    }

    void enterMethod(Node& node) {
        auto& method = static_cast<MethodDefinition&>(node);
        if (!method.value ||
            method.value->kind() != NodeKind::FunctionExpression) {
            return;
        }
        const bool derived =
            !classStack_.empty() && classStack_.back();
        methodInfo_[method.value->id()] = MethodInfo{method.methodKind,
                                                     derived};
    }

    void enterProperty(Node& node) {
        auto& property = static_cast<PropertyDefinition&>(node);
        if (property.value) {
            fieldValues_.insert(property.value->id());
        }
    }

    // 对象字面量方法同样有 HomeObject，允许 super.prop（但不允许 super()）。
    void enterObjectMethod(Node& node) {
        auto& property = static_cast<frontend::Property&>(node);
        if (!property.method || !property.value ||
            property.value->kind() != NodeKind::FunctionExpression) {
            return;
        }
        methodInfo_[property.value->id()] =
            MethodInfo{MethodKind::Method, false};
    }

    void enterLabel(Node& node, SemaContext& context) {
        auto& statement = static_cast<LabeledStatement&>(node);
        for (std::size_t i = labelBase_; i < labels_.size(); ++i) {
            if (labels_[i].name == statement.label) {
                context.error(statement.range().start,
                              "Label '" + statement.label +
                                  "' has already been declared");
                break;
            }
        }
        labels_.push_back(LabelInfo{statement.label,
                                    isIterationStatement(statement.body.get())});
    }

    const LabelInfo* findLabel(std::string_view name) const {
        for (std::size_t i = labelBase_; i < labels_.size(); ++i) {
            if (labels_[i].name == name) return &labels_[i];
        }
        return nullptr;
    }

    void checkReturn(Node& node, SemaContext& context) {
        if (!ctx().allowsReturn) {
            context.error(node.range().start,
                          "Illegal return statement");
        }
    }

    void checkBreak(Node& node, SemaContext& context) {
        auto& statement = static_cast<BreakStatement&>(node);
        if (statement.label.empty()) {
            if (loopDepth_ == 0 && switchDepth_ == 0) {
                context.error(node.range().start,
                              "Illegal break statement");
            }
            return;
        }
        if (findLabel(statement.label) == nullptr) {
            context.error(node.range().start,
                          "Undefined label '" + statement.label + "'");
        }
    }

    void checkContinue(Node& node, SemaContext& context) {
        auto& statement = static_cast<ContinueStatement&>(node);
        if (statement.label.empty()) {
            if (loopDepth_ == 0) {
                context.error(node.range().start,
                              "Illegal continue statement: no surrounding "
                              "iteration statement");
            }
            return;
        }
        const LabelInfo* label = findLabel(statement.label);
        if (label == nullptr) {
            context.error(node.range().start,
                          "Undefined label '" + statement.label + "'");
            return;
        }
        if (!label->continuable) {
            context.error(node.range().start,
                          "Illegal continue statement: '" + statement.label +
                              "' does not denote an iteration statement");
        }
    }

    void checkMetaProperty(Node& node, SemaContext& context) {
        auto& property = static_cast<MetaProperty&>(node);
        if (!isIdentifier(property.meta.get(), "new") ||
            !isIdentifier(property.property.get(), "target")) {
            return;
        }
        if (!ctx().allowsNewTarget) {
            context.error(node.range().start,
                          "new.target expression is not allowed here");
        }
    }

    void checkCall(Node& node, SemaContext& context) {
        auto& call = static_cast<CallExpression&>(node);
        if (!call.callee || call.callee->kind() != NodeKind::SuperExpression) {
            return;
        }
        if (!ctx().allowsSuperCall) {
            context.error(call.callee->range().start,
                          "super() is only allowed in a derived constructor");
        }
        allowedSuper_.insert(call.callee->id());
    }

    void checkMember(Node& node, SemaContext& context) {
        auto& member = static_cast<MemberExpression&>(node);
        if (!member.object ||
            member.object->kind() != NodeKind::SuperExpression) {
            return;
        }
        if (member.optional) {
            context.error(member.object->range().start,
                          "Optional chaining is not allowed on super");
        }
        if (!ctx().allowsSuperProperty) {
            context.error(member.object->range().start,
                          "'super' keyword unexpected here");
        }
        allowedSuper_.insert(member.object->id());
    }

    void checkNew(Node& node, SemaContext& context) {
        auto& expression = static_cast<NewExpression&>(node);
        if (!expression.callee ||
            expression.callee->kind() != NodeKind::SuperExpression) {
            return;
        }
        context.error(expression.callee->range().start,
                      "super is not a constructor");
        allowedSuper_.insert(expression.callee->id());
    }

    void checkSuper(Node& node, SemaContext& context) {
        if (allowedSuper_.count(node.id()) == 0) {
            context.error(node.range().start,
                          "'super' keyword unexpected here");
        }
    }

    void checkAwait(Node& node, SemaContext& context) {
        if (!ctx().async) {
            context.error(node.range().start,
                          "await is only valid in async functions and the top "
                          "level bodies of modules");
        }
    }

    void checkYield(Node& node, SemaContext& context) {
        if (!ctx().generator) {
            context.error(node.range().start,
                          "yield is only valid in generator functions");
        }
    }

    void checkForAwait(Node& node, SemaContext& context) {
        auto& statement = static_cast<ForOfStatement&>(node);
        if (statement.await && !ctx().async) {
            context.error(node.range().start,
                          "for await is only valid in async functions and the "
                          "top level bodies of modules");
        }
    }

    void checkArguments(Node& node, SemaContext& context) {
        const auto& identifier = static_cast<Identifier&>(node);
        if (identifier.name != "arguments") return;
        if (ctx().allowsArguments) return;
        // 依赖 ResolvePass 标注区分引用与绑定/属性名。
        if (annotations_.get<IdentifierBinding>(node.id()) == nullptr) return;
        context.error(node.range().start,
                      "'arguments' is not allowed in class field initializer "
                      "or static initialization block");
    }

    AnnotationStore& annotations_;
    std::vector<Context> contexts_;
    std::vector<SavedState> saved_;
    std::vector<bool> classStack_;
    std::unordered_map<NodeId, MethodInfo> methodInfo_;
    std::unordered_set<NodeId> fieldValues_;
    std::unordered_set<NodeId> allowedSuper_;
    std::vector<LabelInfo> labels_;
    std::size_t labelBase_ = 0;
    int loopDepth_ = 0;
    int switchDepth_ = 0;
};

}  // namespace

void runControlFlowPass(frontend::Node& program, SemaContext& context,
                        AnnotationStore& annotations) {
    Walker walker(annotations);
    walker.addAll(std::make_unique<ControlFlowVisitor>(annotations));
    walker.walk(program, context);
}

}  // namespace neo::sema
