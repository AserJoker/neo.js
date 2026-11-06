// Task.h - 事件循环任务对象
//
// 任务对象是事件循环的调度单元（非阻塞）：
//   id      —— 任务标识（用于 cancelTask）
//   start   —— 入队时的开始时间戳（毫秒，单调时钟）
//   timeout —— 预计等待时间（毫秒），到期时刻 = start + timeout
//   loop    —— 是否循环执行；为 true 时每次执行完成后以当前时刻重新入队尾
//   entity  —— 任务实体（当前阶段为 Function；后续 async handle 等复用此字段）
//   run     —— 任务执行器回调。默认实现 defaultTaskRunner 以 entity 为函数无参调用，
//              并把 Exception 打印为 "Uncaught ..." 后返回 undefined；Fatal 透传。
//
// 关联 ADR: 006（异常即值）、017 §4（Fatal 透传）、010（接口-实现-工厂）

#ifndef NEO_CORE_TASK_H
#define NEO_CORE_TASK_H

#include <cstdint>

namespace neo::core {

class IContext;
class Value;

// 任务执行器：ctx=语言上下文，task=任务实体，返回执行结果（正常/Exception/Fatal 同一通道）。
// 返回 nullptr 视为 undefined（由事件循环兜底）。
using TaskRunner = Value* (*)(IContext* ctx, Value* task);

struct Task {
    uint64_t id = 0;         // 任务标识（cancelTask 用）
    uint64_t start = 0;      // 开始时间戳（毫秒）
    uint64_t timeout = 0;    // 预计等待时间（毫秒）
    bool loop = false;       // 是否循环执行
    Value* entity = nullptr; // 任务实体
    TaskRunner run = nullptr;  // 执行器回调

    // 到期时刻（毫秒）。start + timeout 溢出在实际时间尺度上不可达。
    uint64_t due() const { return start + timeout; }
};

// 默认任务执行器：entity 必须是函数，以 undefined 为 this 无参调用。
//   - Exception：打印 "Uncaught <消息>"（当前阶段无 Error 对象，直接打印），返回 undefined；
//   - Fatal：原样透传（不可捕获，交给宿主）；
//   - 其他：返回函数返回值。
Value* defaultTaskRunner(IContext* ctx, Value* task);

}  // namespace neo::core

#endif  // NEO_CORE_TASK_H
