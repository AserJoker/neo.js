// Compiler.cpp - AST + Sema 标注 → 字节码（ADR-061）
//
// 分派函数只做 kind → 具体函数的路由；具体函数只发射本节点的字节码。
// 当前最小链路覆盖 V0/V1 的字面量、表达式语句、return、块、标识符与二元运算。

#include "compiler/Compiler.h"

#include "core/OperatorKind.h"
#include "regex/RegexEngine.h"
#include "sema/FunctionInfo.h"
#include "sema/ResolvePass.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

namespace neo::compiler {

namespace {

// 指令序言中是否含 "use strict"（只看开头 Directive 序列）。
bool hasUseStrictDirective(const frontend::NodeList& body) {
    for (const frontend::NodePtr& statement : body) {
        if (!statement || statement->kind() != frontend::NodeKind::Directive) {
            break;
        }
        if (static_cast<const frontend::Directive&>(*statement).isUseStrict()) {
            return true;
        }
    }
    return false;
}

}  // namespace

using frontend::Node;
using frontend::NodeKind;
using frontend::NodePtr;
using frontend::SourceType;

namespace {

// for-in/of 的编译器内部槽位（JS 标识符不可能包含 '*'，不会与用户绑定冲突）。
constexpr std::string_view kEvalCompletion = "*eval-completion*";
constexpr std::string_view kForKeys = "*for-keys*";
constexpr std::string_view kForIndex = "*for-index*";
constexpr std::string_view kForValue = "*for-value*";
constexpr std::string_view kForOwner = "*for-owner*";
constexpr std::string_view kForIterator = "*for-iterator*";
constexpr std::string_view kForResult = "*for-result*";
constexpr std::string_view kForDone = "*for-done*";
constexpr std::string_view kSwitchDisc = "*switch-disc*";

// 收集绑定模式中的标识符名（解构声明/参数提升用）。
void collectPatternNames(const frontend::Node& pattern,
                         std::vector<std::string>& names) {
    switch (pattern.kind()) {
        case NodeKind::Identifier:
            names.push_back(
                static_cast<const frontend::Identifier&>(pattern).name);
            break;
        case NodeKind::AssignmentPattern: {
            const auto& node =
                static_cast<const frontend::AssignmentPattern&>(pattern);
            if (node.left) collectPatternNames(*node.left, names);
            break;
        }
        case NodeKind::RestElement: {
            const auto& node =
                static_cast<const frontend::RestElement&>(pattern);
            if (node.argument) collectPatternNames(*node.argument, names);
            break;
        }
        case NodeKind::ArrayPattern: {
            const auto& node =
                static_cast<const frontend::ArrayPattern&>(pattern);
            for (const frontend::NodePtr& element : node.elements) {
                if (element) collectPatternNames(*element, names);
            }
            break;
        }
        case NodeKind::ObjectPattern: {
            const auto& node =
                static_cast<const frontend::ObjectPattern&>(pattern);
            for (const frontend::NodePtr& property : node.properties) {
                if (!property) continue;
                if (property->kind() == NodeKind::Property) {
                    const auto& prop =
                        static_cast<const frontend::Property&>(*property);
                    if (prop.value) collectPatternNames(*prop.value, names);
                } else if (property->kind() == NodeKind::RestElement) {
                    const auto& rest =
                        static_cast<const frontend::RestElement&>(*property);
                    if (rest.argument) {
                        collectPatternNames(*rest.argument, names);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

std::string exportNameOf(const Node* node) {
    if (node == nullptr) return {};
    if (node->kind() == NodeKind::Identifier) {
        return static_cast<const frontend::Identifier&>(*node).name;
    }
    if (node->kind() == NodeKind::StringLiteral) {
        return static_cast<const frontend::StringLiteral&>(*node).value;
    }
    return {};
}

}  // namespace

Compiler::Compiler(const frontend::Program& program,
                   const sema::SemaResult& sema, CompilerOptions options)
    : program_(program), sema_(sema), options_(std::move(options)) {}

CompileResult Compiler::compile() {
    // 动态 import 的相对说明符以本 Code 所属模块 id 为基准。
    code_->moduleId = options_.filename;
    // 前置条件被破坏时不在错误 AST 上继续生成字节码。
    if (!sema_.ok()) {
        errors_.push_back({program_.range(), "compiler: sema reported errors"});
    } else if (options_.eval || program_.sourceType == SourceType::Eval) {
        compileEvalProgram();
    } else if (program_.sourceType == SourceType::Module) {
        compileModule();
    } else {
        compileProgram();
    }

    CompileResult result;
    result.code = std::move(code_);
    result.errors = std::move(errors_);
    result.declarePc = declarePc_;
    result.instantiatePc = instantiatePc_;
    result.evaluatePc = evaluatePc_;
    result.imports = std::move(imports_);
    result.exports = std::move(exports_);
    return result;
}

void Compiler::compileProgram() {
    strict_ = hasUseStrictDirective(program_.body);
    code_->isScriptProgram = true;
    code_->strict = strict_;
    globalCode_ = true;
    hoistScope(program_.body, true);
    for (const frontend::NodePtr& statement : program_.body) {
        if (statement) compileStatement(*statement);
    }
    code_->halt();
}

void Compiler::compileEvalProgram() {
    strict_ = options_.evalStrict;
    globalCode_ = false;
    // sloppy direct eval：var/函数声明进入调用方变量环境（declare_var 只建
    // 缺失的槽，不覆盖已有绑定）；let/const 进入 eval 自己的词法作用域。
    if (!options_.evalStrict) {
        hoistVarSlots(program_.body, true);
        hoistEvalFunctions(program_.body);
    }
    code_->pushScope();
    // 严格 eval 的 eval 词法作用域自身带 strict 标志，供 with/未声明赋值/
    // delete 等运行期严格性判定使用；sloppy eval 保持继承的宽松语义。
    if (options_.evalStrict) code_->setScopeStrict();
    if (options_.evalStrict) {
        hoistVarSlots(program_.body, true);
        hoistEvalFunctions(program_.body);
    }
    hoistLexicalSlots(program_.body);

    // 完成值寄存器：初始 EMPTY（Uninitialized 哨兵），由各语句按
    // Completion Record 规则写入；嵌套函数体不参与。
    code_->pushUninitialized();
    code_->define(kEvalCompletion, true, true);
    code_->enableCompletion();
    evalCompletion_ = true;
    for (const frontend::NodePtr& statement : program_.body) {
        if (statement) compileStatement(*statement);
    }
    evalCompletion_ = false;
    code_->loadCompletionResult(kEvalCompletion);
    // 作用域由 evalSource 在返回值归属调用方后再弹出（异常路径也统一收口）。
    code_->ret();
}

std::string Compiler::newCompletionSlot(std::string_view prefix) {
    return std::string(prefix) + std::to_string(completionSlotCounter_++);
}

void Compiler::emitDefineCompletionSlot(const std::string& slot) {
    code_->pushUndefined();
    code_->defineIfMissing(slot);
}

void Compiler::emitSaveCompletion(const std::string& slot) {
    code_->load(kEvalCompletion);
    code_->store(slot);
    code_->pushUninitialized();
    code_->store(kEvalCompletion);
}

void Compiler::emitMergeCompletion(const std::string& slot) {
    code_->mergeCompletion(kEvalCompletion, slot);
}

void Compiler::emitCompletionToUndefined() {
    code_->completionToUndefined(kEvalCompletion);
}

void Compiler::emitClearCompletion() {
    code_->pushUninitialized();
    code_->store(kEvalCompletion);
}

void Compiler::emitUpdateLoopValue(const std::string& slot) {
    code_->updateLoopValue(kEvalCompletion, slot);
}

void Compiler::emitLoopEnd(const std::string& slot) {
    code_->loopEnd(kEvalCompletion, slot);
}

void Compiler::emitLabelledEnd() {
    code_->labelledEnd(kEvalCompletion);
}

void Compiler::compileModule() {
    strict_ = true;
    globalCode_ = false;
    // [1] 符号定义区：只建槽。所有模块先声明，再统一 link。
    declarePc_ = code_->here();
    hoistSlots(program_.body, true);
    // export default 的匿名函数/表达式需要一个隐藏槽位承载默认导出值。
    for (const NodePtr& statement : program_.body) {
        if (statement && statement->kind() == NodeKind::ExportDefaultDeclaration &&
            usesDefaultSlot(static_cast<const frontend::ExportDefaultDeclaration&>(
                *statement))) {
            code_->pushUninitialized();
            code_->define(kDefaultSlot, false, false);
            break;
        }
    }
    code_->halt();

    // [2] 函数定义区：import 绑定完成后创建函数对象。
    instantiatePc_ = code_->here();
    hoistFunctions(program_.body);
    code_->halt();

    // [3] 源码逻辑入口：求值阶段执行。
    evaluatePc_ = code_->here();
    for (const NodePtr& statement : program_.body) {
        if (statement) compileStatement(*statement);
    }
    code_->halt();
}

void Compiler::compileStatement(const Node& node) {
    uint32_t begin = code_->here();
    switch (node.kind()) {
        case NodeKind::EmptyStatement:
        case NodeKind::DebuggerStatement:
        case NodeKind::Directive:
            // debugger 暂为 no-op；指令序言（"use strict"）只影响 Sema/编译选项。
            break;
        case NodeKind::ExpressionStatement:
            compileExpressionStatement(
                static_cast<const frontend::ExpressionStatement&>(node));
            break;
        case NodeKind::ReturnStatement:
            compileReturnStatement(
                static_cast<const frontend::ReturnStatement&>(node));
            break;
        case NodeKind::BlockStatement:
            compileBlockStatement(
                static_cast<const frontend::BlockStatement&>(node));
            break;
        case NodeKind::FunctionDeclaration:
            // 已在作用域入口提升并创建，执行期无操作。
            break;
        case NodeKind::ClassDeclaration:
            compileClassDeclaration(
                static_cast<const frontend::ClassDeclaration&>(node));
            break;
        case NodeKind::VariableDeclaration:
            compileVariableDeclaration(
                static_cast<const frontend::VariableDeclaration&>(node));
            break;
        case NodeKind::IfStatement:
            compileIfStatement(
                static_cast<const frontend::IfStatement&>(node));
            break;
        case NodeKind::ForStatement:
            compileForStatement(
                static_cast<const frontend::ForStatement&>(node));
            break;
        case NodeKind::ForInStatement:
            compileForInStatement(
                static_cast<const frontend::ForInStatement&>(node));
            break;
        case NodeKind::ForOfStatement:
            compileForOfStatement(
                static_cast<const frontend::ForOfStatement&>(node));
            break;
        case NodeKind::SwitchStatement:
            compileSwitchStatement(
                static_cast<const frontend::SwitchStatement&>(node));
            break;
        case NodeKind::WhileStatement:
            compileWhileStatement(
                static_cast<const frontend::WhileStatement&>(node));
            break;
        case NodeKind::DoWhileStatement:
            compileDoWhileStatement(
                static_cast<const frontend::DoWhileStatement&>(node));
            break;
        case NodeKind::LabeledStatement:
            compileLabeledStatement(
                static_cast<const frontend::LabeledStatement&>(node));
            break;
        case NodeKind::BreakStatement:
            compileBreakStatement(
                static_cast<const frontend::BreakStatement&>(node));
            break;
        case NodeKind::ContinueStatement:
            compileContinueStatement(
                static_cast<const frontend::ContinueStatement&>(node));
            break;
        case NodeKind::ThrowStatement:
            compileThrowStatement(
                static_cast<const frontend::ThrowStatement&>(node));
            break;
        case NodeKind::WithStatement:
            compileWithStatement(
                static_cast<const frontend::WithStatement&>(node));
            break;
        case NodeKind::TryStatement:
            compileTryStatement(
                static_cast<const frontend::TryStatement&>(node));
            break;
        case NodeKind::ImportDeclaration:
            compileImportDeclaration(
                static_cast<const frontend::ImportDeclaration&>(node));
            break;
        case NodeKind::ExportNamedDeclaration:
            compileExportNamedDeclaration(
                static_cast<const frontend::ExportNamedDeclaration&>(node));
            break;
        case NodeKind::ExportDefaultDeclaration:
            compileExportDefaultDeclaration(
                static_cast<const frontend::ExportDefaultDeclaration&>(node));
            break;
        case NodeKind::ExportAllDeclaration:
            compileExportAllDeclaration(
                static_cast<const frontend::ExportAllDeclaration&>(node));
            break;
        default:
            error(node, "compiler: unsupported statement node kind " +
                            std::to_string(static_cast<int>(node.kind())));
            break;
    }
    mapNode(node, begin);
}

void Compiler::compileExpression(const Node& node) {
    uint32_t begin = code_->here();
    switch (node.kind()) {
        case NodeKind::Identifier:
            compileIdentifier(static_cast<const frontend::Identifier&>(node));
            break;
        case NodeKind::NumericLiteral:
            compileNumericLiteral(
                static_cast<const frontend::NumericLiteral&>(node));
            break;
        case NodeKind::BigIntLiteral:
            compileBigIntLiteral(
                static_cast<const frontend::BigIntLiteral&>(node));
            break;
        case NodeKind::StringLiteral:
            compileStringLiteral(
                static_cast<const frontend::StringLiteral&>(node));
            break;
        case NodeKind::RegExpLiteral:
            compileRegExpLiteral(
                static_cast<const frontend::RegExpLiteral&>(node));
            break;
        case NodeKind::BooleanLiteral:
            compileBooleanLiteral(
                static_cast<const frontend::BooleanLiteral&>(node));
            break;
        case NodeKind::NullLiteral:
            compileNullLiteral(static_cast<const frontend::NullLiteral&>(node));
            break;
        case NodeKind::BinaryExpression:
            compileBinaryExpression(
                static_cast<const frontend::BinaryExpression&>(node));
            break;
        case NodeKind::UnaryExpression:
            compileUnaryExpression(
                static_cast<const frontend::UnaryExpression&>(node));
            break;
        case NodeKind::LogicalExpression:
            compileLogicalExpression(
                static_cast<const frontend::LogicalExpression&>(node));
            break;
        case NodeKind::ConditionalExpression:
            compileConditionalExpression(
                static_cast<const frontend::ConditionalExpression&>(node));
            break;
        case NodeKind::SequenceExpression:
            compileSequenceExpression(
                static_cast<const frontend::SequenceExpression&>(node));
            break;
        case NodeKind::FunctionExpression:
            compileFunctionExpression(
                static_cast<const frontend::FunctionExpression&>(node));
            break;
        case NodeKind::ArrowFunctionExpression:
            compileArrowFunctionExpression(
                static_cast<const frontend::ArrowFunctionExpression&>(node));
            break;
        case NodeKind::CallExpression:
            compileCallExpression(
                static_cast<const frontend::CallExpression&>(node));
            break;
        case NodeKind::NewExpression:
            compileNewExpression(
                static_cast<const frontend::NewExpression&>(node));
            break;
        case NodeKind::MetaProperty:
            compileMetaProperty(
                static_cast<const frontend::MetaProperty&>(node));
            break;
        case NodeKind::ImportExpression:
            compileImportExpression(
                static_cast<const frontend::ImportExpression&>(node));
            break;
        case NodeKind::TemplateLiteral:
            compileTemplateLiteral(
                static_cast<const frontend::TemplateLiteral&>(node));
            break;
        case NodeKind::TaggedTemplateExpression:
            compileTaggedTemplateExpression(
                static_cast<const frontend::TaggedTemplateExpression&>(node));
            break;
        case NodeKind::MemberExpression:
            compileMemberExpression(
                static_cast<const frontend::MemberExpression&>(node));
            break;
        case NodeKind::ChainExpression:
            compileChainExpression(
                static_cast<const frontend::ChainExpression&>(node));
            break;
        case NodeKind::ClassExpression:
            compileClassExpression(
                static_cast<const frontend::ClassExpression&>(node));
            break;
        case NodeKind::ObjectExpression:
            compileObjectExpression(
                static_cast<const frontend::ObjectExpression&>(node));
            break;
        case NodeKind::ArrayExpression:
            compileArrayExpression(
                static_cast<const frontend::ArrayExpression&>(node));
            break;
        case NodeKind::ThisExpression:
            compileThisExpression(
                static_cast<const frontend::ThisExpression&>(node));
            break;
        case NodeKind::YieldExpression:
            compileYieldExpression(
                static_cast<const frontend::YieldExpression&>(node));
            break;
        case NodeKind::AwaitExpression:
            compileAwaitExpression(
                static_cast<const frontend::AwaitExpression&>(node));
            break;
        case NodeKind::AssignmentExpression:
            compileAssignmentExpression(
                static_cast<const frontend::AssignmentExpression&>(node));
            break;
        case NodeKind::UpdateExpression:
            compileUpdateExpression(
                static_cast<const frontend::UpdateExpression&>(node));
            break;
        case NodeKind::SuperExpression:
            error(node, "compiler: super is not supported");
            code_->pushUndefined();
            break;
        default:
            error(node, "compiler: unsupported expression node kind " +
                            std::to_string(static_cast<int>(node.kind())));
            // 保持“表达式留下一个值”的栈契约，便于诊断后续指令。
            code_->pushUndefined();
            break;
    }
    mapNode(node, begin);
}

bool Compiler::isAnonymousFunctionDefinition(const frontend::Node& node) {
    switch (node.kind()) {
        case NodeKind::FunctionExpression:
            return static_cast<const frontend::FunctionExpression&>(node).id ==
                   nullptr;
        case NodeKind::ArrowFunctionExpression:
            return true;
        case NodeKind::ClassExpression:
            return static_cast<const frontend::ClassExpression&>(node).id ==
                   nullptr;
        default:
            return false;
    }
}

void Compiler::compileNamedExpression(const frontend::Node& node,
                                      std::string_view name) {
    compileExpression(node);
    if (name.empty() || !isAnonymousFunctionDefinition(node)) return;
    if (node.kind() == NodeKind::ClassExpression) {
        // class 可能有静态 name 方法占用自有 "name"，不得覆盖。
        code_->setFuncNameIfAbsent(name);
    } else {
        code_->setFuncName(name);
    }
}

void Compiler::compileNamedExpressionForKey(const frontend::Node& node,
                                            std::string_view prefix) {
    compileExpression(node);
    if (!isAnonymousFunctionDefinition(node)) return;
    code_->setFuncNameFromKey(prefix);
}

std::string Compiler::staticKeyName(const frontend::Node* key) {
    if (!key) return {};
    switch (key->kind()) {
        case NodeKind::Identifier:
            return static_cast<const frontend::Identifier&>(*key).name;
        case NodeKind::StringLiteral:
            return static_cast<const frontend::StringLiteral&>(*key).value;
        default:
            return {};
    }
}

// —— 语句 ——

void Compiler::compileBlockStatement(const frontend::BlockStatement& node) {
    code_->pushScope();
    hoistScope(node.body, false, true);
    for (const frontend::NodePtr& statement : node.body) {
        if (statement) compileStatement(*statement);
    }
    code_->popScope();
}

void Compiler::compileExpressionStatement(
    const frontend::ExpressionStatement& node) {
    if (!node.expression) return;
    compileExpression(*node.expression);
    if (evalCompletion_) {
        code_->dup();
        code_->store(kEvalCompletion);
    }
    code_->pop();
}

void Compiler::compileReturnStatement(const frontend::ReturnStatement& node) {
    if (node.argument) {
        compileExpression(*node.argument);
    } else {
        code_->pushUndefined();
    }
    code_->ret();
}

void Compiler::compileIfStatement(const frontend::IfStatement& node) {
    if (!node.test) return;
    std::string completionSlot;
    if (evalCompletion_) {
        completionSlot = newCompletionSlot("*eval-if-");
        emitDefineCompletionSlot(completionSlot);
        emitSaveCompletion(completionSlot);
    }
    compileExpression(*node.test);
    uint32_t toElse = code_->jfalse();
    auto compileBranch = [&](const frontend::NodePtr& branch) {
        if (!branch) return;
        // Annex B.3.3：if 子句中的函数声明按合成块处理。
        if (!strict_ && branch->kind() == NodeKind::FunctionDeclaration) {
            compileAnnexBFunction(
                static_cast<const frontend::FunctionDeclaration&>(*branch),
                true);
            return;
        }
        compileStatement(*branch);
    };
    compileBranch(node.consequent);
    if (node.alternate) {
        uint32_t toEnd = code_->jmp();
        code_->patchU32(toElse + 2, code_->here());
        compileBranch(node.alternate);
        code_->patchU32(toEnd + 2, code_->here());
    } else {
        code_->patchU32(toElse + 2, code_->here());
    }
    if (evalCompletion_) {
        emitCompletionToUndefined();
        emitMergeCompletion(completionSlot);
    }
}

void Compiler::compileForStatement(
    const frontend::ForStatement& node,
    const std::vector<std::string>& labels) {
    // for 的 let/const 初始化器不在外层提升：在循环作用域内直接建槽。
    const frontend::VariableDeclaration* lexicalInit = nullptr;
    if (node.init && node.init->kind() == NodeKind::VariableDeclaration) {
        const auto& declaration =
            static_cast<const frontend::VariableDeclaration&>(*node.init);
        if (declaration.variableKind != frontend::VariableKind::Var) {
            lexicalInit = &declaration;
        }
    }

    std::string outerSlot;
    std::string loopSlot;
    if (evalCompletion_) {
        outerSlot = newCompletionSlot("*eval-loop-outer-");
        loopSlot = newCompletionSlot("*eval-loop-v-");
        emitDefineCompletionSlot(outerSlot);
        emitDefineCompletionSlot(loopSlot);
        emitSaveCompletion(outerSlot);
        code_->pushUndefined();
        code_->store(loopSlot);
    }

    if (lexicalInit != nullptr) code_->pushScope();

    if (node.init) {
        if (node.init->kind() != NodeKind::VariableDeclaration) {
            compileExpression(*node.init);
            code_->pop();
        } else if (lexicalInit == nullptr) {
            compileVariableDeclaration(
                static_cast<const frontend::VariableDeclaration&>(*node.init));
        } else {
            bool mutableBinding =
                lexicalInit->variableKind == frontend::VariableKind::Let;
            // 先为所有绑定建 TDZ 槽：初始化器求值期间（含后续 declarator）
            // 这些名字必须可见但不可读（ES2024 §14.7.1）。
            for (const NodePtr& declarator : lexicalInit->declarations) {
                if (!declarator ||
                    declarator->kind() != NodeKind::VariableDeclarator) {
                    continue;
                }
                const auto& decl = static_cast<const frontend::VariableDeclarator&>(
                    *declarator);
                if (!decl.id) continue;
                std::vector<std::string> names;
                collectPatternNames(*decl.id, names);
                for (const std::string& name : names) {
                    code_->pushUninitialized();
                    code_->define(name, mutableBinding, false);
                }
            }
            for (const NodePtr& declarator : lexicalInit->declarations) {
                if (!declarator ||
                    declarator->kind() != NodeKind::VariableDeclarator) {
                    continue;
                }
                const auto& decl = static_cast<const frontend::VariableDeclarator&>(
                    *declarator);
                if (!decl.id) continue;
                if (decl.init) {
                    if (decl.id->kind() == NodeKind::Identifier) {
                        compileNamedExpression(
                            *decl.init,
                            static_cast<const frontend::Identifier&>(*decl.id)
                                .name);
                    } else {
                        compileExpression(*decl.init);
                    }
                } else {
                    code_->pushUndefined();
                }
                compileBindingPattern(*decl.id, true, 0);
            }
        }
    }

    std::vector<std::string> effective = labels;
    if (effective.empty()) effective.push_back("");
    const uint32_t loopValueName =
        evalCompletion_ ? code_->addString(loopSlot) : UINT32_MAX;

    if (lexicalInit != nullptr) {
        // 逐迭代环境：循环作用域持有 let/const 槽；每次迭代复制到子作用域，
        // 闭包捕获的是该迭代的子作用域，迭代结束后写回循环作用域。
        std::vector<std::string> names;
        for (const NodePtr& declarator : lexicalInit->declarations) {
            if (!declarator ||
                declarator->kind() != NodeKind::VariableDeclarator) {
                continue;
            }
            const auto& decl =
                static_cast<const frontend::VariableDeclarator&>(*declarator);
            if (decl.id && decl.id->kind() == NodeKind::Identifier) {
                names.push_back(
                    static_cast<const frontend::Identifier&>(*decl.id).name);
            }
        }

        uint32_t loop = code_->here();
        code_->iterationEnv(names);
        // label 帧指向本次迭代作用域：continue 不会提前弹掉它。
        std::vector<uint32_t> pushes;
        for (const std::string& label : effective) {
            pushes.push_back(code_->pushLabel(label, 0, 0, loopValueName));
        }

        uint32_t toEnd = 0;
        if (node.test) {
            compileExpression(*node.test);
            toEnd = code_->jfalse();
        }
        if (evalCompletion_) emitClearCompletion();
        if (node.body) compileStatement(*node.body);
        if (evalCompletion_) emitUpdateLoopValue(loopSlot);

        // continue 目标是 update；先清理本次迭代的 label 帧，再写回并弹出作用域。
        uint32_t updatePc = code_->here();
        for (uint32_t push : pushes) code_->patchU32(push + 6, updatePc);
        for (auto it = effective.rbegin(); it != effective.rend(); ++it) {
            code_->popLabelIfTop(*it);
        }
        code_->commitIterationEnv(names);
        if (node.update) {
            compileExpression(*node.update);
            code_->pop();
        }
        code_->jmp(loop);

        // 正常退出：清理 label、迭代作用域与循环作用域。
        uint32_t exitPc = code_->here();
        if (toEnd != 0) code_->patchU32(toEnd + 2, exitPc);
        for (auto it = effective.rbegin(); it != effective.rend(); ++it) {
            code_->popLabelIfTop(*it);
        }
        code_->popScope();
        code_->popScope();
        uint32_t done = code_->jmp();

        // break 已由 VM 回退到本次迭代作用域，只需弹两层作用域。
        uint32_t breakTarget = code_->here();
        code_->popScope();
        code_->popScope();
        code_->patchU32(done + 2, code_->here());
        for (uint32_t push : pushes) code_->patchU32(push + 10, breakTarget);
        if (evalCompletion_) {
            emitLoopEnd(loopSlot);
            emitMergeCompletion(outerSlot);
        }
        return;
    }

    std::vector<uint32_t> pushes;
    for (const std::string& label : effective) {
        pushes.push_back(code_->pushLabel(label, 0, 0, loopValueName));
    }

    uint32_t loop = code_->here();
    uint32_t toEnd = 0;
    if (node.test) {
        compileExpression(*node.test);
        toEnd = code_->jfalse();
    }
    if (evalCompletion_) emitClearCompletion();
    if (node.body) compileStatement(*node.body);
    if (evalCompletion_) emitUpdateLoopValue(loopSlot);

    // continue 目标是 update，而不是循环头（否则 update 会被跳过）。
    uint32_t updatePc = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 6, updatePc);
    if (node.update) {
        compileExpression(*node.update);
        code_->pop();
    }
    code_->jmp(loop);

    // 正常退出：pop_label；break 目标在其后。
    uint32_t exitPc = code_->here();
    if (toEnd != 0) code_->patchU32(toEnd + 2, exitPc);
    for (auto it = effective.rbegin(); it != effective.rend(); ++it) {
        code_->popLabel(*it);
    }
    uint32_t endPc = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 10, endPc);
    if (evalCompletion_) {
        emitLoopEnd(loopSlot);
        emitMergeCompletion(outerSlot);
    }
}

void Compiler::compileForTarget(const frontend::NodePtr& left,
                                std::string_view tempName) {
    // 前置条件：栈顶是本次迭代的值。
    if (!left) {
        error(program_, "compiler: for-in/of missing binding target");
        code_->pop();
        return;
    }
    if (left->kind() == NodeKind::VariableDeclaration) {
        const auto& declaration =
            static_cast<const frontend::VariableDeclaration&>(*left);
        const frontend::NodePtr* target = nullptr;
        for (const frontend::NodePtr& declarator : declaration.declarations) {
            if (declarator &&
                declarator->kind() == NodeKind::VariableDeclarator) {
                target = &static_cast<const frontend::VariableDeclarator&>(
                             *declarator)
                             .id;
                break;
            }
        }
        if (target == nullptr || !*target) {
            error(*left, "compiler: for-in/of missing binding target");
            code_->pop();
            return;
        }
        const frontend::Node& pattern = **target;
        if (pattern.kind() == NodeKind::Identifier) {
            const std::string& name =
                static_cast<const frontend::Identifier&>(pattern).name;
            if (declaration.variableKind == frontend::VariableKind::Var) {
                code_->store(name);
            } else {
                code_->define(
                    name,
                    declaration.variableKind == frontend::VariableKind::Let);
            }
            return;
        }
        if (pattern.kind() == NodeKind::ArrayPattern ||
            pattern.kind() == NodeKind::ObjectPattern) {
            // var 的名字已在函数/模块作用域提升；let/const 在当前迭代作用域建槽。
            if (declaration.variableKind != frontend::VariableKind::Var) {
                const bool mutableBinding =
                    declaration.variableKind == frontend::VariableKind::Let;
                std::vector<std::string> names;
                collectPatternNames(pattern, names);
                for (const std::string& name : names) {
                    code_->pushUninitialized();
                    code_->define(name, mutableBinding, false);
                }
                // 解构临时槽位放在独立子作用域，避免下一轮重复 define。
                code_->pushScope();
                compileBindingPattern(pattern, true, 0);
                code_->popScope();
            } else {
                code_->pushScope();
                compileBindingPattern(pattern, false, 0);
                code_->popScope();
            }
            return;
        }
        error(*left, "compiler: unsupported for-in/of binding pattern");
        code_->pop();
        return;
    }
    if (left->kind() == NodeKind::ArrayPattern ||
        left->kind() == NodeKind::ObjectPattern) {
        code_->pushScope();
        compileBindingPattern(*left, false, 0);
        code_->popScope();
        return;
    }
    if (left->kind() == NodeKind::Identifier) {
        code_->store(static_cast<const frontend::Identifier&>(*left).name);
        return;
    }
    if (left->kind() == NodeKind::MemberExpression) {
        const auto& member =
            static_cast<const frontend::MemberExpression&>(*left);
        if (member.optional) {
            error(*left, "compiler: optional chaining not supported");
            code_->pop();
            return;
        }
        if (!member.object) {
            error(*left, "compiler: for-in/of target missing object");
            code_->pop();
            return;
        }
        code_->store(tempName);
        compileExpression(*member.object);
        if (member.property &&
            member.property->kind() == NodeKind::PrivateIdentifier) {
            const std::string& name =
                static_cast<const frontend::PrivateIdentifier&>(*member.property)
                    .name;
            const PrivateInfo* info = findPrivateInfo(name);
            if (info == nullptr) {
                error(*left, "compiler: undeclared private name #" + name);
                code_->pop();
                return;
            }
            code_->load(tempName);
            if (info->field) {
                code_->setPrivateField(name);
            } else {
                code_->setPrivateSetter(name);
            }
            code_->pop();
            return;
        }
        compileMemberKey(member);
        code_->load(tempName);
        code_->setProp();
        code_->pop();
        return;
    }
    error(*left, "compiler: unsupported for-in/of binding target");
    code_->pop();
}

void Compiler::compileForInStatement(
    const frontend::ForInStatement& node,
    const std::vector<std::string>& labels) {
    if (!node.right) {
        error(node, "compiler: for-in missing right-hand side");
        return;
    }

    const frontend::VariableDeclaration* declaration = nullptr;
    if (node.left && node.left->kind() == NodeKind::VariableDeclaration) {
        declaration = &static_cast<const frontend::VariableDeclaration&>(
            *node.left);
    }
    bool lexical = declaration != nullptr &&
                   declaration->variableKind != frontend::VariableKind::Var;

    std::vector<std::string> effective = labels;
    if (effective.empty()) effective.push_back("");

    std::string outerSlot;
    std::string loopSlot;
    if (evalCompletion_) {
        outerSlot = newCompletionSlot("*eval-loop-outer-");
        loopSlot = newCompletionSlot("*eval-loop-v-");
        emitDefineCompletionSlot(outerSlot);
        emitDefineCompletionSlot(loopSlot);
        emitSaveCompletion(outerSlot);
        code_->pushUndefined();
        code_->store(loopSlot);
    }
    const uint32_t loopValueName =
        evalCompletion_ ? code_->addString(loopSlot) : UINT32_MAX;

    code_->pushScope();
    code_->pushUndefined();
    code_->define(kForKeys);
    code_->pushUndefined();
    code_->define(kForIndex);
    code_->pushUndefined();
    code_->define(kForValue);
    code_->pushUndefined();
    code_->define(kForOwner);

    compileExpression(*node.right);
    code_->forInKeys();
    code_->store(kForKeys);
    code_->push(0.0);
    code_->store(kForIndex);

    std::vector<uint32_t> pushes;
    for (const std::string& label : effective) {
        pushes.push_back(code_->pushLabel(label, 0, 0, loopValueName));
    }
    uint32_t loop = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 6, loop);

    // index < keys.length
    code_->load(kForIndex);
    code_->load(kForKeys);
    code_->push("length");
    code_->getProp();
    code_->lt();
    uint32_t toEnd = code_->jfalse();

    // owner = entries[index]; key = entries[index + 1]
    code_->load(kForKeys);
    code_->load(kForIndex);
    code_->getProp();
    code_->store(kForOwner);
    code_->load(kForKeys);
    code_->load(kForIndex);
    code_->push(1.0);
    code_->add();
    code_->getProp();
    code_->store(kForValue);

    // 处理时重新检查：属性可能已被删除或改为非 enumerable。
    code_->load(kForOwner);
    code_->load(kForValue);
    code_->isEnumerableOwn();
    uint32_t toSkip = code_->jfalse();

    if (lexical) code_->pushScope();
    code_->load(kForValue);
    compileForTarget(node.left, kForValue);
    if (evalCompletion_) emitClearCompletion();
    if (node.body) compileStatement(*node.body);
    if (evalCompletion_) emitUpdateLoopValue(loopSlot);
    if (lexical) code_->popScope();

    uint32_t skipPc = code_->here();
    code_->patchU32(toSkip + 2, skipPc);
    code_->load(kForIndex);
    code_->push(2.0);
    code_->add();
    code_->store(kForIndex);
    code_->jmp(loop);

    uint32_t exitPc = code_->here();
    code_->patchU32(toEnd + 2, exitPc);
    for (auto it = effective.rbegin(); it != effective.rend(); ++it) {
        code_->popLabel(*it);
    }
    code_->popScope();
    uint32_t endPc = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 10, endPc);
    if (evalCompletion_) {
        emitLoopEnd(loopSlot);
        emitMergeCompletion(outerSlot);
    }
}

void Compiler::compileForOfStatement(
    const frontend::ForOfStatement& node,
    const std::vector<std::string>& labels) {
    if (!node.right) {
        error(node, "compiler: for-of missing right-hand side");
        return;
    }

    const frontend::VariableDeclaration* declaration = nullptr;
    if (node.left && node.left->kind() == NodeKind::VariableDeclaration) {
        declaration = &static_cast<const frontend::VariableDeclaration&>(
            *node.left);
    }
    bool lexical = declaration != nullptr &&
                   declaration->variableKind != frontend::VariableKind::Var;

    std::vector<std::string> effective = labels;
    if (effective.empty()) effective.push_back("");

    std::string outerSlot;
    std::string loopSlot;
    if (evalCompletion_) {
        outerSlot = newCompletionSlot("*eval-loop-outer-");
        loopSlot = newCompletionSlot("*eval-loop-v-");
        emitDefineCompletionSlot(outerSlot);
        emitDefineCompletionSlot(loopSlot);
        emitSaveCompletion(outerSlot);
        code_->pushUndefined();
        code_->store(loopSlot);
    }
    const uint32_t loopValueName =
        evalCompletion_ ? code_->addString(loopSlot) : UINT32_MAX;

    code_->pushScope();
    code_->pushUndefined();
    code_->define(kForIterator);
    code_->pushUndefined();
    code_->define(kForResult);
    code_->pushUndefined();
    code_->define(kForValue);
    code_->pushUndefined();
    code_->define(kForDone);

    compileExpression(*node.right);
    if (node.await) {
        if (functionDepth_ == 0) code_->hasTopLevelAwait = true;
        code_->getAsyncIterator();
    } else {
        code_->getIterator();
    }
    code_->store(kForIterator);
    code_->pushBoolean(false);
    code_->store(kForDone);

    // try/finally：非正常完成（break/return/throw）时 IteratorClose。
    // 内部实现帧：跨出时不把 break/continue 的 EMPTY 完成值归一化为
    // undefined（该归一化只属于用户 try 语句）。
    uint32_t tryBeginPc = code_->tryBeginInternal();
    std::vector<uint32_t> pushes;
    for (const std::string& label : effective) {
        pushes.push_back(code_->pushLabel(label, 0, 0, loopValueName));
    }
    uint32_t loop = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 6, loop);

    code_->load(kForIterator);
    code_->iteratorNext();
    if (node.await) code_->awaitValue();
    code_->store(kForResult);

    code_->load(kForResult);
    code_->push("done");
    code_->getProp();
    uint32_t toNotDone = code_->jfalse();

    // done：标记正常完成并跳到出口（不 close）。
    code_->pushBoolean(true);
    code_->store(kForDone);
    uint32_t toExit = code_->jmp();

    uint32_t notDone = code_->here();
    code_->patchU32(toNotDone + 2, notDone);
    code_->load(kForResult);
    code_->push("value");
    code_->getProp();
    code_->store(kForValue);

    if (lexical) code_->pushScope();
    code_->load(kForValue);
    compileForTarget(node.left, kForValue);
    if (evalCompletion_) emitClearCompletion();
    if (node.body) compileStatement(*node.body);
    if (evalCompletion_) emitUpdateLoopValue(loopSlot);
    if (lexical) code_->popScope();
    code_->jmp(loop);

    uint32_t exitPc = code_->here();
    code_->patchU32(toExit + 2, exitPc);
    for (auto it = effective.rbegin(); it != effective.rend(); ++it) {
        code_->popLabelIfTop(*it);
    }
    uint32_t breakTarget = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 10, breakTarget);
    code_->tryBodyEnd();

