// frontend/Node.h - AST 节点基类
//
// Parser 产出、Sema 标注、Compiler 消费的语法树。当前只落地基类与最小节点集：
// Program、空语句、表达式语句、标识符与各类字面量。后续按语法族逐个加入。
//
// 约定：**一个节点一个 parse 函数**（如 parseNumericLiteral / parseStringLiteral），
// 解析函数只负责该节点的语法与构造，不承担上层分派。
//
// 所有权：父节点通过 std::unique_ptr 独占子节点；NodeList 中允许空指针，
// 供后续数组字面量 elision 等场景使用。
//
// 向下转型以 kind() 为判别依据：
//   if (node->kind() == NodeKind::Identifier) {
//       auto* id = static_cast<const Identifier*>(node.get());
//   }
//
// 关联 ADR: 001（前端架构）、025（Sema 两遍 walk）、055（Parser 与 AST）

#ifndef NEO_FRONTEND_NODE_H
#define NEO_FRONTEND_NODE_H

#include "frontend/CharStream.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace neo::frontend {

struct SourceRange {
    SourceLocation start;
    SourceLocation end;
};

// 节点种类随实现逐步扩充；不要一次枚举完整 ECMAScript 语法。
enum class NodeKind : uint8_t {
    Program,

    // 语句
    BlockStatement,
    EmptyStatement,
    ExpressionStatement,
    Directive,
    VariableDeclaration,
    VariableDeclarator,
    IfStatement,
    WhileStatement,
    DoWhileStatement,
    ForStatement,
    ForInStatement,
    ForOfStatement,
    ReturnStatement,
    ThrowStatement,
    BreakStatement,
    ContinueStatement,
    TryStatement,
    CatchClause,
    SwitchStatement,
    SwitchCase,
    LabeledStatement,
    WithStatement,
    DebuggerStatement,
    FunctionDeclaration,
    ClassDeclaration,
    ClassBody,
    MethodDefinition,
    PropertyDefinition,
    StaticBlock,

    // 模块
    ImportSpecifier,
    ImportDefaultSpecifier,
    ImportNamespaceSpecifier,
    ImportDeclaration,
    ExportSpecifier,
    ExportAllDeclaration,
    ExportNamedDeclaration,
    ExportDefaultDeclaration,

    // 表达式
    Identifier,
    NumericLiteral,
    BigIntLiteral,
    StringLiteral,
    BooleanLiteral,
    NullLiteral,
    RegExpLiteral,
    TemplateElement,
    TemplateLiteral,
    TaggedTemplateExpression,
    ThisExpression,
    SuperExpression,
    FunctionExpression,
    ClassExpression,
    PrivateIdentifier,

    MemberExpression,
    CallExpression,
    ChainExpression,
    NewExpression,
    MetaProperty,
    ImportExpression,
    AwaitExpression,
    YieldExpression,
    UnaryExpression,
    UpdateExpression,
    BinaryExpression,
    LogicalExpression,
    ConditionalExpression,
    AssignmentExpression,
    SequenceExpression,
    ArrowFunctionExpression,
    ArrayExpression,
    ObjectExpression,
    Property,
    SpreadElement,

    // 模式
    ObjectPattern,
    ArrayPattern,
    AssignmentPattern,
    RestElement,
};

class Node {
public:
    virtual ~Node() = default;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    NodeKind kind() const { return kind_; }
    SourceRange range() const { return range_; }
    void setRange(SourceRange range) { range_ = range; }

    // 是否由括号包裹；用于 ?? 与 ||/&& 的混用限制等语法判断。
    bool isParenthesized() const { return parenthesized_; }
    void setParenthesized(bool value) { parenthesized_ = value; }

protected:
    Node(NodeKind kind, SourceRange range) : kind_(kind), range_(range) {}

private:
    NodeKind kind_;
    SourceRange range_;
    bool parenthesized_ = false;
};

