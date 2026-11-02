// allocator.h - 内存分配器接口（FFI 收口：malloc/free）
//
// 业务代码持 Allocator*，不直接调 malloc/free（ADR-027 §5.6 FFI 封装原则）。
// MallocAllocator 是真实实现，FFI 调用集中在 src/private/infra/allocator.cpp。
// 测试可注入 mock Allocator 验证分配/释放行为。
//
// 关联 ADR: 008（Allocator 与 GC 分层）、027（FFI 封装）、028（infra）

#ifndef NEO_INFRA_ALLOCATOR_H
#define NEO_INFRA_ALLOCATOR_H

#include <cstddef>

namespace neo::infra {

// 内存分配器抽象接口
class Allocator {
public:
    virtual ~Allocator() = default;

    // 分配 bytes 字节，返回未初始化内存；失败返回 nullptr
    virtual void* alloc(size_t bytes) = 0;

    // 对齐分配：bytes 字节，align 对齐（align 必须是 2 的幂）
    virtual void* alloc_aligned(size_t bytes, size_t align) = 0;

    // 释放 alloc 返回的内存
    virtual void free(void* p) = 0;

    // 释放 alloc_aligned 返回的内存
    virtual void free_aligned(void* p) = 0;
};

// 真实实现：包装 malloc/free（FFI 收口在 .cpp 实现文件内）
class MallocAllocator : public Allocator {
public:
    void* alloc(size_t bytes) override;
    void* alloc_aligned(size_t bytes, size_t align) override;
    void  free(void* p) override;
    void  free_aligned(void* p) override;
};

}  // namespace neo::infra

#endif  // NEO_INFRA_ALLOCATOR_H