    uint32_t finallyStart = code_->here();
    code_->load(kForDone);
    uint32_t toSkipClose = code_->jtrue();
    code_->load(kForIterator);
    if (node.await) {
        // AsyncIteratorClose：调用 return 并 Await 其结果（§7.4.7）。
        code_->asyncIteratorClose();
        code_->awaitValue();
        code_->pop();
    } else {
        code_->iteratorClose();
    }
    uint32_t skipClose = code_->here();
    code_->patchU32(toSkipClose + 2, skipClose);
    code_->finallyEnd();

    uint32_t tryEnd = code_->here();
    code_->patchU32(tryBeginPc + 2, 0);
    code_->patchU32(tryBeginPc + 6, finallyStart);
    code_->patchU32(tryBeginPc + 10, tryEnd);
    if (evalCompletion_) {
        emitLoopEnd(loopSlot);
        emitMergeCompletion(outerSlot);
    }

    code_->popScope();
}

void Compiler::compileSwitchStatement(
    const frontend::SwitchStatement& node,
    const std::vector<std::string>& labels) {
    if (!node.discriminant) {
        error(node, "compiler: switch missing discriminant");
        return;
    }

    std::string outerSlot;
    std::string loopSlot;
    if (evalCompletion_) {
        outerSlot = newCompletionSlot("*eval-loop-outer-");
        loopSlot = newCompletionSlot("*eval-loop-v-");
        emitDefineCompletionSlot(outerSlot);
        emitDefineCompletionSlot(loopSlot);
        emitSaveCompletion(outerSlot);
        // CaseBlockEvaluation：V 从 undefined 开始（不是 EMPTY）。
        code_->pushUndefined();
        code_->store(loopSlot);
    }

    // CaseBlock 共享一个词法作用域：先为所有 clause 的 let/const/函数建槽。
    code_->pushScope();
    code_->pushUndefined();
    code_->define(kSwitchDisc);
    for (const NodePtr& caseNode : node.cases) {
        if (!caseNode || caseNode->kind() != NodeKind::SwitchCase) continue;
        hoistScope(
            static_cast<const frontend::SwitchCase&>(*caseNode).consequent,
            false, true);
    }

    compileExpression(*node.discriminant);
    code_->store(kSwitchDisc);

    std::vector<std::string> effective = labels;
    if (effective.empty()) effective.push_back("");
    std::vector<uint32_t> pushes;
    for (const std::string& label : effective) {
        pushes.push_back(code_->pushLabel(label, 0, 0));
    }

    // 先发射全部 case 测试（default 跳过），匹配后跳到对应 body。
    std::vector<uint32_t> matchJumps(node.cases.size(), 0);
    int defaultIndex = -1;
    for (size_t i = 0; i < node.cases.size(); ++i) {
        const NodePtr& caseNode = node.cases[i];
        if (!caseNode || caseNode->kind() != NodeKind::SwitchCase) continue;
        const auto& switchCase =
            static_cast<const frontend::SwitchCase&>(*caseNode);
        if (!switchCase.test) {
            defaultIndex = static_cast<int>(i);
            continue;
        }
        code_->load(kSwitchDisc);
        compileExpression(*switchCase.test);
        code_->strictEqual();
        matchJumps[i] = code_->jtrue();
    }
    uint32_t noMatch = code_->jmp();

    // 顺序发射 case body，支持 fallthrough。
    std::vector<uint32_t> bodyPc(node.cases.size(), 0);
    for (size_t i = 0; i < node.cases.size(); ++i) {
        bodyPc[i] = code_->here();
        const NodePtr& caseNode = node.cases[i];
        if (!caseNode || caseNode->kind() != NodeKind::SwitchCase) continue;
        const auto& switchCase =
            static_cast<const frontend::SwitchCase&>(*caseNode);
        // 每个 clause 从 EMPTY 开始；正常完成后若非 EMPTY 则更新 V。
        if (evalCompletion_) emitClearCompletion();
        for (const NodePtr& statement : switchCase.consequent) {
            if (statement) compileStatement(*statement);
        }
        if (evalCompletion_) emitUpdateLoopValue(loopSlot);
    }
    uint32_t normalEnd = code_->here();

    for (size_t i = 0; i < matchJumps.size(); ++i) {
        if (matchJumps[i] != 0) code_->patchU32(matchJumps[i] + 2, bodyPc[i]);
    }
    code_->patchU32(noMatch + 2,
                    defaultIndex >= 0 ? bodyPc[static_cast<size_t>(defaultIndex)]
                                      : normalEnd);

    for (auto it = effective.rbegin(); it != effective.rend(); ++it) {
        code_->popLabel(*it);
    }
    // break 已由 VM 回退到 switch 作用域，只需跳过 pop_label 清理作用域。
    uint32_t breakTarget = code_->here();
    if (evalCompletion_) {
        // 有 pending break 时取 break 值（EMPTY 则取 V），否则取 V。
        emitLoopEnd(loopSlot);
        emitMergeCompletion(outerSlot);
    }
    code_->popScope();
    uint32_t endPc = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 10, breakTarget);
    (void)endPc;
}