using NodePtr = std::unique_ptr<Node>;
using NodeList = std::vector<NodePtr>;

// ---------------------------------------------------------------------------
// Program
// ---------------------------------------------------------------------------

struct Program final : Node {
    Program(SourceRange range, NodeList body)
        : Node(NodeKind::Program, range), body(std::move(body)) {}
    NodeList body;
};

// ---------------------------------------------------------------------------
// 语句
// ---------------------------------------------------------------------------

struct BlockStatement final : Node {
    BlockStatement(SourceRange range, NodeList body)
        : Node(NodeKind::BlockStatement, range), body(std::move(body)) {}
    NodeList body;
};

struct EmptyStatement final : Node {
    explicit EmptyStatement(SourceRange range)
        : Node(NodeKind::EmptyStatement, range) {}
};

struct ExpressionStatement final : Node {
    ExpressionStatement(SourceRange range, NodePtr expression)
        : Node(NodeKind::ExpressionStatement, range),
          expression(std::move(expression)) {}
    NodePtr expression;
};

// 指令序言中的字符串字面量语句（如 "use strict"）。
// value 是 cooked 值，raw 是含引号的源码切片；严格模式判定用 raw。
struct Directive final : Node {
    Directive(SourceRange range, std::string value, std::string raw)
        : Node(NodeKind::Directive, range),
          value(std::move(value)),
          raw(std::move(raw)) {}
    std::string value;
    std::string raw;

    bool isUseStrict() const {
        return raw == "\"use strict\"" || raw == "'use strict'";
    }
};

enum class VariableKind : uint8_t { Var, Let, Const };

struct VariableDeclarator final : Node {
    VariableDeclarator(SourceRange range, NodePtr id, NodePtr init)
        : Node(NodeKind::VariableDeclarator, range),
          id(std::move(id)),
          init(std::move(init)) {}
    NodePtr id;
    NodePtr init;  // 可空
};

struct VariableDeclaration final : Node {
    VariableDeclaration(SourceRange range, VariableKind variableKind,
                        NodeList declarations)
        : Node(NodeKind::VariableDeclaration, range),
          variableKind(variableKind),
          declarations(std::move(declarations)) {}
    VariableKind variableKind;
    NodeList declarations;
};

struct IfStatement final : Node {
    IfStatement(SourceRange range, NodePtr test, NodePtr consequent,
                NodePtr alternate)
        : Node(NodeKind::IfStatement, range),
          test(std::move(test)),
          consequent(std::move(consequent)),
          alternate(std::move(alternate)) {}
    NodePtr test;
    NodePtr consequent;
    NodePtr alternate;  // 可空
};

struct WhileStatement final : Node {
    WhileStatement(SourceRange range, NodePtr test, NodePtr body)
        : Node(NodeKind::WhileStatement, range),
          test(std::move(test)),
          body(std::move(body)) {}
    NodePtr test;
    NodePtr body;
};

struct DoWhileStatement final : Node {
    DoWhileStatement(SourceRange range, NodePtr body, NodePtr test)
        : Node(NodeKind::DoWhileStatement, range),
          body(std::move(body)),
          test(std::move(test)) {}
    NodePtr body;
    NodePtr test;
};

struct ForStatement final : Node {
    ForStatement(SourceRange range, NodePtr init, NodePtr test, NodePtr update,
                 NodePtr body)
        : Node(NodeKind::ForStatement, range),
          init(std::move(init)),
          test(std::move(test)),
          update(std::move(update)),
          body(std::move(body)) {}
    NodePtr init;  // 可空；VariableDeclaration 或 Expression
    NodePtr test;
    NodePtr update;
    NodePtr body;
};

struct ForInStatement final : Node {
    ForInStatement(SourceRange range, NodePtr left, NodePtr right, NodePtr body)
        : Node(NodeKind::ForInStatement, range),
          left(std::move(left)),
          right(std::move(right)),
          body(std::move(body)) {}
    NodePtr left;  // VariableDeclaration 或赋值目标
    NodePtr right;
    NodePtr body;
};

