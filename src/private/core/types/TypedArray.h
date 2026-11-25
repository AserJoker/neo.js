// types/TypedArray.h - TypedArray 对象（ES2024 §25.2）
//
// 整数索引 exotic 对象：索引元素不存属性表，而是直接读写底层 ArrayBuffer。
// 复用 IType 的 indexed-elements 抽象（hasIndexedElements/indexedLength/
// indexedElement），因此 getOwnProperty/ownKeys/getProperty 的默认实现
// 已能正确处理索引与 length。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）

#ifndef NEO_CORE_PRIVATE_TYPES_TYPED_ARRAY_H
#define NEO_CORE_PRIVATE_TYPES_TYPED_ARRAY_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/IteratorKind.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"

#include <cstddef>
#include <cstdint>

namespace neo::core {

class Value;

class TypedArrayType : public ObjectType {
public:
    TypedArrayType() : ObjectType(TypeKind::TypedArray) {}
    const char* name() const override { return "typed array"; }

    bool hasIndexedElements() const override { return true; }
    bool hasIndexedLength(Value* self) const override;
    uint32_t indexedLength(Value* self) const override;
    Value* indexedElement(IContext* ctx, Value* self, uint32_t index) const override;
    bool indexedWritable(Value* self) const override;
    bool indexedConfigurable(Value* self) const override;
    bool indexedLengthWritable(Value* self) const override;

    // [[Set]]：规范数字索引串但非有效整数索引时静默返回 true（§10.4.5.5），
    // 避免 OrdinarySet 沿原型链在 receiver 上创建属性。
    Value* setWithReceiver(IContext* ctx, Value* self, const PropertyKey& key,
                           Value* value, Value* receiver) override;

    Value* defineProperty(IContext* ctx, Value* self, Value* key, Value* value,
                          bool writable, bool enumerable,
                          bool configurable) override;
};

class TypedArrayData : public ObjectData {
public:
    Value* buffer = nullptr;       // [[ViewedArrayBuffer]]
    size_t byteOffset = 0;         // [[ByteOffset]]
    size_t length = 0;             // [[ArrayLength]]（元素个数）
    TypedArrayKind kind = TypedArrayKind::Uint8;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

TypedArrayData* asTypedArray(Value* v);

// 元素大小 / 是否 BigInt 元素。
size_t typedArrayElementSize(TypedArrayKind kind);
bool typedArrayIsBigInt(TypedArrayKind kind);

// 读写单个元素。写入成功返回 nullptr，转换失败返回错误值。
Value* typedArrayReadElement(IContext* ctx, TypedArrayData* d, size_t index);
Value* typedArrayWriteElement(IContext* ctx, TypedArrayData* d, size_t index,
                              Value* value);

// 按元素类型转换一次（ToNumber / ToBigInt），返回转换后的值或错误值。
// 供 fill 等「值只转换一次」的算法复用。
Value* typedArrayConvertElement(IContext* ctx, TypedArrayKind kind, Value* value);

// 元素原始位与 JS 值互转（Atomics 原子操作复用同一套转换语义）。
Value* typedArrayRawToValue(IContext* ctx, TypedArrayKind kind, uint64_t raw);
Value* typedArrayValueToRaw(IContext* ctx, TypedArrayKind kind, Value* value,
                            uint64_t& raw);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_TYPED_ARRAY_H
