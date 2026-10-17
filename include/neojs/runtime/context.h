#ifndef NEOJS_RUNTIME_CONTEXT_H
#define NEOJS_RUNTIME_CONTEXT_H

/*
 * 内部 C++ 实现：JSContext。
 *
 * 运行时一切信息都从 ctx 出发（规范 #9）：ctx 持有分配器、当前值作用域。
 *
 * 内存管理（规范 #5/#13）：
 *   JSContext 创建**必须**接受外部提供的 JSAllocator（C API 层传入，不支持 nullptr）；
 *   ctx 仅引用分配器、不拥有它（宿主负责在释放 ctx 之后 JS_FreeAllocator）；
 *   JSValue 创建在 ctx->current_scope 上（#9），值生命周期由 JSScope 管理。
 */

#include "neojs/runtime/allocator.h"
#include "neojs/runtime/scope.h"
#include "neojs/runtime/value.h"

namespace neojs {

class JSContext {
 public:
  /* allocator 必须非空（#13：ctx 不拥有分配器，仅引用；宿主保证存活期）。 */
  explicit JSContext(JSAllocator *allocator);
  ~JSContext();

  /* 分配器访问（#5/#13：统一经 JSAllocator 路由） */
  JSAllocator &allocator() { return *allocator_; }
  void *allocate(size_t size) { return allocator_->allocate(size); }
  void deallocate(void *ptr) { allocator_->deallocate(ptr); }
  /* 用宿主注入的 free 回调释放 ctx 自身内存（JS_FreeContext 调用） */
  void deallocateSelf();

  /* 当前值作用域（#9：JSValue 创建在 current_scope 上） */
  JSScope &currentScope() { return *current_scope_; }

  /* 压栈/弹栈（PUSH_SCOPE / POP_SCOPE）：
   * pushScope 新建子作用域（parent = 当前）并成为 current_scope；
   * popScope 回到父作用域，并销毁弹栈的作用域（统一回收其归属值）。 */
  JSScope &pushScope();
  void popScope();

 private:
  JSAllocator *allocator_;  /* 引用外部分配器（#13）；ctx 不拥有 */
  JSScope *current_scope_;  /* 当前值作用域（不透明，不对外暴露） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_CONTEXT_H
