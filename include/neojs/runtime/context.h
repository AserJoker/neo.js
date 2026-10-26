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
#include "neojs/runtime/module.h"
#include "neojs/runtime/object.h"
#include "neojs/runtime/scope.h"
#include "neojs/runtime/value.h"
#include <unordered_map>

namespace neojs {

class JSEnvironment;
class JSModuleRecord;

/* 模块源码供给回调（M5.2a，宿主经 JS_AddModuleLoader 注册）：
 * specifier -> 源码。裸 specifier（非 ./  ../ 开头）原样交给宿主解析；
 * 源码字符串归宿主所有（引擎编译后释放内部副本）。失败返回 nullptr。 */
using JSModuleLoadFunc = const char *(*)(JSContext *ctx, const char *specifier,
                                         void *opaque);

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
  JSValue *createMap(); /* M4.4a：新建 Map（[[MapData]]，原型链 Map.prototype） */
  JSValue *createSet(); /* M4.4a：新建 Set（[[SetData]]，原型链 Set.prototype） */

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

  /* 包装对象原型缓存（E4-2）：Number/String/Boolean/Symbol/BigInt 的 prototype。
   * 装箱（toObject）时包装对象原型链指向对应 prototype（§7.1.13 + §20.1.3 等）。
   * 索引按 ObjectKind（Number..BigInt）映射，进根集（markRoots），
   * installBuiltins 注册。M4.3e 扩到 5 项（含 BigInt）。 */
  JSValue *wrapperProto(ObjectKind kind) const {
    int idx = static_cast<int>(kind) - static_cast<int>(ObjectKind::Number);
    if (idx < 0 || idx >= 5) return nullptr;
    return wrapper_protos_[idx];
  }
  void setWrapperProto(ObjectKind kind, JSValue *v) {
    int idx = static_cast<int>(kind) - static_cast<int>(ObjectKind::Number);
    if (idx >= 0 && idx < 5) wrapper_protos_[idx] = v;
  }

  /* Object.prototype 缓存（M3.3，§20.1.3）：对象字面量/包装对象原型的
   * 原型链根。惰性创建 + 进根集（markRoots）。 */
  JSValue *objectProto() const { return object_proto_; }
  void setObjectProto(JSValue *v) { object_proto_ = v; }

  /* Array.prototype 缓存（M4.1，§23.1）：数组字面量/Array() 新建数组的
   * 原型链根。惰性创建 + 进根集（markRoots）。 */
  JSValue *arrayProto() const { return array_proto_; }
  void setArrayProto(JSValue *v) { array_proto_ = v; }

  /* Map.prototype / Set.prototype 缓存（M4.4a，§24.1/§24.2）：
   * new Map()/new Set() 新建集合的原型链根。惰性创建 + 进根集（markRoots）。 */
  JSValue *mapProto() const { return map_proto_; }
  void setMapProto(JSValue *v) { map_proto_ = v; }
  JSValue *setProto() const { return set_proto_; }
  void setSetProto(JSValue *v) { set_proto_ = v; }

  /* Function.prototype 缓存（M4.2，§20.2.3）：函数（Script/Builtin）的
   * 原型链根（call/apply/bind 所在）。惰性创建 + 进根集（markRoots）。 */
  JSValue *functionProto() const { return function_proto_; }
  void setFunctionProto(JSValue *v) { function_proto_ = v; }

  /* 模块表（M5.2a）：specifier -> 模块记录。加载过的模块都在表中，
   * 保证单例（同一 specifier 只求值一次）与循环依赖回查。
   * 模块表是 GC 根集（markRoots 标记每个 record）。 */
  JSModuleRecord *findModule(const std::string &specifier) const;
  void addModule(JSModuleRecord *record);
  const std::unordered_map<std::string, JSModuleRecord *> &modules() const {
    return modules_;
  }

  /* 模块源码供给回调（M5.2a）：宿主注册 specifier -> 源码。
   * 未注册回调时模块求值报错。 */
  JSModuleLoadFunc moduleLoader() const { return module_loader_; }
  void *moduleLoaderOpaque() const { return module_loader_opaque_; }
  void setModuleLoader(JSModuleLoadFunc func, void *opaque) {
    module_loader_ = func;
    module_loader_opaque_ = opaque;
  }

  /* 微任务队列（M5.3，§8.4）：JSContext 持有，每个 Job 是一个可调用
   * JSFunction（Bound 闭包）。宿主经 JS_RunJobs 驱动执行。
   * Job 队列是 GC 根集（markRoots 标记每个 job，防止 pending job 的
   * 捕获数据——promise/决议闭包——被误回收）。 */
  size_t jobQueueSize() const { return job_queue_.size(); }
  void enqueueJob(JSValue *job) { job_queue_.push_back(job); }
  JSValue *dequeueJob() {
    if (job_queue_.empty()) return nullptr;
    JSValue *job = job_queue_.front();
    job_queue_.erase(job_queue_.begin());
    return job;
  }

