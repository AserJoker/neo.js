// SteadyClock.cpp - 默认时钟实现（FFI 收口：std::chrono）
//
// 本文件是 neo.js 中唯一直接使用 std::chrono::steady_clock 的地方（ADR-027 §5.6）。
// 业务代码通过 IClock 使用，测试注入假时钟控制时间。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装）

#include "infra/SteadyClock.h"

#include <chrono>

namespace neo::infra {

uint64_t SteadyClock::nowMs() const {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

IClock* defaultClock() {
    static SteadyClock instance;
    return &instance;
}

}  // namespace neo::infra
