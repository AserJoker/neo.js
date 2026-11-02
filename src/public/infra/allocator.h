// allocator.h - 内存分配器接口（FFI 收口：malloc/free）
//
// 业务代码持 Allocator*，不直接调 malloc/free（ADR-027 §5.6 FFI 封装原则）。
// 具体实现（MallocAllocator）在 private，通过工厂函数 create_malloc_allocator() 获取。
// 测试可注入 mock Allocator 验证分配/释放行为。
//
// 关联 ADR: 008（Allocator 与 GC 分层）、010（接口-实现-工厂）、027（FFI 封装）、028（infra）

#ifndef NEO_INFRA_ALLOCATOR_H
#define NEO_INFRA_ALLOCATOR_H

#include <cstddef>
#include <memory>

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

// 工厂：创建默认的 malloc 包装分配器（具体实现 MallocAllocator 在 private）
std::unique_ptr<Allocator> create_malloc_allocator();

}  // namespace neo::infra

#endif  // NEO_INFRA_ALLOCATOR_H
