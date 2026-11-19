// types/IntlLocale.cpp - Intl.Locale 实例生命周期

#include "core/types/IntlLocale.h"

#include "core/Value.h"

namespace neo::core {

void IntlLocaleData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* IntlLocaleData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<IntlLocaleData*>(this);
    self->refInc();
    return self;
}

IntlLocaleData* asIntlLocale(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::IntlLocale) {
        return nullptr;
    }
    return static_cast<IntlLocaleData*>(v->data());
}

}  // namespace neo::core
