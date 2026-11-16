// RegexFlags.h - 正则 flags / dialect / 编译错误（ADR-062）
//
// flags 是 ECMAScript RegExp 的 d g i m s u v y；u/v 互斥，重复即 SyntaxError。
// Dialect 控制语法扩展（Annex B、后瞻、反向引用），引擎只服务 ECMAScript，
// 不做通用方言。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_FLAGS_H
#define NEO_REGEX_FLAGS_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace neo::regex {

struct Flags {
    bool hasIndices = false;   // d
    bool global = false;       // g
    bool ignoreCase = false;   // i
    bool multiline = false;    // m
    bool dotAll = false;       // s
    bool unicode = false;      // u
    bool unicodeSets = false;  // v
    bool sticky = false;       // y
};

struct Dialect {
    bool annexB = true;           // Web 兼容语法（Annex B）
    bool allowLookbehind = true;  // (?<= ) / (?<! )
    bool allowBackrefs = true;    // \1 / \k<name>
};

struct CompileError {
    std::size_t offset = 0;  // pattern 内的字节偏移
    std::string message;
};

struct FlagsResult {
    Flags flags;
    std::optional<CompileError> error;

    bool ok() const { return !error.has_value(); }
};

// 解析 flags 字符串（ASCII）。未知/重复 flag、u 与 v 同时出现均返回错误。
FlagsResult parseFlags(std::string_view text);

}  // namespace neo::regex

#endif  // NEO_REGEX_FLAGS_H
