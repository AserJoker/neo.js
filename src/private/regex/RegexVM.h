// RegexVM.h - 回溯匹配执行器（ADR-062）
//
// 显式回溯栈 + 捕获 undo log，不使用 C++ 递归。前后瞻编译为子程序，
// 由 execute() 递归调用（嵌套深度由 pattern 决定）；捕获槽共享。
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
    bool execute(const std::vector<Instruction>& instructions,
                 const std::vector<CharClassTable>& classes,
                 std::uint32_t scratchCount,
                 std::vector<std::size_t>& captures, std::size_t start,
                 std::size_t* matchEnd);
    bool runSub(const SubProgram& sub, std::size_t start,
                std::vector<std::size_t>& captures, std::size_t* matchEnd);

    bool matchChar(std::uint32_t value, std::uint32_t pattern) const;
    bool classMatches(const CharClassTable& table,
                      std::uint32_t codePoint) const;
    static bool inRanges(const std::vector<CharRange>& ranges,
                         std::uint32_t codePoint);
    static std::uint32_t asciiUpper(std::uint32_t codePoint);
    static std::uint32_t asciiLower(std::uint32_t codePoint);
    static bool isLineTerminator(std::uint32_t codePoint);
    bool isWord(std::uint32_t value) const;
    bool isWordAt(std::size_t pos) const;
    bool isWordBefore(std::size_t pos) const;

    const Program& program_;
    const Subject& subject_;
    RegexHost& host_;

    std::uint64_t steps_ = 0;
    bool aborted_ = false;
    std::vector<std::size_t> captures_;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_VM_H
