// Compiler.cpp - AST + Sema 标注 → 字节码（ADR-061）
//
// 分派函数只做 kind → 具体函数的路由；具体函数只发射本节点的字节码。
// 当前最小链路覆盖 V0/V1 的字面量、表达式语句、return、块、标识符与二元运算。

#include "compiler/Compiler.h"

#include "sema/ResolvePass.h"

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
        case NodeKind::FunctionDeclaration:
            compileFunctionDeclaration(
                static_cast<const frontend::FunctionDeclaration&>(node));
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
        case NodeKind::ThisExpression:
            compileThisExpression(
                static_cast<const frontend::ThisExpression&>(node));
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
    compileFunctionObject(node, node.params, *node.body, false, name);
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
    // 成员调用需要 get_prop（V5）；先显式报错，避免生成错误的 this。
    if (node.callee->kind() == NodeKind::MemberExpression) {
        error(node, "compiler: member call requires get_prop (V5)");
        code_->pushUndefined();
        return;
    }

    compileExpression(*node.callee);
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
    code_->call(argc);
}

void Compiler::compileThisExpression(const frontend::ThisExpression&) {
    code_->loadThis();
}

// —— 函数 ——

void Compiler::compileFunctionDeclaration(
    const frontend::FunctionDeclaration& node) {
    if (!node.id || node.id->kind() != NodeKind::Identifier) {
        error(node, "compiler: function declaration missing name");
        return;
    }
    if (!node.body) {
        error(node, "compiler: function declaration missing body");
        return;
    }
    const std::string& name =
        static_cast<const frontend::Identifier&>(*node.id).name;
    compileFunctionObject(node, node.params, *node.body, false, name);
    code_->define(name);
}

void Compiler::compileFunctionObject(const Node& node,
                                     const frontend::NodeList& params,
                                     const Node& body, bool arrow,
                                     std::string_view name) {
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
    if (!arrow) flags |= vm::kFunctionConstructible | vm::kFunctionHasPrototype;
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
