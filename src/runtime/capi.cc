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
 *
 * 遵循 docs/gc.md（GC 引擎）：
 *   G2  C API 返回/接收的是句柄（HandleTable 表索引），非裸指针；
 *       GC 回收不影响宿主已持有的句柄。
 *   C ABI 兼容：typedef struct JSValue JSValue 保持不透明指针不变，
 *   但 C 层 JSValue* 实际是 reinterpret_cast<JSValue*>(handle)（整数索引）。
 */

#include "neojs/neojs.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <utility>

#include "neojs/frontend/diagnostics.h"
#include "neojs/frontend/frontend.h"
#include "neojs/runtime/allocator.h"
#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/handles.h"
#include "neojs/runtime/object.h"
#include "neojs/runtime/value.h"

/* 内部 kind 与 C JSKind 的数值映射保持同步（见 value.h） */
static_assert(static_cast<int>(neojs::Kind::Undefined) == JS_UNDEFINED, "kind mismatch");
static_assert(static_cast<int>(neojs::Kind::Symbol) == JS_SYMBOL, "kind mismatch");
static_assert(static_cast<int>(neojs::Kind::Exception) == JS_EXCEPTION, "kind mismatch");
static_assert(static_cast<int>(neojs::Kind::InterruptYield) == JS_INTERRUPT_YIELD, "kind mismatch");
static_assert(static_cast<int>(neojs::Kind::InterruptAwait) == JS_INTERRUPT_AWAIT, "kind mismatch");

namespace {

/* 默认分配器（宿主未注入时使用，#5） */
void *defaultAlloc(size_t size, void *) { return std::malloc(size); }
void defaultFree(void *ptr, void *) { std::free(ptr); }

/* -------------------------------------------------------------------------- */
/* 句柄互转（docs/gc.md §3.3 + §6）                                           */
/* -------------------------------------------------------------------------- */

/* 句柄 -> C JSValue*：整数索引 reinterpret_cast 为指针（#14：非 0）。 */
JSValue *fromHandle(uint32_t handle) {
  return reinterpret_cast<JSValue *>(static_cast<uintptr_t>(handle));
}

/* C JSValue* -> 句柄：指针低位截断回索引。 */
uint32_t toHandle(JSValue *value) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));
}

/* C 句柄 -> 内部 neojs::JSValue*；无效/空槽返回 nullptr。 */
neojs::JSValue *unhandle(neojs::JSContext *c, JSValue *value) {
  if (value == nullptr) return nullptr;
  return c->handles().get(toHandle(value));
}

/* 内部 neojs::JSValue* -> 分配新句柄 -> C JSValue*（#14：非 0）。 */
JSValue *makeHandle(neojs::JSContext *c, neojs::JSValue *v) {
  if (v == nullptr) return nullptr; /* 未初始化绑定等：语义允许 nullptr */
  return fromHandle(c->handles().alloc(v));
}

template <typename V>
JSValue *makeValue(JSContext *ctx, V &&value) {
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  /* #13 + G1：值经 Heap::allocValue 创建并登记到堆；分配句柄返回（G2）。 */
  neojs::JSValue *v = c->createValue(std::forward<V>(value));
  return makeHandle(c, v);
}

/* 创建值并可选绑定到当前作用域（无孤立值原则）：
 *   name == NULL  -> 临时值（仅宿主句柄持有）；
 *   name != NULL  -> 具名值：自动定义绑定（kind=Let，块级绑定到当前作用域，
 *                    非 var 的函数级提升语义）。返回句柄。 */
template <typename V>
JSValue *makeValueNamed(JSContext *ctx, V &&value, const char *name) {
  JSValue *h = makeValue(ctx, std::forward<V>(value));
  if (h == nullptr || name == nullptr) return h; /* 临时值 / 分配失败 */
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSBinding b;
  b.kind = neojs::BindingKind::Let; /* 块级绑定，绑定到 current_scope 本身 */
  b.value = unhandle(c, h);         /* 解句柄存内部裸指针（绑定是根集） */
  b.initialized = true;             /* CreateX 立即初始化，无 TDZ */
  c->currentScope().defineBinding(name, b);
  return h;
}

/* 已创建的内部值 -> 分配句柄，可选绑定到当前作用域（无孤立值原则）。
 * 用于对象工厂等**已创建内部值**的场景；makeValueNamed 是构造+创建二合一。 */
