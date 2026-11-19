// QuotaAllocator.h (private) - 子分配器实现声明
//
// 本头仅在 infra 模块内部可见（src/private/），其他模块通过 public 的
// IAllocator 接口 + createQuotaAllocator() 工厂使用。
//
// 语义：所有 alloc/free 委托给 parent（内存仍由 parent 的底层实现分配），
// 但 used_/limit_ 独立记账。因此 parent->used() 自然汇总所有子分配器的占用，
// 而每个子分配器又有自己的限额。
//
// 记账表记录 ptr→真实分配字节数（含对齐填充），free 时查表递减 used_。
// 与 MallocAllocator 一致：alloc(0) 记 1 字节，allocAligned 按对齐后的字节数记账。
//
// 关联 ADR: 008（IAllocator）、010（接口-实现-工厂）、028（infra §3 配额）

#ifndef NEO_INFRA_PRIVATE_QUOTA_ALLOCATOR_H
#define NEO_INFRA_PRIVATE_QUOTA_ALLOCATOR_H

#include "infra/IAllocator.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace neo::infra {

// 子分配器：向 parent 借内存，独立配额。
class QuotaAllocator : public IAllocator {
public:
    // parent 非空且生命周期必须覆盖本对象（调用方契约）。
    explicit QuotaAllocator(IAllocator* parent, size_t limit = SIZE_MAX);

    void* alloc(size_t bytes) override;
    void* allocAligned(size_t bytes, size_t align) override;
    void  free(void* p) override;
    void  freeAligned(void* p) override;

    void  setLimit(size_t bytes) override;
    size_t limit() const override;
    size_t used() const override;

private:
    // 预留 bytes 配额；used_ + bytes <= limit_ 才成功。
    bool tryReserve(size_t bytes);
    // 记账 / 释放记账（返回释放的字节数）。
    void record(void* p, size_t bytes);
    size_t release(void* p);

    IAllocator* parent_;
    std::atomic<size_t> used_  {0};
    std::atomic<size_t> limit_ {SIZE_MAX};

    std::mutex guard_;
    std::unordered_map<void*, size_t> records_;
};

}  // namespace neo::infra

#endif  // NEO_INFRA_PRIVATE_QUOTA_ALLOCATOR_H
