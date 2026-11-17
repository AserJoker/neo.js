// RegexEngine.h - 正则引擎门面（ADR-062）
//
// 原语：compile(pattern, flags, dialect) -> Program；exec(Program, Subject, start)
// 在 R1 起提供。R0 只做语法解析与编译产物元数据。
//
// Program 缓存（R1/R6）由 Engine 内部维护，按 (pattern, flags, dialect) 键。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_ENGINE_H
#define NEO_REGEX_ENGINE_H

#include "regex/RegexFlags.h"
#include "regex/RegexHost.h"
#include "regex/RegexMatch.h"
#include "regex/RegexProgram.h"
#include "regex/RegexSubject.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <unordered_map>

namespace neo::regex {

class RegexEngine {
public:
    // Program 缓存容量上限；超出后按 LRU 淘汰。
    static constexpr std::size_t kCacheCapacity = 128;

    explicit RegexEngine(RegexHost& host,
                         std::pmr::memory_resource* memory = nullptr);

    CompileResult compile(std::string_view pattern, Flags flags,
                          Dialect dialect = {});

    // 从 options.startIndex 开始尝试；anchored=false 时向后搜索。
    MatchResult exec(const Program& program, const Subject& subject,
                     ExecOptions options = {});

    // 当前缓存条目数（诊断/测试用）。
    std::size_t cacheSize() const { return cache_.size(); }

private:
    struct CacheKey {
        std::string pattern;
        std::uint8_t flags = 0;
        std::uint8_t dialect = 0;

        bool operator==(const CacheKey& other) const {
            return flags == other.flags && dialect == other.dialect &&
                   pattern == other.pattern;
        }
    };

    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& key) const;
    };

    struct CacheEntry {
        std::shared_ptr<const Program> program;
        std::list<CacheKey>::iterator order;
    };

    static CacheKey makeKey(std::string_view pattern, Flags flags,
                            Dialect dialect);
    void remember(const CacheKey& key, std::shared_ptr<const Program> program);

    RegexHost& host_;
    std::pmr::memory_resource* memory_;
    std::list<CacheKey> lru_;
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> cache_;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_ENGINE_H