void Compiler::compileWhileStatement(
    const frontend::WhileStatement& node,
    const std::vector<std::string>& labels) {
    if (!node.test) return;

    std::string outerSlot;
    std::string loopSlot;
    if (evalCompletion_) {
        outerSlot = newCompletionSlot("*eval-loop-outer-");
        loopSlot = newCompletionSlot("*eval-loop-v-");
        emitDefineCompletionSlot(outerSlot);
        emitDefineCompletionSlot(loopSlot);
        emitSaveCompletion(outerSlot);
        code_->pushUndefined();
        code_->store(loopSlot);
    }

    std::vector<std::string> effective = labels;
    if (effective.empty()) effective.push_back("");
    const uint32_t loopValueName =
        evalCompletion_ ? code_->addString(loopSlot) : UINT32_MAX;
    std::vector<uint32_t> pushes;
    for (const std::string& label : effective) {
        pushes.push_back(code_->pushLabel(label, 0, 0, loopValueName));
    }
    // continue/回跳目标在 push_label 之后，避免重复压帧。
    uint32_t loop = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 6, loop);

    compileExpression(*node.test);
    uint32_t toEnd = code_->jfalse();
    if (evalCompletion_) emitClearCompletion();
    if (node.body) compileStatement(*node.body);
    if (evalCompletion_) emitUpdateLoopValue(loopSlot);
    code_->jmp(loop);

    // 正常退出先执行 pop_label；break 目标在 pop_label 之后。
    uint32_t exitPc = code_->here();
    code_->patchU32(toEnd + 2, exitPc);
    for (auto it = effective.rbegin(); it != effective.rend(); ++it) {
        code_->popLabel(*it);
    }
    uint32_t endPc = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 10, endPc);
    if (evalCompletion_) {
        emitLoopEnd(loopSlot);
        emitMergeCompletion(outerSlot);
    }
}

void Compiler::compileDoWhileStatement(
    const frontend::DoWhileStatement& node,
    const std::vector<std::string>& labels) {
    if (!node.test) return;

    std::string outerSlot;
    std::string loopSlot;
    if (evalCompletion_) {
        outerSlot = newCompletionSlot("*eval-loop-outer-");
        loopSlot = newCompletionSlot("*eval-loop-v-");
        emitDefineCompletionSlot(outerSlot);
        emitDefineCompletionSlot(loopSlot);
        emitSaveCompletion(outerSlot);
        code_->pushUndefined();
        code_->store(loopSlot);
    }

    std::vector<std::string> effective = labels;
    if (effective.empty()) effective.push_back("");
    const uint32_t loopValueName =
        evalCompletion_ ? code_->addString(loopSlot) : UINT32_MAX;
    std::vector<uint32_t> pushes;
    for (const std::string& label : effective) {
        pushes.push_back(code_->pushLabel(label, 0, 0, loopValueName));
    }
    uint32_t loop = code_->here();

    if (evalCompletion_) emitClearCompletion();
    if (node.body) compileStatement(*node.body);
    if (evalCompletion_) emitUpdateLoopValue(loopSlot);
    uint32_t testPc = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 6, testPc);
    compileExpression(*node.test);
    code_->jtrue(loop);

    uint32_t exitPc = code_->here();
    for (auto it = effective.rbegin(); it != effective.rend(); ++it) {
        code_->popLabel(*it);
    }
    uint32_t endPc = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 10, endPc);
    if (evalCompletion_) {
        emitLoopEnd(loopSlot);
        emitMergeCompletion(outerSlot);
    }
    (void)exitPc;
}

void Compiler::compileLabeledStatement(
    const frontend::LabeledStatement& node) {
    // 收集嵌套标签：a: b: while (...) 共享同一 start/end。
    std::vector<std::string> labels;
    const frontend::Node* body = &node;
    while (body != nullptr && body->kind() == NodeKind::LabeledStatement) {
        const auto& labeled = static_cast<const frontend::LabeledStatement&>(*body);
        labels.push_back(labeled.label);
        body = labeled.body.get();
    }
    if (body == nullptr) return;

    // Annex B.3.3：标签函数声明按 var 语义创建（Sema 已按 var-like 绑定）。
    if (!strict_ && body->kind() == NodeKind::FunctionDeclaration) {
        compileAnnexBFunction(
            static_cast<const frontend::FunctionDeclaration&>(*body), false);
        return;
    }

    if (body->kind() == NodeKind::ForStatement) {
        compileForStatement(
            static_cast<const frontend::ForStatement&>(*body), labels);
        return;
    }
    if (body->kind() == NodeKind::ForInStatement) {
        compileForInStatement(
            static_cast<const frontend::ForInStatement&>(*body), labels);
        return;
    }
    if (body->kind() == NodeKind::ForOfStatement) {
        compileForOfStatement(
            static_cast<const frontend::ForOfStatement&>(*body), labels);
        return;
    }
    if (body->kind() == NodeKind::SwitchStatement) {
        compileSwitchStatement(
            static_cast<const frontend::SwitchStatement&>(*body), labels);
        return;
    }
    if (body->kind() == NodeKind::WhileStatement) {
        compileWhileStatement(
            static_cast<const frontend::WhileStatement&>(*body), labels);
        return;
    }
    if (body->kind() == NodeKind::DoWhileStatement) {
        compileDoWhileStatement(
            static_cast<const frontend::DoWhileStatement&>(*body), labels);
        return;
    }

    // 非循环标签：startPc=0（不可 continue），endPc 为 break 目标。
    std::string outerSlot;
    if (evalCompletion_) {
        outerSlot = newCompletionSlot("*eval-label-");
        emitDefineCompletionSlot(outerSlot);
        emitSaveCompletion(outerSlot);
    }
    std::vector<uint32_t> pushes;
    for (const std::string& label : labels) {
        pushes.push_back(code_->pushLabel(label, 0, 0));
    }
    compileStatement(*body);
    for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
        code_->popLabel(*it);
    }
    uint32_t endPc = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 10, endPc);
    if (evalCompletion_) {
        // 匹配 label 的 break 转 NormalCompletion(break 值)，EMPTY 保持 EMPTY；
        // 再由外层 StatementList 的 UpdateEmpty 填入先前值。
        emitLabelledEnd();
        emitMergeCompletion(outerSlot);
    }
}

void Compiler::compileBreakStatement(
    const frontend::BreakStatement& node) {
    if (evalCompletion_) {
        // break 的完成值 = 当前 StatementList 值（UpdateEmpty(break, sl)）。
        // EMPTY 用 Uninitialized 哨兵携带，由消费方（循环/switch/标签）裁决。
        code_->load(kEvalCompletion);
        code_->breakValue(node.label);
        return;
    }
    code_->breakTo(node.label);
}

void Compiler::compileContinueStatement(
    const frontend::ContinueStatement& node) {
    if (evalCompletion_) {
        code_->load(kEvalCompletion);
        code_->continueValue(node.label);
        return;
    }
    code_->continueTo(node.label);
}

void Compiler::compileThrowStatement(const frontend::ThrowStatement& node) {
    if (node.argument) {
        compileExpression(*node.argument);
    } else {
        code_->pushUndefined();
    }
    code_->throwValue();
}

void Compiler::compileWithStatement(const frontend::WithStatement& node) {
    if (!node.object) {
        error(node, "compiler: with missing object");
        return;
    }
    compileExpression(*node.object);
    code_->pushWithScope();
    // WithStatement 返回 UpdateEmpty(C, undefined)：正常路径把 EMPTY 归一为
    // undefined；abrupt 路径借一个无目标的 try 帧做同样的归一化。
    uint32_t tryBeginPc = 0;
    if (evalCompletion_) {
        tryBeginPc = code_->tryBegin();
        emitClearCompletion();
    }
    ++withDepth_;
    if (node.body) compileStatement(*node.body);
    --withDepth_;
    if (evalCompletion_) {
        emitCompletionToUndefined();
        code_->tryBodyEnd();
        uint32_t tryEnd = code_->here();
        code_->patchU32(tryBeginPc + 2, 0);
        code_->patchU32(tryBeginPc + 6, 0);
        code_->patchU32(tryBeginPc + 10, tryEnd);
    }
    code_->popScope();
}

void Compiler::compileTryStatement(const frontend::TryStatement& node) {
    // catch/finally 是 out-of-line 块；出口由 try_body_end/catch_end/finally_end
    // 产生 Interrupt，VM 负责跳转与 try 帧平衡，编译器不发射 jmp。
    uint32_t tryBeginPc = code_->tryBegin();

    // try 体独立求值：B 从 EMPTY 开始（不继承外层 StatementList 值）。
    if (evalCompletion_) emitClearCompletion();
    if (node.block) compileStatement(*node.block);
    // try/catch（无 finally）：C = B，正常出口做 UpdateEmpty(C, undefined)。
    if (evalCompletion_ && !node.finalizer) emitCompletionToUndefined();
    code_->tryBodyEnd();

    uint32_t catchStart = 0;
    if (node.handler) {
        if (node.handler->kind() != NodeKind::CatchClause) {
            error(*node.handler, "compiler: unsupported catch clause");
        } else {
            catchStart = code_->here();
            // catch 只在异常路径进入：C 从 catch 体求值开始，先清空寄存器。
            if (evalCompletion_) emitClearCompletion();
            compileCatchClause(
                static_cast<const frontend::CatchClause&>(*node.handler));
            // try/catch（无 finally）：UpdateEmpty(C, undefined)。
            if (evalCompletion_ && !node.finalizer) emitCompletionToUndefined();
            code_->catchEnd();
        }
    }

    uint32_t finallyStart = 0;
    if (node.finalizer) {
        finallyStart = code_->here();
        std::string finallySlot;
        if (evalCompletion_) {
            // 保存 B/C（可能 EMPTY），清空寄存器供 finally 求值。
            finallySlot = newCompletionSlot("*eval-finally-");
            emitDefineCompletionSlot(finallySlot);
            emitSaveCompletion(finallySlot);
        }
        compileStatement(*node.finalizer);
        if (evalCompletion_) {
            // F normal → F = B/C（恢复保存值），再 UpdateEmpty(F, undefined)。
            // F abrupt 时由 finally_end 分派 frame.res，寄存器不参与。
            code_->load(finallySlot);
            code_->store(kEvalCompletion);
            emitCompletionToUndefined();
        }
        code_->finallyEnd();
    }

    uint32_t tryEnd = code_->here();
    code_->patchU32(tryBeginPc + 2, catchStart);
    code_->patchU32(tryBeginPc + 6, finallyStart);
    code_->patchU32(tryBeginPc + 10, tryEnd);
}

void Compiler::compileCatchClause(const frontend::CatchClause& node) {
    // 异常 child 由 VM 压在栈顶；catch 建立自己的作用域并绑定参数。
    code_->pushScope();
    if (node.param) {
        if (node.param->kind() == NodeKind::Identifier) {
            // Annex B.3.4：简单 catch 参数允许 eval 的 var 同名声明。
            code_->markCatchScope();
            code_->define(
                static_cast<const frontend::Identifier&>(*node.param).name);
        } else if (node.param->kind() == NodeKind::ArrayPattern ||
                   node.param->kind() == NodeKind::ObjectPattern) {
            // 先为模式中的名字建槽，再用解构写入（与参数模式同策略）。
            std::vector<std::string> names;
            collectPatternNames(*node.param, names);
            for (const std::string& name : names) {
                code_->pushUndefined();
                code_->define(name);
            }
            compileBindingPattern(*node.param, false, 0);
        } else {
            error(*node.param, "compiler: unsupported catch parameter");
            code_->pop();
        }
    } else {
        code_->pop();
    }

    if (node.body) {
        if (node.body->kind() == NodeKind::BlockStatement) {
            compileBlockStatement(
                static_cast<const frontend::BlockStatement&>(*node.body));
        } else {
            compileStatement(*node.body);
        }
    }
    code_->popScope();
}

// —— 模块声明 ——

void Compiler::compileImportDeclaration(
    const frontend::ImportDeclaration& node) {
    // import 不产生运行期字节码：槽位别名在 link 阶段建立。
    if (!node.source || node.source->kind() != NodeKind::StringLiteral) {
        error(node, "compiler: import source must be a string literal");
        return;
    }
    const std::string& specifier =
        static_cast<const frontend::StringLiteral&>(*node.source).value;

    for (const NodePtr& specifierNode : node.specifiers) {
        if (!specifierNode) continue;
        std::string localName;
        std::string importName;
        bool namespaceImport = false;
        switch (specifierNode->kind()) {
            case NodeKind::ImportDefaultSpecifier: {
                const auto& entry =
                    static_cast<const frontend::ImportDefaultSpecifier&>(
                        *specifierNode);
                if (entry.local &&
                    entry.local->kind() == NodeKind::Identifier) {
                    localName =
                        static_cast<const frontend::Identifier&>(*entry.local)
                            .name;
                }
                importName = "default";
                break;
            }
            case NodeKind::ImportNamespaceSpecifier: {
                const auto& entry =
                    static_cast<const frontend::ImportNamespaceSpecifier&>(
                        *specifierNode);
                if (entry.local &&
                    entry.local->kind() == NodeKind::Identifier) {
                    localName =
                        static_cast<const frontend::Identifier&>(*entry.local)
                            .name;
                }
                namespaceImport = true;
                break;
            }
            case NodeKind::ImportSpecifier: {
                const auto& entry =
                    static_cast<const frontend::ImportSpecifier&>(
                        *specifierNode);
                if (entry.local &&
                    entry.local->kind() == NodeKind::Identifier) {
                    localName =
                        static_cast<const frontend::Identifier&>(*entry.local)
                            .name;
                }
                importName = exportNameOf(entry.imported.get());
                break;
            }
            default:
                error(*specifierNode, "compiler: unsupported import specifier");
                continue;
        }
        if (localName.empty()) {
            error(*specifierNode, "compiler: import binding must be an identifier");
            continue;
        }
        imports_.push_back(
            neo::core::ModuleImportBinding{localName, specifier, importName,
                                           namespaceImport});
    }
}

