// types/IntlRelativeTimeFormat.cpp - Intl.RelativeTimeFormat 实例生命周期

#include "core/types/IntlRelativeTimeFormat.h"

#include "core/Value.h"

namespace neo::core {

IntlRelativeTimeFormatData::~IntlRelativeTimeFormatData() {
    if (formatter != nullptr && backend != nullptr &&
        backend->relative_time_format_destroy != nullptr) {
        backend->relative_time_format_destroy(formatter);
        formatter = nullptr;
    }
}

void IntlRelativeTimeFormatData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* IntlRelativeTimeFormatData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<IntlRelativeTimeFormatData*>(this);
    self->refInc();
    return self;
}

IntlRelativeTimeFormatData* asIntlRelativeTimeFormat(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::IntlRelativeTimeFormat) {
        return nullptr;
    }
    return static_cast<IntlRelativeTimeFormatData*>(v->data());
}

}  // namespace neo::core
