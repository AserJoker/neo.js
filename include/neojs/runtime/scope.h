#ifndef NEOJS_RUNTIME_SCOPE_H
#define NEOJS_RUNTIME_SCOPE_H

/*
 * 内部 C++ 实现：JSScope（运行时值作用域）。
 *
 * 值生命周期管理：JSValue 创建时归属当前作用域（ctx->current_scope），
 * 作用域销毁时统一回收所有归属值。
 *
 * 作用域构成一棵树：parent/children 字段；ctx 通过 pushScope/popScope
 * 压栈弹栈（current_scope 指向栈顶）。
 *
 * 遵循 docs/coding-standards.md：
 *   #5  内存由分配器统一管理，堆内存生命周期由 ctx/scope 管理。
 *   #13 所有分配（含 JSValue、作用域）都经分配器路由，
 *       JSAllocator::create<T>/destroy<T>。
 *
 * 注意：JSScope 是引擎内部对象，不对外暴露（C API 只暴露
 * JSValue / JSContext / JSAllocator）。
 */

#include <unordered_map>
#include <utility>
#include <vector>

#include "neojs/runtime/allocator.h"

namespace neojs {

class JSValue;

class JSScope {
 public:
  /* parent == nullptr 表示根作用域。构造时自动挂载到 parent 的 children。 */
  explicit JSScope(JSAllocator *allocator, JSScope *parent = nullptr);
  ~JSScope(); /* 回收所有归属值 + 未 pop 的子作用域（#5） */

  /* 作用域树访问 */
  JSScope *parent() const { return parent_; }
  const std::vector<JSScope *> &children() const { return children_; }

  /* 在作用域上创建值（#9：值创建在 ctx->current_scope 上）。
   * placement new 经 JSAllocator::create<T>（oom 由分配器 abort），
   * 创建后自动 registerValue 归属本作用域。 */
  template <typename V>
  JSValue *createValue(V &&value) {
    JSValue *v = allocator_->create<JSValue>(std::forward<V>(value));
    registerValue(v);
    return v;
  }

  void registerValue(JSValue *value);   /* 值加入作用域 */
  void unregisterValue(JSValue *value); /* 值移出作用域（不释放） */
  void freeValue(JSValue *value);       /* 移出并析构 + 释放（#13） */

 private:
  void addChild(JSScope *child);   /* 挂载子作用域（构造时调用） */
  void removeChild(JSScope *child); /* 摘除子作用域（子析构时调用） */

  JSAllocator *allocator_;                     /* 引用分配器（不拥有） */
  JSScope *parent_;                            /* 父作用域（根为 nullptr） */
  std::vector<JSScope *> children_;            /* 子作用域集合 */
  std::unordered_map<JSValue *, bool> values_; /* 存活值集合（值为占位） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_SCOPE_H
