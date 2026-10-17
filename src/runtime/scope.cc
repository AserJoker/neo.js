#include "neojs/runtime/scope.h"

#include "neojs/runtime/value.h"

namespace neojs {

JSScope::JSScope(JSAllocator *allocator) : allocator_(allocator) {}

JSScope::~JSScope() {
  /* 统一回收所有归属值（#5：作用域生命周期管理值；
   * #13：经 JSAllocator::destroy<T> 析构 + 释放，不泄漏值内资源）。 */
  for (auto &kv : values_) {
    allocator_->destroy(kv.first);
  }
  values_.clear();
}

void JSScope::registerValue(JSValue *value) { values_[value] = true; }

void JSScope::unregisterValue(JSValue *value) { values_.erase(value); }

void JSScope::freeValue(JSValue *value) {
  if (value == nullptr) return;
  unregisterValue(value);
  allocator_->destroy(value); /* #13：析构 + 释放 */
}

}  // namespace neojs
