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
#include "regex/RegexProgram.h"
#include "regex/RegexSubject.h"

#include <memory>
#include <memory_resource>
#include <optional>
#include <string_view>

namespace neo::regex {

struct CompileResult {
    std::shared_ptr<const Program> program;
    std::optional<CompileError> error;

    bool ok() const { return program != nullptr && !error.has_value(); }
};

class RegexEngine {
public:
    explicit RegexEngine(RegexHost& host,
                         std::pmr::memory_resource* memory = nullptr);

    CompileResult compile(std::string_view pattern, Flags flags,
                          Dialect dialect = {});

    // R1: MatchResult exec(const Program&, const Subject&, size_t startIndex);

private:
    RegexHost& host_;
    std::pmr::memory_resource* memory_;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_ENGINE_H
