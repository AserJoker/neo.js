// types/IntlCollator.cpp - Intl.Collator 实例生命周期

#include "core/types/IntlCollator.h"

#include "core/Value.h"

namespace neo::core {

IntlCollatorData::~IntlCollatorData() {
    if (collator != nullptr && backend != nullptr &&
        backend->collator_destroy != nullptr) {
        backend->collator_destroy(collator);
        collator = nullptr;
    }
}

void IntlCollatorData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* IntlCollatorData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<IntlCollatorData*>(this);
    self->refInc();
    return self;
}

IntlCollatorData* asIntlCollator(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::IntlCollator) return nullptr;
    return static_cast<IntlCollatorData*>(v->data());
}

}  // namespace neo::core
