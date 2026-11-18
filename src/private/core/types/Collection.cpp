// Collection.cpp - 集合 Data 的 GC 遍历与内部槽访问

#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/Collection.h"

namespace neo::core {

CollectionData* asCollection(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::Collection) return nullptr;
    return static_cast<CollectionData*>(v->data());
}

CollectionIteratorData* asCollectionIterator(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::CollectionIterator) return nullptr;
    return static_cast<CollectionIteratorData*>(v->data());
}

void CollectionData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    for (const CollectionEntry& entry : entries) {
        if (entry.key != nullptr) visitor->visit(entry.key);
        if (entry.value != nullptr) visitor->visit(entry.value);
    }
}

IData* CollectionData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<CollectionData*>(this);
    self->refInc();
    return self;
}

void CollectionIteratorData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (collection != nullptr) visitor->visit(collection);
}

IData* CollectionIteratorData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<CollectionIteratorData*>(this);
    self->refInc();
    return self;
}

}  // namespace neo::core
