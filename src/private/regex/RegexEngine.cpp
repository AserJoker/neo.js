// RegexEngine.cpp - 正则引擎门面（ADR-062）

#include "regex/RegexEngine.h"

#include "regex/RegexParser.h"

#include <memory>
#include <utility>

namespace neo::regex {

RegexEngine::RegexEngine(RegexHost& host,
                         std::pmr::memory_resource* memory)
    : host_(host), memory_(memory) {
    // R0 尚未使用 Host/分配器；显式读取避免未使用字段告警。
    (void)host_;
    (void)memory_;
}

CompileResult RegexEngine::compile(std::string_view pattern, Flags flags,
                                   Dialect dialect) {
    if (flags.unicode && flags.unicodeSets) {
        return CompileResult{
            nullptr,
            CompileError{0,
                         "Regular expression flags 'u' and 'v' are mutually "
                         "exclusive"}};
    }

    RegexParser parser(pattern, flags, dialect);
    ParseResult parsed = parser.parse();
    if (!parsed.ok()) {
        return CompileResult{nullptr, std::move(parsed.error)};
    }

    auto program = std::make_shared<Program>();
    program->source = std::string(pattern);
    program->flags = flags;
    program->dialect = dialect;
    program->captureCount = parsed.captureCount;
    program->namedGroups = std::move(parsed.namedGroups);
    return CompileResult{std::move(program), std::nullopt};
}

}  // namespace neo::regex
