// types/DataView.h - DataView 对象（ES2024 §25.3）
//
// DataView 是普通对象 + 对 ArrayBuffer 的窗口（[[ViewedArrayBuffer]] /
// [[ByteOffset]] / [[ByteLength]]）。字节读写直接落在 buffer 的 bytes 上。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）

#ifndef NEO_CORE_PRIVATE_TYPES_DATA_VIEW_H
#define NEO_CORE_PRIVATE_TYPES_DATA_VIEW_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"

#include <cstddef>

namespace neo::core {

class Value;

class DataViewType : public ObjectType {
public:
    DataViewType() : ObjectType(TypeKind::DataView) {}
    const char* name() const override { return "data view"; }
};

class DataViewData : public ObjectData {
public:
    Value* buffer = nullptr;   // [[ViewedArrayBuffer]]
    size_t byteOffset = 0;     // [[ByteOffset]]
    size_t byteLength = 0;     // [[ByteLength]]

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

DataViewData* asDataView(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_DATA_VIEW_H
