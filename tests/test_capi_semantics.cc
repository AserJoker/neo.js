/*
 * E5 纯 C API 语义验证套件（不经过 Compiler/VM）。
 *
 * 用纯 C API（neojs.h）模拟 JS 语义流程，验证 E1-E5 累积的对象模型 /
 * 类型转换 / 函数 / 装箱 / 运算符语义可作为后续 Compiler/bytecode/VM
 * 打通的可靠地基：
 *   1. globalThis 自引用（§19.1.1）+ 全局值属性 Infinity/NaN/undefined（§19.1.2-4）
 *   2. 对象属性读写 + 原型链查找（普通 [[Get]]/[[Set]]，架构 §5.2）
 *   3. 数组：元素访问 / 越界 undefined / length / 空洞
 *   4. 函数调用：宿主 C 回调、参数传递、返回值
 *   5. 原型链：自定义原型对象 + 继承属性查找 + instanceof
 *   6. 类型转换 + 运算符：== / === / < / + / typeof / in
 *   7. GC：对象/数组/原型链在回收后存活（根集保护）
 *
 * 注意：宿主回调收到的 self/argv[i] 是宿主句柄（G2），经 C API 解析。
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

#include "neojs/neojs.h"

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
/* 宿主 C 回调（JSCFunction 签名，#6）                                        */
/* -------------------------------------------------------------------------- */

/* 乘法回调：返回 argc 个参数之积（无参数 -> 1）。 */
JSValue *mulFn(JSContext *ctx, JSValue *self, size_t argc, JSValue *argv) {
  (void)self;
  JSValue *const *args = reinterpret_cast<JSValue *const *>(argv);
  double product = 1.0;
  for (size_t i = 0; i < argc; ++i) {
    product *= JS_ToNumber(ctx, args[i]);
  }
  return JS_CreateNumber(ctx, product, nullptr);
}

/* 读取对象属性的回调：obj 经 this 传入，返回 this["key"]。 */
JSValue *readKeyFn(JSContext *ctx, JSValue *self, size_t, JSValue *) {
  if (self == nullptr) return JS_CreateUndefined(ctx, nullptr);
  return JS_GetProperty(ctx, self, "key");
}

/* -------------------------------------------------------------------------- */
/* 1. globalThis 自引用 + 全局值属性（§19.1.1-4）                              */
/* -------------------------------------------------------------------------- */

TEST(CapiSemantics, GlobalThisSelfReference) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, global), JS_OBJECT);

  /* globalThis 属性值指向全局对象自身（同一内部值 -> 引用相等）。 */
  JSValue *gt = JS_GetProperty(ctx, global, "globalThis");
  ASSERT_NE(gt, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, gt), JS_OBJECT);
  EXPECT_EQ(JS_StrictEqual(ctx, gt, global), 1);

  /* 全局对象属性可写（§19.1.1 Writable:true）：globalThis.x = 42 生效。 */
  JSValue *n42 = JS_CreateNumber(ctx, 42, nullptr);
  JS_SetProperty(ctx, gt, "x", n42);
  JSValue *back = JS_GetProperty(ctx, global, "x");
  ASSERT_NE(back, nullptr);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, back), 42.0);

  JS_FreeValue(ctx, back);
  JS_FreeValue(ctx, n42);
  JS_FreeValue(ctx, gt);
  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(CapiSemantics, GlobalValueProperties) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);

  /* Infinity / NaN / undefined 全局值属性（§19.1.2-4）。 */
  JSValue *inf = JS_GetProperty(ctx, global, "Infinity");
  ASSERT_NE(inf, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, inf), JS_NUMBER);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, inf), INFINITY);

  JSValue *nan = JS_GetProperty(ctx, global, "NaN");
  ASSERT_NE(nan, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, nan), JS_NUMBER);
  EXPECT_TRUE(std::isnan(JS_ToNumber(ctx, nan)));

  JSValue *undef = JS_GetProperty(ctx, global, "undefined");
  ASSERT_NE(undef, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, undef), JS_UNDEFINED);

  /* NaN !== NaN（经全局属性取到的值）。 */
  EXPECT_EQ(JS_StrictEqual(ctx, nan, nan), 0);

  JS_FreeValue(ctx, undef);
  JS_FreeValue(ctx, nan);
  JS_FreeValue(ctx, inf);
  JS_FreeValue(ctx, global);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 2. 对象属性读写 + 原型链查找（架构 §5.2 ordinary [[Get]]/[[Set]]）         */
/* -------------------------------------------------------------------------- */