void Compiler::compileExportNamedDeclaration(
    const frontend::ExportNamedDeclaration& node) {
    if (node.declaration) {
        const Node& declaration = *node.declaration;
        switch (declaration.kind()) {
            case NodeKind::VariableDeclaration:
                compileVariableDeclaration(
                    static_cast<const frontend::VariableDeclaration&>(
                        declaration));
                break;
            case NodeKind::FunctionDeclaration:
                // 函数对象在实例化阶段创建，逻辑入口无操作。
                break;
            case NodeKind::ClassDeclaration:
                compileClassDeclaration(
                    static_cast<const frontend::ClassDeclaration&>(
                        declaration));
                break;
            default:
                error(declaration,
                      "compiler: unsupported exported declaration");
                break;
        }
        // export var/let/const/function：导出名即本地绑定名。
        const Node* inner = declarationOf(declaration);
        if (inner != nullptr) {
            switch (inner->kind()) {
                case NodeKind::VariableDeclaration: {
                    const auto& variables =
                        static_cast<const frontend::VariableDeclaration&>(
                            *inner);
                    for (const NodePtr& declarator : variables.declarations) {
                        if (!declarator ||
                            declarator->kind() != NodeKind::VariableDeclarator) {
                            continue;
                        }
                        const auto& entry =
                            static_cast<const frontend::VariableDeclarator&>(
                                *declarator);
                        if (entry.id &&
                            entry.id->kind() == NodeKind::Identifier) {
                            const std::string& name =
                                static_cast<const frontend::Identifier&>(
                                    *entry.id)
                                    .name;
                            exports_.push_back(neo::core::ModuleExportBinding{
                                name, name, {}, {}, false});
                        }
                    }
                    break;
                }
                case NodeKind::FunctionDeclaration: {
                    const auto& function =
                        static_cast<const frontend::FunctionDeclaration&>(
                            *inner);
                    if (function.id &&
                        function.id->kind() == NodeKind::Identifier) {
                        const std::string& name =
                            static_cast<const frontend::Identifier&>(
                                *function.id)
                                .name;
                        exports_.push_back(neo::core::ModuleExportBinding{
                            name, name, {}, {}, false});
                    }
                    break;
                }
                case NodeKind::ClassDeclaration: {
                    const auto& klass =
                        static_cast<const frontend::ClassDeclaration&>(*inner);
                    if (klass.id &&
                        klass.id->kind() == NodeKind::Identifier) {
                        const std::string& name =
                            static_cast<const frontend::Identifier&>(*klass.id)
                                .name;
                        exports_.push_back(neo::core::ModuleExportBinding{
                            name, name, {}, {}, false});
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    if (!node.specifiers.empty()) {
        std::string source;
        if (node.source && node.source->kind() == NodeKind::StringLiteral) {
            source =
                static_cast<const frontend::StringLiteral&>(*node.source).value;
        }
        for (const NodePtr& specifierNode : node.specifiers) {
            if (!specifierNode ||
                specifierNode->kind() != NodeKind::ExportSpecifier) {
                continue;
            }
            const auto& entry =
                static_cast<const frontend::ExportSpecifier&>(*specifierNode);
            std::string exportName = exportNameOf(entry.exported.get());
            std::string localName = exportNameOf(entry.local.get());
            if (exportName.empty() || localName.empty()) {
                error(*specifierNode, "compiler: invalid export specifier");
                continue;
            }
            if (source.empty()) {
                exports_.push_back(neo::core::ModuleExportBinding{
                    exportName, localName, {}, {}, false});
            } else {
                exports_.push_back(neo::core::ModuleExportBinding{
                    exportName, {}, source, localName, false});
            }
        }
    }
}

void Compiler::compileExportDefaultDeclaration(
    const frontend::ExportDefaultDeclaration& node) {
    if (!node.declaration) {
        error(node, "compiler: export default missing declaration");
        return;
    }
    const Node& declaration = *node.declaration;

    // export default function f() {}：f 是模块作用域绑定，默认导出指向它。
    if (declaration.kind() == NodeKind::FunctionDeclaration) {
        const auto& function =
            static_cast<const frontend::FunctionDeclaration&>(declaration);
        if (function.id &&
            function.id->kind() == NodeKind::Identifier) {
            const std::string& name =
                static_cast<const frontend::Identifier&>(*function.id).name;
            exports_.push_back(
                neo::core::ModuleExportBinding{"default", name, {}, {}, false});
            return;
        }
        if (!function.body) {
            error(declaration, "compiler: function missing body");
            code_->pushUndefined();
            return;
        }
        compileFunctionObject(declaration, function.params, *function.body,
                              false, "default", function.generator,
                              function.async);
        code_->initialize(kDefaultSlot);
        exports_.push_back(neo::core::ModuleExportBinding{
            "default", std::string(kDefaultSlot), {}, {}, false});
        return;
    }

    if (declaration.kind() == NodeKind::ClassDeclaration) {
        const auto& klass =
            static_cast<const frontend::ClassDeclaration&>(declaration);
        const bool named =
            klass.id && klass.id->kind() == NodeKind::Identifier;
        compileClassDeclaration(klass, !named);
        if (!named) code_->setFuncName("default");
        if (named) {
            const std::string& name =
                static_cast<const frontend::Identifier&>(*klass.id).name;
            exports_.push_back(
                neo::core::ModuleExportBinding{"default", name, {}, {}, false});
        } else {
            code_->initialize(kDefaultSlot);
            exports_.push_back(neo::core::ModuleExportBinding{
                "default", std::string(kDefaultSlot), {}, {}, false});
        }
        return;
    }

    // export default <expression>：求值后存入隐藏槽位。
    // 匿名函数/类按 NamedEvaluation 取名字 "default"。
    compileNamedExpression(declaration, "default");
    code_->initialize(kDefaultSlot);
    exports_.push_back(neo::core::ModuleExportBinding{
        "default", std::string(kDefaultSlot), {}, {}, false});
}

void Compiler::compileExportAllDeclaration(
    const frontend::ExportAllDeclaration& node) {
    if (!node.source || node.source->kind() != NodeKind::StringLiteral) {
        error(node, "compiler: export source must be a string literal");
        return;
    }
    std::string specifier =
        static_cast<const frontend::StringLiteral&>(*node.source).value;
    if (node.exported) {
        // export * as ns from "m"：导出命名空间对象本身。
        std::string exportName = exportNameOf(node.exported.get());
        if (exportName.empty()) {
            error(node, "compiler: invalid export name");
            return;
        }
        exports_.push_back(neo::core::ModuleExportBinding{
            exportName, {}, specifier, {}, false});
    } else {
        exports_.push_back(
            neo::core::ModuleExportBinding{{}, {}, specifier, {}, true});
    }
}

// —— 表达式 ——

void Compiler::compileIdentifier(const frontend::Identifier& node) {
    code_->load(node.name);
}

void Compiler::compileNumericLiteral(const frontend::NumericLiteral& node) {
    code_->push(node.value);
}

void Compiler::compileBigIntLiteral(const frontend::BigIntLiteral& node) {
    code_->pushBigInt(node.digits, static_cast<uint32_t>(node.radix));
}

void Compiler::compileStringLiteral(const frontend::StringLiteral& node) {
    code_->push(node.value);
}

void Compiler::compileRegExpLiteral(const frontend::RegExpLiteral& node) {
    // 字面量在编译期预编译 Program，运行期直接复用。Sema 已做过早期校验，
    // 这里的失败只可能是内部不一致；此时回退到运行期编译路径。
    std::shared_ptr<const neo::regex::Program> program;
    auto parsed = neo::regex::parseFlags(node.flags);
    if (parsed.ok()) {
        auto compiled = neo::regex::compilePattern(node.pattern, parsed.flags);
        if (compiled.ok()) program = std::move(compiled.program);
    }
    code_->pushRegExp(node.pattern, node.flags, std::move(program));
}

void Compiler::compileBooleanLiteral(const frontend::BooleanLiteral& node) {
    code_->pushBoolean(node.value);
}

void Compiler::compileNullLiteral(const frontend::NullLiteral&) {
    code_->pushNull();
}

void Compiler::compileBinaryExpression(
    const frontend::BinaryExpression& node) {
    if (!node.left || !node.right) {
        error(node, "compiler: binary expression missing operand");
        code_->pushUndefined();
        return;
    }
    // `#x in obj`：左操作数是私有名，不是普通表达式。
    if (node.op == frontend::BinaryOperator::In &&
        node.left->kind() == NodeKind::PrivateIdentifier) {
        const std::string& name =
            static_cast<const frontend::PrivateIdentifier&>(*node.left).name;
        if (findPrivateInfo(name) == nullptr) {
            error(*node.left, "compiler: undeclared private name #" + name);
            code_->pushUndefined();
            return;
        }
        compileExpression(*node.right);
        code_->privateIn(name);
        return;
    }
    compileExpression(*node.left);
    compileExpression(*node.right);

    switch (node.op) {
        case frontend::BinaryOperator::Add:              code_->add(); break;
        case frontend::BinaryOperator::Subtract:         code_->sub(); break;
        case frontend::BinaryOperator::Multiply:         code_->mul(); break;
        case frontend::BinaryOperator::Divide:           code_->div(); break;
        case frontend::BinaryOperator::LessThan:         code_->lt(); break;
        case frontend::BinaryOperator::LessThanEqual:    code_->le(); break;
        case frontend::BinaryOperator::GreaterThan:      code_->gt(); break;
        case frontend::BinaryOperator::GreaterThanEqual: code_->ge(); break;
        case frontend::BinaryOperator::StrictEqual:      code_->strictEqual(); break;
        case frontend::BinaryOperator::In:               code_->in(); break;
        case frontend::BinaryOperator::InstanceOf:       code_->instanceOf(); break;
        case frontend::BinaryOperator::Remainder:
            code_->binary(static_cast<uint32_t>(neo::core::BinaryOperator::Mod));
            break;
        case frontend::BinaryOperator::Exponentiate:
            code_->binary(static_cast<uint32_t>(neo::core::BinaryOperator::Exp));
            break;
        case frontend::BinaryOperator::Equal:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::AbstractEqual));
            break;
        case frontend::BinaryOperator::NotEqual:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::AbstractEqual));
            code_->unary(static_cast<uint32_t>(
                neo::core::UnaryOperator::LogicalNot));
            break;
        case frontend::BinaryOperator::LeftShift:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::ShiftLeft));
            break;
        case frontend::BinaryOperator::RightShift:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::ShiftRight));
            break;
        case frontend::BinaryOperator::UnsignedRightShift:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::UnsignedShiftRight));
            break;
        case frontend::BinaryOperator::BitAnd:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::BitAnd));
            break;
        case frontend::BinaryOperator::BitOr:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::BitOr));
            break;
        case frontend::BinaryOperator::BitXor:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::BitXor));
            break;
        case frontend::BinaryOperator::StrictNotEqual:
            code_->strictEqual();
            code_->unary(static_cast<uint32_t>(
                neo::core::UnaryOperator::LogicalNot));
            break;
        default:
            error(node, "compiler: unsupported binary operator " +
                            std::to_string(static_cast<int>(node.op)));
            break;
    }
}

void Compiler::compileDeleteExpression(const frontend::Node& argument) {
    if (argument.kind() == NodeKind::MemberExpression) {
        const auto& member =
            static_cast<const frontend::MemberExpression&>(argument);
        if (member.object &&
            member.object->kind() == NodeKind::SuperExpression) {
            // SuperReference 不可删除：求值到 delete 时抛 ReferenceError，
            // 且属性键不求值（§13.5.1.2）。
            code_->throwReferenceError("Unsupported reference to 'super'");
            return;
        }
        if (!member.object) {
            error(argument, "compiler: delete target missing object");
            code_->pushUndefined();
            return;
        }
        if (member.optional) {
            // 未包在 ChainExpression 中的可选成员（防御性处理）。
            const bool savedDelete = deleteChain_;
            deleteChain_ = true;
            deleteChainTarget_ = true;
            compileMemberExpression(member);
            deleteChain_ = savedDelete;
            return;
        }
        compileExpression(*member.object);
        compileMemberKey(member);
        code_->deleteProp();
        return;
    }
    if (argument.kind() == NodeKind::ChainExpression) {
        const auto& chain =
            static_cast<const frontend::ChainExpression&>(argument);
        const bool target =
            chain.expression &&
            chain.expression->kind() == NodeKind::MemberExpression;
        compileDeleteChain(chain);
        if (!target) {
            // 非引用（如 `delete a?.()`）：求值后返回 true。
            code_->pop();
            code_->pushBoolean(true);
        }
        return;
    }
    if (argument.kind() == NodeKind::Identifier) {
        // sloppy `delete Identifier`：声明绑定 false，全局对象属性按可配置性，
        // 未解析引用 true。strict 下 Sema 已提前报错。
        code_->deleteIdentifier(
            static_cast<const frontend::Identifier&>(argument).name);
        return;
    }
    // 非引用：求值副作用后返回 true。
    compileExpression(argument);
    code_->pop();
    code_->pushBoolean(true);
}

void Compiler::compileUnaryExpression(const frontend::UnaryExpression& node) {
    if (!node.argument) {
        error(node, "compiler: unary expression missing operand");
        code_->pushUndefined();
        return;
    }
    if (node.op == frontend::UnaryOperator::Delete) {
        compileDeleteExpression(*node.argument);
        return;
    }

    neo::core::UnaryOperator op = neo::core::UnaryOperator::Plus;
    switch (node.op) {
        case frontend::UnaryOperator::Plus:       op = neo::core::UnaryOperator::Plus; break;
        case frontend::UnaryOperator::Minus:      op = neo::core::UnaryOperator::Minus; break;
        case frontend::UnaryOperator::LogicalNot: op = neo::core::UnaryOperator::LogicalNot; break;
        case frontend::UnaryOperator::BitNot:     op = neo::core::UnaryOperator::BitNot; break;
        case frontend::UnaryOperator::TypeOf:     op = neo::core::UnaryOperator::TypeOf; break;
        case frontend::UnaryOperator::Void:       op = neo::core::UnaryOperator::Void; break;
        default:
            error(node, "compiler: unsupported unary operator");
            code_->pushUndefined();
            return;
    }

    // typeof 裸标识符：未声明时求值为 undefined，不能走会抛 ReferenceError
    // 的普通 load（§13.5.3）。
    if (node.op == frontend::UnaryOperator::TypeOf &&
        node.argument->kind() == NodeKind::Identifier) {
        const auto& id = static_cast<const frontend::Identifier&>(*node.argument);
        code_->loadSoft(id.name);
        code_->unary(static_cast<uint32_t>(op));
        return;
    }

    compileExpression(*node.argument);
    code_->unary(static_cast<uint32_t>(op));
}

void Compiler::compileLogicalExpression(
    const frontend::LogicalExpression& node) {
    if (!node.left || !node.right) {
        error(node, "compiler: logical expression missing operand");
        code_->pushUndefined();
        return;
    }
    compileExpression(*node.left);
    code_->dup();

    uint32_t jump = 0;
    switch (node.op) {
        case frontend::LogicalOperator::And:
            jump = code_->jfalse();
            break;
        case frontend::LogicalOperator::Or:
            jump = code_->jtrue();
            break;
        case frontend::LogicalOperator::Nullish:
            jump = code_->jNotNullish();
            break;
        default:
            error(node, "compiler: unsupported logical operator");
            code_->pop();
            code_->pushUndefined();
            return;
    }

    // 短路：保留左值；否则丢弃左值并求右值。
    code_->pop();
    compileExpression(*node.right);
    code_->patchU32(jump + 2, code_->here());
}

void Compiler::compileConditionalExpression(
    const frontend::ConditionalExpression& node) {
    if (!node.test || !node.consequent || !node.alternate) {
        error(node, "compiler: conditional expression missing operand");
        code_->pushUndefined();
        return;
    }
    compileExpression(*node.test);
    uint32_t toElse = code_->jfalse();
    compileExpression(*node.consequent);
    uint32_t toEnd = code_->jmp();
    code_->patchU32(toElse + 2, code_->here());
    compileExpression(*node.alternate);
    code_->patchU32(toEnd + 2, code_->here());
}

void Compiler::compileSequenceExpression(
    const frontend::SequenceExpression& node) {
    if (node.expressions.empty()) {
        code_->pushUndefined();
        return;
    }
    for (size_t i = 0; i < node.expressions.size(); ++i) {
        const frontend::NodePtr& expression = node.expressions[i];
        if (!expression) {
            code_->pushUndefined();
        } else {
            compileExpression(*expression);
        }
        if (i + 1 < node.expressions.size()) code_->pop();
    }
}

void Compiler::compileFunctionExpression(
    const frontend::FunctionExpression& node) {
    std::string name;
    if (node.id && node.id->kind() == NodeKind::Identifier) {
        name = static_cast<const frontend::Identifier&>(*node.id).name;
    }
    if (!node.body) {
        error(node, "compiler: function expression missing body");
        code_->pushUndefined();
        return;
    }
    compileFunctionObject(node, node.params, *node.body, false, name,
                          node.generator, node.async);
}

void Compiler::compileArrowFunctionExpression(
    const frontend::ArrowFunctionExpression& node) {
    if (!node.body) {
        error(node, "compiler: arrow function missing body");
        code_->pushUndefined();
        return;
    }
    compileFunctionObject(node, node.params, *node.body, true, {}, false,
                          node.async);
}

void Compiler::compileCallExpression(const frontend::CallExpression& node) {
    if (!node.callee) {
        error(node, "compiler: call expression missing callee");
        code_->pushUndefined();
        return;
    }
    if (node.callee->kind() == NodeKind::SuperExpression) {
        bool hasSpread = false;
        for (const frontend::NodePtr& argument : node.arguments) {
            if (argument && argument->kind() == NodeKind::SpreadElement) {
                hasSpread = true;
                break;
            }
        }
        if (hasSpread) {
            compileArgumentsArray(node.arguments);
            code_->constructSuperSpread();
            return;
        }
        uint32_t argc = 0;
        for (const frontend::NodePtr& argument : node.arguments) {
            if (!argument) continue;
            compileExpression(*argument);
            ++argc;
        }
        code_->constructSuper(argc);
        return;
    }

    bool isMember = node.callee->kind() == NodeKind::MemberExpression;
    if (isMember) {
        const auto& member =
            static_cast<const frontend::MemberExpression&>(*node.callee);
        if (!member.object) {
            error(node, "compiler: member call missing object");
            code_->pushUndefined();
            return;
        }
        if (member.property &&
            member.property->kind() == NodeKind::PrivateIdentifier) {
            compilePrivateCall(member, node);
            return;
        }
        if (member.object->kind() == NodeKind::SuperExpression) {
            // super.m()：receiver 是当前 this，方法从 homeObject 原型链取。
            code_->loadThis();
            compileMemberKey(member);
            code_->getSuperProp();
            // 后续实参与 call_method 复用下面的公共路径。
            bool hasSpread = false;
            for (const frontend::NodePtr& argument : node.arguments) {
                if (argument && argument->kind() == NodeKind::SpreadElement) {
                    hasSpread = true;
                    break;
                }
            }
            if (hasSpread) {
                compileArgumentsArray(node.arguments);
                code_->callMethodSpread();
                return;
            }
            uint32_t argc = 0;
            for (const frontend::NodePtr& argument : node.arguments) {
                if (!argument) continue;
                compileExpression(*argument);
                ++argc;
            }
            code_->callMethod(argc);
            return;
        }
        // receiver 只求值一次：栈上保留一份作为 thisArg，另一份用于取方法。
        compileExpression(*member.object);
        if (member.optional) emitOptionalGuard();
        code_->dup();
        compileMemberKey(member);
        code_->getProp();
        // `obj.m?.()`：fn 为 nullish 时 receiver 与 fn 都要丢弃。
        if (node.optional) emitOptionalGuard(1);
    } else {
        compileExpression(*node.callee);
        if (node.optional) emitOptionalGuard();
    }

    // 直接 eval 候选点：无成员访问、非可选调用。VM 在执行期校验被调函数
    // 是否仍是内建 %eval%（被遮蔽时回退普通调用）。
    const bool directEvalCandidate =
        !isMember && !node.optional &&
        node.callee->kind() == NodeKind::Identifier &&
        static_cast<const frontend::Identifier&>(*node.callee).name == "eval";

    bool hasSpread = false;
    for (const frontend::NodePtr& argument : node.arguments) {
        if (argument && argument->kind() == NodeKind::SpreadElement) {
            hasSpread = true;
            break;
        }
    }
    if (hasSpread) {
        compileArgumentsArray(node.arguments);
        if (isMember) {
            code_->callMethodSpread();
        } else if (directEvalCandidate) {
            code_->directEvalSpread();
        } else {
            code_->callSpread();
        }
        return;
    }

    uint32_t argc = 0;
    for (const frontend::NodePtr& argument : node.arguments) {
        if (!argument) continue;
        compileExpression(*argument);
        ++argc;
    }
    if (isMember) {
        code_->callMethod(argc);
    } else if (directEvalCandidate) {
        code_->directEval(argc);
    } else {
        code_->call(argc);
    }
}

void Compiler::compileArgumentsArray(const frontend::NodeList& arguments) {
    code_->newArray(0);
    for (const frontend::NodePtr& argument : arguments) {
        if (!argument) continue;
        if (argument->kind() == NodeKind::SpreadElement) {
            const auto& spread =
                static_cast<const frontend::SpreadElement&>(*argument);
            if (spread.argument) {
                compileExpression(*spread.argument);
            } else {
                code_->pushUndefined();
            }
            code_->arraySpread();
            continue;
        }
        compileExpression(*argument);
        code_->arrayAppend();
    }
}

void Compiler::compileNewExpression(const frontend::NewExpression& node) {
    if (!node.callee) {
        error(node, "compiler: new expression missing callee");
        code_->pushUndefined();
        return;
    }
    compileExpression(*node.callee);

    bool hasSpread = false;
    for (const frontend::NodePtr& argument : node.arguments) {
        if (argument && argument->kind() == NodeKind::SpreadElement) {
            hasSpread = true;
            break;
        }
    }
    if (hasSpread) {
        compileArgumentsArray(node.arguments);
        code_->constructSpread();
        return;
    }

    uint32_t argc = 0;
    for (const frontend::NodePtr& argument : node.arguments) {
        if (!argument) continue;
        compileExpression(*argument);
        ++argc;
    }
    code_->construct(argc);
}

void Compiler::compileMetaProperty(const frontend::MetaProperty& node) {
    if (node.meta != nullptr && node.property != nullptr &&
        node.meta->kind() == NodeKind::Identifier &&
        node.property->kind() == NodeKind::Identifier) {
        const std::string& meta =
            static_cast<const frontend::Identifier&>(*node.meta).name;
        const std::string& property =
            static_cast<const frontend::Identifier&>(*node.property).name;
        if (meta == "new" && property == "target") {
            code_->loadNewTarget();
            return;
        }
        if (meta == "import" && property == "meta") {
            code_->importMeta();
            return;
        }
        error(node, "compiler: unsupported meta property " + meta + "." +
                        property);
        code_->pushUndefined();
        return;
    }
    error(node, "compiler: malformed meta property");
    code_->pushUndefined();
}

void Compiler::compileImportExpression(
    const frontend::ImportExpression& node) {
    // 动态 import：specifier 先求值；options（若存在）求值后丢弃——
    // 本引擎没有 host import attributes，但仍需保持求值副作用。
    if (node.source) {
        compileExpression(*node.source);
    } else {
        code_->pushUndefined();
    }
    if (node.options) {
        compileExpression(*node.options);
        code_->pop();
    }
    code_->dynamicImport();
}

void Compiler::compileTemplateLiteral(
    const frontend::TemplateLiteral& node) {
    if (node.quasis.empty()) {
        code_->push("");
        return;
    }
    const auto& first =
        static_cast<const frontend::TemplateElement&>(*node.quasis[0]);
    code_->push(first.cooked);

    for (size_t i = 0; i < node.expressions.size(); ++i) {
        if (node.expressions[i]) {
            compileExpression(*node.expressions[i]);
            code_->toString();
            code_->add();
        }
        const size_t next = i + 1;
        if (next < node.quasis.size() && node.quasis[next]) {
            const auto& quasi =
                static_cast<const frontend::TemplateElement&>(
                    *node.quasis[next]);
            code_->push(quasi.cooked);
            code_->add();
        }
    }
}

void Compiler::compileTaggedTemplateExpression(
    const frontend::TaggedTemplateExpression& node) {
    if (!node.tag || !node.quasi ||
        node.quasi->kind() != NodeKind::TemplateLiteral) {
        error(node, "compiler: malformed tagged template");
        code_->pushUndefined();
        return;
    }
    const auto& quasi =
        static_cast<const frontend::TemplateLiteral&>(*node.quasi);

    bool isMember = node.tag->kind() == NodeKind::MemberExpression;
    if (isMember) {
        const auto& member =
            static_cast<const frontend::MemberExpression&>(*node.tag);
        if (member.optional) {
            error(node, "compiler: optional chaining not supported");
            code_->pushUndefined();
            return;
        }
        if (!member.object) {
            error(node, "compiler: tagged template missing receiver");
            code_->pushUndefined();
            return;
        }
        compileExpression(*member.object);
        code_->dup();
        compileMemberKey(member);
        code_->getProp();
    } else {
        compileExpression(*node.tag);
    }

    std::vector<std::pair<std::optional<std::string>, std::string>> quasis;
    quasis.reserve(quasi.quasis.size());
    for (const frontend::NodePtr& element : quasi.quasis) {
        if (!element ||
            element->kind() != NodeKind::TemplateElement) {
            error(node, "compiler: malformed template element");
            continue;
        }
        const auto& el =
            static_cast<const frontend::TemplateElement&>(*element);
        if (el.cookedValid) {
            quasis.emplace_back(el.cooked, el.raw);
        } else {
            quasis.emplace_back(std::nullopt, el.raw);
        }
    }
    code_->templateObject(quasis);

    uint32_t argc = 1;
    for (const frontend::NodePtr& expression : quasi.expressions) {
        if (!expression) continue;
        compileExpression(*expression);
        ++argc;
    }

    if (isMember) {
        code_->callMethod(argc);
    } else {
        code_->call(argc);
    }
}

