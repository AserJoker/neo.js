// ArrayBufferBuiltins.cpp - ArrayBuffer 与 DataView（ES2024 §25.1 / §25.3）
//
// 设计要点：
//   - ArrayBuffer 持 vector<uint8_t>；DataView 以 Value* 引用它并直接读写；
//   - DataView 默认大端，littleEndian 参数按 ToBoolean 解释；
//   - 越界访问抛 RangeError；BigInt 读写要求参数为 BigInt（ToBigInt 语义）；
//   - SharedArrayBuffer 与 ArrayBuffer 共用 byteLength()/data() 访问路径；
//   - 暂不实现 Atomics / 分离（detach）。
//
// 关联 ADR: 020（标准库组织）、006（异常即值）

#include "core/ArrayBufferBuiltins.h"

#include "core/BigIntOps.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/ArrayBuffer.h"
#include "core/types/DataView.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace neo::core {

namespace {

constexpr double kMaxIndex = 9007199254740991.0;  // 2^53 - 1

// ToIndex（§7.1.22）：非负整数，越界抛 RangeError。
Value* toIndex(IContext* ctx, Value* v, size_t& out) {
    Value* nv = detail::toNumberValue(ctx, v);
    if (detail::isErrorValue(nv)) return nv;
    const double n = static_cast<NumberData*>(nv->data())->value;
    // ToIndex：先 ToIntegerOrInfinity（NaN → 0，-0.1 → -0），再检查 0..2^53-1。
    const double integer = std::isnan(n) ? 0.0 : std::trunc(n);
    if (integer < 0.0 || integer > kMaxIndex) {
        return detail::rangeError(ctx, "Invalid array buffer length");
    }
    out = static_cast<size_t>(integer);
    return nullptr;
}

Value* argOrUndefined(IContext* ctx, size_t argc, Value* argv[], size_t i) {
    return i < argc ? argv[i] : ctx->createUndefined();
}

// —— ArrayBuffer ——

Value* arrayBufferCtorCall(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                           Value* /*argv*/[]) {
    return detail::typeError(ctx, "Constructor ArrayBuffer requires 'new'");
}

Value* arrayBufferConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                            Value* argv[]) {
    size_t length = 0;
    if (Value* err = toIndex(ctx, argOrUndefined(ctx, argc, argv, 0), length)) {
        return err;
    }
    // §25.1.4：OrdinaryCreateFromConstructor 先于数据块分配；子类化时
    // newTarget.prototype 的 getter 必须先求值（其异常先于分配失败传播）。
    Value* nt = ctx->newTarget();
    Value* ctor = ctx->global()->lookup("ArrayBuffer");
    if (nt != nullptr && ctor != nullptr && nt->data() != ctor->data()) {
        Value* proto = ctx->getProperty(nt, "prototype");
        if (detail::isErrorValue(proto)) return proto;
    }
    Value* result = ctx->createArrayBuffer(length);
    if (result == ctx->oomValue()) {
        return detail::rangeError(ctx, "Array buffer allocation failed");
    }
    return result;
}

Value* arrayBufferByteLengthGet(IContext* ctx, Value* self, size_t /*argc*/,
                                Value* /*argv*/[]) {
    // SharedArrayBuffer 有自己的原型，不能共用该 getter。
    if (self == nullptr || self->type()->kind() != TypeKind::ArrayBuffer) {
        return detail::typeError(ctx, "this is not an ArrayBuffer");
    }
    auto* d = static_cast<ArrayBufferData*>(self->data());
    return ctx->createNumber(static_cast<double>(d->byteLength()));
}

// SpeciesConstructor（§7.3.20）。
Value* bufferSpeciesConstructor(IContext* ctx, Value* obj, Value* defaultCtor) {
    Value* c = ctx->getProperty(obj, "constructor");
    if (detail::isErrorValue(c)) return c;
    if (c->type()->kind() == TypeKind::Undefined) return defaultCtor;
    if (!detail::isObjectKind(c->type()->kind())) {
        return detail::typeError(ctx, "constructor property is not an object");
    }
    Value* s = ctx->getProperty(c, ctx->getWellKnownSymbol(WellKnownSymbol::Species));
    if (detail::isErrorValue(s)) return s;
    TypeKind sk = s->type()->kind();
    if (sk == TypeKind::Null || sk == TypeKind::Undefined) return defaultCtor;
    if (!detail::isConstructor(s)) {
        return detail::typeError(ctx, "@@species is not a constructor");
    }
    return s;
}

