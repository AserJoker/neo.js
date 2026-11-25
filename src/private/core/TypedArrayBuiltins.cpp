// TypedArrayBuiltins.cpp - TypedArray 构造函数与原型方法（ES2024 §25.2）
//
// 设计要点：
//   - 11 种元素类型共用 TypeKind::TypedArray 与 %TypedArray.prototype%，
//     各构造函数有自己的原型对象（[[Prototype]] = %TypedArray.prototype%），
//     提供 BYTES_PER_ELEMENT 与 @@toStringTag；
//   - 元素读写统一走 types/TypedArray.cpp 的 typedArrayReadElement /
//     typedArrayWriteElement（含整数取模、Uint8Clamped 舍入、BigInt 校验）；
//   - 迭代器复用 createArrayIterator（按 length + 索引 live 读取）；
//   - 暂不实现 @@species / 子类构造（from/of 固定按 this 的 kind 创建基类实例）。
//
// 关联 ADR: 020（标准库组织）、006（异常即值）

#include "core/TypedArrayBuiltins.h"

#include "core/BigIntOps.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/ArrayBuffer.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypedArray.h"
#include "core/types/TypeBehavior.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace neo::core {

namespace {

constexpr double kMaxLength = 9007199254740991.0;  // 2^53 - 1

const struct KindInfo {
    const char* name;
    TypedArrayKind kind;
    uint32_t bytes;
} kKinds[] = {
    {"Int8Array", TypedArrayKind::Int8, 1},
    {"Uint8Array", TypedArrayKind::Uint8, 1},
    {"Uint8ClampedArray", TypedArrayKind::Uint8Clamped, 1},
    {"Int16Array", TypedArrayKind::Int16, 2},
    {"Uint16Array", TypedArrayKind::Uint16, 2},
    {"Int32Array", TypedArrayKind::Int32, 4},
    {"Uint32Array", TypedArrayKind::Uint32, 4},
    {"Float32Array", TypedArrayKind::Float32, 4},
    {"Float64Array", TypedArrayKind::Float64, 8},
    {"BigInt64Array", TypedArrayKind::BigInt64, 8},
    {"BigUint64Array", TypedArrayKind::BigUint64, 8},
};

constexpr size_t kKindCount = sizeof(kKinds) / sizeof(kKinds[0]);

bool isUndefined(Value* v) { return v->type()->kind() == TypeKind::Undefined; }

// ToIndex（§7.1.22）：先 ToIntegerOrInfinity（NaN → 0，-0.1 → -0），再检查范围。
Value* toIndex(IContext* ctx, Value* v, size_t& out) {
    Value* nv = detail::toNumberValue(ctx, v);
    if (detail::isErrorValue(nv)) return nv;
    const double n = static_cast<NumberData*>(nv->data())->value;
    const double integer = std::isnan(n) ? 0.0 : std::trunc(n);
    if (integer < 0.0 || integer > kMaxLength) {
        return detail::rangeError(ctx, "Invalid typed array length");
    }
    out = static_cast<size_t>(integer);
    return nullptr;
}

// ToIntegerOrInfinity（§7.1.5）：NaN → 0，其余截断。
Value* toIntegerOrInfinity(IContext* ctx, Value* v, double& out) {
    Value* nv = detail::toNumberValue(ctx, v);
    if (detail::isErrorValue(nv)) return nv;
    const double n = static_cast<NumberData*>(nv->data())->value;
    out = std::isnan(n) ? 0.0 : std::trunc(n);
    return nullptr;
}

// 相对索引（§23.1.3.13 等）：负值从末尾数，结果夹到 [0, len]。
size_t relativeIndex(double d, size_t len) {
    if (d < 0.0) {
        const double fromEnd = static_cast<double>(len) + d;
        return fromEnd <= 0.0 ? 0 : static_cast<size_t>(fromEnd);
    }
    if (d >= static_cast<double>(len)) return len;
    return static_cast<size_t>(d);
}

// ValidateTypedArray（§7.3.24）：非 TypedArray 或底层缓冲区已分离 → TypeError。
Value* requireTypedArray(IContext* ctx, Value* self, TypedArrayData*& out) {
    out = asTypedArray(self);
    if (out == nullptr) {
        return detail::typeError(ctx,
                                 "TypedArray.prototype method called on incompatible receiver");
    }
    ArrayBufferData* buffer = asArrayBuffer(out->buffer);
    if (buffer == nullptr || buffer->detached) {
        return detail::typeError(ctx, "TypedArray buffer is detached");
    }
    return nullptr;
}

bool isDetached(TypedArrayData* d) {
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    return buffer == nullptr || buffer->detached;
}

const char* kindName(TypedArrayKind kind) {
    for (size_t i = 0; i < kKindCount; ++i) {
        if (kKinds[i].kind == kind) return kKinds[i].name;
    }
    return "";
}

Value* makeTypedArray(IContext* ctx, TypedArrayKind kind, size_t length) {
    const size_t elemSize = typedArrayElementSize(kind);
    Value* buffer = ctx->createArrayBuffer(length * elemSize);
    if (detail::isErrorValue(buffer)) return buffer;
    return ctx->createTypedArray(buffer, 0, length, kind);
}

// 收集可迭代对象 / 类数组对象的元素（§7.4.2 / §23.1.3.30）。
Value* collectElements(IContext* ctx, Value* src, std::vector<Value*>& out) {
    TypeKind k = src->type()->kind();
    if (k == TypeKind::Undefined || k == TypeKind::Null) {
        return detail::typeError(ctx, "Cannot convert undefined or null to object");
    }
    Value* iterFn =
        ctx->getProperty(src, ctx->getWellKnownSymbol(WellKnownSymbol::Iterator));
    if (detail::isErrorValue(iterFn)) return iterFn;
    if (detail::isCallable(iterFn)) {
        return detail::iterableToList(ctx, src, out);
    }
    uint32_t len = 0;
    if (Value* err = detail::lengthOf(ctx, src, len)) return err;
    out.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
        Value* v = ctx->getProperty(src, ctx->createString(std::to_string(i)));
        if (detail::isErrorValue(v)) return v;
        out.push_back(v);
    }
    return nullptr;
}

// 按 kind 把元素列表写入新建的 TypedArray。
Value* fromElements(IContext* ctx, TypedArrayKind kind,
                    const std::vector<Value*>& items) {
    Value* result = makeTypedArray(ctx, kind, items.size());
    if (detail::isErrorValue(result)) return result;
    TypedArrayData* d = asTypedArray(result);
    for (size_t i = 0; i < items.size(); ++i) {
        if (Value* err = typedArrayWriteElement(ctx, d, i, items[i])) return err;
    }
    return result;
}

