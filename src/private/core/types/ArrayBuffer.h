// types/ArrayBuffer.h - ArrayBuffer 对象（ES2024 §25.1）
//
// ArrayBuffer 是普通对象 + 字节缓冲区（[[ArrayBufferData]] / [[ArrayBufferByteLength]]）。
// DataView/TypedArray 通过 Value* 引用它并直接读写 bytes。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）

#ifndef NEO_CORE_PRIVATE_TYPES_ARRAY_BUFFER_H
#define NEO_CORE_PRIVATE_TYPES_ARRAY_BUFFER_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"

#include <cstdint>
#include <vector>

namespace neo::core {

class Value;

class ArrayBufferType : public ObjectType {
public:
    ArrayBufferType() : ObjectType(TypeKind::ArrayBuffer) {}
    const char* name() const override { return "array buffer"; }
};

class ArrayBufferData : public ObjectData {
public:
    std::vector<uint8_t> bytes;
    bool detached = false;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

ArrayBufferData* asArrayBuffer(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_ARRAY_BUFFER_H
