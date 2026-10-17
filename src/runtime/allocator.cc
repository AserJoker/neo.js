#include "neojs/runtime/allocator.h"

#include <cstdlib>

namespace neojs {

namespace {

/* 默认分配器（宿主未注入时使用，#5） */
void *defaultAlloc(size_t size, void *) { return std::malloc(size); }
void defaultFree(void *ptr, void *) { std::free(ptr); }

}  // namespace

JSAllocator::JSAllocator(AllocFn alloc, FreeFn free_fn, void *userdata)
    : alloc_fn_(alloc ? alloc : defaultAlloc),
      free_fn_(free_fn ? free_fn : defaultFree),
      userdata_(userdata) {}

void *JSAllocator::allocate(size_t size) {
  return alloc_fn_(size, userdata_);
}

void JSAllocator::deallocate(void *ptr) { free_fn_(ptr, userdata_); }

}  // namespace neojs
