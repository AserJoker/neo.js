// types/MessagePort.cpp - MessagePort 实例生命周期

#include "core/types/MessagePort.h"

#include "core/Value.h"
#include "worker/Port.h"

namespace neo::core {

MessagePortData::~MessagePortData() {
    if (wake) wake->alive.store(false, std::memory_order_release);
    // 已转移的端点归接收方所有；未转移的端点随本对象关闭。
    if (endpoint && !detached) endpoint->close();
}

void MessagePortData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* MessagePortData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<MessagePortData*>(this);
    self->refInc();
    return self;
}

MessagePortData* asMessagePort(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::MessagePort) {
        return nullptr;
    }
    return static_cast<MessagePortData*>(v->data());
}

}  // namespace neo::core