JSValue *makeValueHandleNamed(JSContext *ctx, neojs::JSValue *v, const char *name) {
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  JSValue *h = makeHandle(c, v);
  if (h == nullptr || name == nullptr) return h;
  neojs::JSBinding b;
  b.kind = neojs::BindingKind::Let; /* 块级绑定，同 makeValueNamed */
  b.value = v; /* 绑定存内部裸指针（绑定是根集） */
  b.initialized = true;
  c->currentScope().defineBinding(name, b);
  return h;
}

/* 创建 Exception 值的内部实现（分配器保证非空，#14）。 */
JSValue *makeException(JSContext *ctx, const char *message) {
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSValue *err = c->createValue(neojs::Kind::Exception);
  err->setErrorInfo(message ? message : "");
  return makeHandle(c, err);
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

/* 按名加载值：沿作用域链查找；未找到返回 Exception 值（#4/#14）。
 * 返回新句柄（宿主负责 JS_FreeValue）。M0 不校验 TDZ（initialized），M2 启用。 */
extern "C" JSValue *JS_LoadValue(JSContext *ctx, const char *name) {
  /* ctx 为空属编程错误（#9 一切从 ctx 出发），直接终止。 */
  if (ctx == nullptr) {
    std::fputs("neo.js: null JSContext passed to JS_LoadValue\n", stderr);
    std::abort();
  }
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSBinding *b = c->currentScope().findBinding(name ? name : "");
    if (b == nullptr || b->value == nullptr) {
      return makeException(ctx, "ReferenceError: name is not defined");
    }
    return makeHandle(c, b->value); /* 绑定存内部裸指针，分配新句柄返回（G2） */
  });
}

/* 定义绑定到当前作用域（对应声明）。kind 只记录不校验（M0 打地基，M2 启用）。
 * value 是宿主句柄：解句柄存内部裸指针（绑定存内部指针，G2/§6）。
 * 返回 value（#14）；value==nullptr 时定义未初始化绑定（TDZ 起点）。 */
extern "C" JSValue *JS_DefineValue(JSContext *ctx, const char *name, JSValue *value,
                                    int kind) {
  if (ctx == nullptr) {
    std::fputs("neo.js: null JSContext passed to JS_DefineValue\n", stderr);
    std::abort();
  }
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSBinding b;
    b.kind = static_cast<neojs::BindingKind>(kind); /* M0 不校验 kind 合法性 */
    b.value = unhandle(c, value);                  /* 解句柄存内部裸指针 */
    b.initialized = (value != nullptr); /* nullptr → 未初始化绑定（TDZ 起点） */
    c->currentScope().defineBinding(name ? name : "", b);
    return value; /* 可能为 nullptr：定义未初始化绑定（#14 对非空值返回非空，此处语义允许） */
  });
}

/* 存储绑定：沿作用域链更新已存在的绑定（对应赋值）；未找到返回 Exception。
 * value 是宿主句柄：解句柄存内部裸指针。M0 不校验 const（kind），M2 启用。 */
extern "C" JSValue *JS_StoreValue(JSContext *ctx, const char *name, JSValue *value) {
  if (ctx == nullptr) {
    std::fputs("neo.js: null JSContext passed to JS_StoreValue\n", stderr);
    std::abort();
  }
  return guarded_create(ctx, [&]() -> JSValue * {
    if (value == nullptr) return makeException(ctx, "TypeError: value is null");
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSBinding *b = c->currentScope().findBinding(name ? name : "");
    if (b == nullptr) {
      std::string msg = "ReferenceError: ";
      msg += (name ? name : "");
      msg += " is not defined";
      return makeException(ctx, msg.c_str());
    }
    b->value = unhandle(c, value); /* 解句柄存内部裸指针 */
    b->initialized = true;
    return value;
  });
}

/* -------------------------------------------------------------------------- */
/* 值构造与访问                                                                */
/* -------------------------------------------------------------------------- */

extern "C" JSValue *JS_CreateUndefined(JSContext *ctx, const char *name) {
  return guarded_create(ctx, [&]() -> JSValue * {
    return makeValueNamed(ctx, neojs::JSValue(), name);
  });
}