TEST(CapiSemantics, ObjectPropertyReadWrite) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *obj = JS_CreateObject(ctx, nullptr, nullptr);
  ASSERT_NE(obj, nullptr);

  /* 初始：未定义属性 -> undefined（#14 非 NULL）。 */
  JSValue *missing = JS_GetProperty(ctx, obj, "foo");
  ASSERT_NE(missing, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, missing), JS_UNDEFINED);
  JS_FreeValue(ctx, missing);

  /* 写入 + 读取（JS_SetProperty 返回 value 自身，不额外 FreeValue）。 */
  JSValue *v = JS_CreateString(ctx, "hello", 5, nullptr);
  JS_SetProperty(ctx, obj, "foo", v);

  JSValue *got = JS_GetProperty(ctx, obj, "foo");
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, got), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, got), "hello");
  EXPECT_EQ(JS_HasProperty(ctx, obj, "foo"), 1);
  JS_FreeValue(ctx, got);

  /* 覆盖旧值。 */
  JSValue *v2 = JS_CreateNumber(ctx, 7, nullptr);
  JS_SetProperty(ctx, obj, "foo", v2);
  got = JS_GetProperty(ctx, obj, "foo");
  ASSERT_NE(got, nullptr);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, got), 7.0);
  JS_FreeValue(ctx, got);

  /* 删除。 */
  EXPECT_EQ(JS_DeleteProperty(ctx, obj, "foo"), 1);
  EXPECT_EQ(JS_HasProperty(ctx, obj, "foo"), 0);

  JS_FreeValue(ctx, v2);
  JS_FreeValue(ctx, v);
  JS_FreeValue(ctx, obj);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 3. 数组：元素 / 越界 undefined / length / 空洞                              */
/* -------------------------------------------------------------------------- */

TEST(CapiSemantics, ArraySemantics) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *arr = JS_CreateArray(ctx, nullptr);
  ASSERT_NE(arr, nullptr);

  /* 初始 length 0；越界元素 -> undefined。 */
  EXPECT_EQ(JS_GetLength(ctx, arr), 0u);
  JSValue *hole = JS_GetElement(ctx, arr, 0);
  ASSERT_NE(hole, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, hole), JS_UNDEFINED);
  JS_FreeValue(ctx, hole);

  /* 写元素自动扩容 + length 跟随。 */
  JSValue *e0 = JS_CreateNumber(ctx, 10, nullptr);
  JSValue *e1 = JS_CreateString(ctx, "x", 1, nullptr);
  JS_SetElement(ctx, arr, 0, e0);
  JS_SetElement(ctx, arr, 1, e1);
  EXPECT_EQ(JS_GetLength(ctx, arr), 2u);

  /* 读取。 */
  JSValue *g0 = JS_GetElement(ctx, arr, 0);
  ASSERT_NE(g0, nullptr);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, g0), 10.0);
  JS_FreeValue(ctx, g0);
  JSValue *g1 = JS_GetElement(ctx, arr, 1);
  ASSERT_NE(g1, nullptr);
  EXPECT_STREQ(JS_ToString(ctx, g1), "x");
  JS_FreeValue(ctx, g1);

  /* 设置 length 缩短截断。 */
  EXPECT_EQ(JS_SetLength(ctx, arr, 1), 1);
  EXPECT_EQ(JS_GetLength(ctx, arr), 1u);
  JSValue *g1b = JS_GetElement(ctx, arr, 1);
  ASSERT_NE(g1b, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, g1b), JS_UNDEFINED); /* 截断后成空洞 */
  JS_FreeValue(ctx, g1b);

  JS_FreeValue(ctx, e1);
  JS_FreeValue(ctx, e0);
  JS_FreeValue(ctx, arr);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 4. 函数调用：宿主回调 / 参数传递 / 返回值                                    */
/* -------------------------------------------------------------------------- */