void Compiler::compileMemberExpression(
    const frontend::MemberExpression& node) {
    if (!node.object) {
        error(node, "compiler: member expression missing object");
        code_->pushUndefined();
        return;
    }
    if (node.property &&
        node.property->kind() == NodeKind::PrivateIdentifier) {
        compilePrivateRead(node);
        return;
    }
    if (node.object->kind() == NodeKind::SuperExpression) {
        // GetThisBinding 先于属性表达式求值（§13.3.7.1 步骤 2）。
        code_->loadThis();
        code_->pop();
        compileMemberKey(node);
        code_->getSuperProp();
        return;
    }
    const bool deleteTarget = deleteChainTarget_;
    deleteChainTarget_ = false;
    compileExpression(*node.object);
    if (node.optional) emitOptionalGuard();
    compileMemberKey(node);
    if (deleteTarget) {
        code_->deleteProp();
    } else {
        code_->getProp();
    }
}

void Compiler::compileDeleteChain(const frontend::ChainExpression& node) {
    if (!node.expression) {
        error(node, "compiler: chain expression missing operand");
        code_->pushBoolean(true);
        return;
    }
    const bool savedDelete = deleteChain_;
    const bool savedTarget = deleteChainTarget_;
    deleteChain_ = true;
    deleteChainTarget_ = node.expression->kind() == NodeKind::MemberExpression;
    chainEndPatches_.emplace_back();
    compileExpression(*node.expression);
    std::vector<uint32_t> ends = std::move(chainEndPatches_.back());
    chainEndPatches_.pop_back();
    uint32_t endPc = code_->here();
    for (uint32_t patch : ends) code_->patchU32(patch + 2, endPc);
    deleteChain_ = savedDelete;
    deleteChainTarget_ = savedTarget;
}

void Compiler::compileChainExpression(
    const frontend::ChainExpression& node) {
    if (!node.expression) {
        error(node, "compiler: chain expression missing operand");
        code_->pushUndefined();
        return;
    }
    chainEndPatches_.emplace_back();
    compileExpression(*node.expression);
    std::vector<uint32_t> ends = std::move(chainEndPatches_.back());
    chainEndPatches_.pop_back();
    uint32_t endPc = code_->here();
    for (uint32_t patch : ends) code_->patchU32(patch + 2, endPc);
}

void Compiler::emitOptionalGuard(uint32_t extraPops) {
    code_->dup();
    uint32_t cont = code_->jNotNullish();
    code_->pop();
    for (uint32_t i = 0; i < extraPops; ++i) code_->pop();
    if (deleteChain_) {
        code_->pushBoolean(true);  // delete 可选链短路结果为 true。
    } else {
        code_->pushUndefined();
    }
    uint32_t end = code_->jmp();
    if (!chainEndPatches_.empty()) {
        chainEndPatches_.back().push_back(end);
    } else {
        // 可选节点必然位于 ChainExpression 内；兜底避免留下悬空跳转。
        code_->patchU32(end + 2, code_->here());
    }
    code_->patchU32(cont + 2, code_->here());
}

void Compiler::compileMemberKey(const frontend::MemberExpression& node) {
    if (!node.property) {
        error(node, "compiler: member expression missing property");
        code_->pushUndefined();
        return;
    }
    if (node.computed) {
        compileExpression(*node.property);
        return;
    }
    if (node.property->kind() != NodeKind::Identifier) {
        error(*node.property, "compiler: unsupported property key");
        code_->pushUndefined();
        return;
    }
    code_->push(
        static_cast<const frontend::Identifier&>(*node.property).name);
}

void Compiler::compileObjectExpression(
    const frontend::ObjectExpression& node) {
    code_->newObject();
    for (const frontend::NodePtr& property : node.properties) {
        if (!property) continue;
        if (property->kind() == NodeKind::SpreadElement) {
            const auto& spread =
                static_cast<const frontend::SpreadElement&>(*property);
            if (spread.argument) {
                compileExpression(*spread.argument);
            } else {
                code_->pushUndefined();
            }
            code_->objectSpread();
            continue;
        }
        if (property->kind() != NodeKind::Property) {
            error(*property, "compiler: unsupported object literal element");
            continue;
        }
        const auto& prop = static_cast<const frontend::Property&>(*property);
        if (prop.propertyKind != frontend::PropertyKind::Init) {
            // get/set：缺失的一侧压 null（引擎按无访问器处理）。
            code_->dup();
            compilePropertyKey(prop);
            const bool getter =
                prop.propertyKind == frontend::PropertyKind::Get;
            const std::string accessorKey =
                prop.computed ? std::string() : staticKeyName(prop.key.get());
            const std::string accessorName =
                accessorKey.empty()
                    ? std::string()
                    : (getter ? "get " + accessorKey : "set " + accessorKey);
            if (getter) {
                if (prop.value) {
                    if (prop.computed) {
                        compileNamedExpressionForKey(*prop.value, "get ");
                    } else {
                        compileNamedExpression(*prop.value, accessorName);
                    }
                } else {
                    code_->pushUndefined();
                }
                code_->setHomeObject();
                code_->pushNull();
            } else {
                if (prop.value) {
                    if (prop.computed) {
                        compileNamedExpressionForKey(*prop.value, "set ");
                    } else {
                        compileNamedExpression(*prop.value, accessorName);
                    }
                } else {
                    code_->pushUndefined();
                }
                code_->setHomeObject();
                code_->pushNull();
                code_->swap();  // [obj, key, setter, null] → [obj, key, null, setter]
            }
            code_->defineAccessor(getter, !getter);
            code_->pop();
            continue;
        }
        // __proto__: value 是设置原型而非定义自有属性（B.3.1）；
        // 仅限非 computed、非 shorthand、非方法的 Init 属性。
        if (!prop.computed && !prop.shorthand && !prop.method && prop.key &&
            prop.key->kind() == NodeKind::Identifier &&
            static_cast<const frontend::Identifier&>(*prop.key).name ==
                "__proto__") {
            code_->dup();
            if (prop.value) {
                compileExpression(*prop.value);
            } else {
                code_->pushUndefined();
            }
            code_->setProtoLiteral();
            code_->pop();
            continue;
        }

        // define_prop 消费 obj，栈上保留一份继续累积属性。
        code_->dup();
        compilePropertyKey(prop);
        if (prop.value) {
            if (prop.computed) {
                compileNamedExpressionForKey(*prop.value);
            } else {
                compileNamedExpression(*prop.value,
                                       staticKeyName(prop.key.get()));
            }
        } else {
            code_->pushUndefined();
        }
        // 简洁方法定义 [[HomeObject]] 为对象字面量本身（§13.2.5.5）。
        if (prop.method) code_->setHomeObject();
        code_->defineProp();
        code_->pop();
    }
}

void Compiler::compileArrayExpression(const frontend::ArrayExpression& node) {
    bool hasSpread = false;
    for (const frontend::NodePtr& element : node.elements) {
        if (element && element->kind() == NodeKind::SpreadElement) {
            hasSpread = true;
            break;
        }
    }

    if (!hasSpread) {
        code_->newArray(static_cast<uint32_t>(node.elements.size()));
        uint32_t index = 0;
        for (const frontend::NodePtr& element : node.elements) {
            // 空指针是 elision：跳过该下标，保留 length 造成的空洞。
            if (element) {
                code_->dup();
                code_->push(static_cast<double>(index));
                compileExpression(*element);
                code_->defineProp();
                code_->pop();
            }
            ++index;
        }
        return;
    }

    // 含展开：长度动态增长，普通元素逐个 append；elision 只推进 length。
    code_->newArray(0);
    for (const frontend::NodePtr& element : node.elements) {
        if (!element) {
            // elision：只推进 length，保留空洞。
            code_->arrayGrow();
            continue;
        }
        if (element->kind() == NodeKind::SpreadElement) {
            const auto& spread =
                static_cast<const frontend::SpreadElement&>(*element);
            if (spread.argument) {
                compileExpression(*spread.argument);
            } else {
                code_->pushUndefined();
            }
            code_->arraySpread();
            continue;
        }
        compileExpression(*element);
        code_->arrayAppend();
    }
}

void Compiler::compilePropertyKey(const frontend::Property& node) {
    if (!node.key) {
        error(node, "compiler: property missing key");
        code_->pushUndefined();
        return;
    }
    if (node.computed) {
        compileExpression(*node.key);
        return;
    }
    switch (node.key->kind()) {
        case NodeKind::Identifier:
            code_->push(
                static_cast<const frontend::Identifier&>(*node.key).name);
            break;
        case NodeKind::StringLiteral:
            code_->push(
                static_cast<const frontend::StringLiteral&>(*node.key).value);
            break;
        case NodeKind::NumericLiteral:
            code_->push(
                static_cast<const frontend::NumericLiteral&>(*node.key).value);
            break;
        default:
            error(*node.key, "compiler: unsupported property key");
            code_->pushUndefined();
            break;
    }
}

void Compiler::compileThisExpression(const frontend::ThisExpression&) {
    code_->loadThis();
}

void Compiler::compileYieldExpression(const frontend::YieldExpression& node) {
    if (node.delegate) {
        if (node.argument) {
            compileExpression(*node.argument);
        } else {
            code_->pushUndefined();
        }
        code_->yieldDelegate();
        return;
    }
    if (node.argument) {
        compileExpression(*node.argument);
    } else {
        code_->pushUndefined();
    }
    code_->yieldValue();
}

void Compiler::compileAwaitExpression(const frontend::AwaitExpression& node) {
    if (functionDepth_ == 0) code_->hasTopLevelAwait = true;
    if (node.argument) {
        compileExpression(*node.argument);
    } else {
        code_->pushUndefined();
    }
    code_->awaitValue();
}

uint32_t Compiler::emitLogicalShortCircuit(
    frontend::AssignmentOperator op) {
    switch (op) {
        case frontend::AssignmentOperator::LogicalAnd:
            return code_->jfalse();
        case frontend::AssignmentOperator::LogicalOr:
            return code_->jtrue();
        case frontend::AssignmentOperator::Nullish:
            return code_->jNotNullish();
        default:
            return code_->jmp();
    }
}

void Compiler::compileAssignmentExpression(
    const frontend::AssignmentExpression& node) {
    if (!node.left) {
        error(node, "compiler: assignment missing target");
        code_->pushUndefined();
        return;
    }
    if (!node.right) {
        error(node, "compiler: assignment missing right-hand side");
        code_->pushUndefined();
        return;
    }
    const bool logical =
        node.op == frontend::AssignmentOperator::LogicalAnd ||
        node.op == frontend::AssignmentOperator::LogicalOr ||
        node.op == frontend::AssignmentOperator::Nullish;
    if (node.left->kind() == NodeKind::ArrayPattern ||
        node.left->kind() == NodeKind::ObjectPattern) {
        if (node.op != frontend::AssignmentOperator::Assign) {
            error(node, "compiler: compound destructuring assignment not supported");
            code_->pushUndefined();
            return;
        }
        compileExpression(*node.right);
        // 赋值表达式的值就是 RHS：保留一份，模式消费另一份。
        code_->dup();
        compileBindingPattern(*node.left, false, 0);
        return;
    }
    if (node.left->kind() == NodeKind::MemberExpression) {
        const auto& member =
            static_cast<const frontend::MemberExpression&>(*node.left);
        if (member.optional) {
            error(node, "compiler: optional chaining not supported");
            code_->pushUndefined();
            return;
        }
        if (!member.object) {
            error(node, "compiler: assignment target missing object");
            code_->pushUndefined();
            return;
        }
        bool compound = node.op != frontend::AssignmentOperator::Assign;
        if (member.property &&
            member.property->kind() == NodeKind::PrivateIdentifier) {
            compilePrivateWrite(member, node);
            return;
        }
        if (member.object->kind() == NodeKind::SuperExpression) {
            // GetThisBinding 先于属性表达式求值（§13.3.7.1 步骤 2）。
            code_->loadThis();
            code_->pop();
            compileMemberKey(member);
            // GetSuperBase 先于 ToPropertyKey（§13.3.7.3 MakeSuperPropertyReference）：
            // 键的 toString 可能改变原型，base 必须在此之前固定。
            code_->getSuperBase();  // [key, base]
            code_->swap();          // [base, key]
            // 复合/逻辑赋值需先读旧值，键须在此之前定型；普通赋值把
            // ToPropertyKey 推迟到 RHS 求值后的 PutValue（§13.15.2）。
            if ((compound || logical) && member.computed) {
                code_->toPropertyKey();
            }
            if (logical) {
                code_->dup2();                    // [base, key, base, key]
                code_->getSuperPropFromBase();    // [base, key, old]
                code_->dup();
                uint32_t keep = emitLogicalShortCircuit(node.op);
                code_->pop();                     // [base, key]
                compileExpression(*node.right);   // [base, key, new]
                code_->setSuperPropFromBase();    // [new]
                uint32_t end = code_->jmp();
                code_->patchU32(keep + 2, code_->here());  // [base, key, old]
                code_->swap();                    // [base, old, key]
                code_->pop();                     // [base, old]
                code_->swap();                    // [old, base]
                code_->pop();                     // [old]
                code_->patchU32(end + 2, code_->here());
                return;
            }
            if (compound) {
                code_->dup2();                    // [base, key, base, key]
                code_->getSuperPropFromBase();    // [base, key, old]
                compileExpression(*node.right);
                if (!emitCompoundBinary(node.op, node)) return;
                code_->setSuperPropFromBase();    // [new]
            } else {
                compileExpression(*node.right);   // [base, key, value]
                code_->setSuperPropFromBase();    // [value]
            }
            return;
        }
        compileExpression(*member.object);
        compileMemberKey(member);
        // 读取旧值（复合/逻辑赋值）前须检查基值可强制转换；普通赋值不读取，
        // 基值检查与 ToPropertyKey 都推迟到 RHS 求值后的 PutValue（§13.15.2）。
        if (compound || logical) {
            code_->swap();  // [key, obj]
            code_->dup();
            uint32_t coercible = code_->jNotNullish();
            code_->throwTypeError("Cannot read properties of null or undefined");
            code_->patchU32(coercible + 2, code_->here());
            code_->swap();  // [obj, key]
            if (member.computed) code_->toPropertyKey();
        }
        if (logical) {
            // obj/key 只求值一次；短路时丢弃 obj/key，只保留旧值。
            code_->dup2();
            code_->getProp();  // [obj, key, old]
            code_->dup();
            uint32_t keep = emitLogicalShortCircuit(node.op);  // [obj, key, old]
            code_->pop();                                      // [obj, key]
            compileExpression(*node.right);                    // [obj, key, new]
            code_->setProp();                                  // [new]
            uint32_t end = code_->jmp();
            code_->patchU32(keep + 2, code_->here());          // [obj, key, old]
            code_->swap();                                     // [obj, old, key]
            code_->pop();                                      // [obj, old]
            code_->swap();                                     // [old, obj]
            code_->pop();                                      // [old]
            code_->patchU32(end + 2, code_->here());
            return;
        }
        if (compound) {
            // obj/key 只求值一次：复制一份用于读取旧值。
            code_->dup2();
            code_->getProp();
            compileExpression(*node.right);
            if (!emitCompoundBinary(node.op, node)) return;
            code_->setProp();
        } else {
            // 成员赋值不是 IdentifierRef，不触发 NamedEvaluation。
            compileExpression(*node.right);
            code_->setProp();
        }
        return;
    }
    if (node.left->kind() != NodeKind::Identifier) {
        error(node, "compiler: assignment target must be an identifier");
        code_->pushUndefined();
        return;
    }
    const std::string& name =
        static_cast<const frontend::Identifier&>(*node.left).name;

    if (node.op == frontend::AssignmentOperator::Assign) {
        if (withDepth_ > 0) {
            // with 内：赋值引用在 RHS 求值前解析（§13.15.2），RHS 删除属性
            // 后仍写回同一个 with 对象。
            code_->resolveWithRef(name);
            if (node.left->isParenthesized()) {
                compileExpression(*node.right);
            } else {
                compileNamedExpression(*node.right, name);
            }
            code_->storeWithRef(name);
            return;
        }
        // (fn) = ... 不是 IdentifierRef，不做 NamedEvaluation。
        if (node.left->isParenthesized()) {
            compileExpression(*node.right);
        } else {
            compileNamedExpression(*node.right, name);
        }
        code_->dup();
        code_->store(name);
        return;
    }

    if (logical) {
        // x &&= rhs：旧值短路时保留旧值，否则求值 rhs 并写回。
        code_->load(name);
        code_->dup();
        uint32_t keep = emitLogicalShortCircuit(node.op);
        code_->pop();
        compileNamedExpression(*node.right, name);
        code_->dup();
        code_->store(name);
        uint32_t end = code_->jmp();
        code_->patchU32(keep + 2, code_->here());
        code_->patchU32(end + 2, code_->here());
        return;
    }

    if (withDepth_ > 0) {
        // with 内：引用在 GetValue/RHS 前解析，写回同一对象（§13.15.2）。
        code_->resolveWithRef(name);
        code_->loadWithRef(name);
        compileExpression(*node.right);
        if (!emitCompoundBinary(node.op, node)) return;
        code_->storeWithRef(name);
        return;
    }
    // 复合赋值：load 旧值 → 右值 → 运算 → dup（表达式值）→ store。
    code_->load(name);
    compileExpression(*node.right);
    if (!emitCompoundBinary(node.op, node)) return;
    code_->dup();
    code_->store(name);
}

bool Compiler::emitCompoundBinary(frontend::AssignmentOperator op,
                                  const frontend::Node& node) {
    switch (op) {
        case frontend::AssignmentOperator::Add:      code_->add(); return true;
        case frontend::AssignmentOperator::Subtract: code_->sub(); return true;
        case frontend::AssignmentOperator::Multiply: code_->mul(); return true;
        case frontend::AssignmentOperator::Divide:   code_->div(); return true;
        case frontend::AssignmentOperator::Remainder:
            code_->binary(static_cast<uint32_t>(neo::core::BinaryOperator::Mod));
            return true;
        case frontend::AssignmentOperator::Exponentiate:
            code_->binary(static_cast<uint32_t>(neo::core::BinaryOperator::Exp));
            return true;
        case frontend::AssignmentOperator::LeftShift:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::ShiftLeft));
            return true;
        case frontend::AssignmentOperator::RightShift:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::ShiftRight));
            return true;
        case frontend::AssignmentOperator::UnsignedRightShift:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::UnsignedShiftRight));
            return true;
        case frontend::AssignmentOperator::BitAnd:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::BitAnd));
            return true;
        case frontend::AssignmentOperator::BitOr:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::BitOr));
            return true;
        case frontend::AssignmentOperator::BitXor:
            code_->binary(static_cast<uint32_t>(
                neo::core::BinaryOperator::BitXor));
            return true;
        default:
            error(node, "compiler: unsupported assignment operator " +
                            std::to_string(static_cast<int>(op)));
            code_->pop();
            code_->pop();
            code_->pushUndefined();
            return false;
    }
}

void Compiler::compileUpdateExpression(
    const frontend::UpdateExpression& node) {
    if (node.argument &&
        node.argument->kind() == NodeKind::MemberExpression) {
        const auto& member =
            static_cast<const frontend::MemberExpression&>(*node.argument);
        if (member.optional) {
            error(node, "compiler: optional chaining not supported");
            code_->pushUndefined();
            return;
        }
        if (!member.object) {
            error(node, "compiler: update target missing object");
            code_->pushUndefined();
            return;
        }
        if (member.property &&
            member.property->kind() == NodeKind::PrivateIdentifier) {
            compilePrivateUpdate(member, node);
            return;
        }
        if (member.object->kind() == NodeKind::SuperExpression) {
            // GetThisBinding 先于属性表达式求值（§13.3.7.1 步骤 2）。
            code_->loadThis();
            code_->pop();
            compileMemberKey(member);
            code_->getSuperBase();  // [key, base]
            code_->swap();          // [base, key]
            if (member.computed) code_->toPropertyKey();
            code_->dup2();                    // [base, key, base, key]
            code_->getSuperPropFromBase();    // [base, key, old]
            if (!node.prefix) code_->clone();
            code_->push(1.0);
            if (node.op == frontend::UpdateOperator::Increment) {
                code_->add();
            } else {
                code_->sub();
            }
            if (node.prefix) {
                code_->setSuperPropFromBase();  // [new]
            } else {
                // 后缀：栈为 [base, key, old, new]，交换成 [base, key, new, old]。
                code_->swap();
                code_->setSuperPropFromBaseKeep();  // [old]
            }
            return;
        }
        compileExpression(*member.object);
        compileMemberKey(member);
        code_->dup2();
        code_->getProp();
        if (!node.prefix) code_->clone();
        code_->push(1.0);
        if (node.op == frontend::UpdateOperator::Increment) {
            code_->add();
        } else {
            code_->sub();
        }
        if (node.prefix) {
            code_->setProp();
        } else {
            // 后缀：栈为 [obj, key, old, new]，交换成 [obj, key, new, old]。
            code_->swap();
            code_->setPropKeep();
        }
        return;
    }
    if (!node.argument || node.argument->kind() != NodeKind::Identifier) {
        error(node, "compiler: update target must be an identifier or member");
        code_->pushUndefined();
        return;
    }
    const std::string& name =
        static_cast<const frontend::Identifier&>(*node.argument).name;

    if (withDepth_ > 0) {
        // with 内：引用在 GetValue 前解析，写回同一对象（§13.4.4/§13.4.5）。
        code_->resolveWithRef(name);
        code_->loadWithRef(name);  // [obj, old]
        if (node.prefix) {
            code_->push(1.0);
            if (node.op == frontend::UpdateOperator::Increment) {
                code_->add();
            } else {
                code_->sub();
            }
            code_->storeWithRef(name);  // [new]（storeWithRef 已压回新值）
        } else {
            code_->dup();  // [obj, old, old]
            code_->push(1.0);
            if (node.op == frontend::UpdateOperator::Increment) {
                code_->add();
            } else {
                code_->sub();
            }
            code_->storeWithRefKeep(name);  // [old]
        }
        return;
    }
    code_->load(name);
    if (node.prefix) {
        code_->push(1.0);
        if (node.op == frontend::UpdateOperator::Increment) {
            code_->add();
        } else {
            code_->sub();
        }
        code_->dup();
        code_->store(name);
    } else {
        // 后缀：快照旧值留在栈上作为表达式结果（绑定槽会被 store 原地改写）。
        code_->clone();
        code_->swap();
        code_->push(1.0);
        if (node.op == frontend::UpdateOperator::Increment) {
            code_->add();
        } else {
            code_->sub();
        }
        code_->store(name);
    }
}

