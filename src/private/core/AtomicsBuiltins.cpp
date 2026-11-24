// AtomicsBuiltins.cpp - Atomics 原子操作（ES2024 §25.4）
//
// 仅作用于 SharedArrayBuffer 支撑的整数 TypedArray。底层用 std::atomic_ref<T>
// 直接读写共享块，内存序固定 seq_cst（与规范一致）。
//
// wait/notify：按 (共享块, 块内字节偏移) 维护等待队列（mutex + condition_variable）。
// 队列不主动回收——不同地址的等待点数量由脚本控制，规模通常很小。
//
// 关联 ADR: 020（标准库组织）、006（异常即值）

#include "core/AtomicsBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/ArrayBuffer.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "core/types/TypedArray.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace neo::core {

namespace {

constexpr double kMaxIndex = 9007199254740991.0;  // 2^53 - 1

// 规范 IsIntegerTypedArray：所有整数元素类型（含 Uint8Clamped）。
bool isIntegerKind(TypedArrayKind kind) {
    switch (kind) {
        case TypedArrayKind::Int8:
        case TypedArrayKind::Uint8:
        case TypedArrayKind::Uint8Clamped:
        case TypedArrayKind::Int16:
        case TypedArrayKind::Uint16:
        case TypedArrayKind::Int32:
        case TypedArrayKind::Uint32:
        case TypedArrayKind::BigInt64:
        case TypedArrayKind::BigUint64:
            return true;
        case TypedArrayKind::Float32:
        case TypedArrayKind::Float64:
            return false;
    }
    return false;
}

Value* toIndex(IContext* ctx, Value* v, size_t& out) {
    Value* nv = detail::toNumberValue(ctx, v);
    if (detail::isErrorValue(nv)) return nv;
    const double n = static_cast<NumberData*>(nv->data())->value;
    // ToIndex：NaN → 0，再检查 0..2^53-1。
    if (std::isnan(n)) {
        out = 0;
        return nullptr;
    }
    if (n < 0.0 || n > kMaxIndex) {
        return detail::rangeError(ctx, "Invalid atomic index");
    }
    out = static_cast<size_t>(std::trunc(n));
    return nullptr;
}

Value* argOrUndefined(IContext* ctx, size_t argc, Value* argv[], size_t i) {
    return i < argc ? argv[i] : ctx->createUndefined();
}

struct AtomicTarget {
    uint8_t* data = nullptr;
    const void* block = nullptr;  // 共享块身份（wait/notify 队列键）
    size_t byteOffset = 0;        // 块内绝对偏移
    TypedArrayKind kind = TypedArrayKind::Uint8;
};

// 校验参数并定位到共享块内的绝对地址。
Value* resolveTarget(IContext* ctx, Value* taValue, Value* indexValue,
                     AtomicTarget& out) {
    TypedArrayData* d = asTypedArray(taValue);
    if (d == nullptr) {
        return detail::typeError(ctx, "Atomics operation requires a TypedArray");
    }
    if (!isIntegerKind(d->kind)) {
        return detail::typeError(
            ctx, "Atomics operation requires an integer TypedArray");
    }
    ArrayBufferData* buffer = asArrayBuffer(d->buffer);
    if (buffer == nullptr || !buffer->isShared()) {
        return detail::typeError(
            ctx, "Atomics operation requires a SharedArrayBuffer");
    }

    size_t index = 0;
    if (Value* err = toIndex(ctx, indexValue, index)) return err;
    if (index >= d->length) {
        return detail::rangeError(ctx, "Atomic index is out of range");
    }

    out.kind = d->kind;
    out.byteOffset = d->byteOffset + index * typedArrayElementSize(d->kind);
    out.data = buffer->data() + out.byteOffset;
    out.block = buffer->shared.get();
    return nullptr;
}

// 按元素类型分派到 std::atomic_ref<T>。
template <typename T, typename Fn>
Value* atomicApply(AtomicTarget& t, Fn fn) {
    std::atomic_ref<T> ref(*reinterpret_cast<T*>(t.data));
    return fn(ref);
}

template <typename Fn>
Value* dispatchAtomic(IContext* ctx, AtomicTarget& t, Fn fn) {
    switch (t.kind) {
        case TypedArrayKind::Int8: return atomicApply<int8_t>(t, fn);
        case TypedArrayKind::Uint8:
        case TypedArrayKind::Uint8Clamped:
            return atomicApply<uint8_t>(t, fn);
        case TypedArrayKind::Int16: return atomicApply<int16_t>(t, fn);
        case TypedArrayKind::Uint16: return atomicApply<uint16_t>(t, fn);
        case TypedArrayKind::Int32: return atomicApply<int32_t>(t, fn);
        case TypedArrayKind::Uint32: return atomicApply<uint32_t>(t, fn);
        case TypedArrayKind::BigInt64: return atomicApply<int64_t>(t, fn);
        case TypedArrayKind::BigUint64: return atomicApply<uint64_t>(t, fn);
        case TypedArrayKind::Float32:
        case TypedArrayKind::Float64: break;
    }
    return detail::typeError(ctx, "Unsupported atomic element type");
}

// —— 原子操作 ——

Value* atomicsLoad(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    AtomicTarget t;
    if (Value* err = resolveTarget(ctx, argOrUndefined(ctx, argc, argv, 0),
                                   argOrUndefined(ctx, argc, argv, 1), t)) {
        return err;
    }
    return dispatchAtomic(ctx, t, [&](auto& ref) -> Value* {
        using T = typename std::remove_reference_t<decltype(ref)>::value_type;
        const T value = ref.load(std::memory_order_seq_cst);
        return typedArrayRawToValue(ctx, t.kind, static_cast<uint64_t>(value));
    });
}

Value* atomicsStore(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    AtomicTarget t;
    if (Value* err = resolveTarget(ctx, argOrUndefined(ctx, argc, argv, 0),
                                   argOrUndefined(ctx, argc, argv, 1), t)) {
        return err;
    }
    uint64_t raw = 0;
    if (Value* err = typedArrayValueToRaw(
            ctx, t.kind, argOrUndefined(ctx, argc, argv, 2), raw)) {
        return err;
    }
    return dispatchAtomic(ctx, t, [&](auto& ref) -> Value* {
        using T = typename std::remove_reference_t<decltype(ref)>::value_type;
        ref.store(static_cast<T>(raw), std::memory_order_seq_cst);
        return typedArrayRawToValue(ctx, t.kind, raw);
    });
}

enum class RmwOp { Add, Sub, And, Or, Xor, Exchange };

Value* atomicsRmw(IContext* ctx, size_t argc, Value* argv[], RmwOp op) {
    AtomicTarget t;
    if (Value* err = resolveTarget(ctx, argOrUndefined(ctx, argc, argv, 0),
                                   argOrUndefined(ctx, argc, argv, 1), t)) {
        return err;
    }
    uint64_t raw = 0;
    if (Value* err = typedArrayValueToRaw(
            ctx, t.kind, argOrUndefined(ctx, argc, argv, 2), raw)) {
        return err;
    }
    return dispatchAtomic(ctx, t, [&](auto& ref) -> Value* {
        using T = typename std::remove_reference_t<decltype(ref)>::value_type;
        const T operand = static_cast<T>(raw);
        T old = 0;
        switch (op) {
            case RmwOp::Add: old = ref.fetch_add(operand); break;
            case RmwOp::Sub: old = ref.fetch_sub(operand); break;
            case RmwOp::And: old = ref.fetch_and(operand); break;
            case RmwOp::Or: old = ref.fetch_or(operand); break;
            case RmwOp::Xor: old = ref.fetch_xor(operand); break;
            case RmwOp::Exchange: old = ref.exchange(operand); break;
        }
        return typedArrayRawToValue(ctx, t.kind, static_cast<uint64_t>(old));
    });
}

Value* atomicsAdd(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return atomicsRmw(ctx, argc, argv, RmwOp::Add);
}
Value* atomicsSub(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return atomicsRmw(ctx, argc, argv, RmwOp::Sub);
}
Value* atomicsAnd(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return atomicsRmw(ctx, argc, argv, RmwOp::And);
}
Value* atomicsOr(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return atomicsRmw(ctx, argc, argv, RmwOp::Or);
}
Value* atomicsXor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return atomicsRmw(ctx, argc, argv, RmwOp::Xor);
}
Value* atomicsExchange(IContext* ctx, Value* /*self*/, size_t argc,
                       Value* argv[]) {
    return atomicsRmw(ctx, argc, argv, RmwOp::Exchange);
}

