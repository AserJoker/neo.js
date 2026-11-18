// ArrayBufferBuiltins.cpp - ArrayBuffer 与 DataView（ES2024 §25.1 / §25.3）
//
// 设计要点：
//   - ArrayBuffer 持 vector<uint8_t>；DataView 以 Value* 引用它并直接读写；
//   - DataView 默认大端，littleEndian 参数按 ToBoolean 解释；
//   - 越界访问抛 RangeError；BigInt 读写要求参数为 BigInt（ToBigInt 语义）；
//   - 暂不实现 SharedArrayBuffer / Atomics / 分离（detach）。
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
    if (std::isnan(n) || n < 0.0 || n > kMaxIndex) {
        return detail::rangeError(ctx, "Invalid array buffer length");
    }
    out = static_cast<size_t>(std::trunc(n));
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
    return ctx->createArrayBuffer(length);
}

Value* arrayBufferByteLengthGet(IContext* ctx, Value* self, size_t /*argc*/,
                                Value* /*argv*/[]) {
    ArrayBufferData* d = asArrayBuffer(self);
    if (d == nullptr) {
        return detail::typeError(ctx, "this is not an ArrayBuffer");
    }
    return ctx->createNumber(static_cast<double>(d->bytes.size()));
}

Value* arrayBufferSlice(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    ArrayBufferData* d = asArrayBuffer(self);
    if (d == nullptr) {
        return detail::typeError(ctx, "this is not an ArrayBuffer");
    }
    const size_t len = d->bytes.size();
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
    Value* result = ctx->createArrayBuffer(end - start);
    if (detail::isErrorValue(result)) return result;
    ArrayBufferData* rd = asArrayBuffer(result);
    if (rd != nullptr && end > start) {
        std::memcpy(rd->bytes.data(), d->bytes.data() + start, end - start);
    }
    return result;
}

Value* arrayBufferIsView(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createBoolean(false);
    TypeKind k = argv[0]->type()->kind();
    return ctx->createBoolean(k == TypeKind::DataView);
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
    const size_t bufferLength = buffer->bytes.size();
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
    return ctx->createNumber(static_cast<double>(d->byteOffset));
}

Value* dataViewByteLengthGet(IContext* ctx, Value* self, size_t /*argc*/,
                             Value* /*argv*/[]) {
    DataViewData* d = asDataView(self);
    if (d == nullptr) return detail::typeError(ctx, "this is not a DataView");
    return ctx->createNumber(static_cast<double>(d->byteLength));
}

// 解析 getter/setter 的公共参数，定位到 buffer 内的绝对下标。
Value* viewLocation(IContext* ctx, Value* self, size_t argc, Value* argv[],
                    size_t size, size_t& index, bool& littleEndian,
                    size_t endianIndex) {
    DataViewData* d = asDataView(self);
    if (d == nullptr) return detail::typeError(ctx, "this is not a DataView");
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    if (buffer == nullptr || buffer->detached) {
        return detail::typeError(ctx, "DataView buffer is detached");
    }
    size_t offset = 0;
    if (Value* err = toIndex(ctx, argOrUndefined(ctx, argc, argv, 0), offset)) {
        return err;
    }
    if (offset > d->byteLength || size > d->byteLength - offset) {
        return detail::rangeError(ctx, "Offset is outside the bounds of the DataView");
    }
    index = d->byteOffset + offset;
    littleEndian = false;
    if (argc > endianIndex) littleEndian = detail::toBoolean(argv[endianIndex]);
    return nullptr;
}

uint64_t readBytes(const std::vector<uint8_t>& bytes, size_t index, size_t size,
                   bool littleEndian) {
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) {
        const size_t pos = littleEndian ? size - 1 - i : i;
        value = (value << 8) | bytes[index + pos];
    }
    return value;
}

