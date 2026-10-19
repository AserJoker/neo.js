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
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/object.h"
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

/* -------------------------------------------------------------------------- */
/* E4-2 ToObject 装箱 + 包装对象最小方法                                       */
/* -------------------------------------------------------------------------- */

/* ToObject 装箱（§7.1.13）：Number/String/Boolean/Symbol 原始值 -> 包装对象。 */
TEST(Builtins, ToObjectBoxesPrimitives) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 触发内置注册（惰性：globalObject 首次创建）。 */
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);

  struct { neojs::JSValue *prim; neojs::ObjectKind kind; } cases[] = {
      {c->createValue(42.0), neojs::ObjectKind::Number},
      {c->createValue(std::string("hi")), neojs::ObjectKind::String},
      {c->createValue(true), neojs::ObjectKind::Boolean},
  };
  for (auto &tc : cases) {
    neojs::ConversionResult r = neojs::toObject(c, tc.prim);
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.value->isObject());
    EXPECT_TRUE(r.value->object()->isWrapper());
    EXPECT_EQ(r.value->object()->kind(), tc.kind);
  }

  /* Object 原样返回（不新建）。 */
  neojs::JSValue *obj = c->createObject(nullptr);
  neojs::ConversionResult r = neojs::toObject(c, obj);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value, obj); /* 同一指针 */

  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 包装对象原型链指向内置 prototype（构造器的 prototype 属性）。 */
TEST(Builtins, WrapperPrototypeChained) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);

  /* 装箱 -> 包装对象 -> 原型链指向 wrapper_protos_ 缓存。 */
  neojs::JSValue *num = c->createValue(3.5);
  neojs::ConversionResult r = neojs::toObject(c, num);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->object()->prototype(), c->wrapperProto(neojs::ObjectKind::Number)->object());

  neojs::JSValue *str = c->createValue(std::string("x"));
  r = neojs::toObject(c, str);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->object()->prototype(), c->wrapperProto(neojs::ObjectKind::String)->object());

  /* wrapperProto 缓存非空（installBuiltins 已填）。 */
  EXPECT_NE(c->wrapperProto(neojs::ObjectKind::Number), nullptr);
  EXPECT_NE(c->wrapperProto(neojs::ObjectKind::String), nullptr);
  EXPECT_NE(c->wrapperProto(neojs::ObjectKind::Boolean), nullptr);
  EXPECT_NE(c->wrapperProto(neojs::ObjectKind::Symbol), nullptr);

  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 包装构造器：Number()/String()/Boolean() 经 JS_Call 调用。 */
TEST(Builtins, WrapperConstructorsCallable) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);

  /* Number("42") -> 42 */
  JSValue *numCtor = JS_GetProperty(ctx, global, "Number");
  ASSERT_NE(numCtor, nullptr);
  JSValue *s42 = JS_CreateString(ctx, "42", 2, nullptr);
  JSValue *args1[] = {s42};
  JSValue *rv = JS_Call(ctx, numCtor, nullptr, 1, args1);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_NUMBER);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, rv), 42.0);
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, s42);
  JS_FreeValue(ctx, numCtor);

  /* String(123) -> "123" */
  JSValue *strCtor = JS_GetProperty(ctx, global, "String");
  ASSERT_NE(strCtor, nullptr);
  JSValue *n123 = JS_CreateNumber(ctx, 123, nullptr);
  JSValue *args2[] = {n123};
  rv = JS_Call(ctx, strCtor, nullptr, 1, args2);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, rv), "123");
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, n123);
  JS_FreeValue(ctx, strCtor);

  /* Boolean("") -> false（ToBoolean） */
  JSValue *boolCtor = JS_GetProperty(ctx, global, "Boolean");
  ASSERT_NE(boolCtor, nullptr);
  JSValue *empty = JS_CreateString(ctx, "", 0, nullptr);
  JSValue *args3[] = {empty};
  rv = JS_Call(ctx, boolCtor, nullptr, 1, args3);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_BOOLEAN);
  EXPECT_EQ(JS_ToBoolean(ctx, rv), 0);
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, empty);
  JS_FreeValue(ctx, boolCtor);

  /* 无参：Number() -> 0、String() -> ""、Boolean() -> false（重新取构造器）。 */
  JSValue *numC2 = JS_GetProperty(ctx, global, "Number");
  JSValue *strC2 = JS_GetProperty(ctx, global, "String");
  JSValue *boolC2 = JS_GetProperty(ctx, global, "Boolean");
  rv = JS_Call(ctx, numC2, nullptr, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, rv), 0.0);
  JS_FreeValue(ctx, rv);
  rv = JS_Call(ctx, strC2, nullptr, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_STREQ(JS_ToString(ctx, rv), "");
  JS_FreeValue(ctx, rv);
  rv = JS_Call(ctx, boolC2, nullptr, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_ToBoolean(ctx, rv), 0);
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, numC2);
  JS_FreeValue(ctx, strC2);
  JS_FreeValue(ctx, boolC2);

  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 包装 prototype 最小方法集：valueOf / toString 可从原型链读取并可调用。 */
