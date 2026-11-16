// RegexProgram.h - 正则编译产物（ADR-062）
//
// Program 不可变、可缓存、可共享。R0 只保存元数据（source/flags/dialect/
// 捕获组/命名组）；R1 起加入指令流与字符类表。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_PROGRAM_H
#define NEO_REGEX_PROGRAM_H

#include "regex/RegexFlags.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace neo::regex {

struct Program {
    std::string source;
    Flags flags;
    Dialect dialect;
    std::uint32_t captureCount = 0;  // 捕获组数量（不含整体匹配 group 0）
    std::unordered_map<std::string, std::uint32_t> namedGroups;

    // R1: std::vector<Instruction> instructions;
    // R1: std::vector<CharacterClassTable> classes;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_PROGRAM_H