Value* atomicsCompareExchange(IContext* ctx, Value* /*self*/, size_t argc,
                              Value* argv[]) {
    AtomicTarget t;
    if (Value* err = resolveTarget(ctx, argOrUndefined(ctx, argc, argv, 0),
                                   argOrUndefined(ctx, argc, argv, 1), t)) {
        return err;
    }
    uint64_t rawExpected = 0;
    if (Value* err = typedArrayValueToRaw(
            ctx, t.kind, argOrUndefined(ctx, argc, argv, 2), rawExpected)) {
        return err;
    }
    uint64_t rawReplacement = 0;
    if (Value* err = typedArrayValueToRaw(
            ctx, t.kind, argOrUndefined(ctx, argc, argv, 3), rawReplacement)) {
        return err;
    }
    return dispatchAtomic(ctx, t, [&](auto& ref) -> Value* {
        using T = typename std::remove_reference_t<decltype(ref)>::value_type;
        T expected = static_cast<T>(rawExpected);
        ref.compare_exchange_strong(expected, static_cast<T>(rawReplacement),
                                    std::memory_order_seq_cst);
        // 成功时 expected 保持旧值；失败时被写入实际值——两者都是旧值。
        return typedArrayRawToValue(ctx, t.kind,
                                    static_cast<uint64_t>(expected));
    });
}