// SpeciesConstructor（§7.3.20）：defaultCtor 为 exemplar 类型的内建构造器。
Value* typedArraySpeciesConstructor(IContext* ctx, Value* exemplar,
                                    Value* defaultCtor) {
    Value* c = ctx->getProperty(exemplar, "constructor");
    if (detail::isErrorValue(c)) return c;
    if (c->type()->kind() == TypeKind::Undefined) return defaultCtor;
    if (!detail::isObjectKind(c->type()->kind())) {
        return detail::typeError(ctx, "constructor property is not an object");
    }
    Value* s =
        ctx->getProperty(c, ctx->getWellKnownSymbol(WellKnownSymbol::Species));
    if (detail::isErrorValue(s)) return s;
    TypeKind sk = s->type()->kind();
    if (sk == TypeKind::Null || sk == TypeKind::Undefined) return defaultCtor;
    if (!detail::isConstructor(s)) {
        return detail::typeError(ctx, "@@species is not a constructor");
    }
    return s;
}

// 内建构造器按 [[TypedArrayName]] 从 global 取，不能用 prototype.constructor：
// 后者可被用户改写为 getter（§23.2.4.7 的 defaultConstructor 是固有对象）。
Value* typedArrayDefaultCtor(IContext* ctx, TypedArrayKind kind) {
    return ctx->global()->lookup(kindName(kind));
}

// TypedArrayCreate（§23.2.4.6）：Construct 后校验结果有内部槽且未分离。
Value* typedArrayCreate(IContext* ctx, Value* ctor, size_t argc, Value* argv[],
                        Value*& resultOut, TypedArrayData*& out) {
    Value* result = ctx->construct(ctor, argc, argv);
    if (detail::isErrorValue(result)) return result;
    out = asTypedArray(result);
    if (out == nullptr) {
        return detail::typeError(ctx,
                                 "species constructor did not return a TypedArray");
    }
    if (isDetached(out)) {
        return detail::typeError(ctx, "species constructor returned detached buffer");
    }
    resultOut = result;
    return nullptr;
}

// TypedArraySpeciesCreate(exemplar, « count »)（§23.2.4.7）。
Value* typedArraySpeciesCreate(IContext* ctx, Value* exemplar,
                               TypedArrayKind kind, size_t count,
                               Value*& resultOut, TypedArrayData*& out) {
    Value* defaultCtor = typedArrayDefaultCtor(ctx, kind);
    if (defaultCtor == nullptr) {
        return detail::typeError(ctx, "TypedArray constructor is not defined");
    }
    if (detail::isErrorValue(defaultCtor)) return defaultCtor;
    Value* ctor = typedArraySpeciesConstructor(ctx, exemplar, defaultCtor);
    if (detail::isErrorValue(ctor)) return ctor;
    Value* countValue = ctx->createNumber(static_cast<double>(count));
    if (countValue == ctx->oomValue()) return countValue;
    if (Value* err = typedArrayCreate(ctx, ctor, 1, &countValue, resultOut, out)) {
        return err;
    }
    if (out->length < count) {
        return detail::typeError(ctx, "species constructor returned too short a TypedArray");
    }
    return nullptr;
}

// —— 构造函数 ——

Value* typedArrayCtorCall(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                          Value* /*argv*/[]) {
    return detail::typeError(ctx, "Constructor TypedArray requires 'new'");
}

Value* typedArrayConstruct(IContext* ctx, TypedArrayKind kind, size_t argc,
                           Value* argv[]) {
    const size_t elemSize = typedArrayElementSize(kind);
    if (argc == 0) return makeTypedArray(ctx, kind, 0);

    Value* first = argv[0];
    TypeKind firstKind = first->type()->kind();
    if (firstKind == TypeKind::ArrayBuffer ||
        firstKind == TypeKind::SharedArrayBuffer) {
        ArrayBufferData* buffer = asArrayBuffer(first);
        if (buffer == nullptr || buffer->detached) {
            return detail::typeError(ctx, "Cannot construct on a detached ArrayBuffer");
        }
        size_t offset = 0;
        if (argc > 1 && !isUndefined(argv[1])) {
            if (Value* err = toIndex(ctx, argv[1], offset)) return err;
        }
        if (offset % elemSize != 0) {
            return detail::rangeError(ctx, "Start offset is not a multiple of element size");
        }
        const size_t bufferLength = buffer->byteLength();
        if (offset > bufferLength) {
            return detail::rangeError(ctx, "Start offset is outside the bounds of the buffer");
        }
        size_t length = (bufferLength - offset) / elemSize;
        if (argc > 2 && !isUndefined(argv[2])) {
            if (Value* err = toIndex(ctx, argv[2], length)) return err;
            if (offset + length * elemSize > bufferLength) {
                return detail::rangeError(ctx, "Invalid typed array length");
            }
        }
        return ctx->createTypedArray(first, offset, length, kind);
    }

    if (detail::isObjectKind(first->type()->kind())) {
        // TypedArray 源按内部槽复制（§23.2.5.1），不触发 @@iterator / length。
        TypedArrayData* src = asTypedArray(first);
        if (src != nullptr) {
            if (isDetached(src)) {
                return detail::typeError(ctx, "Source TypedArray buffer is detached");
            }
            std::vector<Value*> items;
            items.reserve(src->length);
            for (size_t i = 0; i < src->length; ++i) {
                Value* v = typedArrayReadElement(ctx, src, i);
                if (detail::isErrorValue(v)) return v;
                items.push_back(v);
            }
            return fromElements(ctx, kind, items);
        }
        std::vector<Value*> items;
        if (Value* err = collectElements(ctx, first, items)) return err;
        return fromElements(ctx, kind, items);
    }

    size_t length = 0;
    if (Value* err = toIndex(ctx, first, length)) return err;
    return makeTypedArray(ctx, kind, length);
}

#define NEO_TA_CONSTRUCT(NAME, KIND)                                           \
    Value* NAME##Construct(IContext* ctx, Value* /*self*/, size_t argc,        \
                           Value* argv[]) {                                    \
        return typedArrayConstruct(ctx, TypedArrayKind::KIND, argc, argv);     \
    }

