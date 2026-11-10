// PromiseCapability.h - NewPromiseCapability 内部辅助（ES2024 §27.2.1.5）
//
// 供 Promise 内建以外的异步机制（AsyncGenerator 等）创建 {promise, resolve, reject}。
// PromiseCapability 结构定义在 types/Promise.h。
//
// 关联 ADR: 043（Promise）、045（协程抽象）

#ifndef NEO_CORE_PRIVATE_PROMISE_CAPABILITY_H
#define NEO_CORE_PRIVATE_PROMISE_CAPABILITY_H

#include "core/types/Promise.h"

namespace neo::core {

class IContext;
class Value;

// 以 ctor（通常是 %Promise% 内建构造函数）创建 capability。
// 成功返回 nullptr 并填充 out；失败返回 Exception/Fatal。
Value* createPromiseCapability(IContext* ctx, Value* ctor, PromiseCapability& out);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_PROMISE_CAPABILITY_H