struct ForOfStatement final : Node {
    ForOfStatement(SourceRange range, NodePtr left, NodePtr right, NodePtr body)
        : Node(NodeKind::ForOfStatement, range),
          left(std::move(left)),
          right(std::move(right)),
          body(std::move(body)) {}
    NodePtr left;
    NodePtr right;
    NodePtr body;
};

struct ReturnStatement final : Node {
    ReturnStatement(SourceRange range, NodePtr argument)
        : Node(NodeKind::ReturnStatement, range),
          argument(std::move(argument)) {}
    NodePtr argument;  // 可空
};

struct ThrowStatement final : Node {
    ThrowStatement(SourceRange range, NodePtr argument)
        : Node(NodeKind::ThrowStatement, range),
          argument(std::move(argument)) {}
    NodePtr argument;
};

struct BreakStatement final : Node {
    BreakStatement(SourceRange range, std::string label)
        : Node(NodeKind::BreakStatement, range), label(std::move(label)) {}
    std::string label;  // 空串表示无标签
};

struct ContinueStatement final : Node {
    ContinueStatement(SourceRange range, std::string label)
        : Node(NodeKind::ContinueStatement, range), label(std::move(label)) {}
    std::string label;
};

struct CatchClause final : Node {
    CatchClause(SourceRange range, NodePtr param, NodePtr body)
        : Node(NodeKind::CatchClause, range),
          param(std::move(param)),
          body(std::move(body)) {}
    NodePtr param;  // 可空（catch { }）
    NodePtr body;
};

struct TryStatement final : Node {
    TryStatement(SourceRange range, NodePtr block, NodePtr handler,
                 NodePtr finalizer)
        : Node(NodeKind::TryStatement, range),
          block(std::move(block)),
          handler(std::move(handler)),
          finalizer(std::move(finalizer)) {}
    NodePtr block;
    NodePtr handler;    // CatchClause，可空
    NodePtr finalizer;  // BlockStatement，可空
};

struct SwitchCase final : Node {
    SwitchCase(SourceRange range, NodePtr test, NodeList consequent)
        : Node(NodeKind::SwitchCase, range),
          test(std::move(test)),
          consequent(std::move(consequent)) {}
    NodePtr test;  // nullptr 表示 default
    NodeList consequent;
};

struct SwitchStatement final : Node {
    SwitchStatement(SourceRange range, NodePtr discriminant, NodeList cases)
        : Node(NodeKind::SwitchStatement, range),
          discriminant(std::move(discriminant)),
          cases(std::move(cases)) {}
    NodePtr discriminant;
    NodeList cases;
};

struct LabeledStatement final : Node {
    LabeledStatement(SourceRange range, std::string label, NodePtr body)
        : Node(NodeKind::LabeledStatement, range),
          label(std::move(label)),
          body(std::move(body)) {}
    std::string label;
    NodePtr body;
};

struct WithStatement final : Node {
    WithStatement(SourceRange range, NodePtr object, NodePtr body)
        : Node(NodeKind::WithStatement, range),
          object(std::move(object)),
          body(std::move(body)) {}
    NodePtr object;
    NodePtr body;
};

struct DebuggerStatement final : Node {
    explicit DebuggerStatement(SourceRange range)
        : Node(NodeKind::DebuggerStatement, range) {}
};

// function [*] name(params) { body }
// id 可空：名字是否必需由 Sema 判定（普通声明必需，export default 可匿名）。
struct FunctionDeclaration final : Node {
    FunctionDeclaration(SourceRange range, NodePtr id, NodeList params,
                        NodePtr body, bool async, bool generator)
        : Node(NodeKind::FunctionDeclaration, range),
          id(std::move(id)),
          params(std::move(params)),
          body(std::move(body)),
          async(async),
          generator(generator) {}
    NodePtr id;
    NodeList params;
    NodePtr body;  // BlockStatement
    bool async;
    bool generator;
};

