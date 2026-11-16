// RegexEngine.cpp - 正则引擎门面（ADR-062）

#include "regex/RegexEngine.h"

#include "regex/RegexCompiler.h"
#include "regex/RegexParser.h"
#include "regex/RegexVM.h"

#include <memory>
#include <utility>

namespace neo::regex {

RegexEngine::RegexEngine(RegexHost& host,
                         std::pmr::memory_resource* memory)
    : host_(host), memory_(memory) {
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

    RegexCompiler compiler;
    return compiler.compile(parsed, pattern, flags, dialect);
}

MatchResult RegexEngine::exec(const Program& program, const Subject& subject,
                              ExecOptions options) {
    RegexVM vm(program, subject, host_);
    return vm.exec(options);
}

}  // namespace neo::regex
