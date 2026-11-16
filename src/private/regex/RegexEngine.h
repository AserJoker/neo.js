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

#include <memory_resource>
#include <string_view>

namespace neo::regex {

class RegexEngine {
public:
    explicit RegexEngine(RegexHost& host,
                         std::pmr::memory_resource* memory = nullptr);

    CompileResult compile(std::string_view pattern, Flags flags,
                          Dialect dialect = {});

    // 从 options.startIndex 开始尝试；anchored=false 时向后搜索。
    MatchResult exec(const Program& program, const Subject& subject,
                     ExecOptions options = {});

private:
    RegexHost& host_;
    std::pmr::memory_resource* memory_;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_ENGINE_H
