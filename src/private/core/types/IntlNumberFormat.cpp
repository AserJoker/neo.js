// types/IntlNumberFormat.cpp - Intl.NumberFormat 实例生命周期

#include "core/types/IntlNumberFormat.h"

#include "core/Value.h"

namespace neo::core {

IntlNumberFormatData::~IntlNumberFormatData() {
    if (formatter != nullptr && backend != nullptr &&
        backend->formatter_destroy != nullptr) {
        backend->formatter_destroy(formatter);
        formatter = nullptr;
    }
}

void IntlNumberFormatData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* IntlNumberFormatData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<IntlNumberFormatData*>(this);
    self->refInc();
    return self;
}

IntlNumberFormatData* asIntlNumberFormat(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::IntlNumberFormat) {
        return nullptr;
    }
    return static_cast<IntlNumberFormatData*>(v->data());
}

}  // namespace neo::core
