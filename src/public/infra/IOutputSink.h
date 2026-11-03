// IOutputSink.h - 输出目标抽象接口（FFI 收口：std::cout）
//
// IOutputSink 抽象输出目标，Console 持 IOutputSink* 注入。
// 具体实现 StdoutSink 在 private，通过工厂 create_stdout_sink() 获取。
// 测试注入 StringSink（自定义 mock sink）截获输出验证。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装/可测试性）、028（infra）

#ifndef NEO_INFRA_IOUTPUTSINK_H
#define NEO_INFRA_IOUTPUTSINK_H

#include <memory>
#include <string_view>

namespace neo::infra {

// 抽象输出目标
class IOutputSink {
public:
    virtual ~IOutputSink() = default;
    virtual void write(std::string_view text) = 0;
    virtual void write_line(std::string_view text) = 0;
    virtual void flush() = 0;
};

// 工厂：创建默认 stdout sink（具体实现 StdoutSink 在 private）
std::unique_ptr<IOutputSink> create_stdout_sink();

}  // namespace neo::infra

#endif  // NEO_INFRA_IOUTPUTSINK_H
