#include "neojs/runtime/context.h"

#include <cstdlib>
#include <new>

namespace neojs {

JSContext::JSContext(AllocFn alloc, FreeFn free_fn, void *userdata)
    : alloc_fn_(alloc ? alloc : [](size_t size, void *) { return std::malloc(size); }),
      free_fn_(free_fn ? free_fn : [](void *ptr, void *) { std::free(ptr); }),
      userdata_(userdata) {}

JSContext::~JSContext() {
  /* 统一回收所有存活值（#5：堆内存由 ctx 管理生命周期） */
  for (auto &kv : values_) {
    free_fn_(kv.first, userdata_);
  }
  values_.clear();
}

void *JSContext::allocate(size_t size) { return alloc_fn_(size, userdata_); }

void JSContext::deallocate(void *ptr) { free_fn_(ptr, userdata_); }

void JSContext::registerValue(JSValue *value) { values_[value] = true; }

void JSContext::unregisterValue(JSValue *value) { values_.erase(value); }

void JSContext::freeValue(JSValue *value) {
  if (value == nullptr) return;
  unregisterValue(value);
  free_fn_(value, userdata_);
}

void JSContext::deallocateSelf() {
  free_fn_(this, userdata_);
}

}  // namespace neojs