NEO_TA_CONSTRUCT(Int8Array, Int8)
NEO_TA_CONSTRUCT(Uint8Array, Uint8)
NEO_TA_CONSTRUCT(Uint8ClampedArray, Uint8Clamped)
NEO_TA_CONSTRUCT(Int16Array, Int16)
NEO_TA_CONSTRUCT(Uint16Array, Uint16)
NEO_TA_CONSTRUCT(Int32Array, Int32)
NEO_TA_CONSTRUCT(Uint32Array, Uint32)
NEO_TA_CONSTRUCT(Float32Array, Float32)
NEO_TA_CONSTRUCT(Float64Array, Float64)
NEO_TA_CONSTRUCT(BigInt64Array, BigInt64)
NEO_TA_CONSTRUCT(BigUint64Array, BigUint64)

#undef NEO_TA_CONSTRUCT

// —— 原型 getter ——

// 访问器只做 RequireInternalSlot，不调用 ValidateTypedArray：分离后
// buffer 返回 undefined，byteLength/byteOffset/length 返回 +0（§23.2.3.1-4）。
Value* requireTypedArraySlot(IContext* ctx, Value* self, TypedArrayData*& out) {
    out = asTypedArray(self);
    if (out == nullptr) {
        return detail::typeError(
            ctx, "TypedArray.prototype accessor called on incompatible receiver");
    }
    return nullptr;
}

Value* typedArrayBufferGet(IContext* ctx, Value* self, size_t /*argc*/,
                           Value* /*argv*/[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArraySlot(ctx, self, d)) return err;
    return d->buffer;
}

Value* typedArrayByteLengthGet(IContext* ctx, Value* self, size_t /*argc*/,
                               Value* /*argv*/[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArraySlot(ctx, self, d)) return err;
    if (isDetached(d)) return ctx->createNumber(0);
    return ctx->createNumber(
        static_cast<double>(d->length * typedArrayElementSize(d->kind)));
}

Value* typedArrayByteOffsetGet(IContext* ctx, Value* self, size_t /*argc*/,
                               Value* /*argv*/[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArraySlot(ctx, self, d)) return err;
    if (isDetached(d)) return ctx->createNumber(0);
    return ctx->createNumber(static_cast<double>(d->byteOffset));
}

Value* typedArrayLengthGet(IContext* ctx, Value* self, size_t /*argc*/,
                           Value* /*argv*/[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArraySlot(ctx, self, d)) return err;
    if (isDetached(d)) return ctx->createNumber(0);
    return ctx->createNumber(static_cast<double>(d->length));
}

// %TypedArray%.prototype[@@toStringTag]（§23.2.3.39）：无内部槽返回 undefined。
Value* typedArrayToStringTagGet(IContext* ctx, Value* self, size_t /*argc*/,
                                Value* /*argv*/[]) {
    TypedArrayData* d = asTypedArray(self);
    if (d == nullptr) return ctx->createUndefined();
    return ctx->createString(kindName(d->kind));
}

// —— 原型方法 ——

Value* typedArraySet(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    if (argc == 0) {
        return detail::typeError(ctx, "TypedArray.prototype.set requires a source");
    }
    double offsetRaw = 0.0;
    if (argc > 1 && !isUndefined(argv[1])) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[1], offsetRaw)) return err;
    }
    if (offsetRaw < 0.0) return detail::rangeError(ctx, "Offset is out of bounds");
    const size_t offset = static_cast<size_t>(offsetRaw);

    // TypedArray 源走内部槽路径（§23.2.3.23.2），不触发 @@iterator / length。
    TypedArrayData* src = asTypedArray(argv[0]);
    if (src != nullptr) {
        ArrayBufferData* targetBuffer = asArrayBuffer(d->buffer);
        ArrayBufferData* srcBuffer = asArrayBuffer(src->buffer);
        if (targetBuffer == nullptr || targetBuffer->detached) {
            return detail::typeError(ctx, "TypedArray buffer is detached");
        }
        if (srcBuffer == nullptr || srcBuffer->detached) {
            return detail::typeError(ctx, "Source TypedArray buffer is detached");
        }
        if (src->length + offset > d->length) {
            return detail::rangeError(ctx, "Source is too large");
        }
        if (src->kind == d->kind) {
            const size_t elemSize = typedArrayElementSize(d->kind);
            if (src->length > 0) {
                std::memmove(targetBuffer->data() + d->byteOffset + offset * elemSize,
                             srcBuffer->data() + src->byteOffset,
                             src->length * elemSize);
            }
            return ctx->createUndefined();
        }
        for (size_t i = 0; i < src->length; ++i) {
            Value* v = typedArrayReadElement(ctx, src, i);
            if (detail::isErrorValue(v)) return v;
            if (Value* err = typedArrayWriteElement(ctx, d, offset + i, v)) {
                return err;
            }
        }
        return ctx->createUndefined();
    }

    // 类数组源（§23.2.3.23.1）：LengthOfArrayLike + Get，逐元素 Set。
    ArrayBufferData* targetBuffer = asArrayBuffer(d->buffer);
    if (targetBuffer == nullptr || targetBuffer->detached) {
        return detail::typeError(ctx, "TypedArray buffer is detached");
    }
    uint32_t srcLength = 0;
    if (Value* err = detail::lengthOf(ctx, argv[0], srcLength)) return err;
    if (static_cast<size_t>(srcLength) + offset > d->length) {
        return detail::rangeError(ctx, "Source is too large");
    }
    for (uint32_t i = 0; i < srcLength; ++i) {
        Value* v =
            ctx->getProperty(argv[0], ctx->createString(std::to_string(i)));
        if (detail::isErrorValue(v)) return v;
        if (Value* err = typedArrayWriteElement(ctx, d, offset + i, v)) return err;
    }
    return ctx->createUndefined();
}

Value* typedArraySubarray(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    // subarray 只做 RequireInternalSlot（不校验 detached）：分离由默认构造器
    // 在 TypedArray(buffer, ...) 中抛出，自定义 species 仍可被调用。
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArraySlot(ctx, self, d)) return err;
    double beginRaw = 0.0;
    if (argc > 0 && !isUndefined(argv[0])) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[0], beginRaw)) return err;
    }
    double endRaw = static_cast<double>(d->length);
    if (argc > 1 && !isUndefined(argv[1])) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[1], endRaw)) return err;
    }
    const size_t begin = relativeIndex(beginRaw, d->length);
    size_t end = relativeIndex(endRaw, d->length);
    if (end < begin) end = begin;
    const size_t elemSize = typedArrayElementSize(d->kind);
    const size_t newLength = end - begin;
    const size_t beginByteOffset = d->byteOffset + begin * elemSize;
    Value* args[3] = {d->buffer,
                      ctx->createNumber(static_cast<double>(beginByteOffset)),
                      ctx->createNumber(static_cast<double>(newLength))};
    if (args[1] == ctx->oomValue() || args[2] == ctx->oomValue()) {
        return ctx->oomValue();
    }
    Value* defaultCtor = typedArrayDefaultCtor(ctx, d->kind);
    if (defaultCtor == nullptr) {
        return detail::typeError(ctx, "TypedArray constructor is not defined");
    }
    if (detail::isErrorValue(defaultCtor)) return defaultCtor;
    Value* ctor = typedArraySpeciesConstructor(ctx, self, defaultCtor);
    if (detail::isErrorValue(ctor)) return ctor;
    Value* result = nullptr;
    TypedArrayData* rd = nullptr;
    if (Value* err = typedArrayCreate(ctx, ctor, 3, args, result, rd)) return err;
    return result;
}

