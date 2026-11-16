// RegexParser.h - 正则 pattern 语法分析（ADR-062）
//
// 只负责语法：把 pattern 解析成 RegexAST，并做 flags/分组/引用等早期校验。
// 不做匹配，也不依赖 JS 运行时。错误以 CompileError{offset,message} 返回。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_PARSER_H
#define NEO_REGEX_PARSER_H

#include "regex/RegexAST.h"
#include "regex/RegexFlags.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neo::regex {

class RegexParser {
public:
    RegexParser(std::string_view pattern, Flags flags, Dialect dialect);

    ParseResult parse();

private:
    struct QuantSpec {
        std::size_t offset = 0;
        std::uint32_t min = 0;
        std::uint32_t max = 0;
        bool greedy = true;
    };

    enum class QuantResult { None, Spec, Error };

    struct BackrefRef {
        std::size_t offset = 0;
        std::uint32_t index = 0;
        std::string name;
    };

    // —— 语法 ——
    NodePtr parseDisjunction();
    NodePtr parseAlternative();
    NodePtr parseTerm();
    NodePtr parseAtom();
    NodePtr parseAssertion();
    NodePtr parseGroup();
    NodePtr parseCharacterClass();
    NodePtr parseAtomEscape();
    bool parseClassAtom(ClassItem& item);
    bool parseClassEscape(ClassItem& item);
    bool parsePropertyName(std::string& name);
    bool parseGroupName(std::string& name);
    bool parseDecimal(std::uint32_t& out);
    bool parseHex(std::uint32_t digits, std::uint32_t& out);
    QuantResult tryParseQuantifierSpec(QuantSpec& spec);

    // —— 输入 ——
    bool atEnd() const { return pos_ >= pattern_.size(); }
    std::size_t here() const { return pos_; }
    std::uint32_t peek();
    std::uint32_t peekNext();
    std::uint32_t advance();
    bool eat(std::uint32_t codePoint);

    void fail(std::size_t offset, std::string message);
    void validateBackrefs();

    std::string_view pattern_;
    std::size_t pos_ = 0;
    Flags flags_;
    Dialect dialect_;

    bool failed_ = false;
    std::optional<CompileError> error_;
    std::uint32_t captureCount_ = 0;
    std::unordered_map<std::string, std::uint32_t> namedGroups_;
    std::vector<BackrefRef> backrefs_;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_PARSER_H
