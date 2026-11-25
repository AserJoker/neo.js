// allocator.cpp - MallocAllocator 实现 + 工厂函数（FFI 收口：malloc/free/aligned_alloc）
//
// 本文件是 neo.js 中唯一直接调用 malloc/free 等内存 FFI 的地方（ADR-027 §5.6）。
// 业务代码通过 IAllocator* 接口注入使用，不直接接触这些 FFI。
//
// 内存配额：alloc 前 tryReserve 原子检查 used+bytes<=limit，超限返回 nullptr 拦截。
// 记账用返回指针前的 AllocHeader（用户字节数 + raw 偏移），free 时 O(1) 读回递减
// used。alloc(0) 统一记 1 字节（malloc 返回非空哨兵，free 兼容）。
//
// 关联 ADR: 008（IAllocator）、010（接口-实现-工厂）、027（FFI 封装）、028（infra §3 配额）

#include "infra/MallocAllocator.h"  // private/infra/MallocAllocator.h

#include <cstdlib>
#include <memory>

namespace neo::infra {

namespace {

// 分配头：紧贴返回指针之前，记录用户字节数与 raw 基址偏移。
// 普通分配 offset 恒为 sizeof(AllocHeader)；对齐分配因前置 slack 而不同。
struct AllocHeader {
    size_t bytes;   // 计入 used_ 的字节数（alloc(0) 记 1）
    size_t offset;  // 返回指针相对 raw 基址的偏移
};

constexpr size_t kHeaderSize = sizeof(AllocHeader);

}  // namespace

MallocAllocator::MallocAllocator() = default;

MallocAllocator::MallocAllocator(size_t limit) {
    limit_.store(limit, std::memory_order_relaxed);
}

bool MallocAllocator::tryReserve(size_t bytes) {
    // 无锁 CAS 循环：used_ + bytes <= limit_ 才提交，否则拦截
    size_t cur = used_.load(std::memory_order_relaxed);
    for (;;) {
        // 溢出检查（bytes 极大时 used+bytes 可能回绕）
        if (cur > SIZE_MAX - bytes) return false;
        size_t next = cur + bytes;
        if (next > limit_.load(std::memory_order_relaxed)) return false;
        if (used_.compare_exchange_weak(cur, next,
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
            return true;
        }
        // CAS 失败：cur 已更新为最新值，重试
    }
}

void* MallocAllocator::alloc(size_t bytes) {
    // malloc(0) 行为实现定义，统一返回非空哨兵（与 free 兼容），记账记 1
    size_t real = bytes == 0 ? 1 : bytes;
    if (!tryReserve(real)) return nullptr;  // 超限拦截
    void* raw = std::malloc(kHeaderSize + real);
    if (raw == nullptr) {
        // malloc 失败：回滚已预留配额
        used_.fetch_sub(real, std::memory_order_acq_rel);
        return nullptr;
    }
    auto* h = static_cast<AllocHeader*>(raw);
    h->bytes = real;
    h->offset = kHeaderSize;
    return static_cast<char*>(raw) + kHeaderSize;
}

void* MallocAllocator::allocAligned(size_t bytes, size_t align) {
    // align 必须是 2 的幂（调用方契约，不防御性校验：ADR-007 内部信任契约）
    // aligned_alloc 要求 size 是 align 的整数倍（C11），用倍数对齐
    size_t aligned_bytes = bytes == 0 ? align : ((bytes + align - 1) / align) * align;
    if (!tryReserve(aligned_bytes)) return nullptr;  // 超限拦截
    // 预留 header + 对齐 slack；底层对齐取 max(align, alignof(AllocHeader))。
    const size_t base_align = align > alignof(AllocHeader) ? align : alignof(AllocHeader);
    size_t total = aligned_bytes + kHeaderSize + base_align;
    size_t alloc_size = ((total + base_align - 1) / base_align) * base_align;
    void* raw;
#if defined(_WIN32)
    raw = _aligned_malloc(alloc_size, base_align);
#else
    raw = std::aligned_alloc(base_align, alloc_size);
#endif
    if (raw == nullptr) {
        used_.fetch_sub(aligned_bytes, std::memory_order_acq_rel);
        return nullptr;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(raw) + kHeaderSize;
    const uintptr_t aligned = (base + align - 1) & ~(static_cast<uintptr_t>(align) - 1);
    auto* h = reinterpret_cast<AllocHeader*>(aligned - kHeaderSize);
    h->bytes = aligned_bytes;
    h->offset = aligned - reinterpret_cast<uintptr_t>(raw);
    return reinterpret_cast<void*>(aligned);
}

void MallocAllocator::free(void* p) {
    if (p == nullptr) return;
    auto* h = reinterpret_cast<AllocHeader*>(static_cast<char*>(p) - kHeaderSize);
    const size_t bytes = h->bytes;
    const size_t offset = h->offset;
    used_.fetch_sub(bytes, std::memory_order_acq_rel);
    std::free(static_cast<char*>(p) - offset);
}

void MallocAllocator::freeAligned(void* p) {
    if (p == nullptr) return;
    auto* h = reinterpret_cast<AllocHeader*>(static_cast<char*>(p) - kHeaderSize);
    const size_t bytes = h->bytes;
    const size_t offset = h->offset;
    used_.fetch_sub(bytes, std::memory_order_acq_rel);
    void* raw = static_cast<char*>(p) - offset;
#if defined(_WIN32)
    _aligned_free(raw);
#else
    std::free(raw);
#endif
}

void MallocAllocator::setLimit(size_t bytes) {
    limit_.store(bytes, std::memory_order_relaxed);
}

size_t MallocAllocator::limit() const {
    return limit_.load(std::memory_order_relaxed);
}

size_t MallocAllocator::used() const {
    return used_.load(std::memory_order_relaxed);
}

// 工厂函数：创建无限额 MallocAllocator（默认）
std::unique_ptr<IAllocator> createMallocAllocator() {
    return std::make_unique<MallocAllocator>();
}

// 工厂函数：创建带限额 MallocAllocator
std::unique_ptr<IAllocator> createMallocAllocatorWithLimit(size_t bytes) {
    return std::make_unique<MallocAllocator>(bytes);
}

}  // namespace neo::infra
