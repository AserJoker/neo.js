// SteadyClock.h (private) - 默认时钟实现声明
//
// 本头仅在 infra 模块内部可见；外部通过 public 的 IClock 接口 + defaultClock() 使用。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装）

#ifndef NEO_INFRA_PRIVATE_STEADY_CLOCK_H
#define NEO_INFRA_PRIVATE_STEADY_CLOCK_H

#include "infra/IClock.h"

namespace neo::infra {

// 真实实现：包 std::chrono::steady_clock（FFI 调用在 .cpp 实现文件内）
class SteadyClock : public IClock {
public:
    uint64_t nowMs() const override;
};

}  // namespace neo::infra

#endif  // NEO_INFRA_PRIVATE_STEADY_CLOCK_H
