// DataView.cpp - DataViewData 内部槽访问与生命周期

#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/DataView.h"

namespace neo::core {

DataViewData* asDataView(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::DataView) return nullptr;
    return static_cast<DataViewData*>(v->data());
}

void DataViewData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (buffer != nullptr) visitor->visit(buffer);
}

IData* DataViewData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<DataViewData*>(this);
    self->refInc();
    return self;
}

}  // namespace neo::core