extern "C" JSValue *JS_CreateNull(JSContext *ctx, const char *name) {
  return guarded_create(ctx, [&]() -> JSValue * {
    return makeValueNamed(ctx, neojs::JSValue(neojs::Kind::Null), name);
  });
}

extern "C" JSValue *JS_CreateBoolean(JSContext *ctx, int value, const char *name) {
  return guarded_create(ctx, [&]() -> JSValue * {
    return makeValueNamed(ctx, neojs::JSValue(value != 0), name);
  });
}

extern "C" JSValue *JS_CreateNumber(JSContext *ctx, double value, const char *name) {
  return guarded_create(ctx, [&]() -> JSValue * {
    return makeValueNamed(ctx, neojs::JSValue(value), name);
  });
}

extern "C" JSValue *JS_CreateInteger(JSContext *ctx, int64_t value, const char *name) {
  return guarded_create(ctx, [&]() -> JSValue * {
    return makeValueNamed(ctx, neojs::JSValue(value), name);
  });
}

extern "C" JSValue *JS_CreateString(JSContext *ctx, const char *str, size_t len,
                                    const char *name) {
  return guarded_create(ctx, [&]() -> JSValue * {
    std::string s(str ? str : "", len);
    return makeValueNamed(ctx, std::move(s), name);
  });
}

/* 创建 Symbol 值（kind = JS_SYMBOL）。description 为可选的符号描述符
 * （无描述时传 NULL/空串）；每个 Symbol 都是唯一值（引用相等）。
 * name 语义同其它 CreateX。 */
extern "C" JSValue *JS_CreateSymbol(JSContext *ctx, const char *description,
                                    const char *name) {
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *sym = c->createValue(neojs::Kind::Symbol);
    sym->setSymbolDescription(description ? description : "");
    return makeValueHandleNamed(ctx, sym, name);
  });
}

/* 创建宿主 C 函数值（E3）。func 为回调；name 为函数名（可空）。
 * 函数对象是 JSFunction（继承 JSObject，可调用对象）。 */
extern "C" JSValue *JS_NewCFunction(JSContext *ctx, JSCFunction func,
                                    const char *name) {
  if (ctx == nullptr) {
    std::fputs("neo.js: null JSContext passed to JS_NewCFunction\n", stderr);
    std::abort();
  }
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    /* JSFunction 继承 JSObject：经 Heap 创建并登记。 */
    neojs::JSFunction *fn =
        c->heap().allocGCObject<neojs::JSFunction>(
            nullptr /* prototype */,
            neojs::FunctionKind::Host,
            reinterpret_cast<void *>(func));
    neojs::JSValue *v = c->heap().allocGCObject<neojs::JSValue>();
    v->setObject(fn);
    fn->setHolder(v); /* 反向包装指针（与 createObject 一致） */
    return makeValueHandleNamed(ctx, v, name);
  });
}

/* 调用函数（§7.3.12 Call）。func 为函数值；this_val 为 this 绑定（可空）；
 * argv 为参数数组（宿主句柄指针）；argc 为参数个数。
 * 返回结果句柄；func 不可调用返回 Exception 值（#4）。 */
extern "C" JSValue *JS_Call(JSContext *ctx, JSValue *func, JSValue *this_val,
                            size_t argc, JSValue *const *argv) {
  if (ctx == nullptr) {
    std::fputs("neo.js: null JSContext passed to JS_Call\n", stderr);
    std::abort();
  }
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *fn = unhandle(c, func);
    neojs::JSValue *tv = unhandle(c, this_val);
    if (fn == nullptr) {
      return makeException(ctx, "TypeError: not a function");
    }
    /* argv：宿主句柄数组 -> 内部值指针数组。 */
    std::vector<neojs::JSValue *> args_internal;
    args_internal.reserve(argc);
    for (size_t i = 0; i < argc; ++i) {
      args_internal.push_back(unhandle(c, argv ? argv[i] : nullptr));
    }
    neojs::ConversionResult r =
        neojs::call(c, fn, tv, args_internal.data(), argc);
    if (!r.ok()) {
      return makeException(ctx, r.error.c_str());
    }
    /* 结果为内部值：分配句柄返回（宿主负责 FreeValue）。 */
    return makeHandle(c, r.value);
  });
}

