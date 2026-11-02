// console.h - 输出工具（FFI 收口：std::cout）
//
// OutputSink 抽象输出目标，StdoutSink 默认包 std::cout（FFI 收口在 .cpp）。
// Console 持 OutputSink* 注入，print/print_line 用 std::format 泛型。
// 测试注入 StringSink 截获输出验证。
//
// 关联 ADR: 027（FFI 封装/可测试性）、028（infra）

#ifndef NEO_INFRA_CONSOLE_H
#define NEO_INFRA_CONSOLE_H

#include <format>
#include <string>
#include <string_view>

namespace neo::infra {

// 抽象输出目标
class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual void write(std::string_view text) = 0;
    virtual void write_line(std::string_view text) = 0;
    virtual void flush() = 0;
};

// 真实实现：包 std::cout（FFI 收口在 .cpp 实现文件内）
class StdoutSink : public OutputSink {
public:
    void write(std::string_view text) override;
    void write_line(std::string_view text) override;
    void flush() override;
};

// 输出工具，通过 OutputSink* 注入
class Console {
    OutputSink* sink_;  // 不持有所有权
public:
    // sink=nullptr 时使用默认 StdoutSink（进程级单例）
    explicit Console(OutputSink* sink = nullptr);

    // 无格式写
    void write(std::string_view text);
    void write_line(std::string_view text);

    // std::format 泛型格式化输出
    template <typename... Args>
    void print(std::format_string<Args...> fmt, Args&&... args);

    template <typename... Args>
    void print_line(std::format_string<Args...> fmt, Args&&... args);

    OutputSink* sink() const { return sink_; }
};

}  // namespace neo::infra

#include "infra/console.inl"

#endif  // NEO_INFRA_CONSOLE_H
