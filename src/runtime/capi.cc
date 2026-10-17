/*
 * C 对外接口实现：C++ 内部对象的薄封装层。
 *
 * 遵循 docs/coding-standards.md：
 *   #2  C API 是 C++ 内部对象方法的薄封装（self 指向内部对象，转发）。
 *   #3  不向 C ABI 边界抛出 C++ 异常——标准库/内部可能抛异常处必须 try-catch。
 *   #4  JSValue 为一等公民，错误通过 kind = Exception 的值传递。
 *   #5  内存由分配器统一管理，JSValue/JSContext/JSAllocator 为不透明指针。
 *   #9  所有 API 首参为 ctx，运行时信息从 ctx 获取。
 *   #13 JSAllocator 暴露到 C API（JS_NewAllocator/JS_FreeAllocator）；
 *       JSContext 创建接受 JSAllocator*；oomAbort 由分配器自己处理。
 *   #14 返回 JSValue 的接口永不返回 nullptr；其它错误返回 kind = Exception 值。
 */

#include "neojs/neojs.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <utility>

#include "neojs/runtime/allocator.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/value.h"

/* 内部 kind 与 C JSKind 的数值映射保持同步（见 value.h） */
static_assert(static_cast<int>(neojs::Kind::Undefined) == JS_UNDEFINED, "kind mismatch");
static_assert(static_cast<int>(neojs::Kind::Exception) == JS_EXCEPTION, "kind mismatch");
static_assert(static_cast<int>(neojs::Kind::InterruptYield) == JS_INTERRUPT_YIELD, "kind mismatch");
static_assert(static_cast<int>(neojs::Kind::InterruptAwait) == JS_INTERRUPT_AWAIT, "kind mismatch");

namespace {

/* 默认分配器（宿主未注入时使用，#5） */
void *defaultAlloc(size_t size, void *) { return std::malloc(size); }
void defaultFree(void *ptr, void *) { std::free(ptr); }

template <typename V>
JSValue *makeValue(JSContext *ctx, V &&value) {
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  /* #9 + #13：值创建在 ctx->current_scope 上（placement new 经 JSAllocator::create<T>）。 */
  neojs::JSValue *v = c->currentScope().createValue(std::forward<V>(value));
  return reinterpret_cast<JSValue *>(v);
}

/* 创建 Exception 值的内部实现（分配器保证非空，#14）。 */
JSValue *makeException(JSContext *ctx, const char *message) {
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSValue *err = c->currentScope().createValue(neojs::Kind::Exception);
  err->setErrorInfo(message ? message : "");
  return reinterpret_cast<JSValue *>(err);
}

/* #3 + #14：包裹“返回 JSValue 的接口”。捕获 C++ 异常时返回 Exception 值；
 * 内存不足（bad_alloc）由分配器 abort。永不返回 nullptr。 */
template <typename Fn>
JSValue *guarded_create(JSContext *ctx, Fn &&fn) {
  try {
    return fn();
  } catch (const std::bad_alloc &) {
    std::abort(); /* oom：分配器未拦截的兜底 */
  } catch (const std::exception &e) {
    return makeException(ctx, e.what());
  } catch (...) {
    return makeException(ctx, "unknown C++ exception");
  }
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 分配器（#13）                                                               */
/* -------------------------------------------------------------------------- */

extern "C" JSAllocator *JS_NewAllocator(void *(*alloc)(size_t, void *),
                                        void (*free_fn)(void *, void *),
                                        void *userdata) {
  /* #3：跨 C 边界不抛异常 */
  try {
    /* 分配器自身的内存用默认 malloc 分配（此时还没有分配器可用）。 */
    void *mem = std::malloc(sizeof(neojs::JSAllocator));
    if (mem == nullptr) {
      std::fputs("neo.js: out of memory\n", stderr);
      std::abort();
    }
    neojs::JSAllocator *a =
        new (mem) neojs::JSAllocator(alloc ? alloc : defaultAlloc,
                                     free_fn ? free_fn : defaultFree,
                                     userdata);
    return reinterpret_cast<JSAllocator *>(a);
  } catch (...) {
    std::abort();
  }
}

extern "C" void JS_FreeAllocator(JSAllocator *allocator) {
  if (allocator == nullptr) return;
  neojs::JSAllocator *a = reinterpret_cast<neojs::JSAllocator *>(allocator);
  try {
    a->~JSAllocator();
    std::free(a); /* 分配器自身用默认 malloc 分配，用默认 free 释放 */
  } catch (...) {
    /* #3 */
  }
}

/* -------------------------------------------------------------------------- */
/* 生命周期                                                                    */
/* -------------------------------------------------------------------------- */

extern "C" JSContext *JS_NewContext(JSAllocator *allocator) {
  /* #3：跨 C 边界不抛异常 */
  try {
    /* #13：必须提供分配器，不支持 nullptr（编程错误，直接终止）。 */
    if (allocator == nullptr) {
      std::fputs("neo.js: JS_NewContext requires a non-null JSAllocator\n", stderr);
      std::abort();
    }
    neojs::JSAllocator *a = reinterpret_cast<neojs::JSAllocator *>(allocator);
    neojs::JSContext *ctx = a->create<neojs::JSContext>(a);
    return reinterpret_cast<JSContext *>(ctx);
  } catch (const std::exception &) {
    std::abort();
  } catch (...) {
    std::abort();
  }
}

extern "C" void JS_FreeContext(JSContext *ctx) {
  if (ctx == nullptr) return;
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  try {
    /* 注意：ctx 不拥有分配器（#13），只释放值 + ctx 自身；
     * allocator 由调用方在之后 JS_FreeAllocator。避免悬垂/重复释放。
     * 先取出分配器引用，析构后再释放 ctx 自身内存（不再次析构）。 */
    neojs::JSAllocator &a = c->allocator();
    c->~JSContext(); /* 释放所有存活值 */
    a.deallocate(c); /* 释放 ctx 自身内存 */
  } catch (...) {
    /* #3：跨 C 边界不抛异常 */
  }
}

/* 作用域压栈/弹栈（#9）。根作用域不可弹栈；协程/异步挂起时保留子树不销毁。 */
extern "C" void JS_PushScope(JSContext *ctx) {
  if (ctx == nullptr) return;
  try {
    reinterpret_cast<neojs::JSContext *>(ctx)->pushScope();
  } catch (...) {
    /* #3 */
  }
}

extern "C" void JS_PopScope(JSContext *ctx) {
  if (ctx == nullptr) return;
  try {
    reinterpret_cast<neojs::JSContext *>(ctx)->popScope();
  } catch (...) {
    /* #3 */
  }
}

/* 按名加载值：沿作用域链查找；未找到返回 Exception 值（#4/#14）。 */
extern "C" JSValue *JS_LoadValue(JSContext *ctx, const char *name) {
  /* ctx 为空属编程错误（#9 一切从 ctx 出发），直接终止。 */
  if (ctx == nullptr) {
    std::fputs("neo.js: null JSContext passed to JS_LoadValue\n", stderr);
    std::abort();
  }
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSValue *v =
        reinterpret_cast<neojs::JSContext *>(ctx)->currentScope().getBinding(name ? name : "");
    if (v != nullptr) return reinterpret_cast<JSValue *>(v);
    return makeException(ctx, "ReferenceError: name is not defined");
  });
}

