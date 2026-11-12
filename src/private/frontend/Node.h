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
    EmptyStatement,
    ExpressionStatement,

    // 表达式
    Identifier,
    NumericLiteral,
    BigIntLiteral,
    StringLiteral,
    BooleanLiteral,
    NullLiteral,
    RegExpLiteral,

    MemberExpression,
    CallExpression,
    NewExpression,
    UnaryExpression,
    UpdateExpression,
    BinaryExpression,
    LogicalExpression,
    ConditionalExpression,
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
    Program(SourceRange range, NodeList body, bool strict)
        : Node(NodeKind::Program, range),
          body(std::move(body)),
          strict(strict) {}
    NodeList body;
    bool strict;
};

// ---------------------------------------------------------------------------
// 语句
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// 表达式
// ---------------------------------------------------------------------------

struct Identifier final : Node {
    Identifier(SourceRange range, std::string name)
        : Node(NodeKind::Identifier, range), name(std::move(name)) {}
    std::string name;
};

struct NumericLiteral final : Node {
    NumericLiteral(SourceRange range, double value)
        : Node(NodeKind::NumericLiteral, range), value(value) {}
    double value;
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
    StringLiteral(SourceRange range, std::string value)
        : Node(NodeKind::StringLiteral, range), value(std::move(value)) {}
    std::string value;
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

// obj.property / obj[expression]
struct MemberExpression final : Node {
    MemberExpression(SourceRange range, NodePtr object, NodePtr property,
                     bool computed)
        : Node(NodeKind::MemberExpression, range),
          object(std::move(object)),
          property(std::move(property)),
          computed(computed) {}
    NodePtr object;
    NodePtr property;
    bool computed;
};

// callee(arguments)
struct CallExpression final : Node {
    CallExpression(SourceRange range, NodePtr callee, NodeList arguments)
        : Node(NodeKind::CallExpression, range),
          callee(std::move(callee)),
          arguments(std::move(arguments)) {}
    NodePtr callee;
    NodeList arguments;
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

}  // namespace neo::frontend

#endif  // NEO_FRONTEND_NODE_H