Value* typedArraySlice(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    double beginRaw = 0.0;
    if (argc > 0 && !isUndefined(argv[0])) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[0], beginRaw)) return err;
    }
    double endRaw = static_cast<double>(d->length);
    if (argc > 1 && !isUndefined(argv[1])) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[1], endRaw)) return err;
    }
    const size_t begin = relativeIndex(beginRaw, d->length);
    size_t end = relativeIndex(endRaw, d->length);
    if (end < begin) end = begin;
    const size_t count = end - begin;
    Value* result = nullptr;
    TypedArrayData* rd = nullptr;
    if (Value* err = typedArraySpeciesCreate(ctx, self, d->kind, count, result, rd)) {
        return err;
    }
    // Species 构造可能执行用户代码并分离源缓冲区（§23.2.3.26）。
    if (count > 0 && isDetached(d)) {
        return detail::typeError(ctx, "TypedArray buffer is detached");
    }
    for (size_t i = 0; i < count; ++i) {
        Value* v = typedArrayReadElement(ctx, d, begin + i);
        if (detail::isErrorValue(v)) return v;
        if (Value* err = typedArrayWriteElement(ctx, rd, i, v)) return err;
    }
    return result;
}

Value* typedArrayFill(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();
    double beginRaw = 0.0;
    if (argc > 1 && !isUndefined(argv[1])) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[1], beginRaw)) return err;
    }
    double endRaw = static_cast<double>(d->length);
    if (argc > 2 && !isUndefined(argv[2])) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[2], endRaw)) return err;
    }
    const size_t begin = relativeIndex(beginRaw, d->length);
    size_t end = relativeIndex(endRaw, d->length);
    if (end < begin) end = begin;

    // 值只转换一次（§23.2.3.8）。
    Value* converted = typedArrayConvertElement(ctx, d->kind, value);
    if (detail::isErrorValue(converted)) return converted;
    if (isDetached(d)) {
        return detail::typeError(ctx, "TypedArray buffer is detached");
    }
    for (size_t i = begin; i < end; ++i) {
        if (Value* err = typedArrayWriteElement(ctx, d, i, converted)) return err;
    }
    return self;
}

bool sameValueZero(Value* a, Value* b) {
    if (a == b) return true;
    if (a->type()->kind() == TypeKind::Number && b->type()->kind() == TypeKind::Number) {
        const double x = static_cast<NumberData*>(a->data())->value;
        const double y = static_cast<NumberData*>(b->data())->value;
        return x == y || (std::isnan(x) && std::isnan(y));
    }
    return detail::strictEquals(a, b);
}

Value* typedArrayIncludes(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    if (d->length == 0) return ctx->createBoolean(false);
    if (argc == 0) return ctx->createBoolean(false);
    double startRaw = 0.0;
    if (argc > 1 && !isUndefined(argv[1])) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[1], startRaw)) return err;
    }
    // 分离后 Get 返回 undefined；searchElement 为 undefined 时仍可匹配。
    size_t start = relativeIndex(startRaw, d->length);
    for (size_t i = start; i < d->length; ++i) {
        Value* v = typedArrayReadElement(ctx, d, i);
        if (detail::isErrorValue(v)) return v;
        if (sameValueZero(v, argv[0])) return ctx->createBoolean(true);
    }
    return ctx->createBoolean(false);
}

Value* typedArrayIndexOf(IContext* ctx, Value* self, size_t argc, Value* argv[],
                         bool reverse) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    if (d->length == 0) return ctx->createNumber(-1);
    Value* search = argc > 0 ? argv[0] : ctx->createUndefined();
    double startRaw = reverse ? static_cast<double>(d->length) - 1.0 : 0.0;
    // fromIndex 只要「存在」就转换（undefined → 0），与缺省值不同。
    if (argc > 1) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[1], startRaw)) return err;
    }
    // fromIndex 转换期间分离：元素全部不可见。
    if (isDetached(d)) return ctx->createNumber(-1);
    if (!reverse) {
        const size_t start = relativeIndex(startRaw, d->length);
        for (size_t i = start; i < d->length; ++i) {
            Value* v = typedArrayReadElement(ctx, d, i);
            if (detail::isErrorValue(v)) return v;
            if (detail::strictEquals(v, search)) {
                return ctx->createNumber(static_cast<double>(i));
            }
        }
    } else {
        double k = startRaw;
        if (k < 0.0) k = static_cast<double>(d->length) + k;
        if (k < 0.0) return ctx->createNumber(-1);
        if (k >= static_cast<double>(d->length)) {
            k = static_cast<double>(d->length) - 1.0;
        }
        if (d->length == 0) return ctx->createNumber(-1);
        for (size_t i = static_cast<size_t>(k) + 1; i-- > 0;) {
            Value* v = typedArrayReadElement(ctx, d, i);
            if (detail::isErrorValue(v)) return v;
            if (detail::strictEquals(v, search)) {
                return ctx->createNumber(static_cast<double>(i));
            }
        }
    }
    return ctx->createNumber(-1);
}

Value* typedArrayIndexOfFn(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return typedArrayIndexOf(ctx, self, argc, argv, false);
}

Value* typedArrayLastIndexOf(IContext* ctx, Value* self, size_t argc,
                             Value* argv[]) {
    return typedArrayIndexOf(ctx, self, argc, argv, true);
}

Value* typedArrayForEach(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    if (argc == 0 || !detail::isCallable(argv[0])) {
        return detail::typeError(ctx, "callback is not a function");
    }
    Value* thisArg = argc > 1 ? argv[1] : ctx->createUndefined();
    for (size_t i = 0; i < d->length; ++i) {
        Value* v = typedArrayReadElement(ctx, d, i);
        if (detail::isErrorValue(v)) return v;
        Value* args[3] = {v, ctx->createNumber(static_cast<double>(i)), self};
        Value* r = ctx->call(argv[0], thisArg, 3, args);
        if (detail::isErrorValue(r)) return r;
    }
    return ctx->createUndefined();
}

