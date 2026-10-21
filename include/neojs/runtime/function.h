#ifndef NEOJS_RUNTIME_FUNCTION_H
#define NEOJS_RUNTIME_FUNCTION_H

/*
 * 内部 C++ 实现：JSFunction（函数对象）+ Call 统一入口。
 *
 * 遵循 docs/architecture.md §5.3 函数模型与 docs/coding-standards.md #6：
 *   - 函数是可调用对象（callable exotic object）：继承 JSObject（拥有属性表/
 *     原型链），并携带调用语义（回调指针 + this 模式 + 预留扩展字段）。
 *   - 宿主注入 C 函数（#6）：JSCFunction 签名统一，经 JS_NewCFunction 创建。
 *   - Call（§7.3.12）：统一调用入口，所有内置/宿主函数复用。IsCallable
 *     检查（§7.2.12）+ this 绑定（非严格模式 this 装箱）。
 *
 * E3 范围（用户确认）：只做可创建/可调用的最小集——Callable + C++ 回调可
 * 创建/可调用 + 统一 Call 入口 + this 绑定（非严格装箱，null/undefined ->
 * globalThis）。不做 call/apply/bind、闭包、类对象行为；预留扩展字段
 * （[[Construct]]/[[Environment]]/[[ThisMode]]）。
 *
 * GC（docs/gc.md §4.1）：JSFunction 继承 JSObject 的 gcMarkRefs（扩展字段
 * 当前均为裸值/回调，无引用边；E3 后若加 [[Environment]] 引用需补充标记）。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#include <cstddef>

#include "neojs/compiler/bytecode.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/object.h"

namespace neojs {

/* C 宿主函数签名（#6，与 neojs.h 的 JSCFunction 对应）：
 *   ctx   —— JSContext（#9 首参）
 *   self  —— this 绑定（已按调用语义解析）
 *   argc  —— 参数个数
 *   argv  —— 参数数组（宿主句柄指针）
 * 返回 JSValue *（宿主句柄；错误返回 kind=Exception 值，#4/#14）。
 * 为保持 C++ 内部层不依赖 C 头文件，用前向声明的 JSContext 与 JSValue。
 */
class JSContext;
class JSValue;
class JSEnvironment;

/* 函数调用语义（ECMA-262 §10.2.5）：
 *   Host        宿主 C 回调（句柄语义，E3）
 *   Builtin     引擎内置函数（内部值指针语义，E4）
 *   Script      JS 源码函数（M2.4 ③：携带 FunctionBytecode，VM 解释执行）
 *   Bound       bind 生成函数（M4.2，§20.2.3.5 BoundFunctionCreate）：
 *               携带 [[BoundTargetFunction]]/[[BoundThis]]/[[BoundArguments]]，
 *               调用时拼接参数转发（call() 内统一处理，非回调）。
 */
enum class FunctionKind : uint8_t { Host, Builtin, Script, Bound };

/* 引擎内置函数回调签名（E4）：参数/返回值均为**内部值指针**（非宿主句柄）。
 * 与 Host 的句柄语义区分：内置函数在引擎内部，无需句柄保护互转，
 * 错误经 ConversionResult（value=nullptr + error）传递（#3/#4）。
 *   ctx     —— JSContext（#9 首参）
 *   thisVal —— this 绑定（内部指针，已按 ThisMode 解析）
 *   args    —— 参数数组（内部指针；可为 nullptr 当 argc==0）
 *   argc    —— 参数个数 */
using JSBuiltinFn = ConversionResult (*)(JSContext *ctx, JSValue *thisVal,
                                         JSValue *const *args, size_t argc);

/* this 绑定模式（§10.2.5.6 预留）：
 *   Strict     严格模式：this 不装箱
 *   NonStrict  非严格模式：null/undefined -> globalThis；原始值装箱（E4）
 *   Lexical    箭头函数：继承外层 this（E3 不实现）
 */
enum class ThisMode : uint8_t { Strict, NonStrict, Lexical };

/* 函数对象：继承 JSObject（获得属性/原型链），携带调用语义。 */
class JSFunction : public JSObject {
 public:
  /* 创建宿主 C 回调函数。name 为函数名（用于 toString 等，可空）。
   * func 为回调指针（可为空——空函数调用返回 undefined 占位）。 */
  JSFunction(JSObject *prototype, FunctionKind kind, void *func,
             ThisMode this_mode = ThisMode::NonStrict);
  /* 创建引擎内置函数（E4）：内置回调用内部值指针语义。 */
  JSFunction(JSObject *prototype, JSBuiltinFn func,
             ThisMode this_mode = ThisMode::NonStrict);
  ~JSFunction() override = default;

  /* 函数类别 */
  FunctionKind functionKind() const { return kind_; }
  bool isHostFunction() const { return kind_ == FunctionKind::Host; }
  bool isBuiltinFunction() const { return kind_ == FunctionKind::Builtin; }

  /* 回调指针（FunctionKind::Host 时有效） */
  void *functionPtr() const { return func_; }
  /* 内置回调（FunctionKind::Builtin 时有效） */
  JSBuiltinFn builtinFn() const {
    return reinterpret_cast<JSBuiltinFn>(func_);
  }

  /* this 绑定模式（预留：E3 只实现 NonStrict 的 null/undefined->global 装箱） */
  ThisMode thisMode() const { return this_mode_; }
  void setThisMode(ThisMode m) { this_mode_ = m; } /* M5.1d-a：class 方法/构造器严格模式 */

  /* 函数名（可空） */
  const char *name() const { return name_; }

