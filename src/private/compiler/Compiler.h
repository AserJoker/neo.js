// compiler/Compiler.h - AST + Sema 标注 → 字节码
//
// 结构对齐 Parser：一个节点一个 compile 函数，分派函数只做 kind → 具体
// compile 函数的路由，不发射指令；具体函数只负责该节点的字节码与子节点。
//
// 输入：frontend::Program（AST）+ sema::SemaResult（AnnotationStore 标注）。
// 输出：vm::Code（扁平字节码 + 常量表 + SourceMap）与编译错误列表。
//
// 当前只打通 V0/V1 的最小纵向链路（字面量、表达式语句、return、块、二元
// 运算、标识符读写）。未支持的节点记录 CompileError，不静默生成错误字节码。
//
// 关联 ADR: 024（字节码指令集）、055（Parser 与 AST）、058（Sema 标注）、
//          060（VM 框架）、061（Compiler 框架）

#ifndef NEO_COMPILER_COMPILER_H
#define NEO_COMPILER_COMPILER_H

#include "frontend/Node.h"
#include "sema/Sema.h"
#include "vm/Code.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace neo::compiler {

struct CompilerOptions {
    // SourceMap 中记录的文件名；无文件名时用 "<anonymous>"。
    std::string filename = "<anonymous>";
};

struct CompileError {
    frontend::SourceRange range;
    std::string message;
};

struct CompileResult {
    // Code 含扁平字节码/常量表等大数组，按值传递会整块复制。
    // 由 Module 唯一持有；VM / script Function 只借用 const Code*。
    std::unique_ptr<vm::Code> code;
    std::vector<CompileError> errors;

    bool ok() const { return code != nullptr && errors.empty(); }
};

class Compiler {
public:
    // 前置条件：sema.ok()。标注通过 sema.annotations 读取。
    Compiler(const frontend::Program& program, const sema::SemaResult& sema,
             CompilerOptions options = {});

    CompileResult compile();

private:
    // —— 分派 ——
    void compileProgram();
    void compileStatement(const frontend::Node& node);
    void compileExpression(const frontend::Node& node);

    // —— 语句 ——
    void compileBlockStatement(const frontend::BlockStatement& node);
    void compileExpressionStatement(const frontend::ExpressionStatement& node);
    void compileReturnStatement(const frontend::ReturnStatement& node);
    void compileVariableDeclaration(const frontend::VariableDeclaration& node);
    void compileIfStatement(const frontend::IfStatement& node);
    void compileWhileStatement(const frontend::WhileStatement& node,
                               const std::vector<std::string>& labels = {});
    void compileDoWhileStatement(
        const frontend::DoWhileStatement& node,
        const std::vector<std::string>& labels = {});
    void compileLabeledStatement(const frontend::LabeledStatement& node);
    void compileBreakStatement(const frontend::BreakStatement& node);
    void compileContinueStatement(const frontend::ContinueStatement& node);
    void compileThrowStatement(const frontend::ThrowStatement& node);
    void compileTryStatement(const frontend::TryStatement& node);
    void compileCatchClause(const frontend::CatchClause& node);

    // —— 表达式 ——
    void compileIdentifier(const frontend::Identifier& node);
    void compileNumericLiteral(const frontend::NumericLiteral& node);
    void compileStringLiteral(const frontend::StringLiteral& node);
    void compileBooleanLiteral(const frontend::BooleanLiteral& node);
    void compileNullLiteral(const frontend::NullLiteral& node);
    void compileBinaryExpression(const frontend::BinaryExpression& node);
    void compileFunctionExpression(const frontend::FunctionExpression& node);
    void compileArrowFunctionExpression(
        const frontend::ArrowFunctionExpression& node);
    void compileCallExpression(const frontend::CallExpression& node);
    void compileMemberExpression(const frontend::MemberExpression& node);
    // 把成员访问的 key 压栈（computed 求值，否则标识符名）。
    void compileMemberKey(const frontend::MemberExpression& node);
    void compileObjectExpression(const frontend::ObjectExpression& node);
    void compileArrayExpression(const frontend::ArrayExpression& node);
    // 把对象字面量属性的 key 压栈。
    void compilePropertyKey(const frontend::Property& node);
    void compileThisExpression(const frontend::ThisExpression& node);
    void compileAssignmentExpression(
        const frontend::AssignmentExpression& node);
    void compileUpdateExpression(const frontend::UpdateExpression& node);

    // —— 函数体 ——
    // 发射函数值：跳过函数体 → 函数体 → push_function → 名称/捕获/this 绑定。
    // 结果函数值留在操作数栈上。
    void compileFunctionObject(const frontend::Node& node,
                               const frontend::NodeList& params,
                               const frontend::Node& body, bool arrow,
                               std::string_view name);
    // 函数序言 + 函数体 + 隐式 return undefined。
    void compileFunctionBody(const frontend::NodeList& params,
                             const frontend::Node& body, bool arrow);

    // —— 声明提升 ——
    // 在当前作用域入口发射提升槽位：
    //   varScope=true：递归收集 var（不进入嵌套函数）→ undefined 槽；
    //   直接 let/const → uninitialized 槽（TDZ）；
    //   直接函数声明 → 先建 undefined 槽，再创建函数并 store（支持互相引用）。
    void hoistScope(const frontend::NodeList& body, bool varScope);
    void collectVarNames(const frontend::Node& statement,
                         std::vector<std::string>& names);

    // —— 辅助 ——
    void error(const frontend::Node& node, std::string message);
    // 把 [begin, code_.here()) 映射到 node 的源码起始位置。
    void mapNode(const frontend::Node& node, uint32_t begin);

    const frontend::Program& program_;
    const sema::SemaResult& sema_;
    CompilerOptions options_;
    std::unique_ptr<vm::Code> code_ = std::make_unique<vm::Code>();
    std::vector<CompileError> errors_;
};

}  // namespace neo::compiler

#endif  // NEO_COMPILER_COMPILER_H
