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

#include <cstddef>
#include <cstdlib>
#include <new>

namespace neojs {
namespace core {

/* Allocator：纯类对象（非实例），static 接口，底层 malloc/free。
 * 与构造无关，纯内存管理；构造走真实的构造函数（见 AllocatedObject）。
 * oomAbort：alloc 失败由分配器自己终止（不返回 nullptr）。 */
class Allocator {
 public:
  static void *alloc(size_t size) {
    void *ptr = std::malloc(size);
    if (ptr == nullptr) oomAbort();
    return ptr;
  }
  static void free(void *ptr) { std::free(ptr); }

  /* alloc 失败终止（不返回 nullptr）；由 alloc/对齐 new 调用。 */
  [[noreturn]] static void oomAbort();
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
  static void *operator new(std::size_t size, std::align_val_t align);
  static void operator delete(void *ptr, std::align_val_t align) noexcept;
};

}  // namespace core
}  // namespace neojs

#endif  // NEOJS_CORE_ALLOCATOR_H