  /* JS 源码函数（FunctionKind::Script）：字节码（M2.4 ③）。
   * 注意：FunctionBytecode 只含指令/常量池（无 JSValue 引用边），
   * 不作为 GC 引用边标记（常量池是字符串/数字，无对象引用）。 */
  const FunctionBytecode &bytecode() const { return bc_; }
  void setBytecode(FunctionBytecode bc) { bc_ = std::move(bc); }

  /* 预留扩展字段（E3 不实现，占位防后续破坏布局）：
   *   construct_ —— [[Construct]]（new 语义）
   *   environment_ —— [[Environment]]（闭包环境，M2.5） */
  bool isConstructor() const { return construct_; }
  void setConstructor(bool c) { construct_ = c; }

  /* 构造器包装 kind（M4.3f）：new String()/Number()/Boolean()/BigInt() 应
   * 返回对应包装对象（String exotic object 等，§22.1.1.1 等）。
   * Ordinary 表示普通对象（无特殊包装语义）。 */
  ObjectKind constructWrapperKind() const { return construct_wrapper_kind_; }
  void setConstructWrapperKind(ObjectKind k) { construct_wrapper_kind_ = k; }

  /* [[Environment]]（M2.5 闭包）：创建该函数时的词法环境。
   * 函数被调用时新建环境的 parent 指向它（环境链）。GC 引用边。 */
  JSEnvironment *environment() const { return environment_; }
  void setEnvironment(JSEnvironment *env) { environment_ = env; }

  /* [[HomeObject]]（M5.1d-b class super）：方法定义时所在的对象
   * （实例方法 = prototype 对象，static 方法/构造器 = 构造器对象）。
   * super 属性访问从它的 [[Prototype]] 读取。GC 引用边。 */
  JSObject *homeObject() const { return home_object_; }
  void setHomeObject(JSObject *obj) { home_object_ = obj; }

  /* [[BoundTargetFunction]]/[[BoundThis]]/[[BoundArguments]]（M4.2）：
   * bind 生成函数（FunctionKind::Bound）的绑定元数据。GC 引用边。 */
  bool isBoundFunction() const { return kind_ == FunctionKind::Bound; }
  JSValue *boundTarget() const { return bound_target_; }
  JSValue *boundThis() const { return bound_this_; }
  const std::vector<JSValue *> &boundArgs() const { return bound_args_; }
  void setBoundData(JSValue *target, JSValue *bthis,
                    std::vector<JSValue *> args) {
    bound_target_ = target;
    bound_this_ = bthis;
    bound_args_ = std::move(args);
  }

  /* GC：JSObject 的引用边 + [[Environment]]（闭包环境，M2.5）
   * + [[BoundTargetFunction]]/[[BoundThis]]/[[BoundArguments]]（M4.2）
   * + [[HomeObject]]（M5.1d-b class super）。
   * 实现在 function.cc（需完整 Heap/JSEnvironment 类型）。 */
  void gcMarkRefs(Heap *heap) override;

 private:
  FunctionKind kind_ = FunctionKind::Host;
  void *func_ = nullptr;                /* 回调指针 */
  ThisMode this_mode_ = ThisMode::NonStrict;
  const char *name_ = nullptr;          /* 函数名（静态/堆字符串） */
  bool construct_ = false;              /* [[Construct]] 预留 */
  ObjectKind construct_wrapper_kind_ = ObjectKind::Ordinary; /* new 返回包装对象 */
  FunctionBytecode bc_;                 /* Script 函数字节码（M2.4 ③） */
  JSEnvironment *environment_ = nullptr; /* [[Environment]]（M2.5 闭包） */
  JSObject *home_object_ = nullptr;      /* [[HomeObject]]（M5.1d-b class super） */
  JSValue *bound_target_ = nullptr;     /* [[BoundTargetFunction]]（M4.2） */
  JSValue *bound_this_ = nullptr;       /* [[BoundThis]]（M4.2） */
  std::vector<JSValue *> bound_args_;   /* [[BoundArguments]]（M4.2） */
};

/* -------------------------------------------------------------------------- */
/* Call 统一入口（§7.3.12）                                                   */
/* -------------------------------------------------------------------------- */

/* Call(ctx, func, thisVal, args, argc)：调用函数。
 *   func    函数值（内部指针，kind=Object 且为 JSFunction）
 *   thisVal this 绑定（原始值按 ThisMode 装箱；null/undefined -> globalThis）
 *   args    参数数组（内部值指针数组；可为 nullptr 当 argc==0）
 * 返回 ConversionResult：成功 value 为结果（宿主句柄），失败 value=nullptr。
 * 规范语义：IsCallable 检查 -> PrepareForOrdinaryCall -> this 解析 ->
 * 回调调用。 */
ConversionResult call(JSContext *ctx, JSValue *func, JSValue *thisVal,
                      JSValue *const *args, size_t argc);

/* Construct（§7.3.13，M4.2）：new F(args) 语义。
 *   F     构造器（可调用对象）
 *   args  参数数组（内部值指针数组；可为 nullptr 当 argc==0）
 * 流程：IsConstructor 检查 -> 新对象（原型 = F.prototype）-> 调 F（this=新对象）
 *      -> 返回值是对象则返回之，否则返回新对象。
 * 当前简化：所有可调用对象都可构造（isConstructor 检查留 M5 class）。 */
ConversionResult construct(JSContext *ctx, JSValue *F, JSValue *const *args,
                           size_t argc);

/* IsCallable（§7.2.12）：v 是否为可调用对象（当前仅 JSFunction）。 */
bool isCallable(const JSValue *v);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_FUNCTION_H