// ArrayBuffer / SharedArrayBuffer 共用的 slice 实现：shared 决定结果类型。
Value* sliceBuffer(IContext* ctx, Value* self, size_t argc, Value* argv[],
                   bool shared) {
    const TypeKind wantKind =
        shared ? TypeKind::SharedArrayBuffer : TypeKind::ArrayBuffer;
    if (self == nullptr || self->type()->kind() != wantKind) {
        return detail::typeError(
            ctx, shared ? "this is not a SharedArrayBuffer"
                        : "this is not an ArrayBuffer");
    }
    auto* d = static_cast<ArrayBufferData*>(self->data());
    const size_t len = d->byteLength();
    auto clamp = [len](double v) -> size_t {
        if (std::isnan(v) || v <= 0.0) return 0;
        if (v >= static_cast<double>(len)) return len;
        return static_cast<size_t>(std::trunc(v));
    };
    double startRaw = 0.0;
    if (argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined) {
        Value* nv = detail::toNumberValue(ctx, argv[0]);
        if (detail::isErrorValue(nv)) return nv;
        startRaw = static_cast<NumberData*>(nv->data())->value;
    }
    size_t start = 0;
    if (startRaw < 0.0) {
        const double fromEnd = static_cast<double>(len) + std::trunc(startRaw);
        start = fromEnd <= 0.0 ? 0 : static_cast<size_t>(fromEnd);
    } else {
        start = clamp(startRaw);
    }
    size_t end = len;
    if (argc > 1 && argv[1]->type()->kind() != TypeKind::Undefined) {
        Value* nv = detail::toNumberValue(ctx, argv[1]);
        if (detail::isErrorValue(nv)) return nv;
        const double endRaw = static_cast<NumberData*>(nv->data())->value;
        if (endRaw < 0.0) {
            const double fromEnd = static_cast<double>(len) + std::trunc(endRaw);
            end = fromEnd <= 0.0 ? 0 : static_cast<size_t>(fromEnd);
        } else {
            end = clamp(endRaw);
        }
    }
    if (end < start) end = start;

    // SpeciesConstructor + Construct(newLen)：结果必须是同类缓冲区、不同对象、
    // 且容量不小于 newLen（§25.1.5.3 / §25.2.4.3）。
    const size_t newLen = end - start;
    Value* defaultCtor = ctx->global()->lookup(shared ? "SharedArrayBuffer"
                                                      : "ArrayBuffer");
    if (defaultCtor == nullptr) {
        return detail::typeError(ctx, "buffer constructor is not defined");
    }
    Value* ctor = bufferSpeciesConstructor(ctx, self, defaultCtor);
    if (detail::isErrorValue(ctor)) return ctor;
    Value* newLenValue = ctx->createNumber(static_cast<double>(newLen));
    if (newLenValue == ctx->oomValue()) return newLenValue;
    Value* result = ctx->construct(ctor, 1, &newLenValue);
    if (detail::isErrorValue(result)) return result;

    const TypeKind want = shared ? TypeKind::SharedArrayBuffer : TypeKind::ArrayBuffer;
    if (result->type()->kind() != want) {
        return detail::typeError(ctx, "species constructor returned wrong buffer type");
    }
    if (result->data() == self->data()) {
        return detail::typeError(ctx, "species constructor returned the same buffer");
    }
    auto* rd = static_cast<ArrayBufferData*>(result->data());
    if (rd->byteLength() < newLen) {
        return detail::typeError(ctx, "species constructor returned a smaller buffer");
    }
    if (newLen > 0) {
        std::memcpy(rd->data(), d->data() + start, newLen);
    }
    return result;
}

Value* arrayBufferSlice(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return sliceBuffer(ctx, self, argc, argv, false);
}

// —— SharedArrayBuffer ——

Value* sharedArrayBufferCtorCall(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                                 Value* /*argv*/[]) {
    return detail::typeError(ctx,
                             "Constructor SharedArrayBuffer requires 'new'");
}

