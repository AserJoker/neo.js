// RegexAST.h - 正则 pattern 的语法树（ADR-062）
//
// Parser 产出 AST；Compiler（R1）把它翻译成 Program 指令流。AST 只描述
// 语法结构，不包含匹配状态。offset 为 pattern 内的字节偏移，用于 SyntaxError。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_AST_H
#define NEO_REGEX_AST_H

#include "regex/RegexFlags.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace neo::regex {

enum class NodeKind {
    Disjunction,
    Alternative,
    Empty,
    Character,
    Dot,
    CharacterClass,
    Group,
    Quantifier,
    Assertion,
    Backreference,
    PropertyEscape,
};

struct Node {
    Node(NodeKind kind, std::size_t offset) : kind(kind), offset(offset) {}
    virtual ~Node() = default;

    NodeKind kind;
    std::size_t offset;
};

using NodePtr = std::unique_ptr<Node>;

struct Disjunction final : Node {
    explicit Disjunction(std::size_t offset)
        : Node(NodeKind::Disjunction, offset) {}
    std::vector<NodePtr> alternatives;
};

struct Alternative final : Node {
    explicit Alternative(std::size_t offset)
        : Node(NodeKind::Alternative, offset) {}
    std::vector<NodePtr> terms;
};

struct Empty final : Node {
    explicit Empty(std::size_t offset) : Node(NodeKind::Empty, offset) {}
};

struct Character final : Node {
    Character(std::size_t offset, std::uint32_t codePoint)
        : Node(NodeKind::Character, offset), codePoint(codePoint) {}
    std::uint32_t codePoint;
};

struct Dot final : Node {
    explicit Dot(std::size_t offset) : Node(NodeKind::Dot, offset) {}
};

struct Assertion final : Node {
    enum class Kind { Start, End, WordBoundary, NotWordBoundary };

    Assertion(std::size_t offset, Kind assertion)
        : Node(NodeKind::Assertion, offset), assertion(assertion) {}
    Kind assertion;
};

struct Backreference final : Node {
    Backreference(std::size_t offset, std::uint32_t index,
                  std::string name = {})
        : Node(NodeKind::Backreference, offset),
          index(index),
          name(std::move(name)) {}
    std::uint32_t index;  // 0 表示按 name 解析
    std::string name;
    // Annex B：无效数字反向引用退化为 LegacyOctalEscapeSequence。
    bool legacyOctal = false;
    std::string digits;
};

struct PropertyEscape final : Node {
    PropertyEscape(std::size_t offset, bool negated, std::string property)
        : Node(NodeKind::PropertyEscape, offset),
          negated(negated),
          property(std::move(property)) {}
    bool negated;
    std::string property;
};

struct CharacterClass;

struct ClassItem {
    enum class Kind { Range, Escape, Property, Nested, String };
    enum class EscapeKind { Digit, NotDigit, Word, NotWord, Space, NotSpace };

    Kind kind = Kind::Range;
    std::size_t offset = 0;
    // Range
    std::uint32_t from = 0;
    std::uint32_t to = 0;
    // Escape
    EscapeKind escape = EscapeKind::Digit;
    // Property
    bool negated = false;
    std::string property;
    // Nested（v 模式字符类嵌套）
    std::shared_ptr<CharacterClass> nested;
    // String（v 模式 \q{...} 的单个替代项；R5 只支持单码点）
    std::vector<std::uint32_t> chars;
};

struct CharacterClass final : Node {
    enum class SetOp { Union, Intersection, Subtraction };

    explicit CharacterClass(std::size_t offset)
        : Node(NodeKind::CharacterClass, offset) {}
    bool negated = false;
    std::vector<ClassItem> items;
    // v 模式集合运算：left/right 非空时，本节点表示二元运算；
    // 否则 items 是隐式并集。
    SetOp setOp = SetOp::Union;
    std::shared_ptr<CharacterClass> left;
    std::shared_ptr<CharacterClass> right;
};

struct Group final : Node {
    enum class Kind {
        Capturing,
        NamedCapturing,
        NonCapturing,
        Lookahead,
        NegativeLookahead,
        Lookbehind,
        NegativeLookbehind,
    };

    Group(std::size_t offset, Kind kind)
        : Node(NodeKind::Group, offset), groupKind(kind) {}
    Kind groupKind;
    std::uint32_t captureIndex = 0;  // 1-based；非捕获为 0
    std::string name;
    NodePtr body;
};

struct Quantifier final : Node {
    Quantifier(std::size_t offset, std::uint32_t min, std::uint32_t max,
               bool greedy)
        : Node(NodeKind::Quantifier, offset),
          min(min),
          max(max),
          greedy(greedy) {}
    NodePtr atom;
    std::uint32_t min;
    std::uint32_t max;  // UINT32_MAX 表示无上界
    bool greedy;
};

struct ParseResult {
    NodePtr root;
    std::uint32_t captureCount = 0;  // 捕获组数量（不含整体匹配 group 0）
    std::unordered_map<std::string, std::uint32_t> namedGroups;
    std::optional<CompileError> error;

    bool ok() const { return root != nullptr && !error.has_value(); }
};

}  // namespace neo::regex

#endif  // NEO_REGEX_AST_H
