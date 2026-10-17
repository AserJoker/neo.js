#ifndef NEOJS_RUNTIME_ALLOCATOR_H
#define NEOJS_RUNTIME_ALLOCATOR_H

/*
 * 内部 C++ 实现：JSAllocator（统一分配器管理）。
 *
 * 遵循 docs/coding-standards.md：
 *   #5  外部通过 alloc/free 回调管理内存；内部所有分配经统一分配器路由。
 *   #13 JSAllocator 统一管理分配器。当前阶段只实现分配/释放，不做追踪/统计；
 *       后续在性能调试场景下扩展运行时内存监控与性能探针。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#include <cstddef>

namespace neojs {

using AllocFn = void *(*)(size_t size, void *userdata);
using FreeFn = void (*)(void *ptr, void *userdata);

class JSAllocator {
 public:
  JSAllocator(AllocFn alloc, FreeFn free_fn, void *userdata);

  /* 分配/释放（#5：转发宿主回调） */
  void *allocate(size_t size);
  void deallocate(void *ptr);

 private:
  AllocFn alloc_fn_;
  FreeFn free_fn_;
  void *userdata_;
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_ALLOCATOR_H
