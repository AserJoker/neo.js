#include "neojs/runtime/allocator.h"

#include <cstdio>
#include <cstdlib>

namespace neojs {

namespace {

/* 默认分配器（宿主未注入时使用，#5） */
void *defaultAlloc(size_t size, void *) { return std::malloc(size); }
void defaultFree(void *ptr, void *) { std::free(ptr); }

/* #14：alloc 失败由分配器自己 abort */
[[noreturn]] void oomAbort() {
  std::fputs("neo.js: out of memory\n", stderr);
  std::abort();
}

}  // namespace

JSAllocator::JSAllocator(AllocFn alloc, FreeFn free_fn, void *userdata)
    : alloc_fn_(alloc ? alloc : defaultAlloc),
      free_fn_(free_fn ? free_fn : defaultFree),
      userdata_(userdata) {}

void *JSAllocator::allocate(size_t size) {
  void *ptr = alloc_fn_(size, userdata_);
  if (ptr == nullptr) oomAbort();
  return ptr;
}

void JSAllocator::deallocate(void *ptr) { free_fn_(ptr, userdata_); }

}  // namespace neojs
