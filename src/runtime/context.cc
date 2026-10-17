#include "neojs/runtime/context.h"

namespace neojs {

JSContext::JSContext(AllocFn alloc, FreeFn free_fn, void *userdata)
    : allocator_(alloc, free_fn, userdata) {}

JSContext::~JSContext() {
  /* 统一回收所有存活值（#5：堆内存由 ctx 管理生命周期） */
  for (auto &kv : values_) {
    allocator_.deallocate(kv.first);
  }
  values_.clear();
}

void JSContext::registerValue(JSValue *value) { values_[value] = true; }

void JSContext::unregisterValue(JSValue *value) { values_.erase(value); }

void JSContext::freeValue(JSValue *value) {
  if (value == nullptr) return;
  unregisterValue(value);
  allocator_.deallocate(value);
}

void JSContext::deallocateSelf() { allocator_.deallocate(this); }

}  // namespace neojs
