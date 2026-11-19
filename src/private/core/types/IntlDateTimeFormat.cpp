// types/IntlDateTimeFormat.cpp - Intl.DateTimeFormat 实例生命周期

#include "core/types/IntlDateTimeFormat.h"

#include "core/Value.h"

namespace neo::core {

IntlDateTimeFormatData::~IntlDateTimeFormatData() {
    if (formatter != nullptr && backend != nullptr &&
        backend->formatter_destroy != nullptr) {
        backend->formatter_destroy(formatter);
        formatter = nullptr;
    }
}

void IntlDateTimeFormatData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* IntlDateTimeFormatData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<IntlDateTimeFormatData*>(this);
    self->refInc();
    return self;
}

IntlDateTimeFormatData* asIntlDateTimeFormat(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::IntlDateTimeFormat) {
        return nullptr;
    }
    return static_cast<IntlDateTimeFormatData*>(v->data());
}

}  // namespace neo::core
