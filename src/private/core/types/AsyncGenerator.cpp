// AsyncGenerator.cpp - AsyncGeneratorData GC/clone + 内部槽访问
//
// Type 行为复用 CoroutineType（见 Coroutine.cpp），本文件只实现数据层。
//
// 关联 ADR: 015（data 持子 value）、019（赋值语义）、043（Promise）、045（协程抽象）

#include "core/types/AsyncGenerator.h"

#include "core/IGCEngine.h"
#include "core/Value.h"

namespace neo::core {

void AsyncGeneratorData::enumerateChildren(IGCVisitor* visitor) {
    CoroutineData::enumerateChildren(visitor);
    // 队列中的每个请求：completion 值 + capability 三元组
    for (const AsyncGeneratorRequest& r : queue) {
        if (r.value != nullptr) visitor->visit(r.value);
        if (r.promise != nullptr) visitor->visit(r.promise);
        if (r.resolve != nullptr) visitor->visit(r.resolve);
        if (r.reject != nullptr) visitor->visit(r.reject);
    }
    if (currentResolve != nullptr) visitor->visit(currentResolve);
    if (currentReject != nullptr) visitor->visit(currentReject);
}

IData* AsyncGeneratorData::clone(neo::infra::IAllocator* /*alloc*/) const {
    // 引用语义（ADR-019 §2）：clone 不复制容器，ref++ 后返回 this。
    auto* self = const_cast<AsyncGeneratorData*>(this);
    self->refInc();
    return self;
}

AsyncGeneratorData* asAsyncGenerator(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::AsyncGenerator) return nullptr;
    return static_cast<AsyncGeneratorData*>(v->data());
}

}  // namespace neo::core