extern "C" void JS_FreeValue(JSContext *ctx, JSValue *value) {
  if (ctx == nullptr || value == nullptr) return;
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  try {
    /* G2 + §5.2：释放宿主句柄槽（对象生死由 GC 判定，不立即释放）。
     * 直接释放传入句柄对应的槽，不反查内部值（同一值可被多句柄指向）。 */
    c->freeHandle(toHandle(value));
  } catch (...) {
    /* #3 */
  }
}

extern "C" JSKind JS_GetKind(JSContext *ctx, const JSValue *value) {
  if (ctx == nullptr || value == nullptr) return JS_UNDEFINED;
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  const neojs::JSValue *v =
      c->handles().get(toHandle(const_cast<JSValue *>(value))); /* 解句柄 */
  if (v == nullptr) return JS_UNDEFINED;
  return static_cast<JSKind>(static_cast<int>(v->kind()));
}

extern "C" int JS_ToBoolean(JSContext *ctx, const JSValue *value) {
  if (ctx == nullptr || value == nullptr) return 0;
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  const neojs::JSValue *v =
      c->handles().get(toHandle(const_cast<JSValue *>(value)));
  if (v == nullptr) return 0;
  return neojs::toBoolean(v) ? 1 : 0;
}

extern "C" double JS_ToNumber(JSContext *ctx, const JSValue *value) {
  if (ctx == nullptr || value == nullptr) return 0.0;
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSValue *v =
      c->handles().get(toHandle(const_cast<JSValue *>(value)));
  if (v == nullptr) return 0.0;
  /* E2：完整 ToNumber（§7.1.4）。转换失败（如 Symbol）返回 NaN
   * （QuickJS 风格：JS_ToNumber 无法传错误，失败统一 NaN）。 */
  neojs::ConversionResult r = neojs::toNumber(c, v);
  return r.ok() ? r.value->toNumber() : NAN;
}

extern "C" int64_t JS_ToInteger(JSContext *ctx, const JSValue *value) {
  if (ctx == nullptr || value == nullptr) return 0;
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSValue *v =
      c->handles().get(toHandle(const_cast<JSValue *>(value)));
  if (v == nullptr) return 0;
  /* E2：ToNumber 后按 §7.1.5 ToIntegerOrInfinity 截断。 */
  neojs::ConversionResult r = neojs::toNumber(c, v);
  if (!r.ok()) return 0; /* 失败（Symbol）-> 0 */
  double d = r.value->toNumber();
  if (std::isnan(d)) return 0;
  if (std::isinf(d)) return d > 0 ? INT64_MAX : INT64_MIN;
  return static_cast<int64_t>(d);
}

extern "C" const char *JS_ToString(JSContext *ctx, const JSValue *value) {
  if (ctx == nullptr || value == nullptr) return nullptr;
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSValue *v =
      c->handles().get(toHandle(const_cast<JSValue *>(value)));
  if (v == nullptr) return nullptr;
  /* E2：完整 ToString（§7.1.17）。失败（Symbol）返回 nullptr。
   * 转换可能产生临时内部值（无宿主句柄）；结果字符串拷贝到 ctx 缓冲
   * （生命周期归 ctx，下一次调用覆盖），避免悬垂。 */
  neojs::ConversionResult r = neojs::toString(c, v);
  if (!r.ok()) return nullptr;
  return c->storeTmpString(r.value->toString());
}

/* -------------------------------------------------------------------------- */
/* 错误值（#4/#14）                                                             */
/* -------------------------------------------------------------------------- */

/* 创建错误值（kind = Exception）。永不返回 nullptr（#14）。
 * name 语义同其它 CreateX：NULL = 临时值，非空 = 绑定到当前作用域。 */
extern "C" JSValue *JS_CreateError(JSContext *ctx, const char *message,
                                    const char *name) {
  return guarded_create(ctx, [&]() -> JSValue * {
    JSValue *h = makeException(ctx, message);
    if (h == nullptr || name == nullptr) return h;
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSBinding b;
    b.kind = neojs::BindingKind::Let; /* 块级绑定，同 makeValueNamed */
    b.value = unhandle(c, h);
    b.initialized = true;
    c->currentScope().defineBinding(name, b);
    return h;
  });
}

/* -------------------------------------------------------------------------- */
/* 全局对象与 well-known symbol（E4）                                          */
/* -------------------------------------------------------------------------- */