TEST(CapiSemantics, FunctionCallFlow) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 具名函数绑定到作用域（无孤立值原则）。 */
  JSValue *fn = JS_NewCFunction(ctx, mulFn, "mul");
  ASSERT_NE(fn, nullptr);
  JS_FreeValue(ctx, fn); /* 释放句柄，值由作用域绑定持有 */

  JSValue *loaded = JS_LoadValue(ctx, "mul");
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, loaded), JS_OBJECT);

  /* mul(3, 4) -> 12。 */
  JSValue *x = JS_CreateNumber(ctx, 3, nullptr);
  JSValue *y = JS_CreateNumber(ctx, 4, nullptr);
  JSValue *args[] = {x, y};
  JSValue *rv = JS_Call(ctx, loaded, nullptr, 2, args);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_NUMBER);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, rv), 12.0);
  JS_FreeValue(ctx, rv);

  /* 无参调用 -> 1。 */
  rv = JS_Call(ctx, loaded, nullptr, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, rv), 1.0);
  JS_FreeValue(ctx, rv);

  /* 非函数调用 -> Exception（#4）。 */
  rv = JS_Call(ctx, x, nullptr, 0, nullptr);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_EXCEPTION);
  JS_FreeValue(ctx, rv);

  JS_FreeValue(ctx, x);
  JS_FreeValue(ctx, y);
  JS_FreeValue(ctx, loaded);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 5. 原型链：自定义原型 + 继承属性查找 + instanceof                           */
/* -------------------------------------------------------------------------- */

TEST(CapiSemantics, PrototypeChainAndInstanceof) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 原型对象：proto.base = "P"；构造器函数 ctor.prototype = proto。 */
  JSValue *proto = JS_CreateObject(ctx, nullptr, nullptr);
  ASSERT_NE(proto, nullptr);
  JSValue *base = JS_CreateString(ctx, "P", 1, nullptr);
  JS_SetProperty(ctx, proto, "base", base);

  JSValue *ctor = JS_NewCFunction(ctx, readKeyFn, nullptr);
  ASSERT_NE(ctor, nullptr);
  JS_SetProperty(ctx, ctor, "prototype", proto);

  /* 实例：instance -> proto（继承 base）。 */
  JSValue *instance = JS_CreateObject(ctx, nullptr, nullptr);
  ASSERT_NE(instance, nullptr);
  EXPECT_EQ(JS_SetPrototype(ctx, instance, proto), 1);

  /* 继承属性查找：instance.base -> "P"（经原型链，架构 §5.2 [[Get]]）。 */
  JSValue *got = JS_GetProperty(ctx, instance, "base");
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, got), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, got), "P");
  JS_FreeValue(ctx, got);

  /* hasProperty 沿原型链：instance 无 own base，但原型链上有。 */
  EXPECT_EQ(JS_HasProperty(ctx, instance, "base"), 1);

  /* instanceof：instance -> proto -> (proto.prototype=null)。 */
  JSValue *rv = JS_Instanceof(ctx, instance, ctor);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, rv), JS_BOOLEAN);
  EXPECT_EQ(JS_ToBoolean(ctx, rv), 1);
  JS_FreeValue(ctx, rv);

  /* 无关对象 -> false。 */
  JSValue *other = JS_CreateObject(ctx, nullptr, nullptr);
  ASSERT_NE(other, nullptr);
  rv = JS_Instanceof(ctx, other, ctor);
  ASSERT_NE(rv, nullptr);
  EXPECT_EQ(JS_ToBoolean(ctx, rv), 0);
  JS_FreeValue(ctx, rv);
  JS_FreeValue(ctx, other);

  JS_FreeValue(ctx, instance);
  JS_FreeValue(ctx, ctor);
  JS_FreeValue(ctx, proto);
  JS_FreeValue(ctx, base);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 6. 类型转换 + 运算符（E4-3 / E4-4）                                          */
/* -------------------------------------------------------------------------- */