// —— 函数 ——

void Compiler::compileBindingPattern(const frontend::Node& pattern,
                                     bool lexical, uint32_t depth) {
    // 临时变量名用全局唯一编号，避免同一作用域内多个解构模式重名。
    const uint32_t tempId = destTempId_++;
    switch (pattern.kind()) {
        case NodeKind::Identifier: {
            const std::string& name =
                static_cast<const frontend::Identifier&>(pattern).name;
            if (lexical) {
                code_->initialize(name);
            } else {
                code_->store(name);
            }
            return;
        }
        case NodeKind::AssignmentPattern: {
            const auto& node =
                static_cast<const frontend::AssignmentPattern&>(pattern);
            if (!node.left) {
                error(pattern, "compiler: assignment pattern missing target");
                code_->pop();
                return;
            }
            // [v] → [v, v === undefined]；非 undefined 跳过后面的默认值。
            code_->dup();
            code_->pushUndefined();
            code_->strictEqual();
            uint32_t keep = code_->jfalse();
            code_->pop();
            if (node.right) {
                // [f = function(){}]：默认值按绑定目标名做 NamedEvaluation。
                if (node.left->kind() == NodeKind::Identifier) {
                    compileNamedExpression(
                        *node.right,
                        static_cast<const frontend::Identifier&>(*node.left)
                            .name);
                } else {
                    compileExpression(*node.right);
                }
            } else {
                code_->pushUndefined();
            }
            code_->patchU32(keep + 2, code_->here());
            compileBindingPattern(*node.left, lexical, depth + 1);
            return;
        }
        case NodeKind::ArrayPattern: {
            const auto& node =
                static_cast<const frontend::ArrayPattern&>(pattern);
            const std::string iterName =
                "*dest-iter-" + std::to_string(tempId) + "*";
            const std::string resultName =
                "*dest-res-" + std::to_string(tempId) + "*";
            const std::string doneName =
                "*dest-done-" + std::to_string(tempId) + "*";
            code_->getIterator();
            code_->define(iterName);
            code_->pushUndefined();
            code_->define(resultName);
            code_->pushBoolean(false);
            code_->define(doneName);

            // 整个解构包在 try/finally 中：正常或异常退出时，若迭代器尚未
            // 耗尽则 IteratorClose（§8.6.3 / §14.3.3.5）。
            uint32_t tryBeginPc = code_->tryBegin();

            for (const frontend::NodePtr& element : node.elements) {
                if (!element) {
                    // elision：推进迭代器，记录 done 后丢弃结果。
                    // 先假定 Done=true：IteratorNext/IteratorComplete 抛错时
                    // 规范置 [[Done]]=true，不再 IteratorClose（§7.4.5）。
                    code_->pushBoolean(true);
                    code_->store(doneName);
                    code_->load(iterName);
                    code_->iteratorNext();
                    code_->store(resultName);
                    code_->load(resultName);
                    code_->push("done");
                    code_->getProp();
                    code_->store(doneName);
                    code_->load(doneName);
                    uint32_t notDone = code_->jfalse();
                    code_->patchU32(notDone + 2, code_->here());
                    continue;
                }
                if (element->kind() == NodeKind::RestElement) {
                    const auto& rest =
                        static_cast<const frontend::RestElement&>(*element);
                    const std::string arrayName =
                        "*dest-rest-" + std::to_string(tempId) + "*";
                    // 成员 rest 目标：Reference 在迭代之前求值
                    // （§13.15.5.4 AssignmentRestElement 步骤 1）。
                    const frontend::MemberExpression* restMember = nullptr;
                    const frontend::PrivateIdentifier* restPrivate = nullptr;
                    const PrivateInfo* restInfo = nullptr;
                    if (!lexical && rest.argument &&
                        rest.argument->kind() == NodeKind::MemberExpression) {
                        restMember =
                            static_cast<const frontend::MemberExpression*>(
                                rest.argument.get());
                        if (restMember->optional) {
                            error(*restMember,
                                  "compiler: optional chaining not supported");
                            continue;
                        }
                        if (!restMember->object) {
                            error(*restMember,
                                  "compiler: assignment target missing object");
                            continue;
                        }
                        compileExpression(*restMember->object);
                        if (restMember->property &&
                            restMember->property->kind() ==
                                NodeKind::PrivateIdentifier) {
                            restPrivate =
                                static_cast<const frontend::PrivateIdentifier*>(
                                    restMember->property.get());
                            restInfo = findPrivateInfo(restPrivate->name);
                            if (restInfo == nullptr) {
                                error(*restMember,
                                      "compiler: undeclared private name #" +
                                          restPrivate->name);
                                code_->pop();
                                continue;
                            }
                        } else {
                            compileMemberKey(*restMember);
                        }
                    }
                    code_->newArray(0);
                    code_->define(arrayName);
                    uint32_t loop = code_->here();
                    code_->pushBoolean(true);
                    code_->store(doneName);
                    code_->load(iterName);
                    code_->iteratorNext();
                    code_->store(resultName);
                    code_->load(resultName);
                    code_->push("done");
                    code_->getProp();
                    code_->store(doneName);
                    code_->load(doneName);
                    uint32_t notDone = code_->jfalse();
                    uint32_t exit = code_->jmp();
                    code_->patchU32(notDone + 2, code_->here());
                    code_->load(arrayName);
                    code_->load(resultName);
                    code_->push("value");
                    code_->getProp();
                    code_->arrayAppend();
                    code_->pop();
                    code_->jmp(loop);
                    code_->patchU32(exit + 2, code_->here());
                    code_->load(arrayName);
                    if (restMember != nullptr) {
                        if (restPrivate != nullptr) {
                            if (restInfo->field) {
                                code_->setPrivateField(restPrivate->name);
                            } else {
                                code_->setPrivateSetter(restPrivate->name);
                            }
                        } else {
                            code_->setProp();
                        }
                        code_->pop();
                    } else if (rest.argument) {
                        compileBindingPattern(*rest.argument, lexical,
                                              depth + 1);
                    } else {
                        code_->pop();
                    }
                    continue;
                }

                // 成员目标：Reference 在 IteratorStep 之前求值
                // （§13.15.5.3 步骤 1），但 ToPropertyKey/PutValue 仍在取得值之后。
                if (!lexical &&
                    element->kind() == NodeKind::MemberExpression) {
                    const auto& member =
                        static_cast<const frontend::MemberExpression&>(*element);
                    if (member.optional) {
                        error(member, "compiler: optional chaining not supported");
                        code_->pop();
                        continue;
                    }
                    if (!member.object) {
                        error(member,
                              "compiler: assignment target missing object");
                        code_->pop();
                        continue;
                    }
                    compileExpression(*member.object);
                    const frontend::PrivateIdentifier* privateKey = nullptr;
                    const PrivateInfo* info = nullptr;
                    if (member.property &&
                        member.property->kind() ==
                            NodeKind::PrivateIdentifier) {
                        privateKey =
                            static_cast<const frontend::PrivateIdentifier*>(
                                member.property.get());
                        info = findPrivateInfo(privateKey->name);
                        if (info == nullptr) {
                            error(member,
                                  "compiler: undeclared private name #" +
                                      privateKey->name);
                            code_->pop();
                            continue;
                        }
                    } else {
                        compileMemberKey(member);
                    }
                    code_->pushBoolean(true);
                    code_->store(doneName);
                    code_->load(iterName);
                    code_->iteratorNext();
                    code_->store(resultName);
                    code_->load(resultName);
                    code_->push("done");
                    code_->getProp();
                    code_->store(doneName);
                    code_->load(doneName);
                    uint32_t haveValue = code_->jfalse();
                    code_->pushUndefined();
                    uint32_t gotValue = code_->jmp();
                    code_->patchU32(haveValue + 2, code_->here());
                    code_->load(resultName);
                    code_->push("value");
                    code_->getProp();
                    code_->patchU32(gotValue + 2, code_->here());
                    if (privateKey != nullptr) {
                        if (info->field) {
                            code_->setPrivateField(privateKey->name);
                        } else {
                            code_->setPrivateSetter(privateKey->name);
                        }
                    } else {
                        code_->setProp();
                    }
                    code_->pop();
                    continue;
                }

                code_->pushBoolean(true);
                code_->store(doneName);
                code_->load(iterName);
                code_->iteratorNext();
                code_->store(resultName);
                code_->load(resultName);
                code_->push("done");
                code_->getProp();
                code_->store(doneName);
                code_->load(doneName);
                uint32_t haveValue = code_->jfalse();
                code_->pushUndefined();
                uint32_t gotValue = code_->jmp();
                code_->patchU32(haveValue + 2, code_->here());
                code_->load(resultName);
                code_->push("value");
                code_->getProp();
                code_->patchU32(gotValue + 2, code_->here());
                compileBindingPattern(*element, lexical, depth + 1);
            }

            code_->tryBodyEnd();
            uint32_t finallyStart = code_->here();
            code_->load(doneName);
            uint32_t skipClose = code_->jtrue();
            code_->load(iterName);
            code_->iteratorClose();
            code_->patchU32(skipClose + 2, code_->here());
            code_->finallyEnd();
            uint32_t tryEnd = code_->here();
            code_->patchU32(tryBeginPc + 2, 0);  // 无 catch
            code_->patchU32(tryBeginPc + 6, finallyStart);
            code_->patchU32(tryBeginPc + 10, tryEnd);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& node =
                static_cast<const frontend::ObjectPattern&>(pattern);
            const std::string objectName =
                "*dest-obj-" + std::to_string(tempId) + "*";
            // RequireObjectCoercible：null/undefined 不能做对象解构（§13.3.3.5）。
            code_->dup();
            uint32_t coercible = code_->jNotNullish();
            code_->throwTypeError(
                "Cannot destructure null or undefined");
            code_->patchU32(coercible + 2, code_->here());
            code_->define(objectName);

            // computed 键只求值一次：非 rest 属性在解构时把键存入临时槽，
            // rest 再按源码顺序取回，避免重复求值（可能带副作用）。
            std::vector<std::string> keySlots(node.properties.size());
            uint32_t computedIndex = 0;
            for (size_t i = 0; i < node.properties.size(); ++i) {
                const frontend::NodePtr& property = node.properties[i];
                if (!property || property->kind() != NodeKind::Property) {
                    continue;
                }
                const auto& prop =
                    static_cast<const frontend::Property&>(*property);
                if (prop.computed) {
                    keySlots[i] = "*dest-key-" + std::to_string(tempId) + "-" +
                                  std::to_string(computedIndex++) + "*";
                }
            }

            size_t propertyIndex = 0;
            for (const frontend::NodePtr& property : node.properties) {
                const size_t index = propertyIndex++;
                if (!property) continue;
                if (property->kind() == NodeKind::RestElement) {
                    const auto& rest =
                        static_cast<const frontend::RestElement&>(*property);
                    code_->newObject();
                    code_->load(objectName);
                    uint32_t excludedCount = 0;
                    for (size_t i = 0; i < index; ++i) {
                        const frontend::NodePtr& other = node.properties[i];
                        if (!other || other->kind() != NodeKind::Property) {
                            continue;
                        }
                        const auto& prop =
                            static_cast<const frontend::Property&>(*other);
                        if (prop.computed) {
                            code_->load(keySlots[i]);
                        } else {
                            compilePropertyKey(prop);
                            if (prop.key &&
                                prop.key->kind() == NodeKind::NumericLiteral) {
                                code_->toPropertyKey();
                            }
                        }
                        ++excludedCount;
                    }
                    code_->objectRestDynamic(excludedCount);
                    if (rest.argument) {
                        compileBindingPattern(*rest.argument, lexical,
                                              depth + 1);
                    } else {
                        code_->pop();
                    }
                    continue;
                }
                if (property->kind() != NodeKind::Property) {
                    error(*property, "compiler: unsupported object pattern element");
                    continue;
                }
                const auto& prop =
                    static_cast<const frontend::Property&>(*property);
                if (prop.propertyKind != frontend::PropertyKind::Init) {
                    error(*property, "compiler: accessor in pattern not supported");
                    continue;
                }
                // 赋值目标的 Reference 在 GetV 之前求值（§13.15.5.3）：
                // 成员目标的 object/key 必须先于读取源属性。
                if (!lexical && prop.value &&
                    prop.value->kind() == NodeKind::MemberExpression) {
                    const auto& member =
                        static_cast<const frontend::MemberExpression&>(
                            *prop.value);
                    if (member.optional) {
                        error(member, "compiler: optional chaining not supported");
                        code_->pop();
                        continue;
                    }
                    if (!member.object) {
                        error(member,
                              "compiler: assignment target missing object");
                        code_->pop();
                        continue;
                    }
                    // 1. 源 PropertyName 先求值（computed 需 ToPropertyKey）
                    //    （§13.15.5.2 步骤 1）。
                    const std::string srcKeySlot =
                        prop.computed
                            ? keySlots[index]
                            : "*dest-srckey-" + std::to_string(tempId) + "-" +
                                  std::to_string(index) + "*";
                    compilePropertyKey(prop);
                    if (prop.computed) code_->toPropertyKey();
                    code_->define(srcKeySlot);
                    // 2. 目标 Reference（§13.15.5.4 步骤 1）。
                    compileExpression(*member.object);
                    const frontend::PrivateIdentifier* privateKey = nullptr;
                    const PrivateInfo* info = nullptr;
                    if (member.property &&
                        member.property->kind() ==
                            NodeKind::PrivateIdentifier) {
                        privateKey =
                            static_cast<const frontend::PrivateIdentifier*>(
                                member.property.get());
                        info = findPrivateInfo(privateKey->name);
                        if (info == nullptr) {
                            error(member,
                                  "compiler: undeclared private name #" +
                                      privateKey->name);
                            code_->pop();
                            continue;
                        }
                    } else {
                        compileMemberKey(member);
                    }
                    // 3. GetV(value, propertyName)。
                    code_->load(objectName);
                    code_->load(srcKeySlot);
                    code_->getProp();
                    if (privateKey != nullptr) {
                        if (info->field) {
                            code_->setPrivateField(privateKey->name);
                        } else {
                            code_->setPrivateSetter(privateKey->name);
                        }
                    } else {
                        code_->setProp();
                    }
                    code_->pop();
                    continue;
                }
                code_->load(objectName);
                compilePropertyKey(prop);
                if (prop.computed) {
                    // 键先 ToPropertyKey，rest 排除按属性键比较（§13.15.5.3）。
                    code_->toPropertyKey();
                    // 保留一份键值给 rest 排除；define 弹走副本。
                    code_->dup();
                    code_->define(keySlots[index]);
                }
                code_->getProp();
                if (prop.value) {
                    compileBindingPattern(*prop.value, lexical, depth + 1);
                } else {
                    code_->pop();
                }
            }
            return;
        }
        case NodeKind::MemberExpression: {
            const auto& member =
                static_cast<const frontend::MemberExpression&>(pattern);
            if (member.optional) {
                error(pattern, "compiler: optional chaining not supported");
                code_->pop();
                return;
            }
            if (!member.object) {
                error(pattern, "compiler: assignment target missing object");
                code_->pop();
                return;
            }
            const std::string valueName =
                "*dest-val-" + std::to_string(tempId) + "*";
            code_->define(valueName);
            compileExpression(*member.object);
            if (member.property &&
                member.property->kind() == NodeKind::PrivateIdentifier) {
                const std::string& name =
                    static_cast<const frontend::PrivateIdentifier&>(
                        *member.property)
                        .name;
                const PrivateInfo* info = findPrivateInfo(name);
                if (info == nullptr) {
                    error(member, "compiler: undeclared private name #" + name);
                    code_->pop();
                    return;
                }
                code_->load(valueName);
                if (info->field) {
                    code_->setPrivateField(name);
                } else {
                    code_->setPrivateSetter(name);
                }
                code_->pop();
                return;
            }
            compileMemberKey(member);
            code_->load(valueName);
            code_->setProp();
            code_->pop();
            return;
        }
        default:
            error(pattern, "compiler: unsupported binding pattern");
            code_->pop();
            return;
    }
}

void Compiler::compileVariableDeclaration(
    const frontend::VariableDeclaration& node) {
    for (const frontend::NodePtr& declarator : node.declarations) {
        if (!declarator || declarator->kind() != NodeKind::VariableDeclarator) {
            continue;
        }
        const auto& decl =
            static_cast<const frontend::VariableDeclarator&>(*declarator);
        if (!decl.id) continue;
        const bool lexical =
            node.variableKind != frontend::VariableKind::Var;

        // var x;（无初始化）是 no-op：提升阶段已建槽，不能把已有值重置为
        // undefined（尤其 var arguments 不能覆盖隐式 arguments 对象）。
        if (!lexical && !decl.init &&
            decl.id->kind() == NodeKind::Identifier) {
            continue;
        }

        if (decl.init) {
            if (decl.id->kind() == NodeKind::Identifier) {
                compileNamedExpression(
                    *decl.init,
                    static_cast<const frontend::Identifier&>(*decl.id).name);
            } else {
                compileExpression(*decl.init);
            }
        } else {
            code_->pushUndefined();
        }

        compileBindingPattern(*decl.id, lexical, 0);
    }
}

void Compiler::compileClassDeclaration(
    const frontend::ClassDeclaration& node, bool leaveValue) {
    if (!node.body || node.body->kind() != NodeKind::ClassBody) {
        error(node, "compiler: class declaration missing body");
        code_->pushUndefined();
        return;
    }
    std::string name;
    if (node.id && node.id->kind() == NodeKind::Identifier) {
        name = static_cast<const frontend::Identifier&>(*node.id).name;
    }
    if (node.superClass) {
        compileExpression(*node.superClass);
    } else {
        code_->pushUndefined();
    }
    const auto& body = static_cast<const frontend::ClassBody&>(*node.body);
    privateScopes_.push_back(collectPrivateElements(body));
    compileClassConstructor(body, node.superClass != nullptr, name);
    // 静态元素执行期间类名必须可见：先初始化绑定，再定义元素。
    if (!name.empty()) {
        code_->dup();
        code_->initialize(name);
    }
    compileClassElements(body);
    privateScopes_.pop_back();
    if (!leaveValue) code_->pop();
}

void Compiler::compileClassExpression(const frontend::ClassExpression& node) {
    if (!node.body || node.body->kind() != NodeKind::ClassBody) {
        error(node, "compiler: class expression missing body");
        code_->pushUndefined();
        return;
    }
    std::string name;
    if (node.id && node.id->kind() == NodeKind::Identifier) {
        name = static_cast<const frontend::Identifier&>(*node.id).name;
    }
    if (node.superClass) {
        compileExpression(*node.superClass);
    } else {
        code_->pushUndefined();
    }
    const auto& body = static_cast<const frontend::ClassBody&>(*node.body);
    // 具名 class 表达式在类体内建立不可变的 ClassName 绑定（§15.7.14）。
    if (!name.empty()) {
        code_->pushScope();
        code_->pushUninitialized();
        code_->define(name, false, false);
    }
    privateScopes_.push_back(collectPrivateElements(body));
    compileClassConstructor(body, node.superClass != nullptr, name);
    if (!name.empty()) {
        code_->dup();
        code_->initialize(name);
    }
    compileClassElements(body);
    privateScopes_.pop_back();
    if (!name.empty()) code_->popScope();
}

