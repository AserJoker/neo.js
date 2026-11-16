// RegexProgram.h - 正则编译产物与指令集（ADR-062）
//
// Program 不可变、可缓存、可共享。R1 起包含扁平指令流与字符类表。
// pc 为指令下标；Split/Jmp/CounterLt 的目标均为指令下标。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_PROGRAM_H
#define NEO_REGEX_PROGRAM_H

#include "regex/RegexFlags.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace neo::regex {

enum class OpCode : std::uint8_t {
    Char,        // a = code point / code unit
    CharClass,   // a = classes 下标
    Any,         // .（dotAll 由 Program::flags 决定）
    Split,       // a = 优先分支 pc，b = 回退分支 pc
    Jmp,         // a = target pc
    Save,        // a = 捕获槽（2*i / 2*i+1）
    AssertStart,
    AssertEnd,
    AssertWordBoundary,
    AssertNotWordBoundary,
    Mark,        // a = scratch 槽：记录当前位置
    Progress,    // a = scratch 槽，b = 目标 pc：未推进则跳转
    SetCounter,  // a = scratch 槽，b = 初值
    IncCounter,  // a = scratch 槽
    CounterLt,   // a = scratch 槽，b = 上界，c = 目标 pc（槽 < 上界时跳转）
    Backref,     // a = 捕获组编号（1-based）
    Lookahead,   // a = subPrograms 下标，b = 1 表示否定
    Lookbehind,  // a = subPrograms 下标，b = 1 表示否定
    Match,
};

struct Instruction {
    OpCode op = OpCode::Match;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
};

struct CharRange {
    std::uint32_t from = 0;
    std::uint32_t to = 0;
};

struct CharClassTable {
    bool negated = false;
    std::vector<CharRange> ranges;
};

// 前后瞻子程序：指令下标、字符类表、scratch 槽均独立；捕获槽与主程序共享。
struct SubProgram {
    std::vector<Instruction> instructions;
    std::vector<CharClassTable> classes;
    std::uint32_t scratchCount = 0;
};

struct Program {
    std::string source;
    Flags flags;
    Dialect dialect;
    std::uint32_t captureCount = 0;  // 捕获组数量（不含整体匹配 group 0）
    std::uint32_t scratchCount = 0;
    std::unordered_map<std::string, std::uint32_t> namedGroups;
    std::vector<Instruction> instructions;
    std::vector<CharClassTable> classes;
    std::vector<SubProgram> subPrograms;
};

struct CompileResult {
    std::shared_ptr<const Program> program;
    std::optional<CompileError> error;

    bool ok() const { return program != nullptr && !error.has_value(); }
};

}  // namespace neo::regex

#endif  // NEO_REGEX_PROGRAM_H
