// impl_console.h (private) - StdoutSink 实现声明
//
// 本头仅在 infra 模块内部可见（src/private/），其他模块无法引用 StdoutSink，
// 只能通过 public 的 OutputSink 接口 + create_stdout_sink() 工厂使用。
// 文件名加 impl_ 前缀，避免与 public/infra/console.h 同名产生 include 歧义。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装）

#ifndef NEO_INFRA_PRIVATE_IMPL_CONSOLE_H
#define NEO_INFRA_PRIVATE_IMPL_CONSOLE_H

#include "infra/console.h"  // public 接口（impl_ 前缀消除歧义）

namespace neo::infra {

// 真实实现：包 std::cout（FFI 调用在 .cpp 实现文件内）
class StdoutSink : public OutputSink {
public:
    void write(std::string_view text) override;
    void write_line(std::string_view text) override;
    void flush() override;
};

}  // namespace neo::infra

#endif  // NEO_INFRA_PRIVATE_CONSOLE_H
