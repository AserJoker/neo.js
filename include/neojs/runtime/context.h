#ifndef NEOJS_RUNTIME_CONTEXT_H
#define NEOJS_RUNTIME_CONTEXT_H

/*
 * 内部 C++ 实现：JSContext。
 *
 * 运行时一切信息都从 ctx 出发（规范 #9）：ctx 持有分配器、值注册表。
 *
 * 内存管理（规范 #5/#13）：
 *   外部通过 JS_NewContext 注入 alloc/free 回调；
 *   内部所有对象（含 JSValue）统一经 JSAllocator 分配。
 */

#include <cstddef>
#include <unordered_map>

#include "neojs/runtime/allocator.h"
#include "neojs/runtime/value.h"

namespace neojs {

class JSContext {
 public:
  JSContext(AllocFn alloc, FreeFn free_fn, void *userdata);
  ~JSContext();

  /* 分配器访问（#5/#13：统一经 JSAllocator 路由） */
  JSAllocator &allocator() { return allocator_; }
  void *allocate(size_t size) { return allocator_.allocate(size); }
  void deallocate(void *ptr) { allocator_.deallocate(ptr); }
  /* 用宿主注入的 free 回调释放 ctx 自身内存（JS_FreeContext 调用） */
  void deallocateSelf();

  /* 值生命周期管理：ctx 持有所有存活值的指针，释放时统一回收（#5 堆内存） */
  void registerValue(JSValue *value);
  void unregisterValue(JSValue *value);
  void freeValue(JSValue *value);

 private:
  JSAllocator allocator_;
  std::unordered_map<JSValue *, bool> values_; /* 存活值集合（值为占位） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_CONTEXT_H