Value* typedArrayMap(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    if (argc == 0 || !detail::isCallable(argv[0])) {
        return detail::typeError(ctx, "callback is not a function");
    }
    Value* thisArg = argc > 1 ? argv[1] : ctx->createUndefined();
    Value* result = nullptr;
    TypedArrayData* rd = nullptr;
    if (Value* err = typedArraySpeciesCreate(ctx, self, d->kind, d->length, result, rd)) {
        return err;
    }
    for (size_t i = 0; i < d->length; ++i) {
        Value* v = typedArrayReadElement(ctx, d, i);
        if (detail::isErrorValue(v)) return v;
        Value* args[3] = {v, ctx->createNumber(static_cast<double>(i)), self};
        Value* mapped = ctx->call(argv[0], thisArg, 3, args);
        if (detail::isErrorValue(mapped)) return mapped;
        if (Value* err = typedArrayWriteElement(ctx, rd, i, mapped)) return err;
    }
    return result;
}

Value* typedArrayFilter(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    if (argc == 0 || !detail::isCallable(argv[0])) {
        return detail::typeError(ctx, "callback is not a function");
    }
    Value* thisArg = argc > 1 ? argv[1] : ctx->createUndefined();
    std::vector<Value*> kept;
    for (size_t i = 0; i < d->length; ++i) {
        Value* v = typedArrayReadElement(ctx, d, i);
        if (detail::isErrorValue(v)) return v;
        Value* args[3] = {v, ctx->createNumber(static_cast<double>(i)), self};
        Value* selected = ctx->call(argv[0], thisArg, 3, args);
        if (detail::isErrorValue(selected)) return selected;
        if (detail::toBoolean(selected)) kept.push_back(v);
    }
    Value* result = nullptr;
    TypedArrayData* rd = nullptr;
    if (Value* err =
            typedArraySpeciesCreate(ctx, self, d->kind, kept.size(), result, rd)) {
        return err;
    }
    for (size_t i = 0; i < kept.size(); ++i) {
        if (Value* err = typedArrayWriteElement(ctx, rd, i, kept[i])) return err;
    }
    return result;
}

Value* typedArrayReduce(IContext* ctx, Value* self, size_t argc, Value* argv[],
                        bool reverse) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    if (argc == 0 || !detail::isCallable(argv[0])) {
        return detail::typeError(ctx, "callback is not a function");
    }
    Value* accumulator = nullptr;
    size_t index = 0;
    if (argc > 1) {
        accumulator = argv[1];
        index = 0;
    } else {
        if (d->length == 0) {
            return detail::typeError(ctx, "Reduce of empty typed array with no initial value");
        }
        accumulator = typedArrayReadElement(ctx, d, 0);
        if (detail::isErrorValue(accumulator)) return accumulator;
        index = 1;
    }
    if (!reverse) {
        for (size_t i = index; i < d->length; ++i) {
            Value* v = typedArrayReadElement(ctx, d, i);
            if (detail::isErrorValue(v)) return v;
            Value* args[4] = {accumulator, v, ctx->createNumber(static_cast<double>(i)),
                              self};
            accumulator = ctx->call(argv[0], ctx->createUndefined(), 4, args);
            if (detail::isErrorValue(accumulator)) return accumulator;
        }
    } else {
        size_t start = d->length;
        if (argc == 1) {
            if (d->length == 0) {
                return detail::typeError(ctx,
                                         "Reduce of empty typed array with no initial value");
            }
            accumulator = typedArrayReadElement(ctx, d, d->length - 1);
            if (detail::isErrorValue(accumulator)) return accumulator;
            start = d->length - 1;
        }
        for (size_t i = start; i-- > 0;) {
            Value* v = typedArrayReadElement(ctx, d, i);
            if (detail::isErrorValue(v)) return v;
            Value* args[4] = {accumulator, v, ctx->createNumber(static_cast<double>(i)),
                              self};
            accumulator = ctx->call(argv[0], ctx->createUndefined(), 4, args);
            if (detail::isErrorValue(accumulator)) return accumulator;
        }
    }
    return accumulator;
}

Value* typedArrayReduceFn(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return typedArrayReduce(ctx, self, argc, argv, false);
}

Value* typedArrayReduceRight(IContext* ctx, Value* self, size_t argc,
                             Value* argv[]) {
    return typedArrayReduce(ctx, self, argc, argv, true);
}

Value* typedArraySome(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    if (argc == 0 || !detail::isCallable(argv[0])) {
        return detail::typeError(ctx, "callback is not a function");
    }
    Value* thisArg = argc > 1 ? argv[1] : ctx->createUndefined();
    for (size_t i = 0; i < d->length; ++i) {
        Value* v = typedArrayReadElement(ctx, d, i);
        if (detail::isErrorValue(v)) return v;
        Value* args[3] = {v, ctx->createNumber(static_cast<double>(i)), self};
        Value* r = ctx->call(argv[0], thisArg, 3, args);
        if (detail::isErrorValue(r)) return r;
        if (detail::toBoolean(r)) return ctx->createBoolean(true);
    }
    return ctx->createBoolean(false);
}

Value* typedArrayEvery(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    if (argc == 0 || !detail::isCallable(argv[0])) {
        return detail::typeError(ctx, "callback is not a function");
    }
    Value* thisArg = argc > 1 ? argv[1] : ctx->createUndefined();
    for (size_t i = 0; i < d->length; ++i) {
        Value* v = typedArrayReadElement(ctx, d, i);
        if (detail::isErrorValue(v)) return v;
        Value* args[3] = {v, ctx->createNumber(static_cast<double>(i)), self};
        Value* r = ctx->call(argv[0], thisArg, 3, args);
        if (detail::isErrorValue(r)) return r;
        if (!detail::toBoolean(r)) return ctx->createBoolean(false);
    }
    return ctx->createBoolean(true);
}

Value* typedArrayFind(IContext* ctx, Value* self, size_t argc, Value* argv[],
                      bool wantIndex, bool reverse) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    if (argc == 0 || !detail::isCallable(argv[0])) {
        return detail::typeError(ctx, "callback is not a function");
    }
    Value* thisArg = argc > 1 ? argv[1] : ctx->createUndefined();
    for (size_t n = 0; n < d->length; ++n) {
        const size_t i = reverse ? d->length - 1 - n : n;
        Value* v = typedArrayReadElement(ctx, d, i);
        if (detail::isErrorValue(v)) return v;
        Value* args[3] = {v, ctx->createNumber(static_cast<double>(i)), self};
        Value* r = ctx->call(argv[0], thisArg, 3, args);
        if (detail::isErrorValue(r)) return r;
        if (detail::toBoolean(r)) {
            return wantIndex ? ctx->createNumber(static_cast<double>(i)) : v;
        }
    }
    return wantIndex ? ctx->createNumber(-1) : ctx->createUndefined();
}