enum class MethodKind : uint8_t { Method, Get, Set, Constructor };

// key(params) { body } / get key() {} / set key(v) {} / constructor(...) {}
struct MethodDefinition final : Node {
    MethodDefinition(SourceRange range, NodePtr key, NodePtr value,
                     bool computed, bool isStatic, MethodKind methodKind)
        : Node(NodeKind::MethodDefinition, range),
          key(std::move(key)),
          value(std::move(value)),
          computed(computed),
          isStatic(isStatic),
          methodKind(methodKind) {}
    NodePtr key;
    NodePtr value;  // FunctionExpression
    bool computed;
    bool isStatic;
    MethodKind methodKind;
};

// key; / key = value;
struct PropertyDefinition final : Node {
    PropertyDefinition(SourceRange range, NodePtr key, NodePtr value,
                       bool computed, bool isStatic)
        : Node(NodeKind::PropertyDefinition, range),
          key(std::move(key)),
          value(std::move(value)),
          computed(computed),
          isStatic(isStatic) {}
    NodePtr key;
    NodePtr value;  // 可空
    bool computed;
    bool isStatic;
};

// static { ... }
struct StaticBlock final : Node {
    StaticBlock(SourceRange range, NodeList body)
        : Node(NodeKind::StaticBlock, range), body(std::move(body)) {}
    NodeList body;
};

struct ClassBody final : Node {
    ClassBody(SourceRange range, NodeList elements)
        : Node(NodeKind::ClassBody, range), elements(std::move(elements)) {}
    NodeList elements;  // MethodDefinition | PropertyDefinition
};

// class [id] [extends superClass] { body }
// id 可空（export default / 类表达式），名字是否必需由 Sema 判定。
struct ClassDeclaration final : Node {
    ClassDeclaration(SourceRange range, NodePtr id, NodePtr superClass,
                     NodePtr body)
        : Node(NodeKind::ClassDeclaration, range),
          id(std::move(id)),
          superClass(std::move(superClass)),
          body(std::move(body)) {}
    NodePtr id;
    NodePtr superClass;  // 可空
    NodePtr body;        // ClassBody
};

// ---------------------------------------------------------------------------
// 模块
// ---------------------------------------------------------------------------

// import { imported as local }
struct ImportSpecifier final : Node {
    ImportSpecifier(SourceRange range, NodePtr imported, NodePtr local)
        : Node(NodeKind::ImportSpecifier, range),
          imported(std::move(imported)),
          local(std::move(local)) {}
    NodePtr imported;  // IdentifierName
    NodePtr local;     // Identifier
};

// import local from "..."
struct ImportDefaultSpecifier final : Node {
    ImportDefaultSpecifier(SourceRange range, NodePtr local)
        : Node(NodeKind::ImportDefaultSpecifier, range),
          local(std::move(local)) {}
    NodePtr local;  // Identifier
};

// import * as local from "..."
struct ImportNamespaceSpecifier final : Node {
    ImportNamespaceSpecifier(SourceRange range, NodePtr local)
        : Node(NodeKind::ImportNamespaceSpecifier, range),
          local(std::move(local)) {}
    NodePtr local;  // Identifier
};

// import ... from "source" / import "source"
struct ImportDeclaration final : Node {
    ImportDeclaration(SourceRange range, NodeList specifiers, NodePtr source)
        : Node(NodeKind::ImportDeclaration, range),
          specifiers(std::move(specifiers)),
          source(std::move(source)) {}
    NodeList specifiers;  // ImportSpecifier / ImportDefaultSpecifier /
                          // ImportNamespaceSpecifier
    NodePtr source;       // StringLiteral
};