Value* atomicsIsLockFree(IContext* ctx, Value* /*self*/, size_t argc,
                         Value* argv[]) {
    Value* nv = detail::toNumberValue(ctx, argOrUndefined(ctx, argc, argv, 0));
    if (detail::isErrorValue(nv)) return nv;
    const double n = static_cast<NumberData*>(nv->data())->value;
    if (std::isnan(n) || n <= 0.0 || !std::isfinite(n)) {
        return ctx->createBoolean(false);
    }
    const size_t size = static_cast<size_t>(std::trunc(n));
    switch (size) {
        case 1: return ctx->createBoolean(
                    std::atomic_ref<uint8_t>::is_always_lock_free);
        case 2: return ctx->createBoolean(
                    std::atomic_ref<uint16_t>::is_always_lock_free);
        case 4: return ctx->createBoolean(
                    std::atomic_ref<uint32_t>::is_always_lock_free);
        case 8: return ctx->createBoolean(
                    std::atomic_ref<uint64_t>::is_always_lock_free);
        default: return ctx->createBoolean(false);
    }
}

// —— wait / notify ——

struct WaitQueue {
    std::mutex mutex;
    std::condition_variable cv;
    size_t waiting = 0;  // 当前等待者
    size_t woken = 0;    // 已通知但尚未返回的等待者
};

std::mutex g_queuesMutex;
std::map<std::pair<const void*, size_t>, std::shared_ptr<WaitQueue>> g_queues;

std::shared_ptr<WaitQueue> waitQueueFor(const void* block, size_t offset) {
    std::lock_guard<std::mutex> lock(g_queuesMutex);
    auto key = std::make_pair(block, offset);
    std::shared_ptr<WaitQueue>& queue = g_queues[key];
    if (!queue) queue = std::make_shared<WaitQueue>();
    return queue;
}

// wait 只接受 Int32Array / BigInt64Array（规范 WaitableIntegerTypedArray）。
bool isWaitableKind(TypedArrayKind kind) {
    return kind == TypedArrayKind::Int32 || kind == TypedArrayKind::BigInt64;
}

// 在锁内重新读取当前值，判断是否仍等于 expected。
bool stillEquals(AtomicTarget& t, uint64_t rawExpected) {
    if (t.kind == TypedArrayKind::Int32) {
        std::atomic_ref<int32_t> ref(
            *reinterpret_cast<int32_t*>(t.data));
        return ref.load(std::memory_order_seq_cst) ==
               static_cast<int32_t>(rawExpected);
    }
    std::atomic_ref<int64_t> ref(*reinterpret_cast<int64_t*>(t.data));
    return ref.load(std::memory_order_seq_cst) ==
           static_cast<int64_t>(rawExpected);
}