Value* typedArrayFindFn(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return typedArrayFind(ctx, self, argc, argv, false, false);
}

Value* typedArrayFindIndex(IContext* ctx, Value* self, size_t argc,
                           Value* argv[]) {
    return typedArrayFind(ctx, self, argc, argv, true, false);
}

Value* typedArrayFindLast(IContext* ctx, Value* self, size_t argc,
                          Value* argv[]) {
    return typedArrayFind(ctx, self, argc, argv, false, true);
}

Value* typedArrayFindLastIndex(IContext* ctx, Value* self, size_t argc,
                               Value* argv[]) {
    return typedArrayFind(ctx, self, argc, argv, true, true);
}

// %TypedArray%.prototype.at（§23.2.3.2）：相对索引，越界返回 undefined。
Value* typedArrayAt(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    double k = 0.0;
    if (argc > 0) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[0], k)) return err;
    }
    if (k < 0.0) k += static_cast<double>(d->length);
    if (k < 0.0 || k >= static_cast<double>(d->length)) {
        return ctx->createUndefined();
    }
    return typedArrayReadElement(ctx, d, static_cast<size_t>(k));
}

// %TypedArray%.prototype.copyWithin（§23.2.3.5）。
Value* typedArrayCopyWithin(IContext* ctx, Value* self, size_t argc,
                            Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    const size_t len = d->length;
    double targetRaw = 0.0;
    if (argc > 0) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[0], targetRaw)) return err;
    }
    double startRaw = 0.0;
    if (argc > 1) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[1], startRaw)) return err;
    }
    double endRaw = static_cast<double>(len);
    if (argc > 2 && !isUndefined(argv[2])) {
        if (Value* err = toIntegerOrInfinity(ctx, argv[2], endRaw)) return err;
    }
    if (isDetached(d)) {
        return detail::typeError(ctx, "TypedArray buffer is detached");
    }
    const size_t to = relativeIndex(targetRaw, len);
    const size_t from = relativeIndex(startRaw, len);
    const size_t final = relativeIndex(endRaw, len);
    size_t count = final > from ? final - from : 0;
    if (count > len - to) count = len - to;
    if (count == 0) return self;
    // 先读出再写回，避免重叠区域互相覆盖。
    std::vector<Value*> temp;
    temp.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        Value* v = typedArrayReadElement(ctx, d, from + i);
        if (detail::isErrorValue(v)) return v;
        temp.push_back(v);
    }
    for (size_t i = 0; i < count; ++i) {
        if (Value* err = typedArrayWriteElement(ctx, d, to + i, temp[i])) return err;
    }
    return self;
}

Value* typedArrayJoin(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    std::string separator = ",";
    if (argc > 0 && !isUndefined(argv[0])) {
        Value* s = detail::toStringValue(ctx, argv[0]);
        if (detail::isErrorValue(s)) return s;
        separator = static_cast<StringData*>(s->data())->value;
    }
    std::string out;
    for (size_t i = 0; i < d->length; ++i) {
        if (i > 0) out += separator;
        Value* v = typedArrayReadElement(ctx, d, i);
        if (detail::isErrorValue(v)) return v;
        TypeKind vk = v->type()->kind();
        if (vk == TypeKind::Undefined || vk == TypeKind::Null) continue;
        out += detail::valueToString(v);
    }
    return ctx->createString(out);
}

