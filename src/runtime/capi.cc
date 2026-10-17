/*
 * C 对外接口实现：C++ 内部对象的薄封装层。
 *
 * 遵循 docs/coding-standards.md：
 *   #2  C API 是 C++ 内部对象方法的薄封装（self 指向内部对象，转发）。
 *   #3  不向 C ABI 边界抛出 C++ 异常——标准库/内部可能抛异常处必须 try-catch。
 *   #4  JSValue 为一等公民，错误通过 kind = Exception 的值传递。
 *   #5  内存由 ctx 统一管理（alloc/free 回调），JSValue/JSContext 为不透明指针。
 *   #9  所有 API 首参为 ctx，运行时信息从 ctx 获取。
 *   #14 返回 JSValue 的接口永不返回 nullptr：内存不足（alloc 失败）直接 abort；
 *       其它错误返回 kind = Exception 的 JSValue。
 */

#include "neojs/neojs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <utility>

#include "neojs/runtime/context.h"
#include "neojs/runtime/value.h"

/* 内部 kind 与 C JSKind 的数值映射保持同步（见 value.h） */
static_assert(static_cast<int>(neojs::Kind::Undefined) == JS_UNDEFINED, "kind mismatch");
static_assert(static_cast<int>(neojs::Kind::Exception) == JS_EXCEPTION, "kind mismatch");
static_assert(static_cast<int>(neojs::Kind::InterruptYield) == JS_INTERRUPT_YIELD, "kind mismatch");
static_assert(static_cast<int>(neojs::Kind::InterruptAwait) == JS_INTERRUPT_AWAIT, "kind mismatch");

