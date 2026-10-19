#include "neojs/runtime/heap.h"

#include "neojs/runtime/value.h"

namespace neojs {

Heap::Heap(JSAllocator *allocator) : allocator_(allocator) {}

Heap::~Heap() {
  /* 回收堆中所有存活对象（GC 引擎 v1：析构时兜底清理）。 */
  for (JSValue *v : objects_) {
    allocator_->destroy(v);
  }
  objects_.clear();
}

void Heap::destroyValue(JSValue *value) {
  if (value == nullptr) return;
  objects_.erase(value);
  allocator_->destroy(value);
}

void Heap::collect() {
  /* 标记阶段：经注入的 MarkFn 回调收集根集（JSContext::markRoots），
   * 再清除阶段回收未标记对象（docs/gc.md §4）。 */
  if (mark_fn_ != nullptr) mark_fn_(mark_ud_);
  sweep();
  alloc_count_ = 0; /* 重置分配计数 */
}

void Heap::markRoot(JSValue *value) {
  if (value == nullptr || value->gcMark()) return;
  value->setGcMark(true);
  /* M2 阶段 JSValue 是叶子值（无引用边）；对象模型（M3）加 refs_ 后
   * 在此遍历引用边递归标记。 */
}

void Heap::sweep() {
  /* 清除阶段：回收未标记对象，清除标记位。 */
  for (auto it = objects_.begin(); it != objects_.end();) {
    JSValue *v = *it;
    if (v->gcMark()) {
      v->setGcMark(false); /* 清除标记，为下一轮准备 */
      ++it;
    } else {
      allocator_->destroy(v); /* 未标记 -> 回收（析构 + 释放） */
      it = objects_.erase(it);
    }
  }
}

}  // namespace neojs
