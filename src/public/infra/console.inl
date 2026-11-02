// console.inl - Console 模板实现
//
// 关联 ADR: 028（infra）

#ifndef NEO_INFRA_CONSOLE_INL
#define NEO_INFRA_CONSOLE_INL

#include <utility>

namespace neo::infra {

template <typename... Args>
void Console::print(std::format_string<Args...> fmt, Args&&... args) {
    sink_->write(std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Console::print_line(std::format_string<Args...> fmt, Args&&... args) {
    sink_->write_line(std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace neo::infra

#endif  // NEO_INFRA_CONSOLE_INL
