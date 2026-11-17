// RegexEngine.cpp - 正则引擎门面（ADR-062）

#include "regex/RegexEngine.h"

#include "regex/RegexCompiler.h"
#include "regex/RegexParser.h"
#include "regex/RegexVM.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace neo::regex {

namespace {

std::uint8_t packFlags(Flags flags) {
    std::uint8_t bits = 0;
    if (flags.hasIndices) bits |= 1u << 0;
    if (flags.global) bits |= 1u << 1;
    if (flags.ignoreCase) bits |= 1u << 2;
    if (flags.multiline) bits |= 1u << 3;
    if (flags.dotAll) bits |= 1u << 4;
    if (flags.unicode) bits |= 1u << 5;
    if (flags.unicodeSets) bits |= 1u << 6;
    if (flags.sticky) bits |= 1u << 7;
    return bits;
}

std::uint8_t packDialect(Dialect dialect) {
    std::uint8_t bits = 0;
    if (dialect.annexB) bits |= 1u << 0;
    if (dialect.allowLookbehind) bits |= 1u << 1;
    if (dialect.allowBackrefs) bits |= 1u << 2;
    return bits;
}

}  // namespace

RegexEngine::RegexEngine(RegexHost& host,
                         std::pmr::memory_resource* memory)
    : host_(host), memory_(memory) {
    (void)memory_;
}

RegexEngine::CacheKey RegexEngine::makeKey(std::string_view pattern,
                                           Flags flags, Dialect dialect) {
    CacheKey key;
    key.pattern.assign(pattern);
    key.flags = packFlags(flags);
    key.dialect = packDialect(dialect);
    return key;
}

std::size_t RegexEngine::CacheKeyHash::operator()(const CacheKey& key) const {
    std::size_t hash = std::hash<std::string>{}(key.pattern);
    hash ^= static_cast<std::size_t>(key.flags) + 0x9e3779b9u +
            (hash << 6) + (hash >> 2);
    hash ^= static_cast<std::size_t>(key.dialect) + 0x9e3779b9u +
            (hash << 6) + (hash >> 2);
    return hash;
}

void RegexEngine::remember(const CacheKey& key,
                           std::shared_ptr<const Program> program) {
    if (cache_.size() >= kCacheCapacity) {
        const CacheKey& oldest = lru_.back();
        cache_.erase(oldest);
        lru_.pop_back();
    }
    lru_.push_front(key);
    cache_.emplace(key, CacheEntry{std::move(program), lru_.begin()});
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

    const CacheKey key = makeKey(pattern, flags, dialect);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second.order);
        return CompileResult{it->second.program, std::nullopt};
    }

    RegexParser parser(pattern, flags, dialect);
    ParseResult parsed = parser.parse();
    if (!parsed.ok()) {
        return CompileResult{nullptr, std::move(parsed.error)};
    }

    RegexCompiler compiler;
    CompileResult result = compiler.compile(parsed, pattern, flags, dialect);
    if (result.ok()) remember(key, result.program);
    return result;
}

MatchResult RegexEngine::exec(const Program& program, const Subject& subject,
                              ExecOptions options) {
    RegexVM vm(program, subject, host_);
    return vm.exec(options);
}

}  // namespace neo::regex
