// TimerBuiltins.h (private) - setTimeout / setInterval / clearTimeout / clearInterval 注册
//
// 定时器是事件循环的宿主侧入口（HTML Timers 语义）：
//   setTimeout(fn, delay, ...args)    → 宏任务，执行一次
//   setInterval(fn, delay, ...args)   → 循环宏任务，每次执行后以当前时刻重新入队
//   clearTimeout(id) / clearInterval(id) → 按 id 取消（两者等价，可互换）
//
// 定时器 id 直接复用事件循环的任务 id（单调递增，从 1 开始），
// 因此取消就是 cancelTask；未知 id 静默忽略（与浏览器一致）。
//
// 额外参数经 createBoundFunction 绑定到回调（this = undefined），
// 复用既有的绑定函数机制，不额外扩展 Task。
//
// 关联 ADR: 020（标准库组织）、006（异常即值）、017 §4（Fatal 透传）

#ifndef NEO_CORE_PRIVATE_TIMER_BUILTINS_H
#define NEO_CORE_PRIVATE_TIMER_BUILTINS_H

namespace neo::core {

class IContext;

// 把 setTimeout/setInterval/clearTimeout/clearInterval 注册到 global。
// 必须在 Context 构造末尾、事件循环可用之后调用。失败返回 false（调用方 abort）。
bool registerTimerBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TIMER_BUILTINS_H
