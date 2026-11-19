// QuotaAllocator.cpp - 子分配器实现 + 工厂函数
//
// 内存由 parent 实际分配，本类只做独立配额记账。这样 Worker 等子执行环境
// 既有自己的限额，又能在 parent->used() 中统一统计。
//
// 关联 ADR: 008（IAllocator）、010（接口-实现-工厂）、028（infra §3 配额）

#include "infra/QuotaAllocator.h"

#include <memory>

namespace neo::infra {

QuotaAllocator::QuotaAllocator(IAllocator* parent, size_t limit)
    : parent_(parent) {
    limit_.store(limit, std::memory_order_relaxed);
}

bool QuotaAllocator::tryReserve(size_t bytes) {
    size_t cur = used_.load(std::memory_order_relaxed);
    for (;;) {
        if (cur > SIZE_MAX - bytes) return false;
        size_t next = cur + bytes;
        if (next > limit_.load(std::memory_order_relaxed)) return false;
        if (used_.compare_exchange_weak(cur, next,
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
            return true;
        }
    }
}

void QuotaAllocator::record(void* p, size_t bytes) {
    std::lock_guard<std::mutex> lk(guard_);
    records_[p] = bytes;
}

size_t QuotaAllocator::release(void* p) {
    size_t bytes = 0;
    {
        std::lock_guard<std::mutex> lk(guard_);
        auto it = records_.find(p);
        if (it == records_.end()) return 0;
        bytes = it->second;
        records_.erase(it);
    }
    used_.fetch_sub(bytes, std::memory_order_acq_rel);
    return bytes;
}

void* QuotaAllocator::alloc(size_t bytes) {
    // 与 MallocAllocator 的记账口径一致：alloc(0) 记 1 字节。
    size_t real = bytes == 0 ? 1 : bytes;
    if (!tryReserve(real)) return nullptr;
    void* p = parent_->alloc(bytes);
    if (p == nullptr) {
        used_.fetch_sub(real, std::memory_order_acq_rel);
        return nullptr;
    }
    record(p, real);
    return p;
}

void* QuotaAllocator::allocAligned(size_t bytes, size_t align) {
    // 与 MallocAllocator 的口径一致：按对齐后的字节数记账。
    size_t aligned_bytes = bytes == 0 ? align : ((bytes + align - 1) / align) * align;
    if (!tryReserve(aligned_bytes)) return nullptr;
    void* p = parent_->allocAligned(bytes, align);
    if (p == nullptr) {
        used_.fetch_sub(aligned_bytes, std::memory_order_acq_rel);
        return nullptr;
    }
    record(p, aligned_bytes);
    return p;
}

void QuotaAllocator::free(void* p) {
    if (p == nullptr) return;
    release(p);
    parent_->free(p);
}

void QuotaAllocator::freeAligned(void* p) {
    if (p == nullptr) return;
    release(p);
    parent_->freeAligned(p);
}

void QuotaAllocator::setLimit(size_t bytes) {
    limit_.store(bytes, std::memory_order_relaxed);
}

size_t QuotaAllocator::limit() const {
    return limit_.load(std::memory_order_relaxed);
}

size_t QuotaAllocator::used() const {
    return used_.load(std::memory_order_relaxed);
}

std::unique_ptr<IAllocator> createQuotaAllocator(IAllocator* parent,
                                                 size_t limit) {
    return std::make_unique<QuotaAllocator>(parent, limit);
}

}  // namespace neo::infra
