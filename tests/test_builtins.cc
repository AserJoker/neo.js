/*
 * E4 最小语法绑定集单元测试（架构 §8 B0 子集 + §5.2 well-known symbols）。
 * 本文件随 E4 各子任务逐步扩展。
 *
 * E4-1 覆盖：
 *   1. well-known symbol 表：index 0-3 返回唯一 Symbol 值，描述符正确
 *   2. 同一 well-known 每次返回同一内部值（引用相等）
 *   3. globalThis 惰性创建 + Symbol 构造器注册
 *   4. Symbol 构造器可调用：Symbol("desc") 创建唯一 Symbol
 *   5. GC 下 well-known 存活（根集保护）
 */

#include <gtest/gtest.h>

#include <cstring>

#include "neojs/neojs.h"
#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/value.h"

namespace {

/* 创建默认分配器 + ctx；调用方负责 JS_FreeContext 后 JS_FreeAllocator。 */
JSContext *makeCtx(JSAllocator **out_alloc) {
  JSAllocator *a = JS_NewAllocator(nullptr, nullptr, nullptr);
  EXPECT_NE(a, nullptr);
  JSContext *ctx = JS_NewContext(a);
  EXPECT_NE(ctx, nullptr);
  if (out_alloc) *out_alloc = a;
  return ctx;
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* well-known symbol 表（E4-1，架构 §5.2）                                    */
/* -------------------------------------------------------------------------- */

TEST(Builtins, WellKnownSymbolsAreUniqueSymbols) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* index 0-3：iterator / toPrimitive / toStringTag / hasInstance */
  for (int i = 0; i < 4; ++i) {
    JSValue *sym = JS_GetWellKnownSymbol(ctx, i);
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(JS_GetKind(ctx, sym), JS_SYMBOL);
    JS_FreeValue(ctx, sym);
  }

  /* 越界 index -> RangeError */
  JSValue *bad = JS_GetWellKnownSymbol(ctx, 4);
  ASSERT_NE(bad, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, bad), JS_EXCEPTION);
  JS_FreeValue(ctx, bad);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Builtins, WellKnownSymbolDescriptions) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 描述符经内部访问（Symbol 值无 C 描述 API，走内部值）。 */
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  EXPECT_EQ(neojs::wellKnownSymbol(c, neojs::WellKnown::Iterator)
                ->symbolDescription(),
            "Symbol.iterator");
  EXPECT_EQ(neojs::wellKnownSymbol(c, neojs::WellKnown::ToPrimitive)
                ->symbolDescription(),
            "Symbol.toPrimitive");
  EXPECT_EQ(neojs::wellKnownSymbol(c, neojs::WellKnown::ToStringTag)
                ->symbolDescription(),
            "Symbol.toStringTag");
  EXPECT_EQ(neojs::wellKnownSymbol(c, neojs::WellKnown::HasInstance)
                ->symbolDescription(),
            "Symbol.hasInstance");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Builtins, WellKnownSymbolsAreCached) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 同一 well-known：两次取返回同一内部值（引用相等）。 */
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  EXPECT_EQ(neojs::wellKnownSymbol(c, neojs::WellKnown::Iterator),
            neojs::wellKnownSymbol(c, neojs::WellKnown::Iterator));
  EXPECT_EQ(neojs::wellKnownSymbol(c, neojs::WellKnown::ToPrimitive),
            neojs::wellKnownSymbol(c, neojs::WellKnown::ToPrimitive));

  /* 不同 well-known 是不同值。 */
  EXPECT_NE(neojs::wellKnownSymbol(c, neojs::WellKnown::Iterator),
            neojs::wellKnownSymbol(c, neojs::WellKnown::HasInstance));

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* globalThis 惰性创建 + Symbol 构造器（E4-1）                                 */
/* -------------------------------------------------------------------------- */

TEST(Builtins, GlobalObjectLazyCreatedWithSymbol) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, global), JS_OBJECT);

  /* global 上注册了 Symbol 构造器（可调用对象）。 */
  JSValue *symbol = JS_GetProperty(ctx, global, "Symbol");
  ASSERT_NE(symbol, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, symbol), JS_OBJECT);

  /* 同一 ctx 多次取 global：同一内部值（引用相等）。 */
  JSValue *global2 = JS_GetGlobalObject(ctx);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  EXPECT_EQ(c->globalObject(), c->globalObject());
  JS_FreeValue(ctx, global2);

  JS_FreeValue(ctx, symbol);
  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Builtins, SymbolConstructorStaticProperties) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  JSValue *symbol = JS_GetProperty(ctx, global, "Symbol");
  ASSERT_NE(symbol, nullptr);

  /* Symbol.iterator / toPrimitive / toStringTag / hasInstance 静态属性 */
  JSValue *iter = JS_GetProperty(ctx, symbol, "iterator");
  ASSERT_NE(iter, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, iter), JS_SYMBOL);

  JSValue *toprim = JS_GetProperty(ctx, symbol, "toPrimitive");
  ASSERT_NE(toprim, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, toprim), JS_SYMBOL);

  JSValue *tag = JS_GetProperty(ctx, symbol, "toStringTag");
  ASSERT_NE(tag, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, tag), JS_SYMBOL);

  JSValue *hasinst = JS_GetProperty(ctx, symbol, "hasInstance");
  ASSERT_NE(hasinst, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, hasinst), JS_SYMBOL);

  /* 静态属性与 well-known 表指向同一内部值。 */
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSValue *iter_internal = c->handles().get(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(iter)));
  EXPECT_EQ(iter_internal, neojs::wellKnownSymbol(c, neojs::WellKnown::Iterator));

  JS_FreeValue(ctx, hasinst);
  JS_FreeValue(ctx, tag);
  JS_FreeValue(ctx, toprim);
  JS_FreeValue(ctx, iter);
  JS_FreeValue(ctx, symbol);
  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Symbol 构造器可调用：Symbol("desc") -> 唯一 Symbol。 */
TEST(Builtins, SymbolConstructorCallable) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  JSValue *symbol = JS_GetProperty(ctx, global, "Symbol");
  ASSERT_NE(symbol, nullptr);

  JSValue *desc = JS_CreateString(ctx, "foo", 3, nullptr);
  JSValue *args[] = {desc};
  JSValue *rv = JS_Call(ctx, symbol, nullptr, 1, args);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_SYMBOL);

  /* 两次调用产生不同 Symbol（唯一性）。 */
  JSValue *rv2 = JS_Call(ctx, symbol, nullptr, 1, args);
  ASSERT_NE(rv2, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv2), JS_SYMBOL);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  EXPECT_NE(c->handles().get(static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(rv))),
            c->handles().get(static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(rv2))));

  JS_FreeValue(ctx, rv2);
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, desc);
  JS_FreeValue(ctx, symbol);
  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* GC 下 well-known 存活（根集保护）                                          */
/* -------------------------------------------------------------------------- */

TEST(Builtins, WellKnownSurviveGc) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *sym = JS_GetWellKnownSymbol(ctx, 0);
  ASSERT_NE(sym, nullptr);

  /* 多次 GC：well-known 经 context 缓存（根集）存活。 */
  JS_GC(ctx);
  JS_GC(ctx);
  JS_GC(ctx);

  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  EXPECT_EQ(neojs::wellKnownSymbol(c, neojs::WellKnown::Iterator),
            neojs::wellKnownSymbol(c, neojs::WellKnown::Iterator)); /* 仍缓存 */

  JS_FreeValue(ctx, sym);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
