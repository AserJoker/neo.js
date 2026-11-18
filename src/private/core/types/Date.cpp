// Date.cpp - DateData 内部槽访问与生命周期

#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/Date.h"

namespace neo::core {

DateData* asDate(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::Date) return nullptr;
    return static_cast<DateData*>(v->data());
}

void DateData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* DateData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<DateData*>(this);
    self->refInc();
    return self;
}

}  // namespace neo::core
