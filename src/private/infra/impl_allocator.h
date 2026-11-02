// impl_allocator.h (private) - MallocAllocator 实现声明
//
// 本头仅在 infra 模块内部可见（src/private/），其他模块无法引用 MallocAllocator，
// 只能通过 public 的 Allocator 接口 + create_malloc_allocator() 工厂使用。
// 文件名加 impl_ 前缀，避免与 public/infra/allocator.h 同名产生 include 歧义。
//
// 关联 ADR: 010（接口-实现-工厂）、027（FFI 封装）

#ifndef NEO_INFRA_PRIVATE_IMPL_ALLOCATOR_H
#define NEO_INFRA_PRIVATE_IMPL_ALLOCATOR_H

#include "infra/allocator.h"  // public 接口（impl_ 前缀消除歧义）

namespace neo::infra {

// 真实实现：包装 malloc/free（FFI 调用在 .cpp 实现文件内）
class MallocAllocator : public Allocator {
public:
    void* alloc(size_t bytes) override;
    void* alloc_aligned(size_t bytes, size_t align) override;
    void  free(void* p) override;
    void  free_aligned(void* p) override;
};

}  // namespace neo::infra

#endif  // NEO_INFRA_PRIVATE_ALLOCATOR_H
