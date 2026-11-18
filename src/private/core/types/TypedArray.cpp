// TypedArray.cpp - TypedArray 索引元素读写与生命周期
//
// 元素按宿主小端序存储（x86/ARM 一致）。数值转换遵循 §7.1.7/§7.1.8：
// 整数类型按 2^N 取模，Uint8Clamped 采用 round-half-to-even 后钳位。

#include "core/BigIntOps.h"
#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/Array.h"  // parseArrayIndex
#include "core/types/ArrayBuffer.h"
#include "core/types/Number.h"
#include "core/types/TypeBehavior.h"
#include "core/types/TypedArray.h"

#include <cmath>
#include <cstring>

namespace neo::core {

namespace {

uint64_t readRaw(const std::vector<uint8_t>& bytes, size_t index, size_t size) {
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) {
        value |= static_cast<uint64_t>(bytes[index + i]) << (8 * i);
    }
    return value;
}

void writeRaw(std::vector<uint8_t>& bytes, size_t index, size_t size,
              uint64_t value) {
    for (size_t i = 0; i < size; ++i) {
        bytes[index + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

}  // namespace

size_t typedArrayElementSize(TypedArrayKind kind) {
    switch (kind) {
        case TypedArrayKind::Int8:
        case TypedArrayKind::Uint8:
        case TypedArrayKind::Uint8Clamped: return 1;
        case TypedArrayKind::Int16:
        case TypedArrayKind::Uint16: return 2;
        case TypedArrayKind::Int32:
        case TypedArrayKind::Uint32:
        case TypedArrayKind::Float32: return 4;
        case TypedArrayKind::Float64:
        case TypedArrayKind::BigInt64:
        case TypedArrayKind::BigUint64: return 8;
    }
    return 1;
}

bool typedArrayIsBigInt(TypedArrayKind kind) {
    return kind == TypedArrayKind::BigInt64 || kind == TypedArrayKind::BigUint64;
}

TypedArrayData* asTypedArray(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::TypedArray) return nullptr;
    return static_cast<TypedArrayData*>(v->data());
}

Value* typedArrayReadElement(IContext* ctx, TypedArrayData* d, size_t index) {
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    if (buffer == nullptr || index >= d->length) return ctx->createUndefined();
    const size_t size = typedArrayElementSize(d->kind);
    const size_t pos = d->byteOffset + index * size;
    const uint64_t raw = readRaw(buffer->bytes, pos, size);
    switch (d->kind) {
        case TypedArrayKind::Int8:
            return ctx->createNumber(static_cast<double>(static_cast<int8_t>(raw)));
        case TypedArrayKind::Uint8:
        case TypedArrayKind::Uint8Clamped:
            return ctx->createNumber(static_cast<double>(static_cast<uint8_t>(raw)));
        case TypedArrayKind::Int16:
            return ctx->createNumber(static_cast<double>(static_cast<int16_t>(raw)));
        case TypedArrayKind::Uint16:
            return ctx->createNumber(static_cast<double>(static_cast<uint16_t>(raw)));
        case TypedArrayKind::Int32:
            return ctx->createNumber(static_cast<double>(static_cast<int32_t>(raw)));
        case TypedArrayKind::Uint32:
            return ctx->createNumber(static_cast<double>(static_cast<uint32_t>(raw)));
        case TypedArrayKind::Float32: {
            const uint32_t bits = static_cast<uint32_t>(raw);
            float f = 0.0f;
            std::memcpy(&f, &bits, sizeof f);
            return ctx->createNumber(static_cast<double>(f));
        }
        case TypedArrayKind::Float64: {
            double value = 0.0;
            std::memcpy(&value, &raw, sizeof value);
            return ctx->createNumber(value);
        }
        case TypedArrayKind::BigInt64:
            return ctx->createBigInt(
                neo::bigint::BigInt(static_cast<int64_t>(raw)));
        case TypedArrayKind::BigUint64:
            return ctx->createBigInt(neo::bigint::BigInt(raw));
    }
    return ctx->createUndefined();
}

Value* typedArrayWriteElement(IContext* ctx, TypedArrayData* d, size_t index,
                              Value* value) {
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    if (buffer == nullptr || index >= d->length) return nullptr;
    const size_t size = typedArrayElementSize(d->kind);
    const size_t pos = d->byteOffset + index * size;
    uint64_t raw = 0;

    if (typedArrayIsBigInt(d->kind)) {
        if (value->type()->kind() != TypeKind::BigInt) {
            return detail::typeError(ctx, "Cannot convert value to a BigInt");
        }
        raw = detail::bigIntValue(value).toUint64();
    } else {
        Value* nv = detail::toNumberValue(ctx, value);
        if (detail::isErrorValue(nv)) return nv;
        const double n = static_cast<NumberData*>(nv->data())->value;
        switch (d->kind) {
            case TypedArrayKind::Int8:
            case TypedArrayKind::Uint8: {
                const int64_t i = std::isfinite(n) ? static_cast<int64_t>(std::trunc(n))
                                                   : 0;
                raw = static_cast<uint64_t>(i) & 0xFFu;
                break;
            }
            case TypedArrayKind::Uint8Clamped: {
                double clamped = 0.0;
                if (std::isnan(n) || n <= 0.0) {
                    clamped = 0.0;
                } else if (n >= 255.0) {
                    clamped = 255.0;
                } else {
                    clamped = std::nearbyint(n);
                }
                raw = static_cast<uint64_t>(clamped);
                break;
            }
            case TypedArrayKind::Int16:
            case TypedArrayKind::Uint16: {
                const int64_t i = std::isfinite(n) ? static_cast<int64_t>(std::trunc(n))
                                                   : 0;
                raw = static_cast<uint64_t>(i) & 0xFFFFu;
                break;
            }
            case TypedArrayKind::Int32:
            case TypedArrayKind::Uint32: {
                const int64_t i = std::isfinite(n) ? static_cast<int64_t>(std::trunc(n))
                                                   : 0;
                raw = static_cast<uint64_t>(i) & 0xFFFFFFFFu;
                break;
            }
            case TypedArrayKind::Float32: {
                const float f = static_cast<float>(n);
                uint32_t bits = 0;
                std::memcpy(&bits, &f, sizeof bits);
                raw = bits;
                break;
            }
            case TypedArrayKind::Float64: {
                std::memcpy(&raw, &n, sizeof raw);
                break;
            }
            default: break;
        }
    }
    writeRaw(buffer->bytes, pos, size, raw);
    return nullptr;
}

// —— TypedArrayType 索引元素 ——

bool TypedArrayType::hasIndexedLength(Value* /*self*/) const { return true; }

uint32_t TypedArrayType::indexedLength(Value* self) const {
    TypedArrayData* d = asTypedArray(self);
    return d == nullptr ? 0 : static_cast<uint32_t>(d->length);
}

Value* TypedArrayType::indexedElement(IContext* ctx, Value* self,
                                      uint32_t index) const {
    TypedArrayData* d = asTypedArray(self);
    if (d == nullptr || index >= d->length) return nullptr;
    return typedArrayReadElement(ctx, d, index);
}

bool TypedArrayType::indexedWritable(Value* /*self*/) const { return true; }
bool TypedArrayType::indexedConfigurable(Value* /*self*/) const { return false; }
bool TypedArrayType::indexedLengthWritable(Value* /*self*/) const { return false; }

Value* TypedArrayType::defineProperty(IContext* ctx, Value* self, Value* key,
                                      Value* value, bool writable, bool enumerable,
                                      bool configurable) {
    TypedArrayData* d = asTypedArray(self);
    if (d != nullptr) {
        PropertyKey pk;
        if (Value* err = toPropertyKey(ctx, key, pk)) return err;
        if (!pk.isSymbol) {
            uint32_t index = 0;
            if (parseArrayIndex(pk.name, index)) {
                if (index >= d->length) return ctx->createBoolean(false);
                if (Value* err = typedArrayWriteElement(ctx, d, index, value)) {
                    return err;
                }
                return value;
            }
        }
    }
    return ObjectType::defineProperty(ctx, self, key, value, writable, enumerable,
                                      configurable);
}

// —— 生命周期 ——

void TypedArrayData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (buffer != nullptr) visitor->visit(buffer);
}

IData* TypedArrayData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<TypedArrayData*>(this);
    self->refInc();
    return self;
}

}  // namespace neo::core
