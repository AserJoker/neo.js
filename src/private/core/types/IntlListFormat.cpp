// types/IntlListFormat.cpp - Intl.ListFormat 实例生命周期

#include "core/types/IntlListFormat.h"

#include "core/Value.h"

namespace neo::core {

IntlListFormatData::~IntlListFormatData() {
    if (formatter != nullptr && backend != nullptr &&
        backend->list_format_destroy != nullptr) {
        backend->list_format_destroy(formatter);
        formatter = nullptr;
    }
}

void IntlListFormatData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* IntlListFormatData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<IntlListFormatData*>(this);
    self->refInc();
    return self;
}

IntlListFormatData* asIntlListFormat(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::IntlListFormat) {
        return nullptr;
    }
    return static_cast<IntlListFormatData*>(v->data());
}

}  // namespace neo::core
