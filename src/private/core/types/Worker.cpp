// types/Worker.cpp - Worker 实例生命周期

#include "core/types/Worker.h"

#include "core/Value.h"
#include "worker/IWorker.h"

namespace neo::core {

WorkerData::~WorkerData() {
    // 先让外部任务失效，再停止/等待 worker 线程。
    if (state) state->alive.store(false, std::memory_order_release);
    if (worker) {
        worker->requestStop();
        worker->join();
    }
}

void WorkerData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* WorkerData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<WorkerData*>(this);
    self->refInc();
    return self;
}

WorkerData* asWorker(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::Worker) {
        return nullptr;
    }
    return static_cast<WorkerData*>(v->data());
}

}  // namespace neo::core
