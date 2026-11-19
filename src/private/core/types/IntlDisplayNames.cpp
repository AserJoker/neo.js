// types/IntlDisplayNames.cpp - Intl.DisplayNames 实例生命周期

#include "core/types/IntlDisplayNames.h"

#include "core/Value.h"

namespace neo::core {

IntlDisplayNamesData::~IntlDisplayNamesData() {
    if (names != nullptr && backend != nullptr &&
        backend->display_names_destroy != nullptr) {
        backend->display_names_destroy(names);
        names = nullptr;
    }
}

void IntlDisplayNamesData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* IntlDisplayNamesData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<IntlDisplayNamesData*>(this);
    self->refInc();
    return self;
}

IntlDisplayNamesData* asIntlDisplayNames(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::IntlDisplayNames) {
        return nullptr;
    }
    return static_cast<IntlDisplayNamesData*>(v->data());
}

}  // namespace neo::core
