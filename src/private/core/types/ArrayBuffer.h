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
#include "core/SharedBuffer.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace neo::core {

class Value;

class ArrayBufferType : public ObjectType {
public:
    ArrayBufferType() : ObjectType(TypeKind::ArrayBuffer) {}
    const char* name() const override { return "array buffer"; }
};

// SharedArrayBuffer 与 ArrayBuffer 共用数据布局：非共享时用 bytes，共享时用
// shared。两者 TypeKind 不同，以区分原型与 transfer 语义（SAB 不可转移/分离）。
class SharedArrayBufferType : public ObjectType {
public:
    SharedArrayBufferType() : ObjectType(TypeKind::SharedArrayBuffer) {}
    const char* name() const override { return "shared array buffer"; }
};

class ArrayBufferData : public ObjectData {
public:
    std::vector<uint8_t> bytes;                 // 非共享缓冲区
    std::shared_ptr<SharedBufferBlock> shared;  // SharedArrayBuffer 专用
    bool detached = false;

    bool isShared() const { return shared != nullptr; }
    size_t byteLength() const {
        return shared != nullptr ? shared->byteLength() : bytes.size();
    }
    uint8_t* data() {
        return shared != nullptr ? shared->data() : bytes.data();
    }
    const uint8_t* data() const {
        return shared != nullptr ? shared->data() : bytes.data();
    }

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

// ArrayBuffer 或 SharedArrayBuffer（视图读写走 byteLength()/data()）。
ArrayBufferData* asArrayBuffer(Value* v);
// 仅 SharedArrayBuffer。
ArrayBufferData* asSharedArrayBuffer(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_ARRAY_BUFFER_H