// export { local as exported }
struct ExportSpecifier final : Node {
    ExportSpecifier(SourceRange range, NodePtr local, NodePtr exported)
        : Node(NodeKind::ExportSpecifier, range),
          local(std::move(local)),
          exported(std::move(exported)) {}
    NodePtr local;     // IdentifierName
    NodePtr exported;  // IdentifierName
};

// export * from "source"
struct ExportAllDeclaration final : Node {
    ExportAllDeclaration(SourceRange range, NodePtr source)
        : Node(NodeKind::ExportAllDeclaration, range),
          source(std::move(source)) {}
    NodePtr source;  // StringLiteral
};

// export function f() { }
struct ExportNamedDeclaration final : Node {
    ExportNamedDeclaration(SourceRange range, NodePtr declaration)
        : Node(NodeKind::ExportNamedDeclaration, range),
          declaration(std::move(declaration)) {}
    NodePtr declaration;
};

// export default function [name]() { }
struct ExportDefaultDeclaration final : Node {
    ExportDefaultDeclaration(SourceRange range, NodePtr declaration)
        : Node(NodeKind::ExportDefaultDeclaration, range),
          declaration(std::move(declaration)) {}
    NodePtr declaration;
};

// ---------------------------------------------------------------------------
// 表达式
// ---------------------------------------------------------------------------

struct Identifier final : Node {
    Identifier(SourceRange range, std::string name)
        : Node(NodeKind::Identifier, range), name(std::move(name)) {}
    std::string name;
};

struct NumericLiteral final : Node {
    NumericLiteral(SourceRange range, double value,
                   bool legacyLeadingZero = false)
        : Node(NodeKind::NumericLiteral, range),
          value(value),
          legacyLeadingZero(legacyLeadingZero) {}
    double value;
    // 010 / 08 / 09：严格模式下是早期错误，由 Sema 判定。
    bool legacyLeadingZero;
};

struct BigIntLiteral final : Node {
    BigIntLiteral(SourceRange range, std::string digits, int radix)
        : Node(NodeKind::BigIntLiteral, range),
          digits(std::move(digits)),
          radix(radix) {}
    std::string digits;
    int radix;
};

struct StringLiteral final : Node {
    StringLiteral(SourceRange range, std::string value,
                  bool legacyOctalEscape = false,
                  bool legacyEightOrNine = false)
        : Node(NodeKind::StringLiteral, range),
          value(std::move(value)),
          legacyOctalEscape(legacyOctalEscape),
          legacyEightOrNine(legacyEightOrNine) {}
    std::string value;
    // 严格模式下非法的转义形态，由 Sema 判定。
    bool legacyOctalEscape;
    bool legacyEightOrNine;
};

struct BooleanLiteral final : Node {
    BooleanLiteral(SourceRange range, bool value)
        : Node(NodeKind::BooleanLiteral, range), value(value) {}
    bool value;
};

struct NullLiteral final : Node {
    explicit NullLiteral(SourceRange range)
        : Node(NodeKind::NullLiteral, range) {}
};

struct RegExpLiteral final : Node {
    RegExpLiteral(SourceRange range, std::string pattern, std::string flags)
        : Node(NodeKind::RegExpLiteral, range),
          pattern(std::move(pattern)),
          flags(std::move(flags)) {}
    std::string pattern;
    std::string flags;
};

// 模板字面量的一段 cooked 文本（quasi）。
struct TemplateElement final : Node {
    TemplateElement(SourceRange range, std::string cooked)
        : Node(NodeKind::TemplateElement, range),
          cooked(std::move(cooked)) {}
    std::string cooked;
};

// `a${b}c`：quasis 数量恒为 expressions.size() + 1。
struct TemplateLiteral final : Node {
    TemplateLiteral(SourceRange range, NodeList quasis, NodeList expressions)
        : Node(NodeKind::TemplateLiteral, range),
          quasis(std::move(quasis)),
          expressions(std::move(expressions)) {}
    NodeList quasis;       // TemplateElement
    NodeList expressions;
};

