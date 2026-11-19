// types/IntlSegmenter.cpp - Intl.Segmenter 实例生命周期

#include "core/types/IntlSegmenter.h"

#include "core/Value.h"

namespace neo::core {

IntlSegmenterData::~IntlSegmenterData() {
    if (segmenter != nullptr && backend != nullptr &&
        backend->segmenter_destroy != nullptr) {
        backend->segmenter_destroy(segmenter);
        segmenter = nullptr;
    }
}

void IntlSegmenterData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
}

IData* IntlSegmenterData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<IntlSegmenterData*>(this);
    self->refInc();
    return self;
}

IntlSegmenterData* asIntlSegmenter(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::IntlSegmenter) {
        return nullptr;
    }
    return static_cast<IntlSegmenterData*>(v->data());
}

}  // namespace neo::core
