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

#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
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
   *    - Lexical：继承外层（E3 不实现） */
  JSValue *thisArg = thisVal;
  if (fn->thisMode() != ThisMode::Strict) {
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
   *    Host：句柄语义（参数/this 转句柄，回调经 C ABI，返回值是句柄）；
   *    Builtin：内部值指针语义（E4 内置函数，参数/返回值均为内部指针）。 */
  ConversionResult result;
  if (fn->functionKind() == FunctionKind::Script) {
    /* JS 函数：runBytecode 递归解释执行（栈式 VM 单帧模型，嵌套经递归）。
     * 参数/this 已在上面分配临时句柄保护（GC 根集），
     * runBytecode 内部把参数复制到本帧局部槽。 */
    result = runBytecode(ctx, fn->bytecode(), thisArg,
                         args_internal.empty() ? nullptr : args_internal.data(),
                         argc);
    /* 释放本帧 this/参数临时句柄。 */
    for (uint32_t h : tmp_handles) {
      freeTmpHandle(ctx, h);
    }
    if (result.ok()) {
      /* 返回值保护：内层 runBytecode 已弹栈，返回值此时无根集保护；
       * 分配临时句柄（句柄表是 GC 根集），由外层 Call 指令取走时释放。 */
      result.tmp_handle = makeTmpHandle(ctx, result.value);
    }
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