// tag`a${b}c`
struct TaggedTemplateExpression final : Node {
    TaggedTemplateExpression(SourceRange range, NodePtr tag, NodePtr quasi)
        : Node(NodeKind::TaggedTemplateExpression, range),
          tag(std::move(tag)),
          quasi(std::move(quasi)) {}
    NodePtr tag;
    NodePtr quasi;  // TemplateLiteral
};

struct ThisExpression final : Node {
    explicit ThisExpression(SourceRange range)
        : Node(NodeKind::ThisExpression, range) {}
};

struct SuperExpression final : Node {
    explicit SuperExpression(SourceRange range)
        : Node(NodeKind::SuperExpression, range) {}
};

// #name（私有字段/方法名，name 不含 '#'）
struct PrivateIdentifier final : Node {
    PrivateIdentifier(SourceRange range, std::string name)
        : Node(NodeKind::PrivateIdentifier, range), name(std::move(name)) {}
    std::string name;
};

// function [*] [name](params) { body }
// id 可空（匿名函数表达式）。
struct FunctionExpression final : Node {
    FunctionExpression(SourceRange range, NodePtr id, NodeList params,
                       NodePtr body, bool async, bool generator)
        : Node(NodeKind::FunctionExpression, range),
          id(std::move(id)),
          params(std::move(params)),
          body(std::move(body)),
          async(async),
          generator(generator) {}
    NodePtr id;
    NodeList params;
    NodePtr body;  // BlockStatement
    bool async;
    bool generator;
};

// class [id] [extends superClass] { body }
struct ClassExpression final : Node {
    ClassExpression(SourceRange range, NodePtr id, NodePtr superClass,
                    NodePtr body)
        : Node(NodeKind::ClassExpression, range),
          id(std::move(id)),
          superClass(std::move(superClass)),
          body(std::move(body)) {}
    NodePtr id;
    NodePtr superClass;  // 可空
    NodePtr body;        // ClassBody
};

// obj.property / obj[expression]
struct MemberExpression final : Node {
    MemberExpression(SourceRange range, NodePtr object, NodePtr property,
                     bool computed, bool optional = false)
        : Node(NodeKind::MemberExpression, range),
          object(std::move(object)),
          property(std::move(property)),
          computed(computed),
          optional(optional) {}
    NodePtr object;
    NodePtr property;
    bool computed;
    bool optional;  // obj?.prop / obj?.[expr]
};

// callee(arguments)
struct CallExpression final : Node {
    CallExpression(SourceRange range, NodePtr callee, NodeList arguments,
                   bool optional = false)
        : Node(NodeKind::CallExpression, range),
          callee(std::move(callee)),
          arguments(std::move(arguments)),
          optional(optional) {}
    NodePtr callee;
    NodeList arguments;
    bool optional;  // fn?.(args)
};

// a?.b.c：包住整条可选链，短路语义作用于整个表达式。
struct ChainExpression final : Node {
    ChainExpression(SourceRange range, NodePtr expression)
        : Node(NodeKind::ChainExpression, range),
          expression(std::move(expression)) {}
    NodePtr expression;
};

// new.target / import.meta
struct MetaProperty final : Node {
    MetaProperty(SourceRange range, NodePtr meta, NodePtr property)
        : Node(NodeKind::MetaProperty, range),
          meta(std::move(meta)),
          property(std::move(property)) {}
    NodePtr meta;
    NodePtr property;
};

// await argument（仅 async 上下文）
struct AwaitExpression final : Node {
    AwaitExpression(SourceRange range, NodePtr argument)
        : Node(NodeKind::AwaitExpression, range),
          argument(std::move(argument)) {}
    NodePtr argument;
};

// yield [argument] / yield* argument（仅 generator 上下文）
struct YieldExpression final : Node {
    YieldExpression(SourceRange range, NodePtr argument, bool delegate)
        : Node(NodeKind::YieldExpression, range),
          argument(std::move(argument)),
          delegate(delegate) {}
    NodePtr argument;  // 可空
    bool delegate;
};