void writeBytes(std::vector<uint8_t>& bytes, size_t index, size_t size,
                uint64_t value, bool littleEndian) {
    for (size_t i = 0; i < size; ++i) {
        const size_t pos = littleEndian ? i : size - 1 - i;
        bytes[index + pos] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

Value* dataViewGetInt(IContext* ctx, Value* self, size_t argc, Value* argv[],
                      size_t size, bool isSigned) {
    size_t index = 0;
    bool little = false;
    if (Value* err = viewLocation(ctx, self, argc, argv, size, index, little, 1)) {
        return err;
    }
    ArrayBufferData* buffer = asArrayBuffer(asDataView(self)->buffer);
    const uint64_t raw = readBytes(buffer->bytes, index, size, little);
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
    size_t index = 0;
    bool little = false;
    if (Value* err = viewLocation(ctx, self, argc, argv, size, index, little, 1)) {
        return err;
    }
    ArrayBufferData* buffer = asArrayBuffer(asDataView(self)->buffer);
    const uint64_t raw = readBytes(buffer->bytes, index, size, little);
    if (size == 4) {
        const uint32_t bits = static_cast<uint32_t>(raw);
        float f = 0.0f;
        std::memcpy(&f, &bits, sizeof f);
        return ctx->createNumber(static_cast<double>(f));
    }
    double d = 0.0;
    std::memcpy(&d, &raw, sizeof d);
    return ctx->createNumber(d);
}

Value* dataViewGetBigInt(IContext* ctx, Value* self, size_t argc, Value* argv[],
                         bool isSigned) {
    size_t index = 0;
    bool little = false;
    if (Value* err = viewLocation(ctx, self, argc, argv, 8, index, little, 1)) {
        return err;
    }
    ArrayBufferData* buffer = asArrayBuffer(asDataView(self)->buffer);
    const uint64_t raw = readBytes(buffer->bytes, index, 8, little);
    if (isSigned) {
        return ctx->createBigInt(neo::bigint::BigInt(static_cast<int64_t>(raw)));
    }
    return ctx->createBigInt(neo::bigint::BigInt(raw));
}

Value* dataViewSetInt(IContext* ctx, Value* self, size_t argc, Value* argv[],
                      size_t size) {
    size_t index = 0;
    bool little = false;
    if (Value* err = viewLocation(ctx, self, argc, argv, size, index, little, 2)) {
        return err;
    }
    Value* nv = detail::toNumberValue(ctx, argOrUndefined(ctx, argc, argv, 1));
    if (detail::isErrorValue(nv)) return nv;
    const double n = static_cast<NumberData*>(nv->data())->value;
    const int64_t integer = std::isfinite(n) ? static_cast<int64_t>(std::trunc(n)) : 0;
    ArrayBufferData* buffer = asArrayBuffer(asDataView(self)->buffer);
    writeBytes(buffer->bytes, index, size, static_cast<uint64_t>(integer), little);
    return ctx->createUndefined();
}

Value* dataViewSetFloat(IContext* ctx, Value* self, size_t argc, Value* argv[],
                        size_t size) {
    size_t index = 0;
    bool little = false;
    if (Value* err = viewLocation(ctx, self, argc, argv, size, index, little, 2)) {
        return err;
    }
    Value* nv = detail::toNumberValue(ctx, argOrUndefined(ctx, argc, argv, 1));
    if (detail::isErrorValue(nv)) return nv;
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
    ArrayBufferData* buffer = asArrayBuffer(asDataView(self)->buffer);
    writeBytes(buffer->bytes, index, size, raw, little);
    return ctx->createUndefined();
}

Value* dataViewSetBigInt(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    size_t index = 0;
    bool little = false;
    if (Value* err = viewLocation(ctx, self, argc, argv, 8, index, little, 2)) {
        return err;
    }
    Value* value = argOrUndefined(ctx, argc, argv, 1);
    if (value->type()->kind() != TypeKind::BigInt) {
        return detail::typeError(ctx, "Cannot convert value to a BigInt");
    }
    const uint64_t raw = detail::bigIntValue(value).toUint64();
    ArrayBufferData* buffer = asArrayBuffer(asDataView(self)->buffer);
    writeBytes(buffer->bytes, index, 8, raw, little);
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
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
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
    Value* dvProto = ctx->createObject();
    if (abProto == ctx->oomValue() || dvProto == ctx->oomValue()) return false;
    for (Value* proto : {abProto, dvProto}) {
        if (ctx->setPrototype(proto, ctx->objectPrototype())->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }
    ctx->setArrayBufferPrototype(abProto);
    ctx->setDataViewPrototype(dvProto);

    // —— 2. ArrayBuffer.prototype ——
    if (!defineGetter(ctx, abProto, "byteLength", arrayBufferByteLengthGet)) {
        return false;
    }
    if (!defineMethod(ctx, abProto, "slice", arrayBufferSlice, 0)) return false;
    if (!defineTag(ctx, abProto, "ArrayBuffer")) return false;

    // —— 3. DataView.prototype ——
    if (!defineGetter(ctx, dvProto, "buffer", dataViewBufferGet)) return false;
    if (!defineGetter(ctx, dvProto, "byteOffset", dataViewByteOffsetGet)) {
        return false;
    }
    if (!defineGetter(ctx, dvProto, "byteLength", dataViewByteLengthGet)) {
        return false;
    }
    const struct {
        const char* name;
        NativeFn fn;
        size_t minArgc;
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
        if (!defineMethod(ctx, dvProto, m.name, m.fn, m.minArgc)) return false;
    }
    if (!defineTag(ctx, dvProto, "DataView")) return false;

    // —— 4. 构造函数 ——
    if (!registerCtor(ctx, "ArrayBuffer", arrayBufferCtorCall, arrayBufferConstruct,
                      abProto)) {
        return false;
    }
    if (!registerCtor(ctx, "DataView", dataViewCtorCall, dataViewConstruct, dvProto)) {
        return false;
    }
    Value* abFn = ctx->global()->lookup("ArrayBuffer");
    if (abFn == nullptr) return false;
    if (!defineMethod(ctx, abFn, "isView", arrayBufferIsView, 1)) return false;
    return true;
}

}  // namespace neo::core
