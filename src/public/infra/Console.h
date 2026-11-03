// Console.h - 输出工具（持 IOutputSink* 注入，print/printLine 用 std::format 泛型）
//
// Console 是具体类，持 IOutputSink*（注入，无所有权）。sink=nullptr 时使用进程级
// 默认 StdoutSink 单例。具体实现（构造/析构/write/writeLine）在 private/infra/Console.cpp。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装/可测试性）、028（infra）

#ifndef NEO_INFRA_CONSOLE_H
#define NEO_INFRA_CONSOLE_H

#include "infra/IOutputSink.h"

#include <format>
#include <string_view>
#include <utility>

namespace neo::infra {

// 输出工具，通过 IOutputSink* 注入
class Console {
    IOutputSink* sink_;  // 不持有所有权
public:
    // sink=nullptr 时使用进程级默认 StdoutSink 单例
    explicit Console(IOutputSink* sink = nullptr);

    // 无格式写
    void write(std::string_view text);
    void writeLine(std::string_view text);

    // std::format 泛型格式化输出
    template <typename... Args>
    void print(std::format_string<Args...> fmt, Args&&... args) {
        sink_->write(std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void printLine(std::format_string<Args...> fmt, Args&&... args) {
        sink_->writeLine(std::format(fmt, std::forward<Args>(args)...));
    }

    IOutputSink* sink() const { return sink_; }
};

}  // namespace neo::infra

#endif  // NEO_INFRA_CONSOLE_H