// import(source)
struct ImportExpression final : Node {
    ImportExpression(SourceRange range, NodePtr source)
        : Node(NodeKind::ImportExpression, range),
          source(std::move(source)) {}
    NodePtr source;
};

// new callee(arguments)
struct NewExpression final : Node {
    NewExpression(SourceRange range, NodePtr callee, NodeList arguments)
        : Node(NodeKind::NewExpression, range),
          callee(std::move(callee)),
          arguments(std::move(arguments)) {}
    NodePtr callee;
    NodeList arguments;
};

enum class UnaryOperator : uint8_t {
    Delete,
    Void,
    TypeOf,
    Plus,
    Minus,
    BitNot,
    LogicalNot,
};

// op argument
struct UnaryExpression final : Node {
    UnaryExpression(SourceRange range, UnaryOperator op, NodePtr argument)
        : Node(NodeKind::UnaryExpression, range),
          op(op),
          argument(std::move(argument)) {}
    UnaryOperator op;
    NodePtr argument;
};

enum class UpdateOperator : uint8_t { Increment, Decrement };

// prefix ? ++argument : argument++
struct UpdateExpression final : Node {
    UpdateExpression(SourceRange range, UpdateOperator op, bool prefix,
                     NodePtr argument)
        : Node(NodeKind::UpdateExpression, range),
          op(op),
          prefix(prefix),
          argument(std::move(argument)) {}
    UpdateOperator op;
    bool prefix;
    NodePtr argument;
};

enum class BinaryOperator : uint8_t {
    Equal,
    NotEqual,
    StrictEqual,
    StrictNotEqual,
    LessThan,
    LessThanEqual,
    GreaterThan,
    GreaterThanEqual,
    LeftShift,
    RightShift,
    UnsignedRightShift,
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    Exponentiate,
    BitAnd,
    BitOr,
    BitXor,
    In,
    InstanceOf,
};

// left op right
struct BinaryExpression final : Node {
    BinaryExpression(SourceRange range, BinaryOperator op, NodePtr left,
                     NodePtr right)
        : Node(NodeKind::BinaryExpression, range),
          op(op),
          left(std::move(left)),
          right(std::move(right)) {}
    BinaryOperator op;
    NodePtr left;
    NodePtr right;
};

enum class LogicalOperator : uint8_t { And, Or, Nullish };

// left op right（短路求值，与 BinaryExpression 分开）
struct LogicalExpression final : Node {
    LogicalExpression(SourceRange range, LogicalOperator op, NodePtr left,
                      NodePtr right)
        : Node(NodeKind::LogicalExpression, range),
          op(op),
          left(std::move(left)),
          right(std::move(right)) {}
    LogicalOperator op;
    NodePtr left;
    NodePtr right;
};

// test ? consequent : alternate
struct ConditionalExpression final : Node {
    ConditionalExpression(SourceRange range, NodePtr test, NodePtr consequent,
                          NodePtr alternate)
        : Node(NodeKind::ConditionalExpression, range),
          test(std::move(test)),
          consequent(std::move(consequent)),
          alternate(std::move(alternate)) {}
    NodePtr test;
    NodePtr consequent;
    NodePtr alternate;
};

enum class AssignmentOperator : uint8_t {
    Assign,
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    Exponentiate,
    LeftShift,
    RightShift,
    UnsignedRightShift,
    BitAnd,
    BitOr,
    BitXor,
    LogicalAnd,
    LogicalOr,
    Nullish,
};

// left op right
struct AssignmentExpression final : Node {
    AssignmentExpression(SourceRange range, AssignmentOperator op, NodePtr left,
                         NodePtr right)
        : Node(NodeKind::AssignmentExpression, range),
          op(op),
          left(std::move(left)),
          right(std::move(right)) {}
    AssignmentOperator op;
    NodePtr left;
    NodePtr right;
};