Value* atomicsWait(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    // 宿主策略：主上下文禁止阻塞（与浏览器/Node 一致），仅 worker 可 wait。
    if (!ctx->isWorkerContext()) {
        return detail::typeError(
            ctx, "Atomics.wait cannot be called in this context");
    }
    AtomicTarget t;
    if (Value* err = resolveTarget(ctx, argOrUndefined(ctx, argc, argv, 0),
                                   argOrUndefined(ctx, argc, argv, 1), t)) {
        return err;
    }
    if (!isWaitableKind(t.kind)) {
        return detail::typeError(
            ctx, "Atomics.wait requires an Int32Array or BigInt64Array");
    }

    uint64_t rawExpected = 0;
    if (Value* err = typedArrayValueToRaw(
            ctx, t.kind, argOrUndefined(ctx, argc, argv, 2), rawExpected)) {
        return err;
    }

    // timeout：undefined / NaN → 无限等待；负值 → 0。
    double timeoutMs = std::numeric_limits<double>::infinity();
    if (argc > 3 && argv[3]->type()->kind() != TypeKind::Undefined) {
        Value* nv = detail::toNumberValue(ctx, argv[3]);
        if (detail::isErrorValue(nv)) return nv;
        const double n = static_cast<NumberData*>(nv->data())->value;
        if (!std::isnan(n)) timeoutMs = n < 0.0 ? 0.0 : n;
    }

    if (!stillEquals(t, rawExpected)) return ctx->createString("not-equal");

    std::shared_ptr<WaitQueue> queue = waitQueueFor(t.block, t.byteOffset);
    std::unique_lock<std::mutex> lock(queue->mutex);
    if (!stillEquals(t, rawExpected)) return ctx->createString("not-equal");

    ++queue->waiting;
    // 有限时间片轮询停止标记：worker terminate 时即使无限等待也能退出。
    const bool infinite = std::isinf(timeoutMs);
    const auto poll = std::chrono::milliseconds(50);
    auto deadline = std::chrono::steady_clock::time_point::max();
    if (!infinite) {
        deadline = std::chrono::steady_clock::now() +
                   std::chrono::duration_cast<
                       std::chrono::steady_clock::duration>(
                       std::chrono::duration<double, std::milli>(timeoutMs));
    }

    bool woken = false;
    for (;;) {
        if (ctx->isStopRequested()) break;
        auto slice = poll;
        if (!infinite) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            const auto remaining = deadline - now;
            if (remaining < slice) {
                slice = std::chrono::duration_cast<std::chrono::milliseconds>(
                    remaining);
            }
        }
        woken = queue->cv.wait_for(lock, slice,
                                   [&queue] { return queue->woken > 0; });
        if (woken) break;
    }
    --queue->waiting;

    if (woken && queue->woken > 0) {
        --queue->woken;
        return ctx->createString("ok");
    }
    return ctx->createString("timed-out");
}

Value* atomicsNotify(IContext* ctx, Value* /*self*/, size_t argc,
                     Value* argv[]) {
    AtomicTarget t;
    if (Value* err = resolveTarget(ctx, argOrUndefined(ctx, argc, argv, 0),
                                   argOrUndefined(ctx, argc, argv, 1), t)) {
        return err;
    }
    if (!isWaitableKind(t.kind)) {
        return detail::typeError(
            ctx, "Atomics.notify requires an Int32Array or BigInt64Array");
    }

    // count：undefined → 全部；否则 ToIntegerOrInfinity，负值按 0。
    size_t count = std::numeric_limits<size_t>::max();
    if (argc > 2 && argv[2]->type()->kind() != TypeKind::Undefined) {
        Value* nv = detail::toNumberValue(ctx, argv[2]);
        if (detail::isErrorValue(nv)) return nv;
        const double n = static_cast<NumberData*>(nv->data())->value;
        if (std::isnan(n) || n <= 0.0) {
            count = 0;
        } else if (!std::isinf(n)) {
            count = static_cast<size_t>(std::trunc(n));
        }
    }

    std::shared_ptr<WaitQueue> queue = waitQueueFor(t.block, t.byteOffset);
    std::lock_guard<std::mutex> lock(queue->mutex);
    const size_t available =
        queue->waiting > queue->woken ? queue->waiting - queue->woken : 0;
    const size_t woken = std::min(count, available);
    queue->woken += woken;
    if (woken > 0) queue->cv.notify_all();
    return ctx->createNumber(static_cast<double>(woken));
}

// —— 注册 ——

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, false, true)
               ->type()->kind() != TypeKind::Exception;
}

}  // namespace

bool registerAtomicsBuiltins(IContext* ctx) {
    Value* atomics = ctx->createObject();
    if (atomics == ctx->oomValue()) return false;
    if (ctx->setPrototype(atomics, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }

    const struct {
        const char* name;
        NativeFn fn;
        size_t minArgc;
    } methods[] = {
        {"add", atomicsAdd, 3},
        {"sub", atomicsSub, 3},
        {"and", atomicsAnd, 3},
        {"or", atomicsOr, 3},
        {"xor", atomicsXor, 3},
        {"exchange", atomicsExchange, 3},
        {"compareExchange", atomicsCompareExchange, 4},
        {"load", atomicsLoad, 2},
        {"store", atomicsStore, 3},
        {"isLockFree", atomicsIsLockFree, 1},
        {"wait", atomicsWait, 4},
        {"notify", atomicsNotify, 3},
    };
    for (const auto& method : methods) {
        if (!defineMethod(ctx, atomics, method.name, method.fn,
                          method.minArgc)) {
            return false;
        }
    }

    if (ctx->defineProperty(
            atomics, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
            ctx->createString("Atomics"), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    ctx->defineGlobal("Atomics", atomics, true, false, true);
    return true;
}

}  // namespace neo::core
