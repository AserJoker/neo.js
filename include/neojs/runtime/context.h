#ifndef NEOJS_RUNTIME_CONTEXT_H
#define NEOJS_RUNTIME_CONTEXT_H

/*
 * 内部 C++ 实现：JSContext。
 *
 * 运行时一切信息都从 ctx 出发（规范 #9）：ctx 持有分配器、值注册表。
 *
 * 内存管理（规范 #5）：
 *   外部通过 JS_NewContext 注入 alloc/free 回调；
 *   内部所有对象（含 JSValue）都通过该分配器分配，统一 new/delete 入口。
 */

#include <cstddef>
#include <unordered_map>

#include "neojs/runtime/value.h"

namespace neojs {

using AllocFn = void *(*)(size_t size, void *userdata);
using FreeFn = void (*)(void *ptr, void *userdata);

class JSContext {
 public:
  JSContext(AllocFn alloc, FreeFn free_fn, void *userdata);
  ~JSContext();

  /* 分配器访问（#5） */
  void *allocate(size_t size);
  void deallocate(void *ptr);
  /* 用宿主注入的 free 回调释放 ctx 自身内存（JS_FreeContext 调用） */
  void deallocateSelf();

  /* 值生命周期管理：ctx 持有所有存活值的指针，释放时统一回收（#5 堆内存） */
  void registerValue(JSValue *value);
  void unregisterValue(JSValue *value);
  void freeValue(JSValue *value);

 private:
  AllocFn alloc_fn_;
  FreeFn free_fn_;
  void *userdata_;
  std::unordered_map<JSValue *, bool> values_; /* 存活值集合（值为占位） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_CONTEXT_H
