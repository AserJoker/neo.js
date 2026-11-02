// command.cpp - CommandParser 实现
//
// 关联 ADR: 028（infra）

#include "infra/command.h"

#include <algorithm>
#include <string>

namespace neo::infra {

// --- ParsedArgs ---
bool ParsedArgs::has(const std::string& name) const {
    return options_.find(name) != options_.end();
}

std::string_view ParsedArgs::value(const std::string& name) const {
    auto it = options_.find(name);
    if (it == options_.end()) return "";
    return it->second;
}

// --- CommandParser ---
void CommandParser::add_option(CommandOption opt) {
    options_.push_back(std::move(opt));
}

const CommandOption* CommandParser::find_long(std::string_view name) const {
    for (const auto& o : options_) {
        if (o.name == name) return &o;
    }
    return nullptr;
}

const CommandOption* CommandParser::find_short(std::string_view name) const {
    for (const auto& o : options_) {
        if (!o.short_name.empty() && o.short_name == name) return &o;
    }
    return nullptr;
}

Result<ParsedArgs> CommandParser::parse(int argc, const char* const argv[]) const {
    if (argc < 1) {
        return Result<ParsedArgs>::err(ErrCode::InvalidArg);
    }

    ParsedArgs result;
    // i=0 是程序名，跳过
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        if (arg.empty()) {
            continue;  // 忽略空参数
        }

        // -- 形式：之后全是 positional
        if (arg == "--") {
            for (int j = i + 1; j < argc; ++j) {
                result.positionals_.emplace_back(argv[j]);
            }
            return Result<ParsedArgs>::ok(std::move(result));
        }

        // 长选项 --name 或 --name=value
        if (arg.starts_with("--")) {
            std::string_view rest = arg.substr(2);
            auto eq = rest.find('=');
            std::string_view name = (eq == std::string_view::npos) ? rest : rest.substr(0, eq);
            const CommandOption* opt = find_long(name);
            if (opt == nullptr) {
                return Result<ParsedArgs>::err(ErrCode::InvalidArg);  // 未知选项
            }
            if (opt->takes_value) {
                if (eq != std::string_view::npos) {
                    result.options_[opt->name] = std::string(rest.substr(eq + 1));
                } else {
                    // 值在下一个 argv
                    if (i + 1 >= argc) {
                        return Result<ParsedArgs>::err(ErrCode::InvalidArg);  // 缺值
                    }
                    result.options_[opt->name] = std::string(argv[++i]);
                }
            } else {
                // 布尔标志，不接受 =value
                if (eq != std::string_view::npos) {
                    return Result<ParsedArgs>::err(ErrCode::InvalidArg);  // 标志带值非法
                }
                result.options_[opt->name] = "true";
            }
            continue;
        }

        // 短选项 -n（不支持合并如 -abc，保持简单）
        if (arg.starts_with("-") && arg.size() > 1) {
            std::string_view name = arg.substr(1);
            const CommandOption* opt = find_short(name);
            if (opt == nullptr) {
                return Result<ParsedArgs>::err(ErrCode::InvalidArg);  // 未知短选项
            }
            if (opt->takes_value) {
                if (i + 1 >= argc) {
                    return Result<ParsedArgs>::err(ErrCode::InvalidArg);  // 缺值
                }
                result.options_[opt->name] = std::string(argv[++i]);
            } else {
                result.options_[opt->name] = "true";
            }
            continue;
        }

        // positional
        result.positionals_.emplace_back(arg);
    }

    return Result<ParsedArgs>::ok(std::move(result));
}

}  // namespace neo::infra