Value* sharedArrayBufferConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                                  Value* argv[]) {
    size_t length = 0;
    if (Value* err = toIndex(ctx, argOrUndefined(ctx, argc, argv, 0), length)) {
        return err;
    }
    std::shared_ptr<SharedBufferBlock> block;
    try {
        block = SharedBufferBlock::create(length);
    } catch (...) {
        return ctx->oomValue();
    }
    return ctx->createSharedArrayBuffer(std::move(block));
}

Value* sharedArrayBufferByteLengthGet(IContext* ctx, Value* self, size_t /*argc*/,
                                      Value* /*argv*/[]) {
    ArrayBufferData* d = asSharedArrayBuffer(self);
    if (d == nullptr) {
        return detail::typeError(ctx, "this is not a SharedArrayBuffer");
    }
    return ctx->createNumber(static_cast<double>(d->byteLength()));
}

Value* sharedArrayBufferSlice(IContext* ctx, Value* self, size_t argc,
                              Value* argv[]) {
    return sliceBuffer(ctx, self, argc, argv, true);
}

// ArrayBuffer[@@species] / SharedArrayBuffer[@@species]：返回 this。
Value* bufferSpeciesGet(IContext* /*ctx*/, Value* self, size_t /*argc*/,
                        Value* /*argv*/[]) {
    return self;
}

Value* arrayBufferIsView(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createBoolean(false);
    TypeKind k = argv[0]->type()->kind();
    return ctx->createBoolean(k == TypeKind::DataView ||
                              k == TypeKind::TypedArray);
}

// —— DataView ——

Value* dataViewCtorCall(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                        Value* /*argv*/[]) {
    return detail::typeError(ctx, "Constructor DataView requires 'new'");
}

Value* dataViewConstruct(IContext* ctx, Value* /*self*/, size_t argc,
                         Value* argv[]) {
    if (argc == 0) {
        return detail::typeError(ctx, "DataView requires an ArrayBuffer");
    }
    ArrayBufferData* buffer = asArrayBuffer(argv[0]);
    if (buffer == nullptr) {
        return detail::typeError(ctx, "DataView requires an ArrayBuffer");
    }
    size_t offset = 0;
    if (argc > 1 && argv[1]->type()->kind() != TypeKind::Undefined) {
        if (Value* err = toIndex(ctx, argv[1], offset)) return err;
    }
    if (buffer->detached) {
        return detail::typeError(ctx, "DataView buffer is detached");
    }
    const size_t bufferLength = buffer->byteLength();
    if (offset > bufferLength) {
        return detail::rangeError(ctx, "DataView offset is out of bounds");
    }
    size_t length = bufferLength - offset;
    if (argc > 2 && argv[2]->type()->kind() != TypeKind::Undefined) {
        if (Value* err = toIndex(ctx, argv[2], length)) return err;
        if (length > bufferLength - offset) {
            return detail::rangeError(ctx, "DataView length is out of bounds");
        }
    }
    // OrdinaryCreateFromConstructor 可能执行用户代码并分离 buffer；
    // 之后必须再做一次 IsDetachedBuffer 检查（§25.3.2.1）。
    Value* nt = ctx->newTarget();
    Value* ctor = ctx->global()->lookup("DataView");
    if (nt != nullptr && ctor != nullptr && nt->data() != ctor->data()) {
        Value* proto = ctx->getProperty(nt, "prototype");
        if (detail::isErrorValue(proto)) return proto;
    }
    if (buffer->detached) {
        return detail::typeError(ctx, "DataView buffer is detached");
    }
    return ctx->createDataView(argv[0], offset, length);
}

Value* dataViewBufferGet(IContext* ctx, Value* self, size_t /*argc*/,
                         Value* /*argv*/[]) {
    DataViewData* d = asDataView(self);
    if (d == nullptr) return detail::typeError(ctx, "this is not a DataView");
    return d->buffer;
}

Value* dataViewByteOffsetGet(IContext* ctx, Value* self, size_t /*argc*/,
                             Value* /*argv*/[]) {
    DataViewData* d = asDataView(self);
    if (d == nullptr) return detail::typeError(ctx, "this is not a DataView");
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    if (buffer == nullptr || buffer->detached) return ctx->createNumber(0.0);
    return ctx->createNumber(static_cast<double>(d->byteOffset));
}

