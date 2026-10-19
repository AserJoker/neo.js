#ifndef NEOJS_RUNTIME_HEAP_H
#define NEOJS_RUNTIME_HEAP_H

/*
 * 内部 C++ 实现：Heap（GC 堆）。
 *
 * 遵循 docs/gc.md（GC 引擎设计）：
 *   G1  所有 JSValue 由 Heap 统一分配与回收（取代 ctx 全局登记表）。
 *   G5  触发双模式：分配计数达到阈值自动触发 + 宿主 JS_GC() 手动触发。
 *   G6  正确性优先：v1 用 std::unordered_set 存堆对象、线性扫描标记。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露（对外只暴露 neojs.h 的 C 接口）。
 */

#include <cstddef>
#include <unordered_set>

#include "neojs/runtime/allocator.h"

namespace neojs {

class JSValue;

class Heap {
 public:
  /* 标记回调：收集根集（docs/gc.md §3.4）。根集定义在 JSContext 层
   * （句柄表 + 作用域绑定链 + 未来 VM 栈帧等），Heap 不自知，
   * 由 JSContext 构造时注入。 */
  using MarkFn = void (*)(void *userdata);

  /* allocator 必须非空（#13：统一经 JSAllocator 路由）。默认阈值 1000。 */
  explicit Heap(JSAllocator *allocator);
  ~Heap();

  /* 注入根集标记回调（JSContext 构造时调用；userdata 通常为 ctx） */
  void setMarkFn(MarkFn fn, void *userdata) {
    mark_fn_ = fn;
    mark_ud_ = userdata;
  }

  /* 分配一个值（经分配器 create<T>）并登记到堆。返回内部指针；
   * 若分配计数达到阈值则先触发一次 GC（G5 自动触发）。 */
  template <typename V>
  JSValue *allocValue(V &&value) {
    JSValue *v = allocator_->create<JSValue>(std::forward<V>(value));
    objects_.insert(v);
    if (++alloc_count_ >= gc_threshold_) {
      collect(); /* 自动触发（G5） */
    }
    return v;
  }

  /* 回收对象（析构 + 释放），从堆中移除。GC 内部与显式释放共用。 */
  void destroyValue(JSValue *value);

  /* 堆中存活对象数（调试/测试观测） */
  size_t size() const { return objects_.size(); }

  /* 分配计数（自上次 GC 起） */
  size_t allocCount() const { return alloc_count_; }

  /* 设置自动 GC 阈值；0 = 关闭自动触发（G5） */
  void setGcThreshold(size_t threshold) { gc_threshold_ = threshold; }
  size_t gcThreshold() const { return gc_threshold_; }

  /* 触发一次完整 GC（mark-sweep）：先标记根集，再清除未标记对象。 */
  void collect();

  /* 标记一个根对象（标记阶段由 MarkFn 回调收集根集后逐个调用） */
  void markRoot(JSValue *value);

 private:
  /* 清除阶段：回收未标记对象，清除标记位 */
  void sweep();

  JSAllocator *allocator_;                       /* 引用分配器（#13，不拥有） */
  std::unordered_set<JSValue *> objects_;        /* 所有堆对象（G6） */
  size_t alloc_count_ = 0;                       /* 自上次 GC 的累计分配数 */
  size_t gc_threshold_ = 1000;                   /* 分配阈值（默认 1000） */
  MarkFn mark_fn_ = nullptr;                     /* 根集标记回调（G3） */
  void *mark_ud_ = nullptr;                      /* 回调 userdata（ctx） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_HEAP_H
