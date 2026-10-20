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
  /* 从分配保护集移除：对象已显式销毁，防止下轮 GC 标记悬垂指针。 */
  for (size_t i = 0; i < pin_roots_.size();) {
    if (pin_roots_[i] == object) {
      pin_roots_[i] = pin_roots_.back();
      pin_roots_.pop_back();
    } else {
      ++i;
    }
  }
  allocator_->destroy(object);
}

void Heap::collect() {
  /* 标记阶段：经注入的 MarkFn 回调收集根集（JSContext::markRoots），
   * 再清除阶段回收未标记对象（docs/gc.md §4）。 */
  if (mark_fn_ != nullptr) mark_fn_(mark_ud_);

  /* 分配保护集（M3.3）：pin 期间每轮 GC 都标记存活。
   * pin 对象在两段式创建（先分配 JSObject/JSFunction 再包 JSValue）的
   * 中间窗口里无任何根引用，必须在本轮 GC 存活；标记后保留（persistent），
   * 由调用方在包装完成后显式 unpin。 */
  for (GCHeader *obj : pin_roots_) {
    if (obj != nullptr) markRoot(obj);
  }

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