/* 定义绑定到当前作用域（对应声明）。返回 value（#14）。 */
extern "C" JSValue *JS_DefineValue(JSContext *ctx, const char *name, JSValue *value) {
  if (ctx == nullptr) {
    std::fputs("neo.js: null JSContext passed to JS_DefineValue\n", stderr);
    std::abort();
  }
  return guarded_create(ctx, [&]() -> JSValue * {
    if (value == nullptr) return makeException(ctx, "TypeError: value is null");
    reinterpret_cast<neojs::JSContext *>(ctx)->currentScope().setBinding(name ? name : "",
                                                                       reinterpret_cast<neojs::JSValue *>(value));
    return value;
  });
}

/* 存储绑定：沿作用域链更新已存在的绑定（对应赋值）；未找到返回 Exception。 */
extern "C" JSValue *JS_StoreValue(JSContext *ctx, const char *name, JSValue *value) {
  if (ctx == nullptr) {
    std::fputs("neo.js: null JSContext passed to JS_StoreValue\n", stderr);
    std::abort();
  }
  return guarded_create(ctx, [&]() -> JSValue * {
    if (value == nullptr) return makeException(ctx, "TypeError: value is null");
    bool ok = reinterpret_cast<neojs::JSContext *>(ctx)->currentScope().storeBinding(
        name ? name : "", reinterpret_cast<neojs::JSValue *>(value));
    if (!ok) {
      std::string msg = "ReferenceError: ";
      msg += (name ? name : "");
      msg += " is not defined";
      return makeException(ctx, msg.c_str());
    }
    return value;
  });
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
  try {
    /* #9 + #13：值归属当前作用域，经 JSScope::freeValue 析构 + 释放。 */
    c->currentScope().freeValue(reinterpret_cast<neojs::JSValue *>(value));
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

/* 创建错误值（kind = Exception）。永不返回 nullptr（#14）。 */
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
