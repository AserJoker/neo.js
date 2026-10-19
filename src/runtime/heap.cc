#include "neojs/runtime/heap.h"

namespace neojs {

Heap::Heap(JSAllocator *allocator) : allocator_(allocator) {}

Heap::~Heap() {
  /* 回收堆中所有存活对象（GC 引擎 v1：析构时兜底清理）。 */
  for (GCHeader *obj : objects_) {
    allocator_->destroy(obj);
  }
  objects_.clear();
}

void Heap::destroyValue(GCHeader *object) {
  if (object == nullptr) return;
  objects_.erase(object);
  allocator_->destroy(object);
}

void Heap::collect() {
  /* 标记阶段：经注入的 MarkFn 回调收集根集（JSContext::markRoots），
   * 再清除阶段回收未标记对象（docs/gc.md §4）。 */
  if (mark_fn_ != nullptr) mark_fn_(mark_ud_);
  sweep();
  alloc_count_ = 0; /* 重置分配计数 */
}

void Heap::markRoot(GCHeader *object) {
  /* 沿对象图递归标记（docs/gc.md §4.1）：置标记位后遍历引用边。
   * 已标记对象直接返回，循环引用安全。 */
  if (object == nullptr || object->gcMark()) return;
  object->setGcMark(true);
  object->gcMarkRefs(this); /* 子类把自己的引用边交给 Heap 递归标记 */
}

void Heap::sweep() {
  /* 清除阶段：回收未标记对象，清除标记位。 */
  for (auto it = objects_.begin(); it != objects_.end();) {
    GCHeader *obj = *it;
    if (obj->gcMark()) {
      obj->setGcMark(false); /* 清除标记，为下一轮准备 */
      ++it;
    } else {
      allocator_->destroy(obj); /* 未标记 -> 回收（析构 + 释放） */
      it = objects_.erase(it);
    }
  }
}

}  // namespace neojs
