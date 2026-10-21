/*
 * 内部 C++ 实现：JSFunction + Call 统一入口（E3 函数最小集）。
 *
 * 遵循 docs/coding-standards.md：
 *   #3 禁止 C++ 异常跨 C ABI 边界——回调调用经 try-catch 保护。
 *   #4 JSValue 为一等公民，错误经 kind=Exception 值传递（ConversionResult）。
 *   #6 宿主注入 C 函数（JSCFunction 签名统一）。
 *
 * 规范对照（ECMA-262）：
 *   §7.2.12  IsCallable
 *   §7.3.12  Call（含 PrepareForOrdinaryCall）
 *   §10.2.5.6 this 绑定：非严格模式 null/undefined -> globalThis，
 *            原始值装箱（E4 完整；当前 Number/String/Boolean/Symbol 原样传）
 *
 * E3 范围（用户确认）：只做可创建/可调用的最小集。
 */

#include "neojs/runtime/function.h"

#include <exception>
#include <new>
#include <utility>
#include <vector>

#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/environment.h"
#include "neojs/runtime/value.h"
#include "neojs/runtime/vm.h"

namespace neojs {

namespace {

ConversionResult fail(const std::string &error) {
  ConversionResult r;
  r.error = error;
  return r;
}

ConversionResult ok(JSValue *v) {
  ConversionResult r;
  r.value = v;
  return r;
}

/* 分配一个宿主句柄保护内部值（供回调参数/this 使用，防 GC 回收）。 */
uint32_t makeTmpHandle(JSContext *ctx, JSValue *v) {
  return ctx->handles().alloc(v);
}

/* 释放临时句柄。 */
void freeTmpHandle(JSContext *ctx, uint32_t h) {
  ctx->handles().free(h);
}

/* C 宿主回调签名：JSValue* 是 C 层句柄指针（reinterpret_cast<uintptr_t>）。 */
using JSHostFn = JSValue *(*)(void *ctx, void *self, size_t argc, void *argv);

}  // namespace

/* -------------------------------------------------------------------------- */
/* JSFunction                                                                  */
/* -------------------------------------------------------------------------- */

JSFunction::JSFunction(JSObject *prototype, FunctionKind kind, void *func,
                       ThisMode this_mode)
    : JSObject(prototype), kind_(kind), func_(func), this_mode_(this_mode) {}

JSFunction::JSFunction(JSObject *prototype, JSBuiltinFn func,
                       ThisMode this_mode)
    : JSObject(prototype),
      kind_(FunctionKind::Builtin),
      func_(reinterpret_cast<void *>(func)),
      this_mode_(this_mode) {}

/* GC（M2.5）：JSObject 引用边 + [[Environment]] 闭包环境引用边。
 * M4.2：+ [[BoundTargetFunction]]/[[BoundThis]]/[[BoundArguments]] 引用边。 */
void JSFunction::gcMarkRefs(Heap *heap) {
  JSObject::gcMarkRefs(heap);
  if (environment_ != nullptr) heap->markRoot(environment_);
  if (bound_target_ != nullptr) heap->markRoot(bound_target_);
  if (bound_this_ != nullptr) heap->markRoot(bound_this_);
  for (JSValue *a : bound_args_) {
    heap->markRoot(a);
  }
  if (home_object_ != nullptr) heap->markRoot(home_object_); /* M5.1d-b super */
  if (lexical_this_ != nullptr) heap->markRoot(lexical_this_); /* M5.1d-c2 箭头函数词法 this */
}

/* -------------------------------------------------------------------------- */
/* Construct（§7.3.13，M4.2）                                                 */
/* -------------------------------------------------------------------------- */

ConversionResult construct(JSContext *ctx, JSValue *F, JSValue *const *args,
                            size_t argc) {
  /* 1. IsConstructor 检查：当前所有可调用对象均可构造（简化，M5 class 细化）。 */
  if (!isCallable(F)) {
    return fail("TypeError: F is not a constructor");
  }

  /* 2. 新对象原型 = F.prototype（须对象，否则回退 Object.prototype）。
   *    注意：F.prototype 访问不触发 GC 分配（getProperty 无分配），安全。 */
  JSObject *proto = nullptr;
  JSValue *fproto = F->object()->getProperty("prototype");
  if (fproto != nullptr && fproto->isObject()) {
    proto = fproto->object();
  } else {
    JSValue *op = objectProto(ctx); /* 惰性创建，可能触发 GC */
    if (op != nullptr) proto = op->object();
  }

  /* 3. 创建新对象（原型已定）。createObject 内部 pin/unpin 保护（M3.3）。
   *    M4.3f：包装构造器（new String/Number/Boolean/BigInt）应返回对应
   *    包装对象（String exotic object 等，§22.1.1.1/§21.1.1.1/§20.1.1.1/
   *    §21.2.1.2）：创建包装对象（原型 = wrapperProto[kind]），构造器
   *    返回值（原始值）在步骤 4 写回 [[Data]]。 */
  ObjectKind wk = static_cast<JSFunction *>(F->object())->constructWrapperKind();
  JSValue *obj = nullptr;
  if (wk != ObjectKind::Ordinary) {
    JSValue *wdata = ctx->createValue(Kind::Undefined); /* 占位，步骤 4 写回 */
    obj = ctx->createWrapper(wk, wdata);
    JSValue *wproto = ctx->wrapperProto(wk);
    if (wproto != nullptr && wproto->isObject()) {
      obj->object()->setPrototype(wproto->object());
    }
  } else {
    obj = ctx->createObject(proto);
  }

  /* 4. 调用构造器（this = 新对象）。返回值是对象则用之，否则返回新对象。
   *    注意：call 内部分配临时句柄保护 obj，obj 作为参数传入安全。 */
  ConversionResult r = call(ctx, F, obj, args, argc);
  if (!r.ok()) return r;
  if (r.value != nullptr && r.value->isObject()) return r;
  /* M4.3f：包装构造器返回原始值 -> 写回包装对象 [[Data]]（§22.1.1.1 步骤 7）。 */
  if (wk != ObjectKind::Ordinary && r.value != nullptr) {
    obj->object()->setWrapperData(r.value);
  }
  return ok(obj);
}

/* -------------------------------------------------------------------------- */
/* IsCallable（§7.2.12）                                                      */
/* -------------------------------------------------------------------------- */

bool isCallable(const JSValue *v) {
  if (v == nullptr || !v->isObject()) return false;
  /* 当前仅 JSFunction 可调用（E3 最小集）。 */
  return dynamic_cast<const JSFunction *>(v->object()) != nullptr;
}

/* -------------------------------------------------------------------------- */
/* Call（§7.3.12）                                                            */
/* -------------------------------------------------------------------------- */

ConversionResult call(JSContext *ctx, JSValue *func, JSValue *thisVal,
                      JSValue *const *args, size_t argc) {
  /* 1. IsCallable 检查（§7.2.12）：不可调用 -> TypeError。 */
  if (!isCallable(func)) {
    return fail("TypeError: not a function");
  }
  JSFunction *fn = static_cast<JSFunction *>(func->object());

  /* 2. this 绑定解析（§10.2.5.6）：
   *    - Strict：this 原样
   *    - NonStrict：null/undefined -> globalThis；原始值装箱（E4）
   *    - Lexical：箭头函数——忽略传入 this，用定义处捕获的词法 this
   *      （MakeFunction 时 setLexicalThis，M5.1d-c2） */
  JSValue *thisArg = thisVal;
  if (fn->thisMode() == ThisMode::Lexical) {
    thisArg = fn->lexicalThis();
  } else if (fn->thisMode() != ThisMode::Strict) {
    if (thisArg == nullptr || thisArg->kind() == Kind::Undefined ||
        thisArg->kind() == Kind::Null) {
      /* 非严格模式 null/undefined this -> globalThis（E5 完整内置）。 */
      thisArg = ctx->globalObject();
    } else if (thisArg->kind() != Kind::Object) {
      /* 非严格模式原始值 this 装箱（§10.2.5.6）。
       * E4 实现包装对象后启用；当前原样传（占位）。 */
      /* (void)0; */
    }
  }

  /* 3. 参数/this 分配临时句柄保护（回调执行期间防 GC 回收参数）。
   *    同时构造内部值指针数组（Builtin 直接用，Host 转句柄）。 */
  std::vector<uint32_t> tmp_handles;
  tmp_handles.reserve(argc + 1);
  std::vector<void *> argv_handles;
  argv_handles.reserve(argc);
  std::vector<JSValue *> args_internal;
  args_internal.reserve(argc);

  /* 内部值指针数组（this 已解析）。 */
  JSValue *this_internal = thisArg;
  uint32_t this_handle = makeTmpHandle(ctx, this_internal);
  tmp_handles.push_back(this_handle);
  for (size_t i = 0; i < argc; ++i) {
    if (args[i] == nullptr) {
      /* 空参数（undefined 占位）：用全局 undefined？当前无；直接分配空句柄。 */
      argv_handles.push_back(nullptr);
      args_internal.push_back(nullptr);
      continue;
    }
    uint32_t h = makeTmpHandle(ctx, args[i]);
    tmp_handles.push_back(h);
    argv_handles.push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(h)));
    args_internal.push_back(args[i]);
  }

  /* 4. 调用回调。
   *    Script：JS 源码函数（M2.4 ③）——解释执行字节码，递归调用本入口。
   *    Bound（M4.2）：bind 生成函数——拼接 [[BoundArguments]] + 调用参数，
   *      以 [[BoundThis]] 转发给 [[BoundTargetFunction]]（§20.2.3.5）。
   *    Host：句柄语义（参数/this 转句柄，回调经 C ABI，返回值是句柄）；
   *    Builtin：内部值指针语义（E4 内置函数，参数/返回值均为内部指针）。 */
  ConversionResult result;
  if (fn->functionKind() == FunctionKind::Bound) {
    /* 拼接绑定参数 + 调用参数（绑定参数在前，§20.2.3.5 步骤 5-6）。
     * 转发期间 target/bound_this/bound_args 由函数对象持有（GC 引用边），
     * 拼接产生的临时 vector 只含既有指针，无新分配，安全。 */
    const std::vector<JSValue *> &bargs = fn->boundArgs();
    std::vector<JSValue *> all;
    all.reserve(bargs.size() + argc);
    for (JSValue *a : bargs) all.push_back(a);
    for (size_t i = 0; i < argc; ++i) all.push_back(args[i]);
    JSValue *target = fn->boundTarget();
    if (target == nullptr || !isCallable(target)) {
      return fail("TypeError: invalid bound function");
    }
    return call(ctx, target, fn->boundThis(),
                all.empty() ? nullptr : all.data(), all.size());
  }
  if (fn->functionKind() == FunctionKind::Script) {
    /* 调用栈深度上限（M2.5，架构 §5.4）：递归过深报 RangeError 防爆栈。
     * 用 RAII 保证返回路径（含错误）都递减深度。 */
    if (ctx->callDepth() >= ctx->maxCallDepth()) {
      return fail("RangeError: Maximum call stack size exceeded");
    }
    ctx->incCallDepth();
    /* JS 函数：runBytecode 递归解释执行（栈式 VM 单帧模型，嵌套经递归）。
     * 参数/this 已在上面分配临时句柄保护（GC 根集），
     * runBytecode 内部把参数复制到本帧局部槽。
     * M2.5 闭包：创建新环境（parent = 函数 [[Environment]]），传给
     * runBytecode 作为本帧的闭包环境。 */
    JSEnvironment *call_env = ctx->heap().allocGCObject<JSEnvironment>(
        fn->environment());
    /* M2.5：参数按名写入本帧环境（内层闭包 LoadUpvalue 沿链查找参数）。
     * 参数名在字节码 locals[0..argc-1]（编译器按声明顺序分配槽）。 */
    const FunctionBytecode &bc = fn->bytecode();
    size_t nparams = argc < bc.locals.size() ? argc : bc.locals.size();
    for (size_t i = 0; i < nparams; ++i) {
      if (i < argc && args_internal[i] != nullptr &&
          !bc.locals[i].empty()) {
        call_env->setBinding(bc.locals[i], args_internal[i]);
      }
    }
    result = runBytecode(ctx, bc, thisArg,
                         args_internal.empty() ? nullptr : args_internal.data(),
                         argc, call_env, fn);
    /* 释放本帧 this/参数临时句柄。 */
    for (uint32_t h : tmp_handles) {
      freeTmpHandle(ctx, h);
    }
    if (result.ok()) {
      /* 返回值保护：内层 runBytecode 已弹栈，返回值此时无根集保护；
       * 分配临时句柄（句柄表是 GC 根集），由外层 Call 指令取走时释放。 */
      result.tmp_handle = makeTmpHandle(ctx, result.value);
    }
    ctx->decCallDepth(); /* 本帧返回：递减调用深度 */
    return result;
  }
  if (fn->functionKind() == FunctionKind::Builtin && fn->builtinFn() != nullptr) {
    /* 内置函数：直接传内部指针数组（无句柄互转）。 */
    try {
      result = fn->builtinFn()(ctx, thisArg,
                               args_internal.empty() ? nullptr
                                                     : args_internal.data(),
                               argc);
    } catch (const std::exception &e) {
      result = fail(std::string("Error: ") + e.what());
    } catch (...) {
      result = fail("Error: unknown C++ exception in builtin function");
    }
    /* 释放临时句柄。 */
    for (uint32_t h : tmp_handles) {
      freeTmpHandle(ctx, h);
    }
    return result;
  }
  if (fn->functionKind() != FunctionKind::Host || fn->functionPtr() == nullptr) {
    /* 空函数/未实现函数 -> undefined 占位（E3 最小集）。 */
    result = ok(ctx->createValue(Kind::Undefined));
  } else {
    try {
      JSHostFn host = reinterpret_cast<JSHostFn>(fn->functionPtr());
      JSValue *rv = host(ctx, reinterpret_cast<void *>(this_handle), argc,
                          argv_handles.empty() ? nullptr : argv_handles.data());
      /* 回调返回宿主句柄：解句柄存内部指针，并释放该句柄槽（G2）。
       * 返回值归属本次调用：引擎接管后 JS_Call 会为新结果分配句柄，
       * 原句柄槽立即释放防泄漏（值生死由 GC 判定，释放槽不影响值）。 */
      if (rv == nullptr) {
        result = ok(ctx->createValue(Kind::Undefined)); /* #14 语义：空返回 */
      } else {
        uint32_t rv_handle =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rv));
        JSValue *internal = ctx->handles().get(rv_handle);
        result = ok(internal != nullptr ? internal
                                        : ctx->createValue(Kind::Undefined));
        ctx->handles().free(rv_handle);
      }
    } catch (const std::exception &e) {
      result = fail(std::string("Error: ") + e.what());
    } catch (...) {
      result = fail("Error: unknown C++ exception in host function");
    }
  }

  /* 5. 释放临时句柄。 */
  for (uint32_t h : tmp_handles) {
    freeTmpHandle(ctx, h);
  }
  return result;
}

}  // namespace neojs
