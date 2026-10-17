#ifndef NEOJS_RUNTIME_ALLOCATOR_H
#define NEOJS_RUNTIME_ALLOCATOR_H

/*
 * 内部 C++ 实现：JSAllocator（统一分配器管理）。
 *
 * 遵循 docs/coding-standards.md：
 *   #5  外部通过 alloc/free 回调管理内存；内部所有分配经统一分配器路由。
 *   #13 JSAllocator 暴露到 C API（不透明指针）；JSContext 创建接受 JSAllocator*。
 *   #14 返回 JSValue 的接口永不返回 nullptr；alloc 失败由分配器自己 abort。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#include <cstddef>
#include <new>
#include <utility>

namespace neojs {

using AllocFn = void *(*)(size_t size, void *userdata);
using FreeFn = void (*)(void *ptr, void *userdata);

class JSAllocator {
 public:
  JSAllocator(AllocFn alloc, FreeFn free_fn, void *userdata);

  /* 分配/释放（#5：转发宿主回调）。allocate 失败直接 abort（#14，由分配器处理）。 */
  void *allocate(size_t size);
  void deallocate(void *ptr);

  /* #13：泛型统一创建/销毁。placement new 经分配器路由：
   *   内存由 allocate 分配（oom 由分配器 abort），构造经 new (mem) T(args...)。
   *   destroy 对应析构 + deallocate。 */
  template <class T, class... Args>
  T *create(Args &&...args) {
    void *mem = allocate(sizeof(T));
    return new (mem) T(std::forward<Args>(args)...);
  }

  template <class T>
  void destroy(T *ptr) {
    if (ptr == nullptr) return;
    ptr->~T();
    deallocate(ptr);
  }

 private:
  AllocFn alloc_fn_;
  FreeFn free_fn_;
  void *userdata_;
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_ALLOCATOR_H
