// types/IntlSegments.cpp - Segments 对象生命周期

#include "core/types/IntlSegments.h"

#include "core/IGCEngine.h"
#include "core/Value.h"

namespace neo::core {

IntlSegmentsData::~IntlSegmentsData() = default;

void IntlSegmentsData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (records != nullptr) visitor->visit(records);
}

IData* IntlSegmentsData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<IntlSegmentsData*>(this);
    self->refInc();
    return self;
}

IntlSegmentsData* asIntlSegments(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::IntlSegments) {
        return nullptr;
    }
    return static_cast<IntlSegmentsData*>(v->data());
}

}  // namespace neo::core