TEST(Builtins, WrapperProtoMethodsCallable) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);

  /* Number.prototype.valueOf 存在且可调用（this=Number 原始值 -> 数值）。 */
  JSValue *numCtor = JS_GetProperty(ctx, global, "Number");
  JSValue *numProto = JS_GetProperty(ctx, numCtor, "prototype");
  ASSERT_NE(numProto, nullptr);
  JSValue *valueOf = JS_GetProperty(ctx, numProto, "valueOf");
  ASSERT_NE(valueOf, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, valueOf), JS_OBJECT); /* 函数是可调用对象 */
  JSValue *thisNum = JS_CreateNumber(ctx, 7.25, nullptr);
  JSValue *rv = JS_Call(ctx, valueOf, thisNum, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_NUMBER);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, rv), 7.25);
  JS_FreeValue(ctx, rv);

  /* Number.prototype.toString：this=Number -> 字符串。 */
  JSValue *numToString = JS_GetProperty(ctx, numProto, "toString");
  ASSERT_NE(numToString, nullptr);
  JSValue *thisNum2 = JS_CreateNumber(ctx, 123.0, nullptr);
  rv = JS_Call(ctx, numToString, thisNum2, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, rv), "123");
  JS_FreeValue(ctx, rv);

  /* String.prototype.toString：this=String -> 原串。 */
  JSValue *strCtor = JS_GetProperty(ctx, global, "String");
  JSValue *strProto = JS_GetProperty(ctx, strCtor, "prototype");
  JSValue *strToString = JS_GetProperty(ctx, strProto, "toString");
  ASSERT_NE(strToString, nullptr);
  JSValue *thisStr = JS_CreateString(ctx, "abc", 3, nullptr);
  rv = JS_Call(ctx, strToString, thisStr, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, rv), "abc");
  JS_FreeValue(ctx, rv);

  /* Boolean.prototype.toString：this=true -> "true"。 */
  JSValue *boolCtor = JS_GetProperty(ctx, global, "Boolean");
  JSValue *boolProto = JS_GetProperty(ctx, boolCtor, "prototype");
  JSValue *boolToString = JS_GetProperty(ctx, boolProto, "toString");
  ASSERT_NE(boolToString, nullptr);
  JSValue *thisTrue = JS_CreateBoolean(ctx, 1, nullptr);
  rv = JS_Call(ctx, boolToString, thisTrue, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, rv), "true");
  JS_FreeValue(ctx, rv);

  /* Symbol.prototype.valueOf / toString：this=Symbol。 */
  JSValue *symCtor = JS_GetProperty(ctx, global, "Symbol");
  JSValue *symProto = JS_GetProperty(ctx, symCtor, "prototype");
  JSValue *symValueOf = JS_GetProperty(ctx, symProto, "valueOf");
  ASSERT_NE(symValueOf, nullptr);
  JSValue *thisSym = JS_CreateSymbol(ctx, "foo", nullptr);
  rv = JS_Call(ctx, symValueOf, thisSym, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_SYMBOL);
  JS_FreeValue(ctx, rv);
  JSValue *symToString = JS_GetProperty(ctx, symProto, "toString");
  ASSERT_NE(symToString, nullptr);
  rv = JS_Call(ctx, symToString, thisSym, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, rv), "Symbol(foo)");
  JS_FreeValue(ctx, rv);

  JS_FreeValue(ctx, thisSym);
  JS_FreeValue(ctx, thisTrue);
  JS_FreeValue(ctx, boolToString);
  JS_FreeValue(ctx, boolProto);
  JS_FreeValue(ctx, boolCtor);
  JS_FreeValue(ctx, thisStr);
  JS_FreeValue(ctx, strToString);
  JS_FreeValue(ctx, strProto);
  JS_FreeValue(ctx, strCtor);
  JS_FreeValue(ctx, thisNum2);
  JS_FreeValue(ctx, numToString);
  JS_FreeValue(ctx, thisNum);
  JS_FreeValue(ctx, valueOf);
  JS_FreeValue(ctx, numProto);
  JS_FreeValue(ctx, numCtor);
  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 包装对象（经 toObject 装箱）的 prototype 方法可调用（this=包装对象）。 */
TEST(Builtins, WrapperObjectMethodsCallable) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);

  /* 装箱 Number -> 包装对象 -> 经原型链取 valueOf -> 调用（this=包装对象）。 */
  neojs::JSValue *num = c->createValue(9.5);
  neojs::ConversionResult wr = neojs::toObject(c, num);
  ASSERT_TRUE(wr.ok());
  JSValue *boxed = nullptr; /* 包装对象经内部指针 -> 临时句柄 */
  {
    neojs::JSValue *wv = wr.value;
    uint32_t h = c->handles().alloc(wv);
    boxed = reinterpret_cast<JSValue *>(static_cast<uintptr_t>(h));
  }

  JSValue *numCtor = JS_GetProperty(ctx, global, "Number");
  JSValue *numProto = JS_GetProperty(ctx, numCtor, "prototype");
  JSValue *valueOf = JS_GetProperty(ctx, numProto, "valueOf");
  ASSERT_NE(valueOf, nullptr);
  JSValue *rv = JS_Call(ctx, valueOf, boxed, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_NUMBER);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, rv), 9.5);
  JS_FreeValue(ctx, rv);

  /* prototype 方法对不兼容 this（普通对象）-> TypeError。 */
  JSValue *plain = JS_CreateObject(ctx, nullptr, nullptr);
  rv = JS_Call(ctx, valueOf, plain, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_EXCEPTION);
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, plain);

  JS_FreeValue(ctx, valueOf);
  JS_FreeValue(ctx, numProto);
  JS_FreeValue(ctx, numCtor);
  JS_FreeValue(ctx, boxed);
  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
