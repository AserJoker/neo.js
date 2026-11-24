// types/WeakRef.h - WeakRef 对象（ES2024 §26.1）
//
// WeakRef 是普通对象 + [[Target]] 内部槽。属性/原型语义复用 ObjectData。
// 当前以强引用保存 target（未接入弱引用 GC），仅实现可观察的 JS 语义。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）

#ifndef NEO_CORE_PRIVATE_TYPES_WEAKREF_H
#define NEO_CORE_PRIVATE_TYPES_WEAKREF_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"

namespace neo::core {

class Value;

class WeakRefType : public ObjectType {
public:
    WeakRefType() : ObjectType(TypeKind::WeakRef) {}
    const char* name() const override { return "weakref"; }
};

class WeakRefData : public ObjectData {
public:
    Value* target = nullptr;  // [[Target]]（nullptr = 已被清空）

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

WeakRefData* asWeakRef(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_WEAKREF_H
