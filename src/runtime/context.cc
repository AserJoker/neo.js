#include "neojs/runtime/context.h"

#include "neojs/runtime/builtins.h"
#include "neojs/runtime/environment.h"

namespace neojs {

JSContext::JSContext(JSAllocator *allocator)
    : allocator_(allocator),
      /* 根作用域由 ctx 经分配器创建（归属 ctx，释放 ctx 时一并回收） */
      current_scope_(allocator_->create<JSScope>(allocator)),
      heap_(allocator),
      handles_() {
  /* 注入根集标记回调（G3）：自动触发 GC 时标记句柄表 + 作用域链。 */
  heap_.setMarkFn(&JSContext::markRootsThunk, this);
}

JSContext::~JSContext() {
  /* GC 引擎（docs/gc.md §6）：ctx 析构回收堆中所有对象 + 作用域链。
   * 注意：ctx 不拥有分配器，这里只释放值 + 作用域，不释放 allocator。
   * heap_ / handles_ 是成员对象，析构函数体结束后自动析构（声明逆序）：
   *   heap_ 回收所有存活值对象；handles_ 释放句柄槽内存。 */

  /* 根作用域析构时递归回收所有未 pop 的子作用域（只清绑定，不 free 值）。
   * 作用域绑定链不持有值生命周期（P1/G1），值对象由 heap_ 统一回收。 */
  allocator_->destroy(current_scope_);
}

/* PUSH_SCOPE：新建子作用域并成为当前作用域（#9）。 */
JSScope &JSContext::pushScope() {
  current_scope_ = allocator_->create<JSScope>(allocator_, current_scope_);
  return *current_scope_;
}

/* POP_SCOPE：销毁当前作用域（只清绑定关系），回到父作用域。
 * 值生命周期归 GC 堆，不随作用域销毁（P2 + G1，见 docs/gc.md）。 */
void JSContext::popScope() {
  JSScope *popped = current_scope_;
  JSScope *parent = popped->parent();
  if (parent == nullptr) return; /* 根作用域不可弹栈 */
  allocator_->destroy(popped);   /* 析构会从父的 children 摘除自己 */
  current_scope_ = parent;
}

/* 模块表（M5.2a）：specifier -> 模块记录。加载过的模块单例存在。 */
JSModuleRecord *JSContext::findModule(const std::string &specifier) const {
  auto it = modules_.find(specifier);
  return it == modules_.end() ? nullptr : it->second;
}

void JSContext::addModule(JSModuleRecord *record) {
  if (record == nullptr) return;
  modules_[record->specifier()] = record;
}

void JSContext::freeValue(JSValue *value) {
  /* G2：内部值释放。非宿主句柄持有的内部值直接交还堆。 */
  if (value == nullptr) return;
  heap_.destroyValue(value); /* 内部值（非经句柄）直接销毁 */
}

void JSContext::freeHandle(uint32_t handle) {
  /* 宿主经句柄释放：直接释放该句柄槽（对象生死由 GC 判定，G2）。
   * 不反查持有值——同一内部值可被多个句柄指向（如属性返回的临时句柄），
   * find 会释放第一个匹配槽（错误）。 */
  handles_.free(handle);
}

/* 对象工厂（架构 §5.2 + docs/gc.md G1）：
 * JSObject 与包装 JSValue 都经 Heap 登记（引用边：JSValue -> JSObject）。
 * 返回内部值指针（kind = Object），由宿主句柄或作用域绑定持有。 */
JSValue *JSContext::createObject(JSObject *proto) {
  JSObject *obj = heap_.allocGCObject<JSObject>(proto);
  heap_.pin(obj); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  JSValue *v = heap_.allocGCObject<JSValue>(); /* kind=Undefined，随后置 Object */
  v->setObject(obj);
  obj->setHolder(v); /* 反向包装指针：JSObject -> JSValue */
  heap_.unpin(obj);
  return v;
}

JSValue *JSContext::createArray(size_t capacity) {
  /* 原型链 -> Array.prototype（M4.1）：数组字面量/Array() 新建数组
   * 汇聚于此。arrayProto 惰性创建；未注册内置时返回 nullptr（数组无原型）。 */
  JSObject *proto = nullptr;
  JSValue *ap = neojs::arrayProto(this); /* 限定名：成员函数 arrayProto() 为 0 参数 getter */
  if (ap != nullptr) proto = ap->object();
  JSObject *arr = heap_.allocGCObject<JSObject>(ObjectKind::Array, capacity);
  if (proto != nullptr) arr->setPrototype(proto);
  heap_.pin(arr);
  JSValue *v = heap_.allocGCObject<JSValue>();
  v->setObject(arr);
  arr->setHolder(v);
  heap_.unpin(arr);
  return v;
}

/* Map 工厂（M4.4a，[[MapData]]）：新建 Map 对象，原型链 Map.prototype。
 * mapProto 惰性创建；未注册内置时返回 nullptr（Map 无原型）。 */
JSValue *JSContext::createMap() {
  JSObject *proto = nullptr;
  JSValue *mp = neojs::mapProto(this);
  if (mp != nullptr) proto = mp->object();
  JSObject *obj = heap_.allocGCObject<JSObject>(ObjectKind::Map, 0);
  if (proto != nullptr) obj->setPrototype(proto);
  heap_.pin(obj);
  JSValue *v = heap_.allocGCObject<JSValue>();
  v->setObject(obj);
  obj->setHolder(v);
  heap_.unpin(obj);
  return v;
}

/* Set 工厂（M4.4a，[[SetData]]）：新建 Set 对象，原型链 Set.prototype。 */
JSValue *JSContext::createSet() {
  JSObject *proto = nullptr;
  JSValue *sp = neojs::setProto(this);
  if (sp != nullptr) proto = sp->object();
  JSObject *obj = heap_.allocGCObject<JSObject>(ObjectKind::Set, 0);
  if (proto != nullptr) obj->setPrototype(proto);
  heap_.pin(obj);
  JSValue *v = heap_.allocGCObject<JSValue>();
  v->setObject(obj);
  obj->setHolder(v);
  heap_.unpin(obj);
  return v;
}

/* 包装对象（E4-2，§7.1.13 ToObject）：kind 为 Number/String/Boolean/Symbol，
 * data 为被包装的原始值（内部指针，GC 引用边）。 */
JSValue *JSContext::createWrapper(ObjectKind kind, JSValue *data) {
  JSObject *obj = heap_.allocGCObject<JSObject>(kind, data);
  heap_.pin(obj);
  JSValue *v = heap_.allocGCObject<JSValue>();
  v->setObject(obj);
  obj->setHolder(v);
  heap_.unpin(obj);
  return v;
}

/* 全局对象（globalThis 雏形，E5 完整注册内置）：惰性创建，首次访问时
 * 生成普通全局对象，后续复用；经根集标记保护（global_ 进 markRoots）。 */
JSValue *JSContext::globalObject() {
  if (global_ == nullptr) {
    global_ = createObject(nullptr);
    /* E4：首次创建全局对象时注册内置（Symbol 构造器 + well-known 属性）。
     * 惰性注册——不触及 global 的测试（GC 精确计数等）不受内置对象影响。 */
    installBuiltins(this);
  }
  return global_;
}

void JSContext::markRootsThunk(void *ud) {
  static_cast<JSContext *>(ud)->markRoots();
}

void JSContext::markRoots() {
  /* 根集（docs/gc.md §3.4）：
   *   1. 句柄表所有非空槽（宿主持有的值不可回收）
   *   2. 作用域绑定链（沿 parent/children 遍历所有 JSBinding::value）
   * M3 再加 Context 全局对象；M2-VM 加 VM 栈帧/常量池；M5 加模块表。 */
  for (JSValue *v : handles_.roots()) {
    heap_.markRoot(v);
  }

  /* 全局对象（globalThis 雏形，E5 完整注册内置）：作为全局根始终可达。 */
  if (global_ != nullptr) heap_.markRoot(global_);

  /* well-known symbol 缓存（E4-1）：内置协议符号是全局根，始终可达。 */
  for (JSValue *wk : well_known_) {
    if (wk != nullptr) heap_.markRoot(wk);
  }

  /* 包装对象原型缓存（E4-2）：装箱对象的原型，始终可达。 */
  for (JSValue *proto : wrapper_protos_) {
    if (proto != nullptr) heap_.markRoot(proto);
  }

  /* Object.prototype 缓存（M3.3）：对象原型链根，始终可达。 */
  if (object_proto_ != nullptr) heap_.markRoot(object_proto_);

  /* Array.prototype 缓存（M4.1）：数组原型链根，始终可达。 */
  if (array_proto_ != nullptr) heap_.markRoot(array_proto_);

  /* Function.prototype 缓存（M4.2）：函数原型链根（call/apply/bind），
   * 始终可达。 */
  if (function_proto_ != nullptr) heap_.markRoot(function_proto_);

  /* Map/Set.prototype 缓存（M4.4a）：集合原型链根，始终可达。 */
  if (map_proto_ != nullptr) heap_.markRoot(map_proto_);
  if (set_proto_ != nullptr) heap_.markRoot(set_proto_);

  /* Promise.prototype 缓存（M5.3a）：Promise 原型链根，始终可达。 */
  if (promise_proto_ != nullptr) heap_.markRoot(promise_proto_);

  /* Generator.prototype 缓存（M5.5a-3）：Generator 原型链根，始终可达。 */
  if (generator_proto_ != nullptr) heap_.markRoot(generator_proto_);

  /* Promise 共享内部函数缓存（M5.3a）：Bound 闭包 target，始终可达。 */
  for (JSValue *pi : promise_internal_) {
    if (pi != nullptr) heap_.markRoot(pi);
  }

  /* 微任务队列（M5.3，§8.4）：pending job 的捕获数据（promise/决议
   * 闭包）不可回收。 */
  for (JSValue *job : job_queue_) {
    if (job != nullptr) heap_.markRoot(job);
  }

  /* VM 操作数栈（M2-VM，栈帧入根集）：解释器运行期间栈上值不可回收。
   * 嵌套 runBytecode（函数递归）时多个栈帧并存，全部标记。 */
  for (std::vector<JSValue *> *stack : vm_stacks_) {
    if (stack == nullptr) continue;
    for (JSValue *v : *stack) {
      if (v != nullptr) heap_.markRoot(v);
    }
  }

  /* VM 帧环境（M2.5 闭包）：当前帧 env 链是捕获变量承载，
   * 不标记则低阈值 GC 下被误回收（闭包计数器悬垂崩溃）。 */
  for (JSEnvironment *env : vm_envs_) {
    if (env != nullptr) heap_.markRoot(env);
  }

  /* 模块表（M5.2a，架构 §7 根集「模块表」）：已加载模块的 env（模块
   * 作用域绑定）+ 求值函数对象经 gcMarkRefs 递归标记，不可回收。 */
  for (const auto &kv : modules_) {
    if (kv.second != nullptr) heap_.markRoot(kv.second);
  }

  /* 作用域树：从根作用域沿 children 递归覆盖整棵树。所有存活作用域
   * （含挂起子树 P5）的绑定值都是根；被 pop 的作用域已销毁并摘除，
   * 不会出现在树中，其绑定值自然成为垃圾。 */
  JSScope *root = current_scope_;
  while (root->parent() != nullptr) root = root->parent();
  markScope(root);
}

void JSContext::markScope(JSScope *scope) {
  if (scope == nullptr) return;
  for (const auto &kv : scope->bindings()) {
    if (kv.second.value != nullptr) heap_.markRoot(kv.second.value);
  }
  for (JSScope *child : scope->children()) {
    markScope(child);
  }
}

void JSContext::deallocateSelf() { allocator_->deallocate(this); }

}  // namespace neojs
