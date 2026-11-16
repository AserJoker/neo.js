// Compiler.cpp - AST + Sema 标注 → 字节码（ADR-061）
//
// 分派函数只做 kind → 具体函数的路由；具体函数只发射本节点的字节码。
// 当前最小链路覆盖 V0/V1 的字面量、表达式语句、return、块、标识符与二元运算。

#include "compiler/Compiler.h"

#include "sema/ResolvePass.h"

#include <string>
#include <unordered_set>
#include <utility>

namespace neo::compiler {

using frontend::Node;
using frontend::NodeKind;
using frontend::NodePtr;
using frontend::SourceType;

namespace {

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
    // 前置条件被破坏时不在错误 AST 上继续生成字节码。
    if (!sema_.ok()) {
        errors_.push_back({program_.range(), "compiler: sema reported errors"});
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
    hoistScope(program_.body, true);
    for (const frontend::NodePtr& statement : program_.body) {
        if (statement) compileStatement(*statement);
    }
    code_->halt();
}

void Compiler::compileModule() {
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
        case NodeKind::VariableDeclaration:
            compileVariableDeclaration(
                static_cast<const frontend::VariableDeclaration&>(node));
            break;
        case NodeKind::IfStatement:
            compileIfStatement(
                static_cast<const frontend::IfStatement&>(node));
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
        case NodeKind::StringLiteral:
            compileStringLiteral(
                static_cast<const frontend::StringLiteral&>(node));
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
        case NodeKind::MemberExpression:
            compileMemberExpression(
                static_cast<const frontend::MemberExpression&>(node));
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
        case NodeKind::AssignmentExpression:
            compileAssignmentExpression(
                static_cast<const frontend::AssignmentExpression&>(node));
            break;
        case NodeKind::UpdateExpression:
            compileUpdateExpression(
                static_cast<const frontend::UpdateExpression&>(node));
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

// —— 语句 ——

void Compiler::compileBlockStatement(const frontend::BlockStatement& node) {
    code_->pushScope();
    hoistScope(node.body, false);
    for (const frontend::NodePtr& statement : node.body) {
        if (statement) compileStatement(*statement);
    }
    code_->popScope();
}

void Compiler::compileExpressionStatement(
    const frontend::ExpressionStatement& node) {
    if (!node.expression) return;
    compileExpression(*node.expression);
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
    compileExpression(*node.test);
    uint32_t toElse = code_->jfalse();
    if (node.consequent) compileStatement(*node.consequent);
    if (node.alternate) {
        uint32_t toEnd = code_->jmp();
        code_->patchU32(toElse + 2, code_->here());
        compileStatement(*node.alternate);
        code_->patchU32(toEnd + 2, code_->here());
    } else {
        code_->patchU32(toElse + 2, code_->here());
    }
}

void Compiler::compileWhileStatement(
    const frontend::WhileStatement& node,
    const std::vector<std::string>& labels) {
    if (!node.test) return;

    std::vector<std::string> effective = labels;
    if (effective.empty()) effective.push_back("");
    std::vector<uint32_t> pushes;
    for (const std::string& label : effective) {
        pushes.push_back(code_->pushLabel(label, 0, 0));
    }
    // continue/回跳目标在 push_label 之后，避免重复压帧。
    uint32_t loop = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 6, loop);

    compileExpression(*node.test);
    uint32_t toEnd = code_->jfalse();
    if (node.body) compileStatement(*node.body);
    code_->jmp(loop);

    // 正常退出先执行 pop_label；break 目标在 pop_label 之后。
    uint32_t exitPc = code_->here();
    code_->patchU32(toEnd + 2, exitPc);
    for (auto it = effective.rbegin(); it != effective.rend(); ++it) {
        code_->popLabel(*it);
    }
    uint32_t endPc = code_->here();
    for (uint32_t push : pushes) code_->patchU32(push + 10, endPc);
}

void Compiler::compileDoWhileStatement(
    const frontend::DoWhileStatement& node,
    const std::vector<std::string>& labels) {
    if (!node.test) return;

    std::vector<std::string> effective = labels;
    if (effective.empty()) effective.push_back("");
    std::vector<uint32_t> pushes;
    for (const std::string& label : effective) {
        pushes.push_back(code_->pushLabel(label, 0, 0));
    }
    uint32_t loop = code_->here();

    if (node.body) compileStatement(*node.body);
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
}

void Compiler::compileBreakStatement(
    const frontend::BreakStatement& node) {
    code_->breakTo(node.label);
}

void Compiler::compileContinueStatement(
    const frontend::ContinueStatement& node) {
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

void Compiler::compileTryStatement(const frontend::TryStatement& node) {
    // catch/finally 是 out-of-line 块；出口由 try_body_end/catch_end/finally_end
    // 产生 Interrupt，VM 负责跳转与 try 帧平衡，编译器不发射 jmp。
    uint32_t tryBeginPc = code_->tryBegin();

    if (node.block) compileStatement(*node.block);
    code_->tryBodyEnd();

    uint32_t catchStart = 0;
    if (node.handler) {
        if (node.handler->kind() != NodeKind::CatchClause) {
            error(*node.handler, "compiler: unsupported catch clause");
        } else {
            catchStart = code_->here();
            compileCatchClause(
                static_cast<const frontend::CatchClause&>(*node.handler));
            code_->catchEnd();
        }
    }

    uint32_t finallyStart = 0;
    if (node.finalizer) {
        finallyStart = code_->here();
        compileStatement(*node.finalizer);
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
            code_->define(
                static_cast<const frontend::Identifier&>(*node.param).name);
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
                              false, "default", function.generator);
        code_->initialize(kDefaultSlot);
        exports_.push_back(neo::core::ModuleExportBinding{
            "default", std::string(kDefaultSlot), {}, {}, false});
        return;
    }

    if (declaration.kind() == NodeKind::ClassDeclaration) {
        error(declaration, "compiler: exported class not supported");
        code_->pushUndefined();
        return;
    }

    // export default <expression>：求值后存入隐藏槽位。
    compileExpression(declaration);
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

void Compiler::compileStringLiteral(const frontend::StringLiteral& node) {
    code_->push(node.value);
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
        default:
            error(node, "compiler: unsupported binary operator " +
                            std::to_string(static_cast<int>(node.op)));
            break;
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
                          node.generator);
}

void Compiler::compileArrowFunctionExpression(
    const frontend::ArrowFunctionExpression& node) {
    if (!node.body) {
        error(node, "compiler: arrow function missing body");
        code_->pushUndefined();
        return;
    }
    compileFunctionObject(node, node.params, *node.body, true, {});
}

void Compiler::compileCallExpression(const frontend::CallExpression& node) {
    if (!node.callee) {
        error(node, "compiler: call expression missing callee");
        code_->pushUndefined();
        return;
    }
    bool isMember = node.callee->kind() == NodeKind::MemberExpression;
    if (isMember) {
        const auto& member =
            static_cast<const frontend::MemberExpression&>(*node.callee);
        if (member.optional || node.optional) {
            error(node, "compiler: optional call not supported");
            code_->pushUndefined();
            return;
        }
        if (!member.object) {
            error(node, "compiler: member call missing object");
            code_->pushUndefined();
            return;
        }
        // receiver 只求值一次：栈上保留一份作为 thisArg，另一份用于取方法。
        compileExpression(*member.object);
        code_->dup();
        compileMemberKey(member);
        code_->getProp();
    } else {
        compileExpression(*node.callee);
    }

    uint32_t argc = 0;
    for (const frontend::NodePtr& argument : node.arguments) {
        if (!argument) continue;
        if (argument->kind() == NodeKind::SpreadElement) {
            error(*argument, "compiler: spread arguments not supported");
            continue;
        }
        compileExpression(*argument);
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
    if (node.optional) {
        error(node, "compiler: optional chaining not supported");
        code_->pushUndefined();
        return;
    }
    if (!node.object) {
        error(node, "compiler: member expression missing object");
        code_->pushUndefined();
        return;
    }
    compileExpression(*node.object);
    compileMemberKey(node);
    code_->getProp();
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
        if (property->kind() != NodeKind::Property) {
            error(*property, "compiler: object spread not supported");
            continue;
        }
        const auto& prop = static_cast<const frontend::Property&>(*property);
        if (prop.propertyKind != frontend::PropertyKind::Init) {
            error(*property, "compiler: object accessor not supported");
            continue;
        }
        // __proto__: value 是设置原型而非定义自有属性，语义不同，暂不支持。
        if (!prop.computed && prop.key &&
            prop.key->kind() == NodeKind::Identifier &&
            static_cast<const frontend::Identifier&>(*prop.key).name ==
                "__proto__") {
            error(*property, "compiler: __proto__ property not supported");
            continue;
        }

        // define_prop 消费 obj，栈上保留一份继续累积属性。
        code_->dup();
        compilePropertyKey(prop);
        if (prop.value) {
            compileExpression(*prop.value);
        } else {
            code_->pushUndefined();
        }
        code_->defineProp();
        code_->pop();
    }
}

void Compiler::compileArrayExpression(const frontend::ArrayExpression& node) {
    code_->newArray(static_cast<uint32_t>(node.elements.size()));
    uint32_t index = 0;
    for (const frontend::NodePtr& element : node.elements) {
        if (element && element->kind() == NodeKind::SpreadElement) {
            error(*element, "compiler: array spread not supported");
            ++index;
            continue;
        }
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
        error(node, "compiler: yield* is not supported");
        code_->pushUndefined();
        return;
    }
    if (node.argument) {
        compileExpression(*node.argument);
    } else {
        code_->pushUndefined();
    }
    code_->yieldValue();
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
    if (node.left->kind() == NodeKind::MemberExpression) {
        const auto& member =
            static_cast<const frontend::MemberExpression&>(*node.left);
        if (node.op != frontend::AssignmentOperator::Assign) {
            error(node, "compiler: compound member assignment not supported");
            code_->pushUndefined();
            return;
        }
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
        // set_prop 本身把 RHS 作为表达式值留下。
        compileExpression(*member.object);
        compileMemberKey(member);
        compileExpression(*node.right);
        code_->setProp();
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
        compileExpression(*node.right);
        code_->dup();
        code_->store(name);
        return;
    }

    // 复合赋值：load 旧值 → 右值 → 运算 → dup（表达式值）→ store。
    code_->load(name);
    compileExpression(*node.right);
    switch (node.op) {
        case frontend::AssignmentOperator::Add:      code_->add(); break;
        case frontend::AssignmentOperator::Subtract: code_->sub(); break;
        case frontend::AssignmentOperator::Multiply: code_->mul(); break;
        case frontend::AssignmentOperator::Divide:   code_->div(); break;
        default:
            error(node, "compiler: unsupported assignment operator " +
                            std::to_string(static_cast<int>(node.op)));
            code_->pop();
            code_->pop();
            code_->pushUndefined();
            return;
    }
    code_->dup();
    code_->store(name);
}

void Compiler::compileUpdateExpression(
    const frontend::UpdateExpression& node) {
    if (!node.argument || node.argument->kind() != NodeKind::Identifier) {
        error(node, "compiler: update target must be an identifier");
        code_->pushUndefined();
        return;
    }
    const std::string& name =
        static_cast<const frontend::Identifier&>(*node.argument).name;

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

void Compiler::compileVariableDeclaration(
    const frontend::VariableDeclaration& node) {
    for (const frontend::NodePtr& declarator : node.declarations) {
        if (!declarator || declarator->kind() != NodeKind::VariableDeclarator) {
            continue;
        }
        const auto& decl =
            static_cast<const frontend::VariableDeclarator&>(*declarator);
        if (!decl.id || decl.id->kind() != NodeKind::Identifier) {
            error(*declarator, "compiler: unsupported binding pattern");
            continue;
        }
        const std::string& name =
            static_cast<const frontend::Identifier&>(*decl.id).name;

        if (decl.init) {
            compileExpression(*decl.init);
        } else {
            code_->pushUndefined();
        }

        if (node.variableKind == frontend::VariableKind::Var) {
            // var 槽已在函数/模块入口提升为 undefined，这里只赋值。
            code_->store(name);
        } else {
            // let/const 槽已提升为 uninitialized，这里解除 TDZ。
            code_->initialize(name);
        }
    }
}

void Compiler::compileFunctionObject(const Node& node,
                                     const frontend::NodeList& params,
                                     const Node& body, bool arrow,
                                     std::string_view name, bool generator) {
    // 函数体内联在当前位置，执行路径先跳过它。
    uint32_t skip = code_->jmp();
    uint32_t entryPc = code_->here();
    compileFunctionBody(params, body, arrow);
    uint32_t afterBody = code_->here();
    code_->patchU32(skip + 2, afterBody);

    uint32_t paramCount = 0;
    bool hasRest = false;
    for (const frontend::NodePtr& param : params) {
        if (!param) continue;
        if (param->kind() == NodeKind::Identifier) {
            ++paramCount;
        } else if (param->kind() == NodeKind::RestElement) {
            hasRest = true;
        } else {
            error(*param, "compiler: unsupported parameter pattern");
        }
    }

    uint32_t flags = 0;
    if (hasRest) flags |= vm::kFunctionHasRest;
    if (generator) {
        // 生成器函数不可构造，但有 prototype 属性（其原型链挂 %GeneratorPrototype%）。
        flags |= vm::kFunctionGenerator | vm::kFunctionHasPrototype;
    } else if (!arrow) {
        flags |= vm::kFunctionConstructible | vm::kFunctionHasPrototype;
    }
    code_->pushFunction(entryPc, paramCount, flags);

    if (!name.empty()) code_->setFuncName(name);

    // Sema 的捕获列表（穿透捕获）：逐个从定义点作用域链绑进闭包。
    if (const auto* captures =
            sema_.annotations.get<sema::FunctionCaptures>(node.id())) {
        for (const std::string& capture : captures->names) {
            code_->setClosure(capture);
        }
    }

    // 箭头函数在定义点焊死外层 this。
    if (arrow) {
        code_->loadThis();
        code_->bindThis();
    }
}

void Compiler::compileFunctionBody(const frontend::NodeList& params,
                                   const Node& body, bool arrow) {
    // 形参建槽（argument_scope）：push_undefined; define "p"
    for (const frontend::NodePtr& param : params) {
        if (!param) continue;
        if (param->kind() == NodeKind::Identifier) {
            const auto& id = static_cast<const frontend::Identifier&>(*param);
            code_->pushUndefined();
            code_->define(id.name);
        } else if (param->kind() == NodeKind::RestElement) {
            error(*param, "compiler: rest parameters not supported");
        } else {
            error(*param, "compiler: unsupported parameter pattern");
        }
    }

    // 栈顶是 self，先消费；随后按顺序绑定实参（栈上 argv0 在顶）。
    code_->storeThis();
    for (const frontend::NodePtr& param : params) {
        if (param && param->kind() == NodeKind::Identifier) {
            const auto& id = static_cast<const frontend::Identifier&>(*param);
            code_->store(id.name);
        }
    }

    code_->pushScope();

    if (arrow && body.kind() != NodeKind::BlockStatement) {
        // 表达式体箭头：表达式值即返回值。
        compileExpression(body);
        code_->ret();
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
    return true;
}

void Compiler::hoistScope(const frontend::NodeList& body, bool varScope) {
    hoistSlots(body, varScope);
    hoistFunctions(body);
}

void Compiler::hoistSlots(const frontend::NodeList& body, bool varScope) {
    // 1. var：递归收集（不进入嵌套函数），在函数/模块作用域建 undefined 槽。
    if (varScope) {
        std::vector<std::string> varNames;
        for (const frontend::NodePtr& statement : body) {
            if (statement) collectVarNames(*statement, varNames);
        }
        std::unordered_set<std::string> seen;
        for (const std::string& name : varNames) {
            if (!seen.insert(name).second) continue;
            code_->pushUndefined();
            code_->define(name);
        }
    }

    // 2. 直接 let/const：建 uninitialized 槽（TDZ）。
    for (const frontend::NodePtr& statement : body) {
        const Node* declaration = statement ? declarationOf(*statement)
                                            : nullptr;
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
            if (!decl.id || decl.id->kind() != NodeKind::Identifier) {
                error(*declarator, "compiler: unsupported binding pattern");
                continue;
            }
            const std::string& name =
                static_cast<const frontend::Identifier&>(*decl.id).name;
            code_->pushUninitialized();
            code_->define(name, mutableBinding, false);
        }
    }

    // 3. 直接函数声明：先建 undefined 槽（函数对象在 hoistFunctions 创建）。
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
        code_->pushUndefined();
        code_->define(
            static_cast<const frontend::Identifier&>(*function.id).name);
    }
}

void Compiler::hoistFunctions(const frontend::NodeList& body) {
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
                              false, name, function.generator);
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
                if (decl.id && decl.id->kind() == NodeKind::Identifier) {
                    names.push_back(
                        static_cast<const frontend::Identifier&>(*decl.id).name);
                }
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
