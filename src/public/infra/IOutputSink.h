// console.h - 输出工具（FFI 收口：std::cout）
//
// OutputSink 抽象输出目标，Console 持 OutputSink* 注入，print/print_line 用 std::format 泛型。
// 具体实现 StdoutSink 在 private，通过工厂 create_stdout_sink() 获取。
// 测试注入 StringSink（自定义 mock sink）截获输出验证。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装/可测试性）、028（infra）

#ifndef NEO_INFRA_CONSOLE_H
#define NEO_INFRA_CONSOLE_H

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace neo::infra {

// 抽象输出目标
class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual void write(std::string_view text) = 0;
    virtual void write_line(std::string_view text) = 0;
    virtual void flush() = 0;
};

// 输出工具，通过 OutputSink* 注入
class Console {
    OutputSink* sink_;  // 不持有所有权
public:
    // sink=nullptr 时使用进程级默认 StdoutSink 单例
    explicit Console(OutputSink* sink = nullptr);

    // 无格式写
    void write(std::string_view text);
    void write_line(std::string_view text);

    // std::format 泛型格式化输出
    template <typename... Args>
    void print(std::format_string<Args...> fmt, Args&&... args) {
        sink_->write(std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void print_line(std::format_string<Args...> fmt, Args&&... args) {
        sink_->write_line(std::format(fmt, std::forward<Args>(args)...));
    }

    OutputSink* sink() const { return sink_; }
};

// 工厂：创建默认 stdout sink（具体实现 StdoutSink 在 private）
std::unique_ptr<OutputSink> create_stdout_sink();

}  // namespace neo::infra

#endif  // NEO_INFRA_CONSOLE_H
