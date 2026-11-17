// RegexMatch.h - 单次匹配结果（ADR-062）
//
// 引擎只返回普通数据：匹配区间 + 捕获组区间。JS 层再构造结果数组、
// groups、indices 等对象。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_MATCH_H
#define NEO_REGEX_MATCH_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace neo::regex {

enum class MatchStatus {
    Matched,
    NoMatch,
    Aborted,
    Oom,
};

struct Capture {
    bool matched = false;
    std::size_t start = 0;
    std::size_t end = 0;
};

struct MatchResult {
    MatchStatus status = MatchStatus::NoMatch;
    std::size_t start = 0;
    std::size_t end = 0;
    std::vector<Capture> captures;  // [0] 为整体匹配

    bool matched() const { return status == MatchStatus::Matched; }
};

struct ExecOptions {
    std::size_t startIndex = 0;
    // true：只在 startIndex 尝试（sticky）；false：向后搜索。
    bool anchored = false;
    // 本次 exec 的指令步数上限；0 表示不限制（仍受 Host::shouldAbort 约束）。
    std::uint64_t stepLimit = 0;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_MATCH_H