Value* dataViewByteLengthGet(IContext* ctx, Value* self, size_t /*argc*/,
                             Value* /*argv*/[]) {
    DataViewData* d = asDataView(self);
    if (d == nullptr) return detail::typeError(ctx, "this is not a DataView");
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    if (buffer == nullptr || buffer->detached) return ctx->createNumber(0.0);
    return ctx->createNumber(static_cast<double>(d->byteLength));
}

// GetViewValue/SetViewValue 的前两步：校验 this，ToIndex(requestIndex)。
// 返回 nullptr 表示成功，d/offset 就绪。
Value* viewIndex(IContext* ctx, Value* self, size_t argc, Value* argv[],
                 DataViewData*& d, size_t& offset) {
    d = asDataView(self);
    if (d == nullptr) return detail::typeError(ctx, "this is not a DataView");
    offset = 0;
    return toIndex(ctx, argOrUndefined(ctx, argc, argv, 0), offset);
}

// GetViewValue 的第 2-3 步：detached 检查与越界检查。
Value* viewBounds(IContext* ctx, DataViewData* d, size_t offset, size_t size,
                  size_t& index) {
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    if (buffer == nullptr || buffer->detached) {
        return detail::typeError(ctx, "DataView buffer is detached");
    }
    if (offset > d->byteLength || size > d->byteLength - offset) {
        return detail::rangeError(ctx, "Offset is outside the bounds of the DataView");
    }
    index = d->byteOffset + offset;
    return nullptr;
}

bool viewLittleEndian(size_t argc, Value* argv[], size_t endianIndex) {
    return argc > endianIndex ? detail::toBoolean(argv[endianIndex]) : false;
}

uint64_t readBytes(const uint8_t* bytes, size_t index, size_t size,
                   bool littleEndian) {
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) {
        const size_t pos = littleEndian ? size - 1 - i : i;
        value = (value << 8) | bytes[index + pos];
    }
    return value;
}