void Compiler::compileClassConstructor(const frontend::ClassBody& body,
                                       bool isDerived,
                                       std::string_view name) {
    const frontend::MethodDefinition* constructor = nullptr;
    for (const frontend::NodePtr& element : body.elements) {
        if (!element || element->kind() != NodeKind::MethodDefinition) continue;
        const auto& method =
            static_cast<const frontend::MethodDefinition&>(*element);
        if (method.methodKind == frontend::MethodKind::Constructor) {
            constructor = &method;
            break;
        }
    }

    uint32_t skip = code_->jmp();
    uint32_t entryPc = code_->here();
    uint32_t paramCount = 0;
    bool hasRest = false;

    if (constructor != nullptr && constructor->value &&
        constructor->value->kind() == NodeKind::FunctionExpression) {
        const auto& function = static_cast<const frontend::FunctionExpression&>(
            *constructor->value);
        for (const frontend::NodePtr& param : function.params) {
            if (!param) continue;
            if (param->kind() == NodeKind::RestElement) {
                hasRest = true;
            } else {
                ++paramCount;
            }
        }
        if (function.body) {
            compileFunctionBody(function.params, *function.body, false,
                                !isDerived, true, true);
        } else {
            error(*constructor->value,
                  "compiler: class constructor missing body");
            code_->pushUndefined();
            code_->ret();
        }
    } else if (isDerived) {
        // 默认派生构造器：constructor(...args) { super(...args); }
        hasRest = true;
        code_->storeThis();
        code_->restArgs();
        code_->constructSuperSpread();
        code_->pop();
        code_->pushUndefined();
        code_->ret();
    } else {
        // 默认基类构造器：初始化实例元素后返回 undefined。
        code_->storeThis();
        code_->initInstanceElements();
        code_->pushUndefined();
        code_->ret();
    }

    uint32_t afterBody = code_->here();
    code_->patchU32(skip + 2, afterBody);

    uint32_t flags = isDerived ? vm::kClassDerived : 0;
    if (hasRest) flags |= vm::kFunctionHasRest;
    // class 构造器是严格代码：创建 arguments 但不建立形参映射。
    flags |= vm::kFunctionHasArguments;
    code_->pushClass(entryPc, paramCount, flags);
    if (!name.empty()) code_->setFuncName(name);
    code_->setStrict();

    if (constructor != nullptr && constructor->value) {
        if (const auto* captures = sema_.annotations.get<sema::FunctionCaptures>(
                constructor->value->id())) {
            for (const std::string& capture : captures->names) {
                code_->setClosure(capture);
            }
        }
    }
}

std::unordered_map<std::string, Compiler::PrivateInfo>
Compiler::collectPrivateElements(const frontend::ClassBody& body) const {
    std::unordered_map<std::string, PrivateInfo> result;
    for (const frontend::NodePtr& element : body.elements) {
        if (!element) continue;
        if (element->kind() == NodeKind::MethodDefinition) {
            const auto& method =
                static_cast<const frontend::MethodDefinition&>(*element);
            if (!method.key ||
                method.key->kind() != NodeKind::PrivateIdentifier) {
                continue;
            }
            const std::string& name =
                static_cast<const frontend::PrivateIdentifier&>(*method.key)
                    .name;
            PrivateInfo& info = result[name];
            switch (method.methodKind) {
                case frontend::MethodKind::Get:
                    info.getter = true;
                    break;
                case frontend::MethodKind::Set:
                    info.setter = true;
                    break;
                case frontend::MethodKind::Constructor:
                    break;
                default:
                    info.method = true;
                    break;
            }
            continue;
        }
        if (element->kind() == NodeKind::PropertyDefinition) {
            const auto& field =
                static_cast<const frontend::PropertyDefinition&>(*element);
            if (!field.key ||
                field.key->kind() != NodeKind::PrivateIdentifier) {
                continue;
            }
            result[static_cast<const frontend::PrivateIdentifier&>(*field.key)
                       .name]
                .field = true;
        }
    }
    return result;
}

const Compiler::PrivateInfo* Compiler::findPrivateInfo(
    std::string_view name) const {
    for (auto it = privateScopes_.rbegin(); it != privateScopes_.rend(); ++it) {
        const auto found = it->find(std::string(name));
        if (found != it->end()) return &found->second;
    }
    // 直接 eval：继承调用方 PrivateEnvironment（最外层可见集）。
    const auto inherited = options_.privateEnvironment.find(std::string(name));
    if (inherited != options_.privateEnvironment.end()) return &inherited->second;
    return nullptr;
}

void Compiler::compilePrivateRead(
    const frontend::MemberExpression& member) {
    if (!member.property ||
        member.property->kind() != NodeKind::PrivateIdentifier) {
        error(member, "compiler: malformed private member");
        code_->pushUndefined();
        return;
    }
    const std::string& name =
        static_cast<const frontend::PrivateIdentifier&>(*member.property).name;
    const PrivateInfo* info = findPrivateInfo(name);
    if (info == nullptr) {
        error(member, "compiler: undeclared private name #" + name);
        code_->pushUndefined();
        return;
    }
    if (!member.object) {
        error(member, "compiler: private member missing object");
        code_->pushUndefined();
        return;
    }
    compileExpression(*member.object);
    if (info->field) {
        code_->getPrivateField(name);
    } else if (info->method) {
        code_->getPrivateMethod(name);
    } else {
        code_->getPrivateGetter(name);
    }
}

void Compiler::compilePrivateWrite(
    const frontend::MemberExpression& member,
    const frontend::AssignmentExpression& node) {
    if (!member.property ||
        member.property->kind() != NodeKind::PrivateIdentifier) {
        error(member, "compiler: malformed private member");
        code_->pushUndefined();
        return;
    }
    const std::string& name =
        static_cast<const frontend::PrivateIdentifier&>(*member.property).name;
    const PrivateInfo* info = findPrivateInfo(name);
    if (info == nullptr) {
        error(member, "compiler: undeclared private name #" + name);
        code_->pushUndefined();
        return;
    }
    if (!member.object) {
        error(member, "compiler: private member missing object");
        code_->pushUndefined();
        return;
    }
    compileExpression(*member.object);
    bool compound = node.op != frontend::AssignmentOperator::Assign;
    const bool logical =
        node.op == frontend::AssignmentOperator::LogicalAnd ||
        node.op == frontend::AssignmentOperator::LogicalOr ||
        node.op == frontend::AssignmentOperator::Nullish;
    if (logical) {
        code_->dup();
        if (info->field) {
            code_->getPrivateField(name);
        } else if (info->method) {
            code_->getPrivateMethod(name);
        } else {
            code_->getPrivateGetter(name);
        }
        code_->dup();  // [obj, old, old]
        uint32_t keep = emitLogicalShortCircuit(node.op);  // [obj, old]
        code_->pop();                                      // [obj]
        compileExpression(*node.right);                    // [obj, new]
        if (info->field) {
            code_->setPrivateField(name);
        } else {
            code_->setPrivateSetter(name);
        }                                                  // [new]
        uint32_t end = code_->jmp();
        code_->patchU32(keep + 2, code_->here());          // [obj, old]
        code_->swap();                                     // [old, obj]
        code_->pop();                                      // [old]
        code_->patchU32(end + 2, code_->here());
        return;
    }
    if (compound) {
        code_->dup();
        if (info->field) {
            code_->getPrivateField(name);
        } else if (info->method) {
            code_->getPrivateMethod(name);
        } else {
            code_->getPrivateGetter(name);
        }
        compileExpression(*node.right);
        if (!emitCompoundBinary(node.op, node)) return;
    } else {
        compileExpression(*node.right);
    }
    if (info->field) {
        code_->setPrivateField(name);
    } else {
        code_->setPrivateSetter(name);
    }
}

void Compiler::compilePrivateUpdate(
    const frontend::MemberExpression& member,
    const frontend::UpdateExpression& node) {
    if (!member.property ||
        member.property->kind() != NodeKind::PrivateIdentifier) {
        error(member, "compiler: malformed private member");
        code_->pushUndefined();
        return;
    }
    const std::string& name =
        static_cast<const frontend::PrivateIdentifier&>(*member.property).name;
    const PrivateInfo* info = findPrivateInfo(name);
    if (info == nullptr) {
        error(member, "compiler: undeclared private name #" + name);
        code_->pushUndefined();
        return;
    }
    if (!member.object) {
        error(member, "compiler: private member missing object");
        code_->pushUndefined();
        return;
    }
    compileExpression(*member.object);
    code_->dup();
    if (info->field) {
        code_->getPrivateField(name);
    } else if (info->method) {
        code_->getPrivateMethod(name);
    } else {
        code_->getPrivateGetter(name);
    }
    if (!node.prefix) code_->clone();
    code_->push(1.0);
    if (node.op == frontend::UpdateOperator::Increment) {
        code_->add();
    } else {
        code_->sub();
    }
    if (node.prefix) {
        if (info->field) {
            code_->setPrivateField(name);
        } else {
            code_->setPrivateSetter(name);
        }
        return;
    }
    // 后缀：栈为 [obj, old, new]，交换成 [obj, new, old] 保留旧值。
    code_->swap();
    if (info->field) {
        code_->setPrivateFieldKeep(name);
    } else {
        code_->setPrivateSetterKeep(name);
    }
}

void Compiler::compilePrivateCall(
    const frontend::MemberExpression& member,
    const frontend::CallExpression& node) {
    if (!member.property ||
        member.property->kind() != NodeKind::PrivateIdentifier) {
        error(member, "compiler: malformed private member");
        code_->pushUndefined();
        return;
    }
    const std::string& name =
        static_cast<const frontend::PrivateIdentifier&>(*member.property).name;
    const PrivateInfo* info = findPrivateInfo(name);
    if (info == nullptr) {
        error(member, "compiler: undeclared private name #" + name);
        code_->pushUndefined();
        return;
    }
    if (!member.object) {
        error(member, "compiler: private member missing object");
        code_->pushUndefined();
        return;
    }
    compileExpression(*member.object);
    code_->dup();
    if (info->method) {
        code_->getPrivateMethod(name);
    } else if (info->field) {
        code_->getPrivateField(name);
    } else {
        code_->getPrivateGetter(name);
    }

    bool hasSpread = false;
    for (const frontend::NodePtr& argument : node.arguments) {
        if (argument && argument->kind() == NodeKind::SpreadElement) {
            hasSpread = true;
            break;
        }
    }
    if (hasSpread) {
        compileArgumentsArray(node.arguments);
        code_->callMethodSpread();
        return;
    }
    uint32_t argc = 0;
    for (const frontend::NodePtr& argument : node.arguments) {
        if (!argument) continue;
        compileExpression(*argument);
        ++argc;
    }
    code_->callMethod(argc);
}

void Compiler::compileClassElements(const frontend::ClassBody& body) {
    for (const frontend::NodePtr& element : body.elements) {
        if (!element) continue;
        switch (element->kind()) {
            case NodeKind::MethodDefinition: {
                const auto& method =
                    static_cast<const frontend::MethodDefinition&>(*element);
                if (method.methodKind == frontend::MethodKind::Constructor) {
                    continue;  // 已作为构造器编译
                }
                if (!method.value ||
                    method.value->kind() != NodeKind::FunctionExpression) {
                    error(*element, "compiler: malformed class method");
                    continue;
                }
                const auto& function =
                    static_cast<const frontend::FunctionExpression&>(
                        *method.value);
                if (method.key &&
                    method.key->kind() == NodeKind::PrivateIdentifier) {
                    const std::string& privateName =
                        static_cast<const frontend::PrivateIdentifier&>(
                            *method.key)
                            .name;
                    compileMethodFunction(function, "#" + privateName);
                    uint32_t privateKind = 0;
                    if (method.methodKind == frontend::MethodKind::Get) {
                        privateKind = 1;
                    }
                    if (method.methodKind == frontend::MethodKind::Set) {
                        privateKind = 2;
                    }
                    uint32_t flags =
                        (method.isStatic ? 1u : 0u) | (privateKind << 1);
                    code_->definePrivateMethod(privateName, flags);
                    break;
                }
                std::string methodName;
                if (!method.computed && method.key &&
                    method.key->kind() == NodeKind::Identifier) {
                    methodName = static_cast<const frontend::Identifier&>(
                                     *method.key)
                                     .name;
                }
                compileClassKey(*method.key, method.computed);
                compileMethodFunction(function, methodName);
                if (method.computed) code_->setFuncNameFromKey();
                uint32_t kind = 0;
                if (method.methodKind == frontend::MethodKind::Get) kind = 1;
                if (method.methodKind == frontend::MethodKind::Set) kind = 2;
                uint32_t flags = (method.isStatic ? 1u : 0u) | (kind << 1);
                code_->defineClassMethod(flags);
                break;
            }
            case NodeKind::PropertyDefinition: {
                const auto& field =
                    static_cast<const frontend::PropertyDefinition&>(*element);
                if (field.key &&
                    field.key->kind() == NodeKind::PrivateIdentifier) {
                    const std::string& privateName =
                        static_cast<const frontend::PrivateIdentifier&>(
                            *field.key)
                            .name;
                    if (field.value) {
                        compileFieldInitializer(*field.value, field.id(),
                                                "#" + privateName);
                    } else {
                        code_->pushUndefined();
                    }
                    code_->definePrivateField(
                        privateName, field.isStatic ? 1u : 0u);
                    break;
                }
                if (field.key) {
                    compileClassKey(*field.key, field.computed);
                } else {
                    error(*element, "compiler: class field missing key");
                    code_->pushUndefined();
                }
                if (field.value) {
                    compileFieldInitializer(
                        *field.value, field.id(),
                        field.computed ? std::string()
                                       : staticKeyName(field.key.get()));
                } else {
                    code_->pushUndefined();
                }
                code_->defineClassField(field.isStatic ? 1u : 0u);
                break;
            }
            case NodeKind::StaticBlock: {
                const auto& block =
                    static_cast<const frontend::StaticBlock&>(*element);
                // 保留构造器：call_method 会消费 receiver；多留一份供
                // set_owner_class 使用后仍保留类声明栈上的构造器。
                code_->dup();
                code_->dup();
                uint32_t skip = code_->jmp();
                uint32_t entryPc = code_->here();
                code_->storeThis();
                code_->pushScope();
                hoistScope(block.body, true);
                // static block 是独立执行上下文：不写外层 eval 完成值槽。
                const bool savedEvalCompletion = evalCompletion_;
                evalCompletion_ = false;
                for (const frontend::NodePtr& statement : block.body) {
                    if (statement) compileStatement(*statement);
                }
                evalCompletion_ = savedEvalCompletion;
                code_->pushUndefined();
                code_->ret();
                uint32_t afterBody = code_->here();
                code_->patchU32(skip + 2, afterBody);
                code_->pushFunction(entryPc, 0, 0);
                code_->setStrict();
                if (const auto* captures =
                        sema_.annotations.get<sema::FunctionCaptures>(
                            element->id())) {
                    for (const std::string& capture : captures->names) {
                        code_->setClosure(capture);
                    }
                }
                // static block 函数需要 ownerClass 才能解析私有名。
                // 栈为 [ctor, receiver, ctor, fn]：swap 后 [ctor, receiver, fn, ctor]，
                // set_owner_class 消费 fn/ctor 并压回 fn → [ctor, receiver, fn]。
                code_->swap();
                code_->setOwnerClass();
                // this = class 构造器；丢弃返回值。
                code_->callMethod(0);
                code_->pop();
                break;
            }
            default:
                error(*element, "compiler: unsupported class element");
                break;
        }
    }
}

void Compiler::compileFieldInitializer(const frontend::Node& value,
                                       frontend::NodeId owner,
                                       std::string_view name) {
    uint32_t skip = code_->jmp();
    uint32_t entryPc = code_->here();
    code_->storeThis();
    code_->pushScope();
    compileNamedExpression(value, name);
    code_->ret();
    uint32_t afterBody = code_->here();
    code_->patchU32(skip + 2, afterBody);
    code_->pushFunction(entryPc, 0, 0);
    code_->setStrict();
    if (const auto* captures =
            sema_.annotations.get<sema::FunctionCaptures>(owner)) {
        for (const std::string& capture : captures->names) {
            code_->setClosure(capture);
        }
    }
}

void Compiler::compileMethodFunction(
    const frontend::FunctionExpression& function, std::string_view name) {
    if (!function.body) {
        error(function, "compiler: class method missing body");
        code_->pushUndefined();
        return;
    }
    compileFunctionObject(function, function.params, *function.body, false,
                          name, function.generator, function.async, true);
}

void Compiler::compileClassKey(const frontend::Node& key, bool computed) {
    if (computed) {
        compileExpression(key);
        return;
    }
    switch (key.kind()) {
        case NodeKind::Identifier:
            code_->push(static_cast<const frontend::Identifier&>(key).name);
            return;
        case NodeKind::StringLiteral:
            code_->push(static_cast<const frontend::StringLiteral&>(key).value);
            return;
        case NodeKind::NumericLiteral:
            compileNumericLiteral(
                static_cast<const frontend::NumericLiteral&>(key));
            return;
        case NodeKind::PrivateIdentifier:
            error(key, "compiler: private class elements not supported");
            code_->pushUndefined();
            return;
        default:
            error(key, "compiler: unsupported class element key");
            code_->pushUndefined();
            return;
    }
}

void Compiler::compileFunctionObject(const Node& node,
                                     const frontend::NodeList& params,
                                     const Node& body, bool arrow,
                                     std::string_view name, bool generator,
                                     bool async, bool method) {
    // 函数严格性：class 方法恒严格，其余取 Sema 标注。
    const sema::FunctionInfo* info =
        sema_.annotations.get<sema::FunctionInfo>(node.id());
    const bool strict = method || (info != nullptr && info->isStrict);

    // 函数体内联在当前位置，执行路径先跳过它。
    uint32_t skip = code_->jmp();
    uint32_t entryPc = code_->here();
    compileFunctionBody(params, body, arrow || method, false, !arrow, strict,
                        generator);
    uint32_t afterBody = code_->here();
    code_->patchU32(skip + 2, afterBody);

    uint32_t paramCount = 0;
    bool hasRest = false;
    for (const frontend::NodePtr& param : params) {
        if (!param) continue;
        if (param->kind() == NodeKind::Identifier ||
            param->kind() == NodeKind::ArrayPattern ||
            param->kind() == NodeKind::ObjectPattern ||
            param->kind() == NodeKind::AssignmentPattern) {
            ++paramCount;
        } else if (param->kind() == NodeKind::RestElement) {
            hasRest = true;
        } else {
            error(*param, "compiler: unsupported parameter pattern");
        }
    }

    // arguments（§10.4.4）：非箭头函数都创建；仅非严格 + 简单形参列表建立映射。
    std::vector<std::string> paramNames;
    bool simpleParams = true;
    for (const frontend::NodePtr& param : params) {
        if (param && param->kind() == NodeKind::Identifier) {
            paramNames.push_back(
                static_cast<const frontend::Identifier&>(*param).name);
        } else if (param) {
            simpleParams = false;
        }
    }
    const bool mapped = !arrow && !strict && simpleParams;

    uint32_t flags = 0;
    if (hasRest) flags |= vm::kFunctionHasRest;
    if (!arrow) flags |= vm::kFunctionHasArguments;
    if (mapped) flags |= vm::kFunctionArgumentsMapped;
    if (method) {
        // class 方法/字段初始化器：不可构造；生成器方法仍有 prototype
        // （挂 %GeneratorPrototype% / %AsyncGeneratorPrototype%）。
        if (generator && async) {
            flags |= vm::kFunctionGenerator | vm::kFunctionAsyncGenerator |
                     vm::kFunctionHasPrototype;
        } else if (generator) {
            flags |= vm::kFunctionGenerator | vm::kFunctionHasPrototype;
        } else if (async) {
            flags |= vm::kFunctionAsync;
        }
    } else if (generator && async) {
        // async generator：不可构造，有 prototype（挂 %AsyncGeneratorPrototype%）。
        flags |= vm::kFunctionGenerator | vm::kFunctionAsyncGenerator |
                 vm::kFunctionHasPrototype;
    } else if (generator) {
        // 生成器函数不可构造，但有 prototype 属性（其原型链挂 %GeneratorPrototype%）。
        flags |= vm::kFunctionGenerator | vm::kFunctionHasPrototype;
    } else if (async) {
        // async 函数不可构造、无 prototype 属性。
        flags |= vm::kFunctionAsync;
    } else if (!arrow) {
        flags |= vm::kFunctionConstructible | vm::kFunctionHasPrototype;
    }
    code_->pushFunction(entryPc, paramCount, flags);
    if (mapped) code_->setParamNames(paramNames);
    if (strict) code_->setStrict();

    if (!name.empty()) code_->setFuncName(name);

    // Sema 的捕获列表（穿透捕获）：逐个从定义点作用域链绑进闭包。
    if (const auto* captures =
            sema_.annotations.get<sema::FunctionCaptures>(node.id())) {
        for (const std::string& capture : captures->names) {
            code_->setClosure(capture);
        }
    }

    // 箭头函数在定义点焊死外层 this（class 方法不绑定），并继承外层
    // [[HomeObject]]（箭头自身没有，super 解析走外层方法）。
    if (arrow && !method) {
        code_->inheritHomeObject();
        code_->loadThis();
        code_->bindThis();
    }
}

