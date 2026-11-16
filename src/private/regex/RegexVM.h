// RegexVM.h - 回溯匹配执行器（ADR-062）
//
// 显式回溯栈 + 捕获 undo log，不使用 C++ 递归。执行状态全部在单次 exec
// 内部；Program 保持不可变。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_VM_H
#define NEO_REGEX_VM_H

#include "regex/RegexHost.h"
#include "regex/RegexMatch.h"
#include "regex/RegexProgram.h"
#include "regex/RegexSubject.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace neo::regex {

class RegexVM {
public:
    RegexVM(const Program& program, const Subject& subject, RegexHost& host);

    MatchResult exec(ExecOptions options);

private:
    struct Frame {
        std::uint32_t pc = 0;
        std::size_t pos = 0;
        std::size_t undoMark = 0;
    };

    struct Undo {
        std::uint32_t slot = 0;
        std::size_t oldValue = 0;
    };

    bool runAt(std::size_t start, MatchResult& out);
    bool backtrack();

    bool matchChar(char16_t value, std::uint32_t pattern) const;
    bool classMatches(const CharClassTable& table, char16_t value) const;
    static bool inRanges(const std::vector<CharRange>& ranges,
                         std::uint32_t codePoint);
    static std::uint32_t asciiUpper(std::uint32_t codePoint);
    static std::uint32_t asciiLower(std::uint32_t codePoint);
    static bool isLineTerminator(std::uint32_t codePoint);
    static bool isWord(char16_t value);

    const Program& program_;
    const Subject& subject_;
    RegexHost& host_;

    std::uint32_t pc_ = 0;
    std::size_t pos_ = 0;
    std::uint64_t steps_ = 0;
    bool aborted_ = false;

    std::vector<std::size_t> captures_;
    std::vector<Undo> undo_;
    std::vector<Frame> stack_;
    std::vector<std::size_t> scratch_;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_VM_H
