#include "neojs/runtime/context.h"

namespace neojs {

JSContext::JSContext(JSAllocator *allocator)
    : allocator_(allocator),
      /* 根作用域由 ctx 经分配器创建（归属 ctx，释放 ctx 时一并回收） */
      current_scope_(allocator_->create<JSScope>(allocator)) {}

JSContext::~JSContext() {
  /* 回收 M0 全局登记表里的所有值（P2：#13 经 destroy<T> 析构 + 释放）。
   * 注意：ctx 不拥有分配器，这里只释放值 + 作用域，不释放 allocator。 */
  for (auto &kv : values_) {
    allocator_->destroy(kv.first);
  }
  values_.clear();

  /* 根作用域析构时递归回收所有未 pop 的子作用域（只清绑定，不 free 值）。 */
  allocator_->destroy(current_scope_);
}

/* PUSH_SCOPE：新建子作用域并成为当前作用域（#9）。 */
JSScope &JSContext::pushScope() {
  current_scope_ = allocator_->create<JSScope>(allocator_, current_scope_);
  return *current_scope_;
}

/* POP_SCOPE：销毁当前作用域（只清绑定关系），回到父作用域。
 * 值生命周期归 ctx 登记表，不随作用域销毁（P2，见 docs/scope-value.md）。 */
void JSContext::popScope() {
  JSScope *popped = current_scope_;
  JSScope *parent = popped->parent();
  if (parent == nullptr) return; /* 根作用域不可弹栈 */
  allocator_->destroy(popped);   /* 析构会从父的 children 摘除自己 */
  current_scope_ = parent;
}

void JSContext::freeValue(JSValue *value) {
  if (value == nullptr) return;
  values_.erase(value);
  allocator_->destroy(value); /* #13：析构 + 释放 */
}

void JSContext::deallocateSelf() { allocator_->deallocate(this); }

}  // namespace neojs
