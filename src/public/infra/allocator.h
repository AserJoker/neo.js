// allocator.h - 内存分配器接口（FFI 收口：malloc/free）
//
// 业务代码持 Allocator*，不直接调 malloc/free（ADR-027 §5.6 FFI 封装原则）。
// 具体实现（MallocAllocator）在 private，通过工厂函数 create_malloc_allocator() 获取。
// 测试可注入 mock Allocator 验证分配/释放行为。
//
// 内存配额：Allocator 支持运行时设置最大可分配字节数（set_limit），超限 alloc 返回
// nullptr 拦截，不抛异常、不耗尽 OS 资源。解释器/VM 出现无限分配 bug 时由限额兜底，
// 配合上层 Result<T> 错误通道转为脚本异常，避免 OOM 拖垮宿主进程。
//
// 关联 ADR: 008（Allocator 与 GC 分层）、010（接口-实现-工厂）、027（FFI 封装）、028（infra）

#ifndef NEO_INFRA_ALLOCATOR_H
#define NEO_INFRA_ALLOCATOR_H

#include <cstddef>
#include <cstdint>
#include <memory>

namespace neo::infra {

// 内存分配器抽象接口
class Allocator {
public:
    virtual ~Allocator() = default;

    // 分配 bytes 字节，返回未初始化内存；失败返回 nullptr
    // 失败原因：底层 malloc 失败，或超出 set_limit 设置的配额（超限拦截）
    virtual void* alloc(size_t bytes) = 0;

    // 对齐分配：bytes 字节，align 对齐（align 必须是 2 的幂）
    // 失败语义同 alloc
    virtual void* alloc_aligned(size_t bytes, size_t align) = 0;

    // 释放 alloc 返回的内存
    virtual void free(void* p) = 0;

    // 释放 alloc_aligned 返回的内存
    virtual void free_aligned(void* p) = 0;

    // 设置最大可分配字节数。bytes=SIZE_MAX 表示无限制（默认）。
    // 已分配量超过新限额时不会强制回收，但后续 alloc 将被拦截直到 used 降到限额以下。
    virtual void set_limit(size_t bytes) = 0;

    // 当前最大可分配字节数（SIZE_MAX 表示无限制）
    virtual size_t limit() const = 0;

    // 当前已分配字节数（含对齐填充，反映真实占用而非请求 bytes）
    virtual size_t used() const = 0;
};

// 工厂：创建默认的 malloc 包装分配器，无内存限额（具体实现 MallocAllocator 在 private）
std::unique_ptr<Allocator> create_malloc_allocator();

// 工厂：创建带内存限额的 malloc 包装分配器。
// 超限 alloc 返回 nullptr，用于受控执行环境拦截无限分配 bug。
std::unique_ptr<Allocator> create_malloc_allocator_with_limit(size_t bytes);

}  // namespace neo::infra

#endif  // NEO_INFRA_ALLOCATOR_H
