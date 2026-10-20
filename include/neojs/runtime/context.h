#ifndef NEOJS_RUNTIME_CONTEXT_H
#define NEOJS_RUNTIME_CONTEXT_H

/*
 * 内部 C++ 实现：JSContext。
 *
 * 运行时一切信息都从 ctx 出发（规范 #9）：ctx 持有分配器、当前作用域、
 * GC 堆（heap_）与宿主句柄表（handles_）。
 *
 * 内存管理（规范 #5/#13 + docs/scope-value.md P2 + docs/gc.md）：
 *   JSContext 创建**必须**接受外部提供的 JSAllocator（C API 层传入，不支持 nullptr）；
 *   ctx 仅引用分配器、不拥有它（宿主负责在释放 ctx 之后 JS_FreeAllocator）；
 *   JSValue 由 GC 堆（Heap）统一管理生命周期（G1，取代 M0 全局登记表 values_）；
 *   作用域（JSScope）只管 name->绑定，不持有值生命周期；
 *   宿主经句柄表（HandleTable）访问值（G2）；句柄表与作用域绑定链是 GC 根集。
 */

#include <string>
#include <utility>
#include <vector>

#include "neojs/runtime/allocator.h"
#include "neojs/runtime/handles.h"
#include "neojs/runtime/heap.h"
#include "neojs/runtime/object.h"
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

  /* 当前值作用域（#9：词法环境栈顶） */
  JSScope &currentScope() { return *current_scope_; }

  /* 压栈/弹栈（PUSH_SCOPE / POP_SCOPE）：
   * pushScope 新建子作用域（parent = 当前）并成为 current_scope；
   * popScope 回到父作用域，并销毁弹栈的作用域（只清绑定，不 free 值）。 */
  JSScope &pushScope();
  void popScope();

  /* GC 堆访问（docs/gc.md §3.1） */
  Heap &heap() { return heap_; }

  /* 宿主句柄表访问（docs/gc.md §3.3） */
  HandleTable &handles() { return handles_; }

  /* 手动触发一次完整 GC（对应 C API JS_GC） */
  void gc() { heap_.collect(); }

  /* 值生命周期（P2 + G1）：值经 Heap 统一分配，不归属 scope。
   * createValue 经 Heap::allocValue 创建并登记到堆；
   * freeValue 释放宿主句柄槽（对象生死由 GC 判定，G2）。 */
  template <typename V>
  JSValue *createValue(V &&value) {
    return heap_.allocGCObject<JSValue>(std::forward<V>(value));
  }

  /* 对象工厂：创建 JSObject（普通/数组/包装对象）并登记到堆；再包一层
   * JSValue（kind=Object）。返回内部值指针（对象值）；经宿主句柄或
   * 作用域绑定持有。createWrapper 创建装箱对象（E4-2，§7.1.13）。 */
  JSValue *createObject(JSObject *proto);
  JSValue *createArray(size_t capacity = 0);
  JSValue *createWrapper(ObjectKind kind, JSValue *data);

  /* 释放宿主句柄槽（对象生死由 GC 判定，G2）。freeValue 用于内部值
   * （无句柄时直接交还堆）；freeHandle 用于宿主经句柄释放（直接释放该槽，
   * 不反查——同一内部值可被多个句柄指向，find 会释放错槽）。 */
  void freeValue(JSValue *value);
  void freeHandle(uint32_t handle);

  /* 临时字符串缓冲：C API JS_ToString 等返回 const char* 的稳定存储。
   * 生命周期归 ctx（下一次调用覆盖）；供转换产生的临时字符串使用。 */
  const char *storeTmpString(const std::string &s) {
    tmp_string_ = s;
    return tmp_string_.c_str();
  }

  /* 全局对象（globalThis 雏形，E5 完整注册内置）。
   * 惰性创建：首次访问时创建全局对象（普通 JSObject），后续复用。
   * 非严格模式 this 绑定到 global（§10.2.5.6）。 */
  JSValue *globalObject();

  /* well-known symbol 缓存（E4-1，架构 §5.2）：内置协议（迭代/toPrimitive/
   * instanceof）使用。惰性创建 + 进根集（markRoots）。
   * 索引对应 builtins.h 的 WellKnown 枚举值。 */
  JSValue *wellKnown(int index) const { return well_known_[index]; }
  void setWellKnown(int index, JSValue *v) { well_known_[index] = v; }

  /* 包装对象原型缓存（E4-2）：Number/String/Boolean/Symbol 的 prototype 对象。
   * 装箱（toObject）时包装对象原型链指向对应 prototype（§7.1.13 + §20.1.3 等）。
   * 索引按 ObjectKind（Number/String/Boolean/Symbol）映射，进根集（markRoots），
   * installBuiltins 注册。 */
  JSValue *wrapperProto(ObjectKind kind) const {
    int idx = static_cast<int>(kind) - static_cast<int>(ObjectKind::Number);
    if (idx < 0 || idx >= 4) return nullptr;
    return wrapper_protos_[idx];
  }
  void setWrapperProto(ObjectKind kind, JSValue *v) {
    int idx = static_cast<int>(kind) - static_cast<int>(ObjectKind::Number);
    if (idx >= 0 && idx < 4) wrapper_protos_[idx] = v;
  }

  /* VM 操作数栈注册为 GC 根集（M2-VM，用户确认“栈帧入 GC 根集”）。
   * 解释器运行期间调用 vmStackPush(&stack) 入栈；结束/出错时 vmStackPop 弹出。
   * 嵌套 runBytecode（函数递归）会 push 多个栈帧，markRoots 遍历全部。
   * markRoots 遍历注册的栈：栈上值（表达式中间值 + 局部变量槽）不可回收。 */
  void vmStackPush(std::vector<JSValue *> *stack) { vm_stacks_.push_back(stack); }
  void vmStackPop() {
    if (!vm_stacks_.empty()) vm_stacks_.pop_back();
  }
  const std::vector<std::vector<JSValue *> *> &vmStacks() const {
    return vm_stacks_;
  }

 private:
  /* GC 标记阶段根集遍历（docs/gc.md §3.4）：
   * 句柄表所有非空槽 + 作用域绑定链（沿 parent/children 遍历）。
   * 作为 Heap::MarkFn 回调注入（userdata = this）。 */
  static void markRootsThunk(void *ud); /* Heap 回调入口（转调 markRoots） */
  void markRoots();
  void markScope(JSScope *scope); /* 递归标记作用域树中所有绑定值 */

  JSAllocator *allocator_; /* 引用外部分配器（#13）；ctx 不拥有 */
  JSScope *current_scope_; /* 当前词法环境（不透明，不对外暴露） */
  Heap heap_;              /* GC 堆（G1：统一分配 + mark-sweep） */
  HandleTable handles_;    /* 宿主句柄表（G2：根集之一） */
  JSValue *global_ = nullptr; /* 全局对象（globalThis 雏形，惰性创建） */
  std::string tmp_string_; /* 临时字符串缓冲（JS_ToString 稳定返回用） */
  JSValue *well_known_[4] = {nullptr, nullptr, nullptr, nullptr}; /* well-known 缓存（E4-1） */
  JSValue *wrapper_protos_[4] = {nullptr, nullptr, nullptr, nullptr}; /* 包装原型缓存（E4-2） */
  std::vector<std::vector<JSValue *> *> vm_stacks_; /* VM 操作数栈根集栈（M2-VM） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_CONTEXT_H
