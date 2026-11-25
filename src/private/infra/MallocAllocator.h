// MallocAllocator.h (private) - MallocAllocator 实现声明
//
// 本头仅在 infra 模块内部可见（src/private/），其他模块无法引用 MallocAllocator，
// 只能通过 public 的 IAllocator 接口 + createMallocAllocator() 工厂使用。
// 文件名与实现类名一致（MallocAllocator.h），与 public 接口头 IAllocator.h 不冲突。
//
// 内存配额：MallocAllocator 维护原子已用计数 + 分配头（返回指针前的 header 记录
// 用户请求字节数与 raw 偏移），alloc 前 CAS 检查 used + bytes <= limit，超限返回
// nullptr；free 读 header 递减 used。分配头把记账从「全局 mutex + 哈希表」降为
// 一次 O(1) 指针偏移读写，避免分配/释放热路径上的锁竞争与哈希开销。
// used 用 atomic 供 hot path 无锁读。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装）、028（infra §3 配额）

#ifndef NEO_INFRA_PRIVATE_MALLOC_ALLOCATOR_H
#define NEO_INFRA_PRIVATE_MALLOC_ALLOCATOR_H

#include "infra/IAllocator.h"  // public 接口

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace neo::infra {

// 真实实现：包装 malloc/free（FFI 调用在 .cpp 实现文件内）
class MallocAllocator : public IAllocator {
public:
    // 默认无限额（limit = SIZE_MAX）
    MallocAllocator();
    // 构造时即设置限额
    explicit MallocAllocator(size_t limit);

    void* alloc(size_t bytes) override;
    void* allocAligned(size_t bytes, size_t align) override;
    void  free(void* p) override;
    void  freeAligned(void* p) override;

    void  setLimit(size_t bytes) override;
    size_t limit() const override;
    size_t used() const override;

private:
    // 尝试预留 bytes 配额：原子 CAS 检查并累加 used，成功返回 true。
    // used_ + bytes <= limit_ 才允许；溢出或超限返回 false。
    bool tryReserve(size_t bytes);

    std::atomic<size_t> used_  {0};
    std::atomic<size_t> limit_ {SIZE_MAX};
};

}  // namespace neo::infra

#endif  // NEO_INFRA_PRIVATE_MALLOC_ALLOCATOR_H
