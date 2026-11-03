// allocator.cpp - MallocAllocator 实现 + 工厂函数（FFI 收口：malloc/free/aligned_alloc）
//
// 本文件是 neo.js 中唯一直接调用 malloc/free 等内存 FFI 的地方（ADR-027 §5.6）。
// 业务代码通过 IAllocator* 接口注入使用，不直接接触这些 FFI。
//
// 内存配额：alloc 前 tryReserve 原子检查 used+bytes<=limit，超限返回 nullptr 拦截。
// 记账表 records_ 记录 ptr→真实字节数，free 时查表递减 used。alloc(0) 统一记 1 字节
// （malloc 返回非空哨兵，free 兼容）。
//
// 关联 ADR: 008（IAllocator）、010（接口-实现-工厂）、027（FFI 封装）、028（infra §3 配额）

#include "infra/MallocAllocator.h"  // private/infra/MallocAllocator.h

#include <cstdlib>
#include <memory>

namespace neo::infra {

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
        if (used_.compare_exchange_weak(cur, next,  // NEO_NO_COVERAGE (CAS 重试分支：单线程测试不可达)
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
            return true;
        }
        // CAS 失败：cur 已更新为最新值，重试
    }
}

void MallocAllocator::record(void* p, size_t bytes) {
    if (p == nullptr) return;  // NEO_NO_COVERAGE (alloc 内 malloc 失败已提前 return)
    std::lock_guard<std::mutex> lk(guard_);
    records_[p] = bytes;
}

size_t MallocAllocator::release(void* p) {
    if (p == nullptr) return 0;  // NEO_NO_COVERAGE (free/freeAligned 入口已拦截 nullptr)
    size_t bytes = 0;
    {
        std::lock_guard<std::mutex> lk(guard_);
        auto it = records_.find(p);
        if (it == records_.end()) return 0;  // NEO_NO_COVERAGE (未登记：double free 或外部指针)
        bytes = it->second;
        records_.erase(it);
    }
    // 递减 used_（不会下溢：已登记的 bytes 一定 ≤ used_）
    used_.fetch_sub(bytes, std::memory_order_acq_rel);
    return bytes;
}

void* MallocAllocator::alloc(size_t bytes) {
    // malloc(0) 行为实现定义，统一返回非空哨兵（与 free 兼容），记账记 1
    size_t real = bytes == 0 ? 1 : bytes;
    if (!tryReserve(real)) return nullptr;  // 超限拦截
    void* p = std::malloc(real);
    if (p == nullptr) {  // NEO_NO_COVERAGE (malloc 失败回滚：真实 OOM 不可达)
        // malloc 失败：回滚已预留配额
        used_.fetch_sub(real, std::memory_order_acq_rel);
        return nullptr;
    }
    record(p, real);
    return p;
}

void* MallocAllocator::allocAligned(size_t bytes, size_t align) {
    // align 必须是 2 的幂（调用方契约，不防御性校验：ADR-007 内部信任契约）
    // aligned_alloc 要求 size 是 align 的整数倍（C11），用倍数对齐
    size_t aligned_bytes = bytes == 0 ? align : ((bytes + align - 1) / align) * align;
    if (!tryReserve(aligned_bytes)) return nullptr;  // 超限拦截  NEO_NO_COVERAGE (aligned_bytes 回绕到 0 时不触发)
    void* p;
#if defined(_WIN32)
    p = _aligned_malloc(aligned_bytes, align);
#else
    p = std::aligned_alloc(align, aligned_bytes);
#endif
    if (p == nullptr) {  // NEO_NO_COVERAGE (aligned_malloc 失败回滚：真实 OOM 不可达)
        used_.fetch_sub(aligned_bytes, std::memory_order_acq_rel);
        return nullptr;
    }
    record(p, aligned_bytes);
    return p;
}

void MallocAllocator::free(void* p) {
    if (p == nullptr) return;
    release(p);  // 先释放配额记账，再调 FFI free
    std::free(p);
}

void MallocAllocator::freeAligned(void* p) {
    if (p == nullptr) return;
    release(p);
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
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
