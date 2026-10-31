/*
 * core 层：业务无关基础设施。
 *
 * 遵循 docs/scope-redesign.md 重做原则：
 *   - 除 AST 外完全重写，不迁就旧机制。
 *   - core 只放业务无关 API：分配器、自定义容器/数据结构等。
 *   - 底层内存管理来自 Allocator（纯类对象，static 接口），而非 GC。
 *   - GC 只管 value 生命周期，不跟踪底层内存块。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_CORE_ALLOCATOR_H
#define NEOJS_CORE_ALLOCATOR_H

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace neojs {
namespace core {

/* Allocator：纯类对象（非实例），static 接口，底层 malloc/free。
 * 与构造无关，纯内存管理；构造走真实的构造函数（见 AllocatedObject）。
 * oomAbort：alloc 失败由分配器自己终止（不返回 nullptr）。
 *
 * 分配上限（防失控耗尽系统资源）：
 *   - 统计活跃分配总字节（alloc 累加、free 按块实际大小递减）。
 *   - 超过上限（默认 1 GiB，可 set_limit 配置）→ oomAbort 终止，
 *     避免死循环/泄漏把操作系统内存耗尽导致崩溃。
 *   - 单线程引擎：atomic 计数，无需加锁。 */
class Allocator {
 public:
  static void *alloc(size_t size) {
    if (!reserve(size)) oomAbort();
    void *ptr = std::malloc(size);
    if (ptr == nullptr) oomAbort();
    return ptr;
  }
  static void free(void *ptr) {
    if (ptr == nullptr) return;
    release(ptr);
    std::free(ptr);
  }

  /* alloc 失败 / 超上限终止（不返回 nullptr）；由 alloc/对齐 new 调用。 */
  [[noreturn]] static void oomAbort();

  /* 对齐分配（C++17 over-aligned）：计数 + 平台 aligned 分配。 */
  static void *alloc_aligned(size_t size, std::align_val_t align);

  /* 对齐释放：按块实际大小递减计数 + 平台 aligned 释放。 */
  static void free_aligned(void *ptr, std::align_val_t align);

  /* 当前活跃分配字节数。 */
  static size_t allocated() {
    return allocated_.load(std::memory_order_relaxed);
  }

  /* 分配上限（默认 1 GiB）。 */
  static size_t limit() { return limit_.load(std::memory_order_relaxed); }

  /* 配置分配上限（字节）。测试可用小上限验证触发。 */
  static void set_limit(size_t bytes) {
    limit_.store(bytes, std::memory_order_relaxed);
  }

 private:
  /* 尝试累加 size：未超上限返回 true（计数已更新），超限返回 false。 */
  static bool reserve(size_t size);

  /* free 时按块实际大小递减计数（平台相关；无法获取大小时不减，
   * 只导致计数高估，不泄漏内存）。 */
  static void release(void *ptr);

  static std::atomic<size_t> allocated_; /* 活跃分配总字节 */
  static std::atomic<size_t> limit_;     /* 分配上限 */
};

/* AllocatedObject：业务类基类，重写 operator new/delete。
 * 所有业务类继承它，分配统一经 Allocator 路由，适配 C++ new/delete。 */
class AllocatedObject {
 public:
  /* placement new 经 Allocator 路由：内存由 alloc 分配，构造走真实构造函数。 */
  static void *operator new(std::size_t size) { return Allocator::alloc(size); }
  static void operator delete(void *ptr) { Allocator::free(ptr); }
  static void *operator new[](std::size_t size) { return Allocator::alloc(size); }
  static void operator delete[](void *ptr) { Allocator::free(ptr); }
  /* 对齐版（C++17 over-aligned 类型）。 */
  static void *operator new(std::size_t size, std::align_val_t align) {
    return Allocator::alloc_aligned(size, align);
  }
  static void operator delete(void *ptr, std::align_val_t align) noexcept {
    Allocator::free_aligned(ptr, align);
  }
};

}  // namespace core
}  // namespace neojs

#endif  // NEOJS_CORE_ALLOCATOR_H