void writeBytes(uint8_t* bytes, size_t index, size_t size, uint64_t value,
                bool littleEndian) {
    for (size_t i = 0; i < size; ++i) {
        const size_t pos = littleEndian ? i : size - 1 - i;
        bytes[index + pos] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

Value* dataViewGetInt(IContext* ctx, Value* self, size_t argc, Value* argv[],
                      size_t size, bool isSigned) {
    DataViewData* d = nullptr;
    size_t offset = 0;
    size_t index = 0;
    if (Value* err = viewIndex(ctx, self, argc, argv, d, offset)) return err;
    if (Value* err = viewBounds(ctx, d, offset, size, index)) return err;
    const bool little = viewLittleEndian(argc, argv, 1);
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    const uint64_t raw = readBytes(buffer->data(), index, size, little);
    if (!isSigned) return ctx->createNumber(static_cast<double>(raw));
    // 符号扩展
    const uint64_t signBit = 1ULL << (size * 8 - 1);
    int64_t value = static_cast<int64_t>(raw);
    if ((raw & signBit) != 0) {
        value = static_cast<int64_t>(raw | (~0ULL << (size * 8)));
    }
    return ctx->createNumber(static_cast<double>(value));
}

Value* dataViewGetFloat(IContext* ctx, Value* self, size_t argc, Value* argv[],
                        size_t size) {
    DataViewData* d = nullptr;
    size_t offset = 0;
    size_t index = 0;
    if (Value* err = viewIndex(ctx, self, argc, argv, d, offset)) return err;
    if (Value* err = viewBounds(ctx, d, offset, size, index)) return err;
    const bool little = viewLittleEndian(argc, argv, 1);
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    const uint64_t raw = readBytes(buffer->data(), index, size, little);
    if (size == 4) {
        const uint32_t bits = static_cast<uint32_t>(raw);
        float f = 0.0f;
        std::memcpy(&f, &bits, sizeof f);
        return ctx->createNumber(static_cast<double>(f));
    }
    double value64 = 0.0;
    std::memcpy(&value64, &raw, sizeof value64);
    return ctx->createNumber(value64);
}

Value* dataViewGetBigInt(IContext* ctx, Value* self, size_t argc, Value* argv[],
                         bool isSigned) {
    DataViewData* d = nullptr;
    size_t offset = 0;
    size_t index = 0;
    if (Value* err = viewIndex(ctx, self, argc, argv, d, offset)) return err;
    if (Value* err = viewBounds(ctx, d, offset, 8, index)) return err;
    const bool little = viewLittleEndian(argc, argv, 1);
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    const uint64_t raw = readBytes(buffer->data(), index, 8, little);
    if (isSigned) {
        return ctx->createBigInt(neo::bigint::BigInt(static_cast<int64_t>(raw)));
    }
    return ctx->createBigInt(neo::bigint::BigInt(raw));
}

// SetViewValue 顺序：ToIndex(requestIndex) → ToNumber/ToBigInt(value) →
// IsDetachedBuffer → 越界。value 转换先于 detached 检查（§25.3.1.16）。
Value* dataViewSetInt(IContext* ctx, Value* self, size_t argc, Value* argv[],
                      size_t size) {
    DataViewData* d = nullptr;
    size_t offset = 0;
    size_t index = 0;
    if (Value* err = viewIndex(ctx, self, argc, argv, d, offset)) return err;
    Value* nv = detail::toNumberValue(ctx, argOrUndefined(ctx, argc, argv, 1));
    if (detail::isErrorValue(nv)) return nv;
    if (Value* err = viewBounds(ctx, d, offset, size, index)) return err;
    const bool little = viewLittleEndian(argc, argv, 2);
    const double n = static_cast<NumberData*>(nv->data())->value;
    const int64_t integer = std::isfinite(n) ? static_cast<int64_t>(std::trunc(n)) : 0;
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    writeBytes(buffer->data(), index, size, static_cast<uint64_t>(integer), little);
    return ctx->createUndefined();
}

Value* dataViewSetFloat(IContext* ctx, Value* self, size_t argc, Value* argv[],
                        size_t size) {
    DataViewData* d = nullptr;
    size_t offset = 0;
    size_t index = 0;
    if (Value* err = viewIndex(ctx, self, argc, argv, d, offset)) return err;
    Value* nv = detail::toNumberValue(ctx, argOrUndefined(ctx, argc, argv, 1));
    if (detail::isErrorValue(nv)) return nv;
    if (Value* err = viewBounds(ctx, d, offset, size, index)) return err;
    const bool little = viewLittleEndian(argc, argv, 2);
    const double n = static_cast<NumberData*>(nv->data())->value;
    uint64_t raw = 0;
    if (size == 4) {
        const float f = static_cast<float>(n);
        uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof bits);
        raw = bits;
    } else {
        std::memcpy(&raw, &n, sizeof raw);
    }
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    writeBytes(buffer->data(), index, size, raw, little);
    return ctx->createUndefined();
}

// ToBigInt(value)：对象先 ToPrimitive(Number)，结果必须是 BigInt。
Value* toBigIntForView(IContext* ctx, Value* v, Value*& out) {
    if (v->type()->kind() == TypeKind::BigInt) {
        out = v;
        return nullptr;
    }
    Value* prim = detail::toPrimitive(ctx, v, detail::PreferredType::Number);
    if (detail::isErrorValue(prim)) return prim;
    if (prim->type()->kind() != TypeKind::BigInt) {
        return detail::typeError(ctx, "Cannot convert value to a BigInt");
    }
    out = prim;
    return nullptr;
}

Value* dataViewSetBigInt(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    DataViewData* d = nullptr;
    size_t offset = 0;
    size_t index = 0;
    if (Value* err = viewIndex(ctx, self, argc, argv, d, offset)) return err;
    Value* value = nullptr;
    if (Value* err =
            toBigIntForView(ctx, argOrUndefined(ctx, argc, argv, 1), value)) {
        return err;
    }
    if (Value* err = viewBounds(ctx, d, offset, 8, index)) return err;
    const bool little = viewLittleEndian(argc, argv, 2);
    const uint64_t raw = detail::bigIntValue(value).toUint64();
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    writeBytes(buffer->data(), index, 8, raw, little);
    return ctx->createUndefined();
}

#define NEO_DV_GET_INT(name, size, sign)                                    \
    Value* name(IContext* ctx, Value* self, size_t argc, Value* argv[]) {   \
        return dataViewGetInt(ctx, self, argc, argv, size, sign);           \
    }
#define NEO_DV_GET_FLOAT(name, size)                                        \
    Value* name(IContext* ctx, Value* self, size_t argc, Value* argv[]) {   \
        return dataViewGetFloat(ctx, self, argc, argv, size);               \
    }
#define NEO_DV_SET_INT(name, size)                                          \
    Value* name(IContext* ctx, Value* self, size_t argc, Value* argv[]) {   \
        return dataViewSetInt(ctx, self, argc, argv, size);                 \
    }
#define NEO_DV_SET_FLOAT(name, size)                                        \
    Value* name(IContext* ctx, Value* self, size_t argc, Value* argv[]) {   \
        return dataViewSetFloat(ctx, self, argc, argv, size);               \
    }

NEO_DV_GET_INT(dvGetInt8, 1, true)
NEO_DV_GET_INT(dvGetUint8, 1, false)
NEO_DV_GET_INT(dvGetInt16, 2, true)
NEO_DV_GET_INT(dvGetUint16, 2, false)
NEO_DV_GET_INT(dvGetInt32, 4, true)
NEO_DV_GET_INT(dvGetUint32, 4, false)
NEO_DV_GET_FLOAT(dvGetFloat32, 4)
NEO_DV_GET_FLOAT(dvGetFloat64, 8)
NEO_DV_SET_INT(dvSetInt8, 1)
NEO_DV_SET_INT(dvSetUint8, 1)
NEO_DV_SET_INT(dvSetInt16, 2)
NEO_DV_SET_INT(dvSetUint16, 2)
NEO_DV_SET_INT(dvSetInt32, 4)
NEO_DV_SET_INT(dvSetUint32, 4)
NEO_DV_SET_FLOAT(dvSetFloat32, 4)
NEO_DV_SET_FLOAT(dvSetFloat64, 8)

#undef NEO_DV_GET_INT
#undef NEO_DV_GET_FLOAT
#undef NEO_DV_SET_INT
#undef NEO_DV_SET_FLOAT

Value* dvGetBigInt64(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return dataViewGetBigInt(ctx, self, argc, argv, true);
}
Value* dvGetBigUint64(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return dataViewGetBigInt(ctx, self, argc, argv, false);
}
Value* dvSetBigInt64(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return dataViewSetBigInt(ctx, self, argc, argv);
}
Value* dvSetBigUint64(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return dataViewSetBigInt(ctx, self, argc, argv);
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
        if (ctx->defineProperty(f, "length", len, false, false, true)->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

bool defineGetter(IContext* ctx, Value* target, const char* name, NativeFn getter) {
    Value* f = ctx->createNativeFunction(getter, 0);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, std::string("get ") + name);
    return ctx->defineAccessor(target, name, f, nullptr, false, true)
               ->type()->kind() != TypeKind::Exception;
}

bool defineTag(IContext* ctx, Value* target, const char* tag) {
    return ctx->defineProperty(target, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                               ctx->createString(tag), false, false, true)
               ->type()->kind() != TypeKind::Exception;
}

bool registerCtor(IContext* ctx, const char* name, NativeFn call, NativeFn construct,
                  Value* proto) {
    Value* fn = ctx->createNativeFunction(call, 0);
    if (fn == ctx->oomValue()) return false;
    ctx->setFunctionName(fn, name);
    ctx->setFunctionConstructor(fn, construct);
    // 构造器 length = 1（length 与最小实参数分离）。
    Value* len = ctx->createNumber(1.0);
    if (len == ctx->oomValue()) return false;
    if (ctx->defineProperty(fn, "length", len, false, false, true)->type()->kind() ==
        TypeKind::Exception) {
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
    ctx->defineGlobal(name, fn, true, false, true);
    return true;
}

}  // namespace

bool registerArrayBufferBuiltins(IContext* ctx) {
    // —— 1. 原型 ——
    Value* abProto = ctx->createObject();
    Value* sabProto = ctx->createObject();
    Value* dvProto = ctx->createObject();
    if (abProto == ctx->oomValue() || sabProto == ctx->oomValue() ||
        dvProto == ctx->oomValue()) {
        return false;
    }
    for (Value* proto : {abProto, sabProto, dvProto}) {
        if (ctx->setPrototype(proto, ctx->objectPrototype())->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }
    ctx->setArrayBufferPrototype(abProto);
    ctx->setSharedArrayBufferPrototype(sabProto);
    ctx->setDataViewPrototype(dvProto);

    // —— 2. ArrayBuffer.prototype ——
    if (!defineGetter(ctx, abProto, "byteLength", arrayBufferByteLengthGet)) {
        return false;
    }
    if (!defineMethod(ctx, abProto, "slice", arrayBufferSlice, 0, 2)) return false;
    if (!defineTag(ctx, abProto, "ArrayBuffer")) return false;

    // —— 2b. SharedArrayBuffer.prototype ——
    if (!defineGetter(ctx, sabProto, "byteLength",
                      sharedArrayBufferByteLengthGet)) {
        return false;
    }
    if (!defineMethod(ctx, sabProto, "slice", sharedArrayBufferSlice, 0, 2)) {
        return false;
    }
    if (!defineTag(ctx, sabProto, "SharedArrayBuffer")) return false;

    // —— 3. DataView.prototype ——
    if (!defineGetter(ctx, dvProto, "buffer", dataViewBufferGet)) return false;
    if (!defineGetter(ctx, dvProto, "byteOffset", dataViewByteOffsetGet)) {
        return false;
    }
    if (!defineGetter(ctx, dvProto, "byteLength", dataViewByteLengthGet)) {
        return false;
    }
    // 缺参合法（按 undefined 走规范校验），故 minArgc=0，length 按规范声明。
    const struct {
        const char* name;
        NativeFn fn;
        int length;
    } viewMethods[] = {
        {"getInt8", dvGetInt8, 1},          {"getUint8", dvGetUint8, 1},
        {"getInt16", dvGetInt16, 1},        {"getUint16", dvGetUint16, 1},
        {"getInt32", dvGetInt32, 1},        {"getUint32", dvGetUint32, 1},
        {"getFloat32", dvGetFloat32, 1},    {"getFloat64", dvGetFloat64, 1},
        {"getBigInt64", dvGetBigInt64, 1},  {"getBigUint64", dvGetBigUint64, 1},
        {"setInt8", dvSetInt8, 2},          {"setUint8", dvSetUint8, 2},
        {"setInt16", dvSetInt16, 2},        {"setUint16", dvSetUint16, 2},
        {"setInt32", dvSetInt32, 2},        {"setUint32", dvSetUint32, 2},
        {"setFloat32", dvSetFloat32, 2},    {"setFloat64", dvSetFloat64, 2},
        {"setBigInt64", dvSetBigInt64, 2},  {"setBigUint64", dvSetBigUint64, 2},
    };
    for (const auto& m : viewMethods) {
        if (!defineMethod(ctx, dvProto, m.name, m.fn, 0, m.length)) return false;
    }
    if (!defineTag(ctx, dvProto, "DataView")) return false;

    // —— 4. 构造函数 ——
    if (!registerCtor(ctx, "ArrayBuffer", arrayBufferCtorCall, arrayBufferConstruct,
                      abProto)) {
        return false;
    }
    if (!registerCtor(ctx, "SharedArrayBuffer", sharedArrayBufferCtorCall,
                      sharedArrayBufferConstruct, sabProto)) {
        return false;
    }
    if (!registerCtor(ctx, "DataView", dataViewCtorCall, dataViewConstruct, dvProto)) {
        return false;
    }
    Value* abFn = ctx->global()->lookup("ArrayBuffer");
    if (abFn == nullptr) return false;
    if (!defineMethod(ctx, abFn, "isView", arrayBufferIsView, 0, 1)) return false;
    // ArrayBuffer[@@species] / SharedArrayBuffer[@@species]
    for (const char* ctorName : {"ArrayBuffer", "SharedArrayBuffer"}) {
        Value* ctor = ctx->global()->lookup(ctorName);
        if (ctor == nullptr) return false;
        Value* getter = ctx->createNativeFunction(bufferSpeciesGet, 0);
        if (getter == ctx->oomValue()) return false;
        ctx->setFunctionName(getter, "get [Symbol.species]");
        if (ctx->defineAccessor(ctor,
                                ctx->getWellKnownSymbol(WellKnownSymbol::Species),
                                getter, nullptr, false, true)
                ->type()->kind() == TypeKind::Exception) {
            return false;
        }
    }
    return true;
}

}  // namespace neo::core
