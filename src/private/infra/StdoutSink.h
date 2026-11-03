// StdoutSink.h (private) - StdoutSink 实现声明
//
// 本头仅在 infra 模块内部可见（src/private/），其他模块无法引用 StdoutSink，
// 只能通过 public 的 IOutputSink 接口 + createStdoutSink() 工厂使用。
// 文件名与实现类名一致（StdoutSink.h），Console.h 是具体类头（含 Console）。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装）

#ifndef NEO_INFRA_PRIVATE_STDOUT_SINK_H
#define NEO_INFRA_PRIVATE_STDOUT_SINK_H

#include "infra/IOutputSink.h"  // public 接口

namespace neo::infra {

// 真实实现：包 std::cout（FFI 调用在 .cpp 实现文件内）
class StdoutSink : public IOutputSink {
public:
    void write(std::string_view text) override;
    void writeLine(std::string_view text) override;
    void flush() override;
};

}  // namespace neo::infra

#endif  // NEO_INFRA_PRIVATE_STDOUT_SINK_H
