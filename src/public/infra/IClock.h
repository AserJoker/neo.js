// IClock.h - 单调时钟抽象（事件循环的时间源）
//
// 事件循环用毫秒时间戳判断任务是否到期。时钟是外部副作用（FFI），
// 收口到 IClock 接口：生产用 steady_clock 实现，测试注入假时钟精确控制时间。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装/可测试性）

#ifndef NEO_INFRA_ICLOCK_H
#define NEO_INFRA_ICLOCK_H

#include <cstdint>

namespace neo::infra {

// 单调时钟（毫秒）。nowMs 只增不减，与系统墙钟无关，适合调度。
class IClock {
public:
    virtual ~IClock() = default;
    virtual uint64_t nowMs() const = 0;
};

// 进程级默认时钟（std::chrono::steady_clock 毫秒），生命周期与进程一致。
// 测试可自行实现 IClock 并注入 createContext。
IClock* defaultClock();

}  // namespace neo::infra

#endif  // NEO_INFRA_ICLOCK_H
