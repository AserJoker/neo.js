// types/Collection.h - 集合对象与集合迭代器
//
// Map/Set/WeakMap/WeakSet 共用一套对象布局（ES2024 §24.1/§24.2/§24.3）：
// 差异只在 CollectionData::kind 与原型链，方法族由内建注册决定。
// 条目按插入顺序保存在 vector 中，键查找使用 SameValueZero。
//
// 说明：WeakMap/WeakSet 当前以强引用保存键（未接入弱引用 GC），
// 仅实现可观察的 JS 语义。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）

#ifndef NEO_CORE_PRIVATE_TYPES_COLLECTION_H
#define NEO_CORE_PRIVATE_TYPES_COLLECTION_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/IteratorKind.h"
#include "core/types/Iterator.h"  // IteratorObjectType
#include "core/types/Object.h"    // ObjectData
#include "infra/IAllocator.h"

#include <cstdint>
#include <vector>

namespace neo::core {

class Value;

struct CollectionEntry {
    Value* key = nullptr;
    Value* value = nullptr;
};

class CollectionType : public ObjectType {
public:
    CollectionType() : ObjectType(TypeKind::Collection) {}
    const char* name() const override { return "collection"; }
};

class CollectionData : public ObjectData {
public:
    CollectionKind kind = CollectionKind::Map;
    std::vector<CollectionEntry> entries;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

class CollectionIteratorType : public IteratorObjectType {
public:
    CollectionIteratorType() : IteratorObjectType(TypeKind::CollectionIterator) {}
    const char* name() const override { return "collection iterator"; }
};

class CollectionIteratorData : public ObjectData {
public:
    Value* collection = nullptr;  // [[IteratedCollection]]（nullptr = 已耗尽）
    uint32_t index = 0;           // 下一个条目下标
    CollectionIterationKind kind = CollectionIterationKind::Value;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

CollectionData* asCollection(Value* v);
CollectionIteratorData* asCollectionIterator(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_COLLECTION_H
