#ifndef NEOJS_RUNTIME_GC_HEADER_H
#define NEOJS_RUNTIME_GC_HEADER_H

/*
 * 内部 C++ 实现：GCHeader（GC 对象头基类）。
 *
 * 遵循 docs/gc.md：
 *   G1  所有可 GC 对象（JSValue、对象模型的 JSObject 等）由 Heap 统一管理，
 *       统一内嵌 GC 对象头（标记位 + 引用边遍历接口）。
 *   M3  对象模型引入引用边：mark 阶段从根出发沿对象图递归遍历
 *       （docs/gc.md §3.2 / §4.1），循环引用可回收。
 *
 * 设计：
 *   - gc_mark_   标记位（mark 阶段置 1，sweep 清除）。
 *   - gcMarkRefs 虚接口：子类把自己持有的可 GC 引用（属性值、数组元素、
 *     闭包 upvalue 等）逐个传给 Heap::markValue 标记。叶子值（无引用边）
 *     实现为空。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

namespace neojs {

class Heap;

class GCHeader {
 public:
  GCHeader() = default;
  virtual ~GCHeader() = default;

  /* GC 标记位（docs/gc.md §3.2）：mark 阶段置 1，sweep 清除 */
  bool gcMark() const { return gc_mark_; }
  void setGcMark(bool m) { gc_mark_ = m; }

  /* 标记阶段遍历引用边：把本对象持有的可 GC 引用交给 heap 标记
   * （递归处理，循环引用安全）。叶子值实现为空。 */
  virtual void gcMarkRefs(Heap *heap) = 0;

 private:
  bool gc_mark_ = false;
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_GC_HEADER_H