TEST(CapiSemantics, OperatorsFlow) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *n1 = JS_CreateNumber(ctx, 1, nullptr);
  JSValue *n2 = JS_CreateNumber(ctx, 2, nullptr);
  JSValue *s1 = JS_CreateString(ctx, "1", 1, nullptr);
  ASSERT_NE(n1, nullptr);
  ASSERT_NE(n2, nullptr);
  ASSERT_NE(s1, nullptr);

  /* 1 === 1 -> 1；1 === "1" -> 0；1 == "1" -> true。 */
  EXPECT_EQ(JS_StrictEqual(ctx, n1, n1), 1);
  EXPECT_EQ(JS_StrictEqual(ctx, n1, s1), 0);
  JSValue *eq = JS_Equal(ctx, n1, s1);
  ASSERT_NE(eq, nullptr);
  EXPECT_EQ(JS_ToBoolean(ctx, eq), 1);
  JS_FreeValue(ctx, eq);

  /* 1 < 2 -> true。 */
  JSValue *lt = JS_RelationalCompare(ctx, n1, n2);
  ASSERT_NE(lt, nullptr);
  EXPECT_EQ(JS_ToBoolean(ctx, lt), 1);
  JS_FreeValue(ctx, lt);

  /* 1 + "px" -> "1px"（字符串拼接）。 */
  JSValue *px = JS_CreateString(ctx, "px", 2, nullptr);
  JSValue *add = JS_Add(ctx, n1, px);
  ASSERT_NE(add, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, add), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, add), "1px");
  JS_FreeValue(ctx, add);

  /* typeof 1 -> "number"；typeof obj -> "object"；typeof fn -> "function"。 */
  JSValue *tv = JS_Typeof(ctx, n1);
  ASSERT_NE(tv, nullptr);
  EXPECT_STREQ(JS_ToString(ctx, tv), "number");
  JS_FreeValue(ctx, tv);
  JSValue *obj = JS_CreateObject(ctx, nullptr, nullptr);
  tv = JS_Typeof(ctx, obj);
  ASSERT_NE(tv, nullptr);
  EXPECT_STREQ(JS_ToString(ctx, tv), "object");
  JS_FreeValue(ctx, tv);
  JSValue *fn = JS_NewCFunction(ctx, mulFn, nullptr);
  tv = JS_Typeof(ctx, fn);
  ASSERT_NE(tv, nullptr);
  EXPECT_STREQ(JS_ToString(ctx, tv), "function");
  JS_FreeValue(ctx, tv);

  /* in：obj.foo 存在 -> true；不存在 -> false；RHS 非对象 -> Exception。 */
  JSValue *kfoo = JS_CreateString(ctx, "foo", 3, nullptr);
  JSValue *kv = JS_CreateNumber(ctx, 9, nullptr);
  JS_SetProperty(ctx, obj, "foo", kv);
  JSValue *in = JS_In(ctx, kfoo, obj);
  ASSERT_NE(in, nullptr);
  EXPECT_EQ(JS_ToBoolean(ctx, in), 1);
  JS_FreeValue(ctx, in);
  JSValue *kbar = JS_CreateString(ctx, "bar", 3, nullptr);
  in = JS_In(ctx, kbar, obj);
  ASSERT_NE(in, nullptr);
  EXPECT_EQ(JS_ToBoolean(ctx, in), 0);
  JS_FreeValue(ctx, in);
  in = JS_In(ctx, kfoo, n1);
  ASSERT_NE(in, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, in), JS_EXCEPTION);
  JS_FreeValue(ctx, in);

  JS_FreeValue(ctx, kbar);
  JS_FreeValue(ctx, kfoo);
  JS_FreeValue(ctx, kv);
  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, obj);
  JS_FreeValue(ctx, add);
  JS_FreeValue(ctx, px);
  JS_FreeValue(ctx, lt);
  JS_FreeValue(ctx, s1);
  JS_FreeValue(ctx, n2);
  JS_FreeValue(ctx, n1);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 7. GC：对象/数组/原型链在回收后存活（根集保护）                             */
/* -------------------------------------------------------------------------- */

TEST(CapiSemantics, ObjectsSurviveGc) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 构造原型链：obj -> proto。 */
  JSValue *proto = JS_CreateObject(ctx, nullptr, nullptr);
  ASSERT_NE(proto, nullptr);
  JSValue *val = JS_CreateString(ctx, "kept", 4, nullptr);
  JS_SetProperty(ctx, proto, "name", val);
  JSValue *obj = JS_CreateObject(ctx, nullptr, nullptr);
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(JS_SetPrototype(ctx, obj, proto), 1);

  /* 数组 + 元素。 */
  JSValue *arr = JS_CreateArray(ctx, nullptr);
  ASSERT_NE(arr, nullptr);
  JSValue *elem = JS_CreateNumber(ctx, 5, nullptr);
  JS_SetElement(ctx, arr, 0, elem);

  /* 多次 GC：宿主持有的句柄是根集（G2），对象/数组/原型链必须存活。 */
  JS_GC(ctx);
  JS_GC(ctx);
  JS_GC(ctx);

  /* 原型链仍可查找。 */
  JSValue *got = JS_GetProperty(ctx, obj, "name");
  ASSERT_NE(got, nullptr);
  EXPECT_STREQ(JS_ToString(ctx, got), "kept");
  JS_FreeValue(ctx, got);

  /* 数组元素仍可读取。 */
  JSValue *g0 = JS_GetElement(ctx, arr, 0);
  ASSERT_NE(g0, nullptr);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, g0), 5.0);
  JS_FreeValue(ctx, g0);

  JS_FreeValue(ctx, elem);
  JS_FreeValue(ctx, arr);
  JS_FreeValue(ctx, obj);
  JS_FreeValue(ctx, proto);
  JS_FreeValue(ctx, val);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