/* 全局对象（globalThis 雏形）。首次调用时创建并注册 E4 内置。 */
extern "C" JSValue *JS_GetGlobalObject(JSContext *ctx) {
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    return makeHandle(c, c->globalObject());
  });
}

/* 取 well-known symbol（index 对应 WellKnown 枚举）。 */
extern "C" JSValue *JS_GetWellKnownSymbol(JSContext *ctx, int index) {
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    if (index < 0 || index > 3) {
      return makeException(ctx, "RangeError: well-known symbol index out of range");
    }
    neojs::JSValue *sym =
        neojs::wellKnownSymbol(c, static_cast<neojs::WellKnown>(index));
    return makeHandle(c, sym);
  });
}

/* -------------------------------------------------------------------------- */
/* 对象模型（E1；架构 §5.2）                                                   */
/* -------------------------------------------------------------------------- */

/* 创建普通对象。prototype 为 NULL 时无原型（M4 内置注册后接默认 Object.prototype）。 */
extern "C" JSValue *JS_CreateObject(JSContext *ctx, JSValue *prototype,
                                    const char *name) {
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSObject *proto =
        (prototype != nullptr) ? unhandle(c, prototype)->object() : nullptr;
    neojs::JSValue *obj = c->createObject(proto);
    return makeValueHandleNamed(ctx, obj, name); /* obj 已是内部值，仅分配句柄 */
  });
}

/* 创建数组对象（elements 分离存储）。 */
extern "C" JSValue *JS_CreateArray(JSContext *ctx, const char *name) {
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *arr = c->createArray();
    return makeValueHandleNamed(ctx, arr, name); /* arr 已是内部值，仅分配句柄 */
  });
}

/* 属性访问（架构 §5.2 ordinary [[Get]]/[[Set]]，沿原型链）。 */
extern "C" JSValue *JS_GetProperty(JSContext *ctx, JSValue *obj, const char *key) {
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *v = unhandle(c, obj);
    if (v == nullptr || !v->isObject()) {
      return makeException(ctx, "TypeError: not an object");
    }
    neojs::JSValue *found = v->object()->getProperty(key ? key : "");
    return found != nullptr ? makeHandle(c, found)
                            : makeValue(ctx, neojs::JSValue()); /* undefined */
  });
}

extern "C" JSValue *JS_SetProperty(JSContext *ctx, JSValue *obj, const char *key,
                                   JSValue *value) {
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *v = unhandle(c, obj);
    neojs::JSValue *val = unhandle(c, value);
    if (v == nullptr || !v->isObject()) {
      return makeException(ctx, "TypeError: not an object");
    }
    if (val == nullptr) return makeException(ctx, "TypeError: value is null");
    v->object()->setProperty(key ? key : "", val);
    return value;
  });
}

extern "C" int JS_HasProperty(JSContext *ctx, JSValue *obj, const char *key) {
  if (ctx == nullptr || obj == nullptr) return 0;
  try {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *v = unhandle(c, obj);
    if (v == nullptr || !v->isObject()) return 0;
    return v->object()->hasProperty(key ? key : "") ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

extern "C" int JS_DeleteProperty(JSContext *ctx, JSValue *obj, const char *key) {
  if (ctx == nullptr || obj == nullptr) return 0;
  try {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *v = unhandle(c, obj);
    if (v == nullptr || !v->isObject()) return 0;
    return v->object()->deleteProperty(key ? key : "") ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

/* 原型链访问。 */
extern "C" JSValue *JS_GetPrototype(JSContext *ctx, JSValue *obj) {
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *v = unhandle(c, obj);
    if (v == nullptr || !v->isObject()) {
      return makeException(ctx, "TypeError: not an object");
    }
    neojs::JSObject *proto = v->object()->prototype();
    if (proto == nullptr) return makeValue(ctx, neojs::JSValue()); /* undefined */
    /* 经 holder 返回原型对象的值（JSObject -> JSValue）；holder 为空返回 undefined。 */
    neojs::JSValue *pv = proto->holder();
    return pv != nullptr ? makeHandle(c, pv)
                         : makeValue(ctx, neojs::JSValue());
  });
}

extern "C" int JS_SetPrototype(JSContext *ctx, JSValue *obj, JSValue *proto) {
  if (ctx == nullptr || obj == nullptr) return 0;
  try {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *v = unhandle(c, obj);
    if (v == nullptr || !v->isObject()) return 0;
    neojs::JSValue *p = unhandle(c, proto);
    v->object()->setPrototype(p != nullptr ? p->object() : nullptr);
    return 1;
  } catch (...) {
    return 0;
  }
}

/* 数组元素访问。 */
extern "C" JSValue *JS_GetElement(JSContext *ctx, JSValue *arr, size_t index) {
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *v = unhandle(c, arr);
    if (v == nullptr || !v->isObject() || !v->object()->isArray()) {
      return makeException(ctx, "TypeError: not an array");
    }
    neojs::JSValue *elem = v->object()->getElement(index);
    return elem != nullptr ? makeHandle(c, elem)
                           : makeValue(ctx, neojs::JSValue()); /* undefined */
  });
}