namespace {

/* #14：内存不足（alloc 失败）不可恢复，直接 abort。 */
[[noreturn]] void oomAbort() {
  std::fputs("neo.js: out of memory\n", stderr);
  std::abort();
}

/* 分配一块 JSValue 大小的内存并构造值；alloc 失败 abort（#14）。
 * 返回的内部 JSValue* 由 ctx 注册管理。 */
/* 返回 JSValue 的接口要求 ctx 有效（#9）。ctx 为空属于编程错误，直接 abort。 */
[[noreturn]] void nullCtxAbort() {
  std::fputs("neo.js: null JSContext passed to value-returning API\n", stderr);
  std::abort();
}

template <typename V>
JSValue *makeValue(JSContext *ctx, V &&value) {
  if (ctx == nullptr) nullCtxAbort();
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  void *mem = c->allocate(sizeof(neojs::JSValue));
  if (mem == nullptr) oomAbort();
  neojs::JSValue *v = new (mem) neojs::JSValue(std::forward<V>(value));
  c->registerValue(v);
  return reinterpret_cast<JSValue *>(v);
}

/* 创建 Exception 值的内部实现（alloc 失败 abort）。 */
JSValue *makeException(JSContext *ctx, const char *message) {
  if (ctx == nullptr) nullCtxAbort();
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  void *mem = c->allocate(sizeof(neojs::JSValue));
  if (mem == nullptr) oomAbort();
  neojs::JSValue *err = new (mem) neojs::JSValue(neojs::Kind::Exception);
  err->setErrorInfo(message ? message : "");
  c->registerValue(err);
  return reinterpret_cast<JSValue *>(err);
}

/* #3 + #14：包裹“返回 JSValue 的接口”。捕获 C++ 异常（非内存不足）时
 * 返回 Exception 值；内存不足由 oomAbort 处理。永不返回 nullptr。 */
template <typename Fn>
JSValue *guarded_create(JSContext *ctx, Fn &&fn) {
  try {
    return fn();
  } catch (const std::bad_alloc &) {
    oomAbort();
  } catch (const std::exception &e) {
    return makeException(ctx, e.what());
  } catch (...) {
    return makeException(ctx, "unknown C++ exception");
  }
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 生命周期                                                                    */
/* -------------------------------------------------------------------------- */

extern "C" JSContext *JS_NewContext(void *(*alloc)(size_t, void *),
                                    void (*free_fn)(void *, void *),
                                    void *userdata) {
  /* #3：跨 C 边界不抛异常 */
  try {
    /* #5：JSContext 自身的分配也走宿主注入的 alloc/free 回调；失败直接 abort（#14 精神） */
    void *(*a)(size_t, void *) = alloc ? alloc : [](size_t size, void *) -> void * {
      return std::malloc(size);
    };
    void *mem = a(sizeof(neojs::JSContext), userdata);
    if (mem == nullptr) oomAbort();
    neojs::JSContext *ctx = new (mem) neojs::JSContext(alloc, free_fn, userdata);
    return reinterpret_cast<JSContext *>(ctx);
  } catch (const std::exception &) {
    oomAbort(); /* 构造失败视为不可恢复 */
  } catch (...) {
    oomAbort();
  }
}

extern "C" void JS_FreeContext(JSContext *ctx) {
  if (ctx == nullptr) return;
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  try {
    c->~JSContext();
    c->deallocateSelf(); /* #5：用宿主注入的 free 释放 ctx 自身 */
  } catch (...) {
    /* #3：跨 C 边界不抛异常 */
  }
}

/* -------------------------------------------------------------------------- */
/* 值构造与访问                                                                */
/* -------------------------------------------------------------------------- */

extern "C" JSValue *JS_CreateUndefined(JSContext *ctx) {
  return guarded_create(ctx, [&]() -> JSValue * { return makeValue(ctx, neojs::JSValue()); });
}

extern "C" JSValue *JS_CreateNull(JSContext *ctx) {
  return guarded_create(ctx, [&]() -> JSValue * {
    return makeValue(ctx, neojs::JSValue(neojs::Kind::Null));
  });
}

extern "C" JSValue *JS_CreateBoolean(JSContext *ctx, int value) {
  return guarded_create(ctx, [&]() -> JSValue * {
    return makeValue(ctx, neojs::JSValue(value != 0));
  });
}

extern "C" JSValue *JS_CreateNumber(JSContext *ctx, double value) {
  return guarded_create(ctx, [&]() -> JSValue * {
    return makeValue(ctx, neojs::JSValue(value));
  });
}

extern "C" JSValue *JS_CreateInteger(JSContext *ctx, int64_t value) {
  return guarded_create(ctx, [&]() -> JSValue * {
    return makeValue(ctx, neojs::JSValue(value));
  });
}

extern "C" JSValue *JS_CreateString(JSContext *ctx, const char *str, size_t len) {
  return guarded_create(ctx, [&]() -> JSValue * {
    std::string s(str ? str : "", len);
    return makeValue(ctx, std::move(s));
  });
}

extern "C" void JS_FreeValue(JSContext *ctx, JSValue *value) {
  if (ctx == nullptr || value == nullptr) return;
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSValue *v = reinterpret_cast<neojs::JSValue *>(value);
  try {
    v->~JSValue();
    c->freeValue(v);
  } catch (...) {
    /* #3 */
  }
}

extern "C" JSKind JS_GetKind(JSContext *ctx, const JSValue *value) {
  (void)ctx;
  if (value == nullptr) return JS_UNDEFINED;
  const neojs::JSValue *v = reinterpret_cast<const neojs::JSValue *>(value);
  return static_cast<JSKind>(static_cast<int>(v->kind()));
}

extern "C" int JS_ToBoolean(JSContext *ctx, const JSValue *value) {
  (void)ctx;
  if (value == nullptr) return 0;
  return reinterpret_cast<const neojs::JSValue *>(value)->toBoolean() ? 1 : 0;
}

extern "C" double JS_ToNumber(JSContext *ctx, const JSValue *value) {
  (void)ctx;
  if (value == nullptr) return 0.0;
  return reinterpret_cast<const neojs::JSValue *>(value)->toNumber();
}

extern "C" int64_t JS_ToInteger(JSContext *ctx, const JSValue *value) {
  (void)ctx;
  if (value == nullptr) return 0;
  return reinterpret_cast<const neojs::JSValue *>(value)->toInteger();
}

extern "C" const char *JS_ToString(JSContext *ctx, const JSValue *value) {
  (void)ctx;
  if (value == nullptr) return nullptr;
  return reinterpret_cast<const neojs::JSValue *>(value)->toString().c_str();
}

/* -------------------------------------------------------------------------- */
/* 错误值（#4/#14）                                                             */
/* -------------------------------------------------------------------------- */

/* 创建错误值（kind = Exception）。alloc 失败 abort（#14），永不返回 nullptr。 */
extern "C" JSValue *JS_CreateError(JSContext *ctx, const char *message) {
  return guarded_create(ctx, [&]() -> JSValue * {
    return makeException(ctx, message);
  });
}

/* -------------------------------------------------------------------------- */
/* 执行（M0 占位）                                                             */
/* -------------------------------------------------------------------------- */

extern "C" JSValue *JS_Eval(JSContext *ctx, const char *source, size_t source_len) {
  (void)source;
  (void)source_len;
  /* M0：求值器未实现，返回“未实现”错误值以验证错误通道（#4）。 */
  return JS_CreateError(ctx, "JS_Eval: evaluator not implemented (M0)");
}
