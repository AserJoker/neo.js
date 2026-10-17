#include "neojs/runtime/context.h"

namespace neojs {

JSContext::JSContext(JSAllocator *allocator)
    : allocator_(allocator),
      /* 根作用域由 ctx 经分配器创建（归属 ctx，释放 ctx 时一并回收） */
      current_scope_(allocator_->create<JSScope>(allocator)) {}

JSContext::~JSContext() {
  /* 根作用域析构时统一回收所有归属值 + 未 pop 的子作用域（#5）。
   * 注意：ctx 不拥有分配器，这里只释放值 + 作用域，不释放 allocator。 */
  allocator_->destroy(current_scope_);
}

/* PUSH_SCOPE：新建子作用域并成为当前作用域（#9）。 */
JSScope &JSContext::pushScope() {
  current_scope_ = allocator_->create<JSScope>(allocator_, current_scope_);
  return *current_scope_;
}

/* POP_SCOPE：销毁当前作用域（回收其归属值），回到父作用域。 */
void JSContext::popScope() {
  JSScope *popped = current_scope_;
  JSScope *parent = popped->parent();
  if (parent == nullptr) return; /* 根作用域不可弹栈 */
  allocator_->destroy(popped);   /* 析构会从父的 children 摘除自己 */
  current_scope_ = parent;
}

void JSContext::deallocateSelf() { allocator_->deallocate(this); }

}  // namespace neojs
