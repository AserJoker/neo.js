// command.h - 命令行参数解析工具
//
// 纯逻辑无 FFI，最易测试（直接传 argv）。支持 --flag / --opt=value / -o value / positional。
// 解析错误返回 Result<ParsedArgs>::err(ErrCode)，符合 ADR-006/007。
//
// 关联 ADR: 027（可测试性）、028（infra）

#ifndef NEO_INFRA_COMMAND_H
#define NEO_INFRA_COMMAND_H

#include "infra/result.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace neo::infra {

// 选项声明
struct CommandOption {
    std::string name;         // 长选项名（不含 --），如 "verbose"
    std::string short_name;   // 短选项名（不含 -），如 "v"；空表示无短选项
    bool        takes_value;  // true: 期望值（--opt=val 或 -o val）；false: 布尔标志
    std::string description;  // 描述（用于帮助信息）
};

// 解析结果
class ParsedArgs {
public:
    bool has(const std::string& name) const;
    std::string_view value(const std::string& name) const;  // 无值时返回 ""（has 先判）
    const std::vector<std::string>& positionals() const { return positionals_; }

private:
    friend class CommandParser;
    std::unordered_map<std::string, std::string> options_;  // name -> value（flag 为 "true"）
    std::vector<std::string>                      positionals_;
};

// 解析器
class CommandParser {
public:
    // 添加选项声明
    void add_option(CommandOption opt);

    // 解析 argv[0..argc-1]（argv[0] 是程序名，跳过）
    Result<ParsedArgs> parse(int argc, const char* const argv[]) const;

    const std::vector<CommandOption>& options() const { return options_; }

private:
    std::vector<CommandOption> options_;

    const CommandOption* find_long(std::string_view name) const;
    const CommandOption* find_short(std::string_view name) const;
};

}  // namespace neo::infra

#endif  // NEO_INFRA_COMMAND_H
