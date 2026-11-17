// RegexPikeVM.h - 线性 NFA 模拟（Pike VM）兜底（ADR-062 R6）
//
// 只处理 regular 子集：无反向引用、无前后瞻。线程按优先级排序，遇到 Match
// 时截断更低优先级线程，从而保持 ECMAScript 的 leftmost-first 捕获语义。
// 每条输入边界最多保留 (pc, scratch) 一个线程，最坏复杂度与输入长度成线性
// （scratch 已按计数器上界/进度标记规范化）。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_PIKE_VM_H
#define NEO_REGEX_PIKE_VM_H

#include "regex/RegexHost.h"
#include "regex/RegexMatch.h"
#include "regex/RegexProgram.h"
#include "regex/RegexSubject.h"

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace neo::regex {

// Program 是否可交给 Pike VM：无 Backref/Lookahead/Lookbehind，且 scratch
// 元数据完整（旧产物缺少元数据时回退回溯 VM）。
bool supportsPikeVM(const Program& program);

class RegexPikeVM {
public:
    RegexPikeVM(const Program& program, const Subject& subject, RegexHost& host);

    MatchResult exec(ExecOptions options);

private:
    struct Thread {
        std::uint32_t pc = 0;
        std::vector<std::size_t> captures;
        std::vector<std::uint32_t> scratch;
    };

    struct StateKey {
        std::uint32_t pc = 0;
        std::vector<std::uint32_t> scratch;

        bool operator==(const StateKey& other) const {
            return pc == other.pc && scratch == other.scratch;
        }
    };

    struct StateKeyHash {
        std::size_t operator()(const StateKey& key) const;
    };

    using Visited = std::unordered_set<StateKey, StateKeyHash>;

    // 展开 epsilon 闭包；遇到 Match 时写入 best 并返回 true（调用方据此
    // 截断更低优先级线程）。
    bool addThread(std::vector<Thread>& list, Visited& visited, Thread thread,
                   std::size_t pos, Thread& best, bool& matched);

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
    bool step();

    const Program& program_;
    const Subject& subject_;
    RegexHost& host_;

    std::uint64_t steps_ = 0;
    std::uint64_t stepLimit_ = 0;
    bool aborted_ = false;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_PIKE_VM_H
