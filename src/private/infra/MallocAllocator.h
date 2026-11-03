// impl_allocator.h (private) - MallocAllocator 实现声明
//
// 本头仅在 infra 模块内部可见（src/private/），其他模块无法引用 MallocAllocator，
// 只能通过 public 的 Allocator 接口 + create_malloc_allocator() 工厂使用。
// 文件名加 impl_ 前缀，避免与 public/infra/allocator.h 同名产生 include 歧义。
//
// 内存配额：MallocAllocator 维护原子已用计数 + 记账表（ptr→分配字节数），
// alloc 前 CAS 检查 used + bytes <= limit，超限返回 nullptr；free 查表递减 used。
// 记账表用 mutex 保护（free 可能跨线程），used 用 atomic 供 hot path 无锁读。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装）、028（infra §3 配额）

#ifndef NEO_INFRA_PRIVATE_IMPL_ALLOCATOR_H
#define NEO_INFRA_PRIVATE_IMPL_ALLOCATOR_H

#include "infra/allocator.h"  // public 接口（impl_ 前缀消除歧义）

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace neo::infra {

// 真实实现：包装 malloc/free（FFI 调用在 .cpp 实现文件内）
class MallocAllocator : public Allocator {
public:
    // 默认无限额（limit = SIZE_MAX）
    MallocAllocator();
    // 构造时即设置限额
    explicit MallocAllocator(size_t limit);

    void* alloc(size_t bytes) override;
    void* alloc_aligned(size_t bytes, size_t align) override;
    void  free(void* p) override;
    void  free_aligned(void* p) override;

    void  set_limit(size_t bytes) override;
    size_t limit() const override;
    size_t used() const override;

private:
    // 尝试预留 bytes 配额：原子 CAS 检查并累加 used，成功返回 true。
    // used_ + bytes <= limit_ 才允许；溢出或超限返回 false。
    bool try_reserve(size_t bytes);

    // 记账：登记 ptr→字节数，释放时查表递减 used
    void record(void* p, size_t bytes);
    // 释放记账：查表取字节数并递减 used，返回字节数（0 表示未登记，如 nullptr）
    size_t release(void* p);

    std::atomic<size_t> used_  {0};
    std::atomic<size_t> limit_ {SIZE_MAX};

    // 记账表：ptr→真实分配字节数（含对齐填充）。mutex 保护并发读写。
    // 注：alloc 的记账表 key 用真实指针（malloc/_aligned_malloc 返回值），
    // free 时按同一 ptr 查表。alloc(0) 返回的哨兵指针也登记 size=1。
    std::mutex guard_;
    std::unordered_map<void*, size_t> records_;
};

}  // namespace neo::infra

#endif  // NEO_INFRA_PRIVATE_ALLOCATOR_H
