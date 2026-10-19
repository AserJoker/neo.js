#include "neojs/runtime/context.h"

namespace neojs {

JSContext::JSContext(JSAllocator *allocator)
    : allocator_(allocator),
      /* 根作用域由 ctx 经分配器创建（归属 ctx，释放 ctx 时一并回收） */
      current_scope_(allocator_->create<JSScope>(allocator)),
      heap_(allocator),
      handles_() {
  /* 注入根集标记回调（G3）：自动触发 GC 时标记句柄表 + 作用域链。 */
  heap_.setMarkFn(&JSContext::markRootsThunk, this);
}

JSContext::~JSContext() {
  /* GC 引擎（docs/gc.md §6）：ctx 析构回收堆中所有对象 + 作用域链。
   * 注意：ctx 不拥有分配器，这里只释放值 + 作用域，不释放 allocator。
   * heap_ / handles_ 是成员对象，析构函数体结束后自动析构（声明逆序）：
   *   heap_ 回收所有存活值对象；handles_ 释放句柄槽内存。 */

  /* 根作用域析构时递归回收所有未 pop 的子作用域（只清绑定，不 free 值）。
   * 作用域绑定链不持有值生命周期（P1/G1），值对象由 heap_ 统一回收。 */
  allocator_->destroy(current_scope_);
}

/* PUSH_SCOPE：新建子作用域并成为当前作用域（#9）。 */
JSScope &JSContext::pushScope() {
  current_scope_ = allocator_->create<JSScope>(allocator_, current_scope_);
  return *current_scope_;
}

/* POP_SCOPE：销毁当前作用域（只清绑定关系），回到父作用域。
 * 值生命周期归 GC 堆，不随作用域销毁（P2 + G1，见 docs/gc.md）。 */
void JSContext::popScope() {
  JSScope *popped = current_scope_;
  JSScope *parent = popped->parent();
  if (parent == nullptr) return; /* 根作用域不可弹栈 */
  allocator_->destroy(popped);   /* 析构会从父的 children 摘除自己 */
  current_scope_ = parent;
}

void JSContext::freeValue(JSValue *value) {
  /* G2：释放宿主句柄槽（对象生死由 GC 判定，不立即释放）。
   * 内部值（非宿主句柄持有）调用 freeValue 时，直接交还堆。 */
  if (value == nullptr) return;
  uint32_t h = handles_.find(value);
  if (h != 0) {
    handles_.free(h);
  } else {
    heap_.destroyValue(value); /* 非句柄持有的内部值：直接销毁 */
  }
}

void JSContext::markRootsThunk(void *ud) {
  static_cast<JSContext *>(ud)->markRoots();
}

void JSContext::markRoots() {
  /* 根集（docs/gc.md §3.4）：
   *   1. 句柄表所有非空槽（宿主持有的值不可回收）
   *   2. 作用域绑定链（沿 parent/children 遍历所有 JSBinding::value）
   * M3 再加 Context 全局对象；M2-VM 加 VM 栈帧/常量池；M5 加模块表。 */
  for (JSValue *v : handles_.roots()) {
    heap_.markRoot(v);
  }

  /* 作用域树：从根作用域沿 children 递归覆盖整棵树。所有存活作用域
   * （含挂起子树 P5）的绑定值都是根；被 pop 的作用域已销毁并摘除，
   * 不会出现在树中，其绑定值自然成为垃圾。 */
  JSScope *root = current_scope_;
  while (root->parent() != nullptr) root = root->parent();
  markScope(root);
}

void JSContext::markScope(JSScope *scope) {
  if (scope == nullptr) return;
  for (const auto &kv : scope->bindings()) {
    if (kv.second.value != nullptr) heap_.markRoot(kv.second.value);
  }
  for (JSScope *child : scope->children()) {
    markScope(child);
  }
}

void JSContext::deallocateSelf() { allocator_->deallocate(this); }

}  // namespace neojs
