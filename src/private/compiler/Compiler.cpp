// Compiler.cpp - AST + Sema 标注 → 字节码（ADR-061）
//
// 分派函数只做 kind → 具体函数的路由；具体函数只发射本节点的字节码。
// 当前最小链路覆盖 V0/V1 的字面量、表达式语句、return、块、标识符与二元运算。

#include "compiler/Compiler.h"

#include <string>
#include <utility>

namespace neo::compiler {

using frontend::Node;
using frontend::NodeKind;

Compiler::Compiler(const frontend::Program& program,
                   const sema::SemaResult& sema, CompilerOptions options)
    : program_(program), sema_(sema), options_(std::move(options)) {}

CompileResult Compiler::compile() {
    // 前置条件被破坏时不在错误 AST 上继续生成字节码。
    if (!sema_.ok()) {
        errors_.push_back({program_.range(), "compiler: sema reported errors"});
    } else {
        compileProgram();
    }

    CompileResult result;
    result.code = std::move(code_);
    result.errors = std::move(errors_);
    return result;
}

void Compiler::compileProgram() {
    for (const frontend::NodePtr& statement : program_.body) {
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
