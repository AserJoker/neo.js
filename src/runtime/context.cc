#include "neojs/runtime/context.h"

namespace neojs {

JSContext::JSContext(JSAllocator *allocator) : allocator_(allocator) {}

JSContext::~JSContext() {
  /* 统一回收所有存活值（#5：堆内存由 ctx 管理生命周期；
   * #13：经 JSAllocator::destroy<T> 析构 + 释放，不泄漏值内资源）。
   * 注意：ctx 不拥有分配器，这里只释放值，不释放 allocator。 */
  for (auto &kv : values_) {
    allocator_->destroy(kv.first);
  }
  values_.clear();
}

void JSContext::registerValue(JSValue *value) { values_[value] = true; }

void JSContext::unregisterValue(JSValue *value) { values_.erase(value); }

void JSContext::freeValue(JSValue *value) {
  if (value == nullptr) return;
  unregisterValue(value);
  allocator_->destroy(value); /* #13：析构 + 释放 */
}

void JSContext::deallocateSelf() { allocator_->deallocate(this); }

}  // namespace neojs
