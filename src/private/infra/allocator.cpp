// allocator.cpp - MallocAllocator 实现 + 工厂函数（FFI 收口：malloc/free/aligned_alloc）
//
// 本文件是 neo.js 中唯一直接调用 malloc/free 等内存 FFI 的地方（ADR-027 §5.6）。
// 业务代码通过 Allocator* 接口注入使用，不直接接触这些 FFI。
//
// 关联 ADR: 008（Allocator）、010（接口-实现-工厂）、027（FFI 封装）、028（infra）

#include "infra/impl_allocator.h"  // private/infra/impl_allocator.h

#include <cstdlib>
#include <memory>

namespace neo::infra {

void* MallocAllocator::alloc(size_t bytes) {
    // malloc(0) 行为实现定义，统一返回非空哨兵（与 free 兼容）
    return std::malloc(bytes == 0 ? 1 : bytes);
}

void* MallocAllocator::alloc_aligned(size_t bytes, size_t align) {
    // align 必须是 2 的幂（调用方契约，不防御性校验：ADR-007 内部信任契约）
    // aligned_alloc 要求 size 是 align 的整数倍（C11），用倍数对齐
    size_t aligned_bytes = bytes == 0 ? align : ((bytes + align - 1) / align) * align;
#if defined(_WIN32)
    return _aligned_malloc(aligned_bytes, align);
#else
    return std::aligned_alloc(align, aligned_bytes);
#endif
}

void MallocAllocator::free(void* p) {
    std::free(p);
}

void MallocAllocator::free_aligned(void* p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

// 工厂函数：创建 MallocAllocator，返回接口指针
std::unique_ptr<Allocator> create_malloc_allocator() {
    return std::make_unique<MallocAllocator>();
}

}  // namespace neo::infra