// %TypedArray%.prototype.toLocaleString（§23.2.3.29）：逐元素调用其
// toLocaleString，undefined/null 视为空串。
Value* typedArrayToLocaleString(IContext* ctx, Value* self, size_t /*argc*/,
                                Value* /*argv*/[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    std::string out;
    for (size_t i = 0; i < d->length; ++i) {
        if (i > 0) out += ",";
        Value* v = typedArrayReadElement(ctx, d, i);
        if (detail::isErrorValue(v)) return v;
        TypeKind vk = v->type()->kind();
        if (vk == TypeKind::Undefined || vk == TypeKind::Null) continue;
        Value* method = ctx->getProperty(v, "toLocaleString");
        if (detail::isErrorValue(method)) return method;
        if (!detail::isCallable(method)) {
            return detail::typeError(ctx, "toLocaleString is not a function");
        }
        Value* r = ctx->call(method, v, 0, nullptr);
        if (detail::isErrorValue(r)) return r;
        Value* s = detail::toStringValue(ctx, r);
        if (detail::isErrorValue(s)) return s;
        out += static_cast<StringData*>(s->data())->value;
    }
    return ctx->createString(out);
}

Value* typedArrayReverse(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    (void)argc;
    (void)argv;
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    for (size_t i = 0, j = d->length; i + 1 < j; ++i, --j) {
        Value* a = typedArrayReadElement(ctx, d, i);
        if (detail::isErrorValue(a)) return a;
        Value* b = typedArrayReadElement(ctx, d, j - 1);
        if (detail::isErrorValue(b)) return b;
        if (Value* err = typedArrayWriteElement(ctx, d, i, b)) return err;
        if (Value* err = typedArrayWriteElement(ctx, d, j - 1, a)) return err;
    }
    return self;
}

Value* typedArraySort(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    Value* comparefn = argc > 0 ? argv[0] : nullptr;
    if (comparefn != nullptr && !isUndefined(comparefn) &&
        !detail::isCallable(comparefn)) {
        return detail::typeError(ctx, "comparator is not a function");
    }
    if (comparefn != nullptr && isUndefined(comparefn)) comparefn = nullptr;

    std::vector<Value*> items;
    items.reserve(d->length);
    for (size_t i = 0; i < d->length; ++i) {
        Value* v = typedArrayReadElement(ctx, d, i);
        if (detail::isErrorValue(v)) return v;
        items.push_back(v);
    }

    Value* sortErr = nullptr;
    const bool bigint = typedArrayIsBigInt(d->kind);
    auto less = [&](Value* a, Value* b) -> bool {
        if (sortErr != nullptr) return false;
        if (comparefn != nullptr) {
            Value* args[2] = {a, b};
            Value* r = ctx->call(comparefn, ctx->createUndefined(), 2, args);
            if (detail::isErrorValue(r)) {
                sortErr = r;
                return false;
            }
            Value* nv = detail::toNumberValue(ctx, r);
            if (detail::isErrorValue(nv)) {
                sortErr = nv;
                return false;
            }
            const double x = static_cast<NumberData*>(nv->data())->value;
            if (std::isnan(x)) return false;
            return x < 0.0;
        }
        if (bigint) return detail::bigIntValue(a) < detail::bigIntValue(b);
        const double x = static_cast<NumberData*>(a->data())->value;
        const double y = static_cast<NumberData*>(b->data())->value;
        if (std::isnan(x)) return false;
        if (std::isnan(y)) return true;
        if (x == 0.0 && y == 0.0) {
            return std::signbit(x) && !std::signbit(y);  // -0 排在 +0 前
        }
        return x < y;
    };
    std::stable_sort(items.begin(), items.end(), less);
    if (sortErr != nullptr) return sortErr;
    for (size_t i = 0; i < items.size(); ++i) {
        if (Value* err = typedArrayWriteElement(ctx, d, i, items[i])) return err;
    }
    return self;
}

Value* typedArrayKeys(IContext* ctx, Value* self, size_t /*argc*/,
                      Value* /*argv*/[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    return ctx->createArrayIterator(self, ArrayIterationKind::Key);
}

Value* typedArrayValues(IContext* ctx, Value* self, size_t /*argc*/,
                        Value* /*argv*/[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    return ctx->createArrayIterator(self, ArrayIterationKind::Value);
}

Value* typedArrayEntries(IContext* ctx, Value* self, size_t /*argc*/,
                         Value* /*argv*/[]) {
    TypedArrayData* d = nullptr;
    if (Value* err = requireTypedArray(ctx, self, d)) return err;
    return ctx->createArrayIterator(self, ArrayIterationKind::KeyAndValue);
}

// —— 静态 from / of ——

// %TypedArray%.from/of 的公共尾部：Construct(C, « len ») 后逐元素写入。
Value* typedArrayFromItems(IContext* ctx, Value* self,
                           const std::vector<Value*>& items, Value* mapfn,
                           Value* thisArg) {
    if (self == nullptr || !detail::isConstructor(self)) {
        return detail::typeError(ctx, "this is not a constructor");
    }
    Value* lenValue = ctx->createNumber(static_cast<double>(items.size()));
    if (lenValue == ctx->oomValue()) return lenValue;
    Value* target = ctx->construct(self, 1, &lenValue);
    if (detail::isErrorValue(target)) return target;
    TypedArrayData* td = asTypedArray(target);
    if (td == nullptr) {
        return detail::typeError(ctx, "constructor did not return a TypedArray");
    }
    if (isDetached(td)) {
        return detail::typeError(ctx, "constructor returned detached buffer");
    }
    if (td->length < items.size()) {
        return detail::typeError(ctx, "constructor returned too short a TypedArray");
    }
    // 映射与写入交错进行：mapper 可分离 target，之后的 Set 成为 no-op。
    for (size_t i = 0; i < items.size(); ++i) {
        Value* value = items[i];
        if (mapfn != nullptr) {
            Value* args[2] = {value, ctx->createNumber(static_cast<double>(i))};
            value = ctx->call(mapfn, thisArg, 2, args);
            if (detail::isErrorValue(value)) return value;
        }
        if (Value* err = typedArrayWriteElement(ctx, td, i, value)) return err;
    }
    return target;
}

Value* typedArrayFrom(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (self == nullptr || !detail::isConstructor(self)) {
        return detail::typeError(ctx, "this is not a constructor");
    }
    Value* mapfn = argc > 1 && !isUndefined(argv[1]) ? argv[1] : nullptr;
    if (mapfn != nullptr && !detail::isCallable(mapfn)) {
        return detail::typeError(ctx, "mapfn is not a function");
    }
    if (argc == 0) {
        return detail::typeError(ctx, "TypedArray.from requires a source");
    }
    std::vector<Value*> items;
    if (Value* err = collectElements(ctx, argv[0], items)) return err;
    Value* thisArg = argc > 2 ? argv[2] : ctx->createUndefined();
    return typedArrayFromItems(ctx, self, items, mapfn, thisArg);
}

Value* typedArrayOf(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    std::vector<Value*> items(argv, argv + argc);
    return typedArrayFromItems(ctx, self, items, nullptr, nullptr);
}

// —— 注册辅助 ——

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc, int lengthOverride = -1) {
    // 内建方法：不可构造、无自有 prototype；length 与最小实参数分离。
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* f = ctx->createNativeFunction(fn, minArgc, {}, opts);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    if (lengthOverride >= 0) {
        Value* len = ctx->createNumber(static_cast<double>(lengthOverride));
        if (len == ctx->oomValue()) return false;
        if (ctx->defineProperty(f, "length", len, false, false, true)
                ->type()->kind() == TypeKind::Exception) {
            return false;
        }
    }
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

bool defineGetter(IContext* ctx, Value* target, const char* name, NativeFn getter) {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* f = ctx->createNativeFunction(getter, 0, {}, opts);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, std::string("get ") + name);
    return ctx->defineAccessor(target, name, f, nullptr, false, true)
               ->type()->kind() != TypeKind::Exception;
}

bool defineSymbolGetter(IContext* ctx, Value* target, Value* symbol,
                        NativeFn getter, const char* name) {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* f = ctx->createNativeFunction(getter, 0, {}, opts);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, std::string("get ") + name);
    return ctx->defineAccessor(target, symbol, f, nullptr, false, true)
               ->type()->kind() != TypeKind::Exception;
}

// %TypedArray%[@@species] / %TypedArray%.prototype[@@species]：返回 this。
Value* typedArraySpeciesGet(IContext* /*ctx*/, Value* self, size_t /*argc*/,
                            Value* /*argv*/[]) {
    return self;
}

bool defineTag(IContext* ctx, Value* target, const char* tag) {
    return ctx->defineProperty(target,
                               ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                               ctx->createString(tag), false, false, true)
               ->type()->kind() != TypeKind::Exception;
}

bool defineNumber(IContext* ctx, Value* target, const char* name, double value,
                  bool writable) {
    return ctx->defineProperty(target, name, ctx->createNumber(value), writable, false,
                               false)
               ->type()->kind() != TypeKind::Exception;
}

// 建立单个 TypedArray 构造函数：prototype + constructor + BYTES_PER_ELEMENT，
// [[Prototype]] 指向 %TypedArray%（from/of 由此继承），并注册到 global。
bool registerCtor(IContext* ctx, const KindInfo& info, NativeFn construct,
                  Value* proto, Value* taCtor) {
    Value* fn = ctx->createNativeFunction(typedArrayCtorCall, 0);
    if (fn == ctx->oomValue()) return false;
    ctx->setFunctionName(fn, info.name);
    ctx->setFunctionConstructor(fn, construct);
    if (ctx->setPrototype(fn, taCtor)->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(fn, "prototype", proto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(proto, "constructor", fn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineNumber(ctx, fn, "BYTES_PER_ELEMENT",
                      static_cast<double>(info.bytes), false)) {
        return false;
    }
    ctx->defineGlobal(info.name, fn, true, false, true);
    return true;
}

}  // namespace

bool registerTypedArrayBuiltins(IContext* ctx) {
    // —— 1. %TypedArray.prototype% ——
    Value* taProto = ctx->createObject();
    if (taProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(taProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }

    // —— 2. 各元素类型原型（[[Prototype]] = %TypedArray.prototype%）——
    Value* protos[kKindCount] = {};
    for (size_t i = 0; i < kKindCount; ++i) {
        protos[i] = ctx->createObject();
        if (protos[i] == ctx->oomValue()) return false;
        if (ctx->setPrototype(protos[i], taProto)->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
        ctx->setTypedArrayPrototype(kKinds[i].kind, protos[i]);
    }

    // —— 3. %TypedArray% 固有构造器（不可构造，仅作各构造函数的原型）——
    // %TypedArray% 是构造器（IsConstructor 为真），但 call/construct 均抛
    // TypeError（§23.2.1.1）；from/of 依赖 this 可构造。
    FunctionCreateOptions taOpts;
    taOpts.constructible = true;
    taOpts.hasPrototypeProperty = true;
    Value* taCtor = ctx->createNativeFunction(typedArrayCtorCall, 0, {}, taOpts);
    if (taCtor == ctx->oomValue()) return false;
    ctx->setFunctionName(taCtor, "TypedArray");
    if (ctx->defineProperty(taCtor, "prototype", taProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(taProto, "constructor", taCtor, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    Value* speciesSymbol =
        ctx->getWellKnownSymbol(WellKnownSymbol::Species);
    if (!defineSymbolGetter(ctx, taCtor, speciesSymbol, typedArraySpeciesGet,
                            "[Symbol.species]")) {
        return false;
    }
    if (!defineSymbolGetter(ctx, taProto, speciesSymbol, typedArraySpeciesGet,
                            "[Symbol.species]")) {
        return false;
    }
    if (!defineMethod(ctx, taCtor, "from", typedArrayFrom, 0, 1)) return false;
    if (!defineMethod(ctx, taCtor, "of", typedArrayOf, 0, 0)) return false;

    // —— 4. %TypedArray.prototype% 成员 ——
    if (!defineGetter(ctx, taProto, "buffer", typedArrayBufferGet)) return false;
    if (!defineGetter(ctx, taProto, "byteLength", typedArrayByteLengthGet)) {
        return false;
    }
    if (!defineGetter(ctx, taProto, "byteOffset", typedArrayByteOffsetGet)) {
        return false;
    }
    if (!defineGetter(ctx, taProto, "length", typedArrayLengthGet)) return false;
    if (!defineSymbolGetter(ctx, taProto,
                            ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            typedArrayToStringTagGet, "[Symbol.toStringTag]")) {
        return false;
    }

    const struct {
        const char* name;
        NativeFn fn;
        size_t minArgc;
        int length;
    } methods[] = {
        {"set", typedArraySet, 1, 1},
        {"subarray", typedArraySubarray, 0, 2},
        {"slice", typedArraySlice, 0, 2},
        {"fill", typedArrayFill, 1, 1},
        {"includes", typedArrayIncludes, 0, 1},
        {"indexOf", typedArrayIndexOfFn, 0, 1},
        {"lastIndexOf", typedArrayLastIndexOf, 0, 1},
        {"forEach", typedArrayForEach, 1, 1},
        {"map", typedArrayMap, 1, 1},
        {"filter", typedArrayFilter, 1, 1},
        {"reduce", typedArrayReduceFn, 1, 1},
        {"reduceRight", typedArrayReduceRight, 1, 1},
        {"some", typedArraySome, 1, 1},
        {"every", typedArrayEvery, 1, 1},
        {"find", typedArrayFindFn, 1, 1},
        {"findIndex", typedArrayFindIndex, 1, 1},
        {"findLast", typedArrayFindLast, 1, 1},
        {"findLastIndex", typedArrayFindLastIndex, 1, 1},
        {"at", typedArrayAt, 0, 1},
        {"copyWithin", typedArrayCopyWithin, 0, 2},
        {"join", typedArrayJoin, 0, 1},
        {"toLocaleString", typedArrayToLocaleString, 0, 0},
        {"reverse", typedArrayReverse, 0, 0},
        {"sort", typedArraySort, 0, 1},
        {"keys", typedArrayKeys, 0, 0},
        {"values", typedArrayValues, 0, 0},
        {"entries", typedArrayEntries, 0, 0},
    };
    for (const auto& m : methods) {
        if (!defineMethod(ctx, taProto, m.name, m.fn, m.minArgc, m.length)) {
            return false;
        }
    }
    // toString 与 Array.prototype.toString 是同一个函数对象（§23.2.3.31）。
    Value* arrayToString = ctx->getProperty(ctx->arrayPrototype(), "toString");
    if (detail::isErrorValue(arrayToString)) return false;
    if (ctx->defineProperty(taProto, "toString", arrayToString, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // values 与 @@iterator 是同一个函数对象（§23.2.3.35）
    Value* valuesFn = ctx->getProperty(taProto, "values");
    if (detail::isErrorValue(valuesFn)) return false;
    if (ctx->defineProperty(taProto, ctx->getWellKnownSymbol(WellKnownSymbol::Iterator),
                            valuesFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 5. 各元素类型原型 + 构造函数 ——
    const NativeFn constructs[kKindCount] = {
        Int8ArrayConstruct,      Uint8ArrayConstruct,   Uint8ClampedArrayConstruct,
        Int16ArrayConstruct,     Uint16ArrayConstruct,  Int32ArrayConstruct,
        Uint32ArrayConstruct,    Float32ArrayConstruct, Float64ArrayConstruct,
        BigInt64ArrayConstruct,  BigUint64ArrayConstruct,
    };
    for (size_t i = 0; i < kKindCount; ++i) {
        if (!defineNumber(ctx, protos[i], "BYTES_PER_ELEMENT",
                          static_cast<double>(kKinds[i].bytes), false)) {
            return false;
        }
        if (!defineTag(ctx, protos[i], kKinds[i].name)) return false;
        if (!registerCtor(ctx, kKinds[i], constructs[i], protos[i], taCtor)) {
            return false;
        }
    }
    return true;
}

}  // namespace neo::core
