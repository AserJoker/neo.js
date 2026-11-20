// ArrayBuffer.cpp - ArrayBufferData 内部槽访问与生命周期

#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/ArrayBuffer.h"

namespace neo::core {

ArrayBufferData* asArrayBuffer(Value* v) {
    if (v == nullptr) return nullptr;
    TypeKind kind = v->type()->kind();
    if (kind != TypeKind::ArrayBuffer && kind != TypeKind::SharedArrayBuffer) {
        return nullptr;
    }
    return static_cast<ArrayBufferData*>(v->data());
}

ArrayBufferData* asSharedArrayBuffer(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::SharedArrayBuffer) {
        return nullptr;
    }
    return static_cast<ArrayBufferData*>(v->data());
}

void ArrayBufferData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* ArrayBufferData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<ArrayBufferData*>(this);
    self->refInc();
    return self;
}

}  // namespace neo::core
