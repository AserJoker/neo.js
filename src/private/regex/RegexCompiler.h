// RegexCompiler.h - RegexAST -> Program 指令流（ADR-062）
//
// 一个 AST 节点一个 compile 函数；量词编译为 Split/计数循环，捕获组用
// Save 槽位。Unicode 属性在编译期展开为字符类范围表；未知属性记录
// CompileError，不生成语义错误的指令流。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_COMPILER_H
#define NEO_REGEX_COMPILER_H

#include "regex/RegexAST.h"
#include "regex/RegexFlags.h"
#include "regex/RegexProgram.h"
#include "regex/RegexUnicode.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace neo::regex {

class RegexCompiler {
public:
    CompileResult compile(const ParseResult& parsed, std::string_view source,
                          Flags flags, Dialect dialect);

private:
    std::uint32_t emit(OpCode op, std::uint32_t a = 0, std::uint32_t b = 0,
                       std::uint32_t c = 0);
    std::uint32_t here() const {
        return static_cast<std::uint32_t>(program_.instructions.size());
    }
    void patchA(std::uint32_t index, std::uint32_t value);
    void patchB(std::uint32_t index, std::uint32_t value);
    void patchC(std::uint32_t index, std::uint32_t value);

    void compileNode(const Node& node);
    void compileDisjunction(const Disjunction& node);
    void compileAlternative(const Alternative& node);
    void compileCharacter(const Character& node);
    void compileCharacterClass(const CharacterClass& node);
    void compileGroup(const Group& node);
    void compileLookaround(const Group& node, bool behind, bool negate);
    void compileBackreference(const Backreference& node);
    void compilePropertyEscape(const PropertyEscape& node);
    void compileQuantifier(const Quantifier& node);
    void compileAssertion(const Assertion& node);

    void appendRange(CharClassTable& table, std::uint32_t from,
                     std::uint32_t to);
    void appendEscape(CharClassTable& table, ClassItem::EscapeKind kind);
    void appendProperty(CharClassTable& table, const ClassItem& item);
    static bool resolveProperty(std::string_view name,
                                UnicodeProperty& property);
    static void appendComplement(std::vector<CharRange>& out,
                                 std::vector<CharRange> base);
    static void normalize(std::vector<CharRange>& ranges);

    void error(const Node& node, std::string message);
    void error(std::size_t offset, std::string message);

    Program program_;
    bool failed_ = false;
    std::optional<CompileError> error_;
    std::uint32_t scratchCount_ = 0;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_COMPILER_H
