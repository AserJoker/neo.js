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

#include "core/Module.h"
#include "frontend/Node.h"
#include "sema/Sema.h"
#include "vm/Code.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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

    // 模块编译产物的三段式入口与静态链接元数据（Script 为空/0）。
    uint32_t declarePc = 0;
    uint32_t instantiatePc = 0;
    uint32_t evaluatePc = 0;
    std::vector<neo::core::ModuleImportBinding> imports;
    std::vector<neo::core::ModuleExportBinding> exports;

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
    // 模块三段式：符号定义区 + 函数定义区（instantiate）→ 源码逻辑入口（evaluate）。
    void compileModule();
    void compileStatement(const frontend::Node& node);
    void compileExpression(const frontend::Node& node);

    // —— 语句 ——
    void compileBlockStatement(const frontend::BlockStatement& node);
    void compileExpressionStatement(const frontend::ExpressionStatement& node);
    void compileReturnStatement(const frontend::ReturnStatement& node);
    void compileVariableDeclaration(const frontend::VariableDeclaration& node);
    // 绑定模式：栈顶值写入 pattern（Identifier/Array/Object/Assignment/Rest）。
    // lexical=true 用 initialize（let/const），否则 store（var/参数）。
    void compileBindingPattern(const frontend::Node& pattern, bool lexical,
                               uint32_t depth);
    void compileIfStatement(const frontend::IfStatement& node);
    void compileForStatement(const frontend::ForStatement& node,
                             const std::vector<std::string>& labels = {});
    void compileForInStatement(const frontend::ForInStatement& node,
                               const std::vector<std::string>& labels = {});
    void compileForOfStatement(const frontend::ForOfStatement& node,
                               const std::vector<std::string>& labels = {});
    void compileSwitchStatement(const frontend::SwitchStatement& node,
                                const std::vector<std::string>& labels = {});
    // for-in/of 左侧目标：栈顶值写入目标。member 目标用 tempName 暂存值。
    void compileForTarget(const frontend::NodePtr& left,
                          std::string_view tempName);
    void compileWhileStatement(const frontend::WhileStatement& node,
                               const std::vector<std::string>& labels = {});
    void compileDoWhileStatement(
        const frontend::DoWhileStatement& node,
        const std::vector<std::string>& labels = {});
    void compileLabeledStatement(const frontend::LabeledStatement& node);
    void compileBreakStatement(const frontend::BreakStatement& node);
    void compileContinueStatement(const frontend::ContinueStatement& node);
    void compileThrowStatement(const frontend::ThrowStatement& node);
    void compileWithStatement(const frontend::WithStatement& node);
    void compileTryStatement(const frontend::TryStatement& node);
    void compileCatchClause(const frontend::CatchClause& node);
    void compileImportDeclaration(const frontend::ImportDeclaration& node);
    void compileExportNamedDeclaration(
        const frontend::ExportNamedDeclaration& node);
    void compileExportDefaultDeclaration(
        const frontend::ExportDefaultDeclaration& node);
    void compileExportAllDeclaration(
        const frontend::ExportAllDeclaration& node);

    // —— 表达式 ——
    void compileIdentifier(const frontend::Identifier& node);
    void compileNumericLiteral(const frontend::NumericLiteral& node);
    void compileBigIntLiteral(const frontend::BigIntLiteral& node);
    void compileStringLiteral(const frontend::StringLiteral& node);
    void compileBooleanLiteral(const frontend::BooleanLiteral& node);
    void compileNullLiteral(const frontend::NullLiteral& node);
    void compileBinaryExpression(const frontend::BinaryExpression& node);
    void compileUnaryExpression(const frontend::UnaryExpression& node);
    // delete 运算符：成员删除返回 deleteProperty 结果；标识符/非引用按规范返回布尔。
    void compileDeleteExpression(const frontend::Node& argument);
    void compileLogicalExpression(const frontend::LogicalExpression& node);
    void compileConditionalExpression(
        const frontend::ConditionalExpression& node);
    void compileSequenceExpression(const frontend::SequenceExpression& node);
    void compileFunctionExpression(const frontend::FunctionExpression& node);
    void compileArrowFunctionExpression(
        const frontend::ArrowFunctionExpression& node);
    void compileCallExpression(const frontend::CallExpression& node);
    void compileNewExpression(const frontend::NewExpression& node);
    void compileMetaProperty(const frontend::MetaProperty& node);
    void compileImportExpression(const frontend::ImportExpression& node);
    void compileTemplateLiteral(const frontend::TemplateLiteral& node);
    void compileTaggedTemplateExpression(
        const frontend::TaggedTemplateExpression& node);
    void compileMemberExpression(const frontend::MemberExpression& node);
    // 可选链：整条链共用短路出口，任一 `?.` 命中 nullish 即跳到链尾。
    void compileChainExpression(const frontend::ChainExpression& node);
    // 栈顶为待检查值；nullish 时弹掉它（再弹 extraPops 个）并压 undefined，
    // 跳到当前链尾。非 nullish 继续执行。
    void emitOptionalGuard(uint32_t extraPops = 0);
    // 把成员访问的 key 压栈（computed 求值，否则标识符名）。
    void compileMemberKey(const frontend::MemberExpression& node);
    void compileObjectExpression(const frontend::ObjectExpression& node);
    // 把实参列表编译成数组（含展开），结果数组留在栈上。
    void compileArgumentsArray(const frontend::NodeList& arguments);
    void compileArrayExpression(const frontend::ArrayExpression& node);
    // 把对象字面量属性的 key 压栈。
    void compilePropertyKey(const frontend::Property& node);
    void compileThisExpression(const frontend::ThisExpression& node);
    void compileYieldExpression(const frontend::YieldExpression& node);
    void compileAwaitExpression(const frontend::AwaitExpression& node);
    void compileAssignmentExpression(
        const frontend::AssignmentExpression& node);
    void compileUpdateExpression(const frontend::UpdateExpression& node);

    // —— 函数体 ——
    // 发射函数值：跳过函数体 → 函数体 → push_function → 名称/捕获/this 绑定。
    // 结果函数值留在操作数栈上。
    void compileFunctionObject(const frontend::Node& node,
                               const frontend::NodeList& params,
                               const frontend::Node& body, bool arrow,
                               std::string_view name, bool generator = false,
                               bool async = false, bool method = false);
    // 函数序言 + 函数体 + 隐式 return undefined。
    // initInstanceElements：class 基类构造器在序言后初始化实例字段。
    void compileFunctionBody(const frontend::NodeList& params,
                             const frontend::Node& body, bool arrow,
                             bool initInstanceElements = false);

    // —— class ——
    // leaveValue=true 时保留构造器值（匿名 export default class 用）。
    void compileClassDeclaration(const frontend::ClassDeclaration& node,
                                 bool leaveValue = false);
    void compileClassExpression(const frontend::ClassExpression& node);
    // 编译 class 构造器（含默认构造器）并留下构造器值。
    void compileClassConstructor(const frontend::ClassBody& body,
                                 bool isDerived, std::string_view name);
    // 把 class 元素（方法/字段/static block）逐个定义到构造器上。
    void compileClassElements(const frontend::ClassBody& body);
    // 字段初始化器：无参函数，返回表达式值（this 为实例/构造器）。
    // owner 为 PropertyDefinition 的节点 id（Sema 捕获标注挂在该节点）。
    void compileFieldInitializer(const frontend::Node& value,
                                 frontend::NodeId owner);
    // 访问器/方法函数：method=true（不可构造、无 prototype、严格）。
    void compileMethodFunction(const frontend::FunctionExpression& function,
                               std::string_view name);
    // 把 class 元素的 key 压栈（computed 求值，否则标识符名）。
    void compileClassKey(const frontend::Node& key, bool computed);

    // —— class 私有元素 ——
    // 私有名在 class 内的种类（同一名字可能是 getter+setter）。
    struct PrivateInfo {
        bool field = false;
        bool method = false;
        bool getter = false;
        bool setter = false;
    };
    // 收集 class body 内的私有声明（不含 '#'）。
    std::unordered_map<std::string, PrivateInfo> collectPrivateElements(
        const frontend::ClassBody& body) const;
    // 从最内层 class 向外查找私有名；未声明返回 nullptr。
    const PrivateInfo* findPrivateInfo(std::string_view name) const;
    void compilePrivateRead(const frontend::MemberExpression& member);
    void compilePrivateWrite(const frontend::MemberExpression& member,
                             const frontend::AssignmentExpression& node);
    void compilePrivateUpdate(const frontend::MemberExpression& member,
                              const frontend::UpdateExpression& node);
    void compilePrivateCall(const frontend::MemberExpression& member,
                            const frontend::CallExpression& node);

    // —— 声明提升 ——
    // 在当前作用域入口发射提升槽位：
    //   varScope=true：递归收集 var（不进入嵌套函数）→ undefined 槽；
    //   直接 let/const → uninitialized 槽（TDZ）；
    //   直接函数声明 → 先建 undefined 槽，再创建函数并 store（支持互相引用）。
    void hoistScope(const frontend::NodeList& body, bool varScope);
    // 模块三段式拆分：建槽（var/let/const/函数占位）与函数对象创建分开发射。
    void hoistSlots(const frontend::NodeList& body, bool varScope);
    void hoistFunctions(const frontend::NodeList& body);
    void collectVarNames(const frontend::Node& statement,
                         std::vector<std::string>& names);

    // 取 export 声明内层声明（无则 nullptr）。
    static const frontend::Node* declarationOf(const frontend::Node& node);
    // export default 是否使用隐藏槽位（匿名函数/类或表达式）。
    static bool usesDefaultSlot(const frontend::ExportDefaultDeclaration& node);
    static constexpr std::string_view kDefaultSlot = "*default*";

    // —— 辅助 ——
    // 复合赋值运算符 → 二元指令；不支持时记录错误并压 undefined。
    bool emitCompoundBinary(frontend::AssignmentOperator op,
                            const frontend::Node& node);
    void error(const frontend::Node& node, std::string message);
    // 把 [begin, code_.here()) 映射到 node 的源码起始位置。
    void mapNode(const frontend::Node& node, uint32_t begin);

    const frontend::Program& program_;
    const sema::SemaResult& sema_;
    CompilerOptions options_;
    std::unique_ptr<vm::Code> code_ = std::make_unique<vm::Code>();
    std::vector<CompileError> errors_;
    // 模块三段式入口与链接元数据（Script 保持 0/空）。
    uint32_t declarePc_ = 0;
    uint32_t instantiatePc_ = 0;
    uint32_t evaluatePc_ = 0;
    std::vector<neo::core::ModuleImportBinding> imports_;
    std::vector<neo::core::ModuleExportBinding> exports_;
    // 每层 ChainExpression 一个待回填的短路跳转列表（支持嵌套链）。
    std::vector<std::vector<uint32_t>> chainEndPatches_;
    // 每层 class 的私有名表（支持嵌套 class 的词法解析）。
    std::vector<std::unordered_map<std::string, PrivateInfo>> privateScopes_;
    // delete 可选链：短路出口压 true，最外层成员改为 delete_prop。
    bool deleteChain_ = false;
    bool deleteChainTarget_ = false;
    void compileDeleteChain(const frontend::ChainExpression& node);
};

}  // namespace neo::compiler

#endif  // NEO_COMPILER_COMPILER_H