  /* Promise 共享内部函数缓存（M5.3a，§27.2）：Bound 闭包的 target。
   * 惰性创建 + 进根集（markRoots 遍历）。索引对应 promise.h 的
   * PromiseInternal 枚举值（14 项：M5.3a 8 项 + M5.3b 组合方法 4 项
   * + M5.4 await 续延 2 项）。 */
  JSValue *promiseInternal(int idx) const {
    return (idx >= 0 && idx < 14) ? promise_internal_[idx] : nullptr;
  }
  void setPromiseInternal(int idx, JSValue *v) {
    if (idx >= 0 && idx < 14) promise_internal_[idx] = v;
  }

  /* Promise.prototype 缓存（M5.3a，§27.2.4）：new Promise() 新建
   * Promise 的原型链根（then/catch/finally 所在）。惰性创建 + 进根集。 */
  JSValue *promiseProtoValue() const { return promise_proto_; }
  void setPromiseProtoValue(JSValue *v) { promise_proto_ = v; }

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

  /* VM 帧环境注册为 GC 根集（M2.5 闭包）：与操作数栈同生命周期，
   * runBytecode 帧进入时注册 env，析构/返回时弹出。
   * 帧环境链（[[Environment]] + parent）是闭包捕获变量的承载，
   * 不注册则在低阈值 GC 下被误回收（访问悬垂崩溃）。 */
  void vmEnvPush(JSEnvironment *env) { vm_envs_.push_back(env); }
  void vmEnvPop() {
    if (!vm_envs_.empty()) vm_envs_.pop_back();
  }
  const std::vector<JSEnvironment *> &vmEnvs() const { return vm_envs_; }

  /* 脚本顶层环境（M5.4，§13.3.2.4 GlobalDeclarationInstantiation）：
   * JS_Eval 顶层程序执行期间指向脚本顶层 env，结束后清空（防悬垂）。
   * op_StoreVar 写脚本顶层 var 时同步写全局对象属性——顶层函数声明
   * （含 async）独立编译走 LoadGlobal 读全局对象，普通闭包 LoadUpvalue
   * 读 top_env，双写保证两条路径一致。执行期间 env 由 vm_envs_ 根集
   * 保护（EnvRootGuard），无需额外 GC 标记。 */
  void setScriptTopEnv(JSEnvironment *env) { script_top_env_ = env; }
  JSEnvironment *scriptTopEnv() const { return script_top_env_; }

  /* 调用栈深度（M2.5，架构 §5.4 栈深度上限防爆栈）。
   * call() Script 分支进入时递增、返回时递减；超上限返回 RangeError。
   * 默认上限 10000（C++ 递归解释器物理栈安全范围内的保守值）。 */
  size_t callDepth() const { return call_depth_; }
  void incCallDepth() { ++call_depth_; }
  void decCallDepth() {
    if (call_depth_ > 0) --call_depth_;
  }
  size_t maxCallDepth() const { return max_call_depth_; }
  void setMaxCallDepth(size_t n) { max_call_depth_ = n; }

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
  JSValue *wrapper_protos_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr}; /* 包装原型缓存（E4-2，M4.3e 含 BigInt） */
  JSValue *object_proto_ = nullptr; /* Object.prototype 缓存（M3.3） */
  JSValue *array_proto_ = nullptr;  /* Array.prototype 缓存（M4.1） */
  JSValue *function_proto_ = nullptr; /* Function.prototype 缓存（M4.2） */
  JSValue *map_proto_ = nullptr;    /* Map.prototype 缓存（M4.4a） */
  JSValue *set_proto_ = nullptr;    /* Set.prototype 缓存（M4.4a） */
  JSValue *promise_proto_ = nullptr; /* Promise.prototype 缓存（M5.3a） */
  JSValue *promise_internal_[14] = {
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}; /* Promise 内部函数缓存（M5.3a/b + M5.4 await） */
  std::vector<JSValue *> job_queue_;  /* 微任务队列（M5.3，§8.4） */
  std::vector<std::vector<JSValue *> *> vm_stacks_; /* VM 操作数栈根集栈（M2-VM） */
  std::vector<JSEnvironment *> vm_envs_; /* VM 帧环境根集栈（M2.5 闭包） */
  JSEnvironment *script_top_env_ = nullptr; /* 脚本顶层 env（JS_Eval 执行期间有效） */
  std::unordered_map<std::string, JSModuleRecord *> modules_; /* 模块表（M5.2a） */
  JSModuleLoadFunc module_loader_ = nullptr; /* 模块源码供给回调（M5.2a） */
  void *module_loader_opaque_ = nullptr;      /* 回调 opaque 数据 */
  size_t call_depth_ = 0;   /* 当前调用栈深度（M2.5 防爆栈） */
  size_t max_call_depth_ = 10000; /* 调用深度上限（默认 10000） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_CONTEXT_H
