// types/AsyncGenerator.h - AsyncGeneratorType + AsyncGeneratorData
//
// 异步生成器（ES2024 §27.6 / ADR-045 §8）：Coroutine + Promise 请求队列。
// 对象布局/属性/原型/GC 复用 CoroutineData（继承），内部槽增加：
//
//   - asyncState      AsyncGeneratorState（SuspendedStart/SuspendedYield/
//                     Executing/AwaitingReturn/Completed）
//   - queue           待处理请求队列（next/return/throw 的 capability）
//   - currentResolve  正在处理的请求的 resolve（settle 后清空）
//   - currentReject   正在处理的请求的 reject
//
// 每个请求携带 {type, value, promise, resolve, reject}；队列串行化并发调用。
// 异步驱动（AsyncGeneratorBuiltins.cpp）负责 resume、await yield 值、settle。
//
// 关联 ADR: 043（Promise）、045（协程抽象）、046（Interrupt）

#ifndef NEO_CORE_PRIVATE_TYPES_ASYNC_GENERATOR_H
#define NEO_CORE_PRIVATE_TYPES_ASYNC_GENERATOR_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Coroutine.h"  // AsyncGeneratorData 继承 CoroutineData
#include "infra/IAllocator.h"

#include <cstdint>
#include <vector>

namespace neo::core {

class Value;

// 异步生成器协议状态（ES2024 §27.6.3）
enum class AsyncGeneratorState : uint8_t {
    SuspendedStart,
    SuspendedYield,
    Executing,
    AwaitingYield,   // yield 值正在 await（请求尚未 settle）
    AwaitingReturn,  // 完成/return 值正在 await
    Completed,
};

enum class AsyncGeneratorRequestType : uint8_t {
    Next,
    Return,
    Throw,
};

// AsyncGeneratorRequest Record（§27.6.3）：completion + capability
struct AsyncGeneratorRequest {
    AsyncGeneratorRequestType type = AsyncGeneratorRequestType::Next;
    Value* value = nullptr;    // completion 值（next 实参 / return 值 / throw 原因）
    Value* promise = nullptr;  // 本次请求返回的 promise
    Value* resolve = nullptr;  // capability.resolve
    Value* reject = nullptr;   // capability.reject
};

class AsyncGeneratorType : public CoroutineType {
public:
    AsyncGeneratorType() : CoroutineType(TypeKind::AsyncGenerator) {}
    const char* name() const override { return "async generator"; }
};

// 异步生成器负载：继承 CoroutineData 复用协程上下文/作用域/GC。
class AsyncGeneratorData : public CoroutineData {
public:
    AsyncGeneratorState asyncState = AsyncGeneratorState::SuspendedStart;
    std::vector<AsyncGeneratorRequest> queue;
    Value* currentResolve = nullptr;
    Value* currentReject = nullptr;

    ~AsyncGeneratorData() override = default;

    // GC 遍历：协程字段 + 队列/当前请求持有的 capability 与 completion 值
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

// 内部槽访问：非 AsyncGenerator 返回 nullptr。
AsyncGeneratorData* asAsyncGenerator(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_ASYNC_GENERATOR_H