extern "C" JSValue *JS_SetElement(JSContext *ctx, JSValue *arr, size_t index,
                                  JSValue *value) {
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *v = unhandle(c, arr);
    neojs::JSValue *val = unhandle(c, value);
    if (v == nullptr || !v->isObject() || !v->object()->isArray()) {
      return makeException(ctx, "TypeError: not an array");
    }
    if (val == nullptr) return makeException(ctx, "TypeError: value is null");
    v->object()->setElement(index, val);
    return value;
  });
}

extern "C" size_t JS_GetLength(JSContext *ctx, JSValue *arr) {
  if (ctx == nullptr || arr == nullptr) return 0;
  try {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *v = unhandle(c, arr);
    if (v == nullptr || !v->isObject() || !v->object()->isArray()) return 0;
    return v->object()->length();
  } catch (...) {
    return 0;
  }
}

extern "C" int JS_SetLength(JSContext *ctx, JSValue *arr, size_t length) {
  if (ctx == nullptr || arr == nullptr) return 0;
  try {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::JSValue *v = unhandle(c, arr);
    if (v == nullptr || !v->isObject() || !v->object()->isArray()) return 0;
    neojs::JSObject *obj = v->object();
    obj->resizeElements(length); /* 直接设置元素存储大小（缩短截断/加长空洞） */
    return 1;
  } catch (...) {
    return 0;
  }
}

/* -------------------------------------------------------------------------- */
/* GC 引擎（docs/gc.md §5）                                                   */
/* -------------------------------------------------------------------------- */

extern "C" void JS_GC(JSContext *ctx) {
  if (ctx == nullptr) return;
  try {
    reinterpret_cast<neojs::JSContext *>(ctx)->gc();
  } catch (...) {
    /* #3：跨 C 边界不抛异常 */
  }
}

extern "C" void JS_SetGCThreshold(JSContext *ctx, size_t threshold) {
  if (ctx == nullptr) return;
  try {
    reinterpret_cast<neojs::JSContext *>(ctx)->heap().setGcThreshold(threshold);
  } catch (...) {
    /* #3 */
  }
}

/* -------------------------------------------------------------------------- */
/* 执行（M1.1：前端流水线骨架；完整求值在 M2）                                 */
/* -------------------------------------------------------------------------- */

extern "C" JSValue *JS_Eval(JSContext *ctx, const char *source, size_t source_len) {
  /* #9：ctx 为空属编程错误 */
  if (ctx == nullptr) {
    std::fputs("neo.js: null JSContext passed to JS_Eval\n", stderr);
    std::abort();
  }
  return guarded_create(ctx, [&]() -> JSValue * {
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::DiagnosticSink sink;

    /* M1.1：源码 → Lexer+Parser → AST dump（求值器 M2 实现）。
     * 解析成功返回 dump 文本（String 值）；语法错误返回 Exception（#4）。 */
    neojs::AST *ast = neojs::parseSource(&c->allocator(), source ? source : "",
                                         source ? source_len : 0, sink);
    if (ast == nullptr) {
      std::string msg = "SyntaxError: ";
      msg += sink.hasErrors() ? sink.diagnostics().front().message : "parse failed";
      return makeException(ctx, msg.c_str());
    }
    std::string dump = neojs::dumpAst(*ast);
    c->allocator().destroy(ast); /* AST 生命周期独立于 ctx 值登记表 */
    return makeValue(ctx, std::move(dump));
  });
}