void Compiler::compileFunctionBody(const frontend::NodeList& params,
                                   const Node& body, bool arrow,
                                   bool initInstanceElements,
                                   bool hasArguments, bool strict,
                                   bool generatorBody) {
    // 函数体是独立执行上下文：不写外层 eval 的完成值槽。
    ++functionDepth_;
    const bool savedEvalCompletion = evalCompletion_;
    evalCompletion_ = false;
    const bool savedGlobalCode = globalCode_;
    globalCode_ = false;
    const bool savedHasArguments = hasArgumentsBinding_;
    hasArgumentsBinding_ = hasArguments;
    const bool savedStrict = strict_;
    strict_ = strict;
    // 形参建槽（argument_scope）：push_undefined; define "p"
    for (const frontend::NodePtr& param : params) {
        if (!param) continue;
        if (param->kind() == NodeKind::Identifier) {
            const auto& id = static_cast<const frontend::Identifier&>(*param);
            code_->pushUndefined();
            code_->define(id.name, true, true, true);
        } else if (param->kind() == NodeKind::RestElement) {
            const auto& rest = static_cast<const frontend::RestElement&>(*param);
            if (rest.argument) {
                std::vector<std::string> names;
                collectPatternNames(*rest.argument, names);
                for (const std::string& name : names) {
                    code_->pushUndefined();
                    code_->define(name, true, true, true);
                }
            }
        } else if (param->kind() == NodeKind::ArrayPattern ||
                   param->kind() == NodeKind::ObjectPattern ||
                   param->kind() == NodeKind::AssignmentPattern) {
            std::vector<std::string> names;
            collectPatternNames(*param, names);
            for (const std::string& name : names) {
                code_->pushUndefined();
                code_->define(name, true, true, true);
            }
        } else {
            error(*param, "compiler: unsupported parameter pattern");
        }
    }

    // 栈顶是 self，先消费；随后按顺序绑定实参（栈上 argv0 在顶）。
    code_->storeThis();
    if (initInstanceElements) code_->initInstanceElements();
    for (const frontend::NodePtr& param : params) {
        if (!param) continue;
        if (param->kind() == NodeKind::Identifier) {
            const auto& id = static_cast<const frontend::Identifier&>(*param);
            code_->store(id.name);
        } else if (param->kind() == NodeKind::RestElement) {
            const auto& rest = static_cast<const frontend::RestElement&>(*param);
            code_->restArgs();
            if (rest.argument) {
                compileBindingPattern(*rest.argument, false, 0);
            } else {
                code_->pop();
            }
        } else if (param->kind() == NodeKind::ArrayPattern ||
                   param->kind() == NodeKind::ObjectPattern ||
                   param->kind() == NodeKind::AssignmentPattern) {
            compileBindingPattern(*param, false, 0);
        }
    }

    code_->pushVarScope();

    // 生成器参数绑定在调用时同步完成；BodyStart 让调用方在此保存现场、
    // 创建协程，函数体待首次 next() 才执行（§27.5.1.1 / §27.6.1.1）。
    if (generatorBody) code_->bodyStart();

    if (arrow && body.kind() != NodeKind::BlockStatement) {
        // 表达式体箭头：表达式值即返回值。
        compileExpression(body);
        code_->ret();
        evalCompletion_ = savedEvalCompletion;
        hasArgumentsBinding_ = savedHasArguments;
        strict_ = savedStrict;
        globalCode_ = savedGlobalCode;
        --functionDepth_;
        return;
    }

    if (body.kind() == NodeKind::BlockStatement) {
        const auto& block = static_cast<const frontend::BlockStatement&>(body);
        hoistScope(block.body, true);
        for (const frontend::NodePtr& statement : block.body) {
            if (statement) compileStatement(*statement);
        }
    } else {
        error(body, "compiler: unsupported function body");
    }

    // 编译器统一补隐式 return undefined（ADR-024）。
    code_->pushUndefined();
    code_->ret();
    evalCompletion_ = savedEvalCompletion;
    hasArgumentsBinding_ = savedHasArguments;
    strict_ = savedStrict;
    globalCode_ = savedGlobalCode;
    --functionDepth_;
}

// —— 声明提升 ——

const Node* Compiler::declarationOf(const Node& node) {
    switch (node.kind()) {
        case NodeKind::ExportNamedDeclaration:
            return static_cast<const frontend::ExportNamedDeclaration&>(node)
                .declaration.get();
        case NodeKind::ExportDefaultDeclaration:
            return static_cast<const frontend::ExportDefaultDeclaration&>(node)
                .declaration.get();
        default:
            return &node;
    }
}

bool Compiler::usesDefaultSlot(
    const frontend::ExportDefaultDeclaration& node) {
    if (!node.declaration) return false;
    const Node& declaration = *node.declaration;
    if (declaration.kind() == NodeKind::FunctionDeclaration) {
        const auto& function =
            static_cast<const frontend::FunctionDeclaration&>(declaration);
        return !(function.id &&
                 function.id->kind() == NodeKind::Identifier);
    }
    if (declaration.kind() == NodeKind::ClassDeclaration) {
        const auto& klass =
            static_cast<const frontend::ClassDeclaration&>(declaration);
        return !(klass.id && klass.id->kind() == NodeKind::Identifier);
    }
    return true;
}

void Compiler::hoistScope(const frontend::NodeList& body, bool varScope,
                          bool annexB) {
    hoistSlots(body, varScope);
    hoistFunctions(body, annexB);
}

void Compiler::hoistSlots(const frontend::NodeList& body, bool varScope) {
    if (varScope) hoistVarSlots(body, false);
    hoistLexicalSlots(body, varScope && globalCode_);

    // 直接函数声明：先建 undefined 槽（函数对象在 hoistFunctions 创建）。
    for (const frontend::NodePtr& statement : body) {
        const Node* declaration = statement ? declarationOf(*statement)
                                            : nullptr;
        if (declaration == nullptr ||
            declaration->kind() != NodeKind::FunctionDeclaration) {
            continue;
        }
        const auto& function =
            static_cast<const frontend::FunctionDeclaration&>(*declaration);
        if (!function.id || function.id->kind() != NodeKind::Identifier) {
            // 匿名 export default function 由隐藏槽位承载。
            if (statement->kind() != NodeKind::ExportDefaultDeclaration) {
                error(*statement, "compiler: function declaration missing name");
            }
            continue;
        }
        const std::string& name =
            static_cast<const frontend::Identifier&>(*function.id).name;
        if (globalCode_ && varScope) {
            // 全局函数声明：属性落到 global object（CreateGlobalFunctionBinding）。
            code_->declareGlobalFunction(name);
        } else {
            code_->pushUndefined();
            code_->define(name, true, true, true);
        }
    }
}

void Compiler::hoistVarSlots(const frontend::NodeList& body, bool declare) {
    // var：递归收集（不进入嵌套函数），在函数/模块/eval 变量环境建槽。
    std::vector<std::string> varNames;
    for (const frontend::NodePtr& statement : body) {
        if (!statement) continue;
        collectVarNames(*statement, varNames);
        // Annex B.3.3：非严格模式下块级/if 子句函数声明也建 var 绑定。
        if (!strict_) collectAnnexBFunctionNames(*statement, varNames, true);
    }
    std::unordered_set<std::string> seen;
    for (const std::string& name : varNames) {
        if (!seen.insert(name).second) continue;
        // 函数已有隐式 arguments 绑定：var arguments 是 no-op（§10.2.1.2）。
        if (hasArgumentsBinding_ && name == "arguments") continue;
        if (declare) {
            code_->declareVar(name);
        } else if (globalCode_) {
            // 脚本全局 var：属性落到 global object（CreateGlobalVarBinding）。
            code_->declareGlobalVar(name);
        } else {
            code_->pushUndefined();
            code_->define(name, true, true, true);
        }
    }
}

void Compiler::hoistLexicalSlots(const frontend::NodeList& body,
                                 bool globalLexical) {
    // 直接 let/const：建 uninitialized 槽（TDZ）。
    for (const frontend::NodePtr& statement : body) {
        const Node* declaration = statement ? declarationOf(*statement)
                                            : nullptr;
        if (declaration != nullptr &&
            declaration->kind() == NodeKind::ClassDeclaration) {
            const auto& klass =
                static_cast<const frontend::ClassDeclaration&>(*declaration);
            if (klass.id && klass.id->kind() == NodeKind::Identifier) {
                const std::string& name =
                    static_cast<const frontend::Identifier&>(*klass.id).name;
                code_->pushUninitialized();
                if (globalLexical) {
                    code_->checkGlobalLexical(name);
                    code_->defineGlobalLexical(name, true, false);
                } else {
                    code_->define(name, true, false);
                }
            }
            continue;
        }
        if (declaration == nullptr ||
            declaration->kind() != NodeKind::VariableDeclaration) {
            continue;
        }
        const auto& variables =
            static_cast<const frontend::VariableDeclaration&>(*declaration);
        if (variables.variableKind == frontend::VariableKind::Var) continue;
        bool mutableBinding =
            variables.variableKind == frontend::VariableKind::Let;
        for (const frontend::NodePtr& declarator : variables.declarations) {
            if (!declarator ||
                declarator->kind() != NodeKind::VariableDeclarator) {
                continue;
            }
            const auto& decl =
                static_cast<const frontend::VariableDeclarator&>(*declarator);
            if (!decl.id) continue;
            std::vector<std::string> names;
            collectPatternNames(*decl.id, names);
            for (const std::string& name : names) {
                code_->pushUninitialized();
                if (globalLexical) {
                    code_->checkGlobalLexical(name);
                    code_->defineGlobalLexical(name, mutableBinding, false);
                } else {
                    code_->define(name, mutableBinding, false);
                }
            }
        }
    }
}

void Compiler::hoistFunctions(const frontend::NodeList& body, bool annexB) {
    for (const frontend::NodePtr& statement : body) {
        const Node* declaration = statement ? declarationOf(*statement)
                                            : nullptr;
        if (declaration == nullptr ||
            declaration->kind() != NodeKind::FunctionDeclaration) {
            continue;
        }
        const auto& function =
            static_cast<const frontend::FunctionDeclaration&>(*declaration);
        if (!function.id || function.id->kind() != NodeKind::Identifier ||
            !function.body) {
            continue;
        }
        const std::string& name =
            static_cast<const frontend::Identifier&>(*function.id).name;
        compileFunctionObject(*declaration, function.params, *function.body,
                              false, name, function.generator,
                              function.async);
        code_->store(name);
        // Annex B.3.3：块级函数在块入口同步写入外层 var 绑定。
        if (annexB && !strict_) {
            code_->load(name);
            code_->storeVar(name);
        }
    }
}

void Compiler::compileAnnexBFunction(
    const frontend::FunctionDeclaration& function, bool blockScoped) {
    if (!function.id || function.id->kind() != NodeKind::Identifier ||
        !function.body) {
        return;
    }
    const std::string& name =
        static_cast<const frontend::Identifier&>(*function.id).name;
    if (blockScoped) code_->pushScope();
    if (blockScoped) {
        code_->pushUndefined();
        code_->define(name);
    }
    compileFunctionObject(function, function.params, *function.body, false,
                          name, function.generator, function.async);
    code_->store(name);
    if (blockScoped) {
        // 同步写入外层变量环境（Annex B.3.3）。
        code_->load(name);
        code_->storeVar(name);
        code_->popScope();
    }
}

void Compiler::hoistEvalFunctions(const frontend::NodeList& body) {
    // sloppy eval：函数声明进入变量环境；declareVar 只补缺失槽，
    // 随后 store 覆盖为函数对象（允许与已有 var/function 同名）。
    for (const frontend::NodePtr& statement : body) {
        const Node* declaration = statement ? declarationOf(*statement)
                                            : nullptr;
        if (declaration == nullptr ||
            declaration->kind() != NodeKind::FunctionDeclaration) {
            continue;
        }
        const auto& function =
            static_cast<const frontend::FunctionDeclaration&>(*declaration);
        if (!function.id || function.id->kind() != NodeKind::Identifier ||
            !function.body) {
            continue;
        }
        const std::string& name =
            static_cast<const frontend::Identifier&>(*function.id).name;
        code_->declareVar(name);
        compileFunctionObject(*declaration, function.params, *function.body,
                              false, name, function.generator,
                              function.async);
        code_->store(name);
    }
}

void Compiler::collectVarNames(const frontend::Node& statement,
                               std::vector<std::string>& names) {
    switch (statement.kind()) {
        case NodeKind::ExportNamedDeclaration:
        case NodeKind::ExportDefaultDeclaration: {
            const Node* declaration = declarationOf(statement);
            if (declaration != nullptr) collectVarNames(*declaration, names);
            break;
        }
        case NodeKind::VariableDeclaration: {
            const auto& declaration =
                static_cast<const frontend::VariableDeclaration&>(statement);
            if (declaration.variableKind != frontend::VariableKind::Var) break;
            for (const frontend::NodePtr& declarator : declaration.declarations) {
                if (!declarator ||
                    declarator->kind() != NodeKind::VariableDeclarator) {
                    continue;
                }
                const auto& decl =
                    static_cast<const frontend::VariableDeclarator&>(*declarator);
                if (decl.id) collectPatternNames(*decl.id, names);
            }
            break;
        }
        case NodeKind::BlockStatement: {
            const auto& block =
                static_cast<const frontend::BlockStatement&>(statement);
            for (const frontend::NodePtr& child : block.body) {
                if (child) collectVarNames(*child, names);
            }
            break;
        }
        case NodeKind::IfStatement: {
            const auto& node =
                static_cast<const frontend::IfStatement&>(statement);
            if (node.consequent) collectVarNames(*node.consequent, names);
            if (node.alternate) collectVarNames(*node.alternate, names);
            break;
        }
        case NodeKind::WhileStatement: {
            const auto& node =
                static_cast<const frontend::WhileStatement&>(statement);
            if (node.body) collectVarNames(*node.body, names);
            break;
        }
        case NodeKind::DoWhileStatement: {
            const auto& node =
                static_cast<const frontend::DoWhileStatement&>(statement);
            if (node.body) collectVarNames(*node.body, names);
            break;
        }
        case NodeKind::ForStatement: {
            const auto& node =
                static_cast<const frontend::ForStatement&>(statement);
            if (node.init) collectVarNames(*node.init, names);
            if (node.body) collectVarNames(*node.body, names);
            break;
        }
        case NodeKind::ForInStatement: {
            const auto& node =
                static_cast<const frontend::ForInStatement&>(statement);
            if (node.left) collectVarNames(*node.left, names);
            if (node.body) collectVarNames(*node.body, names);
            break;
        }
        case NodeKind::ForOfStatement: {
            const auto& node =
                static_cast<const frontend::ForOfStatement&>(statement);
            if (node.left) collectVarNames(*node.left, names);
            if (node.body) collectVarNames(*node.body, names);
            break;
        }
        case NodeKind::LabeledStatement: {
            const auto& node =
                static_cast<const frontend::LabeledStatement&>(statement);
            if (node.body) collectVarNames(*node.body, names);
            break;
        }
        case NodeKind::WithStatement: {
            // with 不建立变量环境，体内 var 仍提升到外层函数/全局。
            const auto& node =
                static_cast<const frontend::WithStatement&>(statement);
            if (node.body) collectVarNames(*node.body, names);
            break;
        }
        case NodeKind::TryStatement: {
            const auto& node =
                static_cast<const frontend::TryStatement&>(statement);
            if (node.block) collectVarNames(*node.block, names);
            if (node.handler) collectVarNames(*node.handler, names);
            if (node.finalizer) collectVarNames(*node.finalizer, names);
            break;
        }
        case NodeKind::CatchClause: {
            const auto& node =
                static_cast<const frontend::CatchClause&>(statement);
            if (node.body) collectVarNames(*node.body, names);
            break;
        }
        case NodeKind::SwitchStatement: {
            const auto& node =
                static_cast<const frontend::SwitchStatement&>(statement);
            for (const frontend::NodePtr& caseNode : node.cases) {
                if (!caseNode || caseNode->kind() != NodeKind::SwitchCase) {
                    continue;
                }
                const auto& switchCase =
                    static_cast<const frontend::SwitchCase&>(*caseNode);
                for (const frontend::NodePtr& child : switchCase.consequent) {
                    if (child) collectVarNames(*child, names);
                }
            }
            break;
        }
        default:
            // 函数/类边界：var 不穿透。
            break;
    }
}

// Annex B.3.3：收集非顶层（块/if 子句/标签）函数声明的名字。
// 顶层函数声明已由 hoistFunctions 在变量环境建槽，不在此重复。
void Compiler::collectAnnexBFunctionNames(const frontend::Node& statement,
                                          std::vector<std::string>& names,
                                          bool topLevel) {
    switch (statement.kind()) {
        case NodeKind::FunctionDeclaration: {
            if (topLevel) return;
            const auto& function =
                static_cast<const frontend::FunctionDeclaration&>(statement);
            if (function.id &&
                function.id->kind() == NodeKind::Identifier) {
                names.push_back(
                    static_cast<const frontend::Identifier&>(*function.id).name);
            }
            return;
        }
        case NodeKind::ClassDeclaration:
        case NodeKind::ClassExpression:
        case NodeKind::FunctionExpression:
        case NodeKind::ArrowFunctionExpression:
            return;
        case NodeKind::BlockStatement: {
            const auto& block =
                static_cast<const frontend::BlockStatement&>(statement);
            for (const frontend::NodePtr& child : block.body) {
                if (child) collectAnnexBFunctionNames(*child, names, false);
            }
            return;
        }
        case NodeKind::IfStatement: {
            const auto& node =
                static_cast<const frontend::IfStatement&>(statement);
            if (node.consequent) {
                collectAnnexBFunctionNames(*node.consequent, names, false);
            }
            if (node.alternate) {
                collectAnnexBFunctionNames(*node.alternate, names, false);
            }
            return;
        }
        case NodeKind::WhileStatement: {
            const auto& node =
                static_cast<const frontend::WhileStatement&>(statement);
            if (node.body) collectAnnexBFunctionNames(*node.body, names, false);
            return;
        }
        case NodeKind::DoWhileStatement: {
            const auto& node =
                static_cast<const frontend::DoWhileStatement&>(statement);
            if (node.body) collectAnnexBFunctionNames(*node.body, names, false);
            return;
        }
        case NodeKind::ForStatement: {
            const auto& node =
                static_cast<const frontend::ForStatement&>(statement);
            if (node.body) collectAnnexBFunctionNames(*node.body, names, false);
            return;
        }
        case NodeKind::ForInStatement: {
            const auto& node =
                static_cast<const frontend::ForInStatement&>(statement);
            if (node.body) collectAnnexBFunctionNames(*node.body, names, false);
            return;
        }
        case NodeKind::ForOfStatement: {
            const auto& node =
                static_cast<const frontend::ForOfStatement&>(statement);
            if (node.body) collectAnnexBFunctionNames(*node.body, names, false);
            return;
        }
        case NodeKind::LabeledStatement: {
            const auto& node =
                static_cast<const frontend::LabeledStatement&>(statement);
            if (node.body) collectAnnexBFunctionNames(*node.body, names, false);
            return;
        }
        case NodeKind::TryStatement: {
            const auto& node =
                static_cast<const frontend::TryStatement&>(statement);
            if (node.block) collectAnnexBFunctionNames(*node.block, names, false);
            if (node.handler) {
                collectAnnexBFunctionNames(*node.handler, names, false);
            }
            if (node.finalizer) {
                collectAnnexBFunctionNames(*node.finalizer, names, false);
            }
            return;
        }
        case NodeKind::CatchClause: {
            const auto& node =
                static_cast<const frontend::CatchClause&>(statement);
            if (node.body) collectAnnexBFunctionNames(*node.body, names, false);
            return;
        }
        case NodeKind::SwitchStatement: {
            const auto& node =
                static_cast<const frontend::SwitchStatement&>(statement);
            for (const frontend::NodePtr& caseNode : node.cases) {
                if (!caseNode || caseNode->kind() != NodeKind::SwitchCase) {
                    continue;
                }
                const auto& switchCase =
                    static_cast<const frontend::SwitchCase&>(*caseNode);
                for (const frontend::NodePtr& child : switchCase.consequent) {
                    if (child) {
                        collectAnnexBFunctionNames(*child, names, false);
                    }
                }
            }
            return;
        }
        default:
            return;
    }
}

// —— 辅助 ——

void Compiler::error(const Node& node, std::string message) {
    errors_.push_back(CompileError{node.range(), std::move(message)});
}

void Compiler::mapNode(const Node& node, uint32_t begin) {
    uint32_t end = code_->here();
    if (end <= begin) return;
    code_->mapSource(begin, end,
                    core::SourcePosition(options_.filename,
                                         node.range().start.line,
                                         node.range().start.column));
}

}  // namespace neo::compiler