// expr1, expr2, ...
struct SequenceExpression final : Node {
    SequenceExpression(SourceRange range, NodeList expressions)
        : Node(NodeKind::SequenceExpression, range),
          expressions(std::move(expressions)) {}
    NodeList expressions;
};

// [element, ...]
// elements 中的空指针表示 elision（如 [1, , 3] 的中间位置）。
struct ArrayExpression final : Node {
    ArrayExpression(SourceRange range, NodeList elements)
        : Node(NodeKind::ArrayExpression, range),
          elements(std::move(elements)) {}
    NodeList elements;
};

enum class PropertyKind : uint8_t { Init, Get, Set };

// key: value / key / [key]: value / key() {} / get key() {} / set key(v) {}
// shorthand 为 true 时 key 与 value 指向同一个名字（value 是独立的 Identifier）。
// method 为 true 时 value 是 FunctionExpression。
struct Property final : Node {
    Property(SourceRange range, NodePtr key, NodePtr value, bool computed,
             bool shorthand, bool method = false,
             PropertyKind propertyKind = PropertyKind::Init)
        : Node(NodeKind::Property, range),
          key(std::move(key)),
          value(std::move(value)),
          computed(computed),
          shorthand(shorthand),
          method(method),
          propertyKind(propertyKind) {}
    NodePtr key;
    NodePtr value;
    bool computed;
    bool shorthand;
    bool method;
    PropertyKind propertyKind;
};

// { property, ...spread }
struct ObjectExpression final : Node {
    ObjectExpression(SourceRange range, NodeList properties)
        : Node(NodeKind::ObjectExpression, range),
          properties(std::move(properties)) {}
    NodeList properties;
};

// ...argument（数组字面量 / 调用参数中的展开）
// 与 RestElement 结构相同但语义不同：SpreadElement 求值展开，
// RestElement 是模式中的绑定目标。
struct SpreadElement final : Node {
    SpreadElement(SourceRange range, NodePtr argument)
        : Node(NodeKind::SpreadElement, range),
          argument(std::move(argument)) {}
    NodePtr argument;
};

// [async] (params) => body / param => body
// expression 为 true 时 body 是表达式（concise body），否则是 BlockStatement。
struct ArrowFunctionExpression final : Node {
    ArrowFunctionExpression(SourceRange range, NodeList params, NodePtr body,
                            bool async, bool expression)
        : Node(NodeKind::ArrowFunctionExpression, range),
          params(std::move(params)),
          body(std::move(body)),
          async(async),
          expression(expression) {}
    NodeList params;
    NodePtr body;
    bool async;
    bool expression;
};

// { property, ...rest }（绑定/赋值模式）
// properties 中的元素是 Property 或 RestElement。
struct ObjectPattern final : Node {
    ObjectPattern(SourceRange range, NodeList properties)
        : Node(NodeKind::ObjectPattern, range),
          properties(std::move(properties)) {}
    NodeList properties;
};

// [element, ...]（绑定/赋值模式）
// elements 中的空指针表示 elision。
struct ArrayPattern final : Node {
    ArrayPattern(SourceRange range, NodeList elements)
        : Node(NodeKind::ArrayPattern, range),
          elements(std::move(elements)) {}
    NodeList elements;
};

// target = default（模式中的默认值）
struct AssignmentPattern final : Node {
    AssignmentPattern(SourceRange range, NodePtr left, NodePtr right)
        : Node(NodeKind::AssignmentPattern, range),
          left(std::move(left)),
          right(std::move(right)) {}
    NodePtr left;
    NodePtr right;
};

// ...argument（函数参数 / 解构模式中的 rest）
struct RestElement final : Node {
    RestElement(SourceRange range, NodePtr argument)
        : Node(NodeKind::RestElement, range),
          argument(std::move(argument)) {}
    NodePtr argument;
};

}  // namespace neo::frontend

#endif  // NEO_FRONTEND_NODE_H
