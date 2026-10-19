/*
 * E1 对象模型地基单元测试（架构 §5.2 + docs/gc.md §3.2/§4.1）。
 * 验证：
 *   1. 对象创建 + 命名属性读写（get/set/has/delete）
 *   2. 原型链（[[Prototype]] 指针 + ordinary [[Get]] 沿原型链）
 *   3. 数组（elements 分离存储 + length 推导 + 越界/空洞）
 *   4. GC：对象引用边（属性值/原型/数组元素）存活 + 循环引用回收
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

#include "neojs/neojs.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/object.h"

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
/* 1. 对象创建 + 命名属性读写                                                  */
/* -------------------------------------------------------------------------- */

TEST(ObjectModel, CreateAndSetGetProperty) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *obj = JS_CreateObject(ctx, nullptr, nullptr);
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, obj), JS_OBJECT);

  /* 初始无属性 */
  EXPECT_EQ(JS_HasProperty(ctx, obj, "x"), 0);

  /* set 新增属性 -> get 读取 */
  JSValue *v = JS_CreateNumber(ctx, 42.0, nullptr);
  JS_SetProperty(ctx, obj, "x", v);
  EXPECT_EQ(JS_HasProperty(ctx, obj, "x"), 1);
  JSValue *got = JS_GetProperty(ctx, obj, "x");
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, got), JS_NUMBER);
  EXPECT_EQ(JS_ToNumber(ctx, got), 42.0);
  JS_FreeValue(ctx, got);

  /* set 覆盖属性 */
  JSValue *v2 = JS_CreateNumber(ctx, 7.0, nullptr);
  JS_SetProperty(ctx, obj, "x", v2);
  EXPECT_EQ(JS_ToNumber(ctx, JS_GetProperty(ctx, obj, "x")), 7.0);

  /* 未找到 -> undefined（#14 非 NULL） */
  JSValue *missing = JS_GetProperty(ctx, obj, "nope");
  ASSERT_NE(missing, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, missing), JS_UNDEFINED);
  JS_FreeValue(ctx, missing);

  /* delete */
  EXPECT_EQ(JS_DeleteProperty(ctx, obj, "x"), 1);
  EXPECT_EQ(JS_HasProperty(ctx, obj, "x"), 0);
  EXPECT_EQ(JS_DeleteProperty(ctx, obj, "x"), 0); /* 已删除 */

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 2. 原型链：ordinary [[Get]] 沿原型链                                       */
/* -------------------------------------------------------------------------- */

TEST(ObjectModel, PrototypeChainGet) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* proto 对象持有属性 inherited */
  JSValue *proto = JS_CreateObject(ctx, nullptr, nullptr);
  ASSERT_NE(proto, nullptr);
  JSValue *pv = JS_CreateString(ctx, "from-proto", std::strlen("from-proto"), nullptr);
  JS_SetProperty(ctx, proto, "inherited", pv);

  /* obj 的原型 = proto */
  JSValue *obj = JS_CreateObject(ctx, proto, nullptr);
  ASSERT_NE(obj, nullptr);

  /* obj 自身无属性，但沿原型链可见 */
  EXPECT_EQ(JS_HasProperty(ctx, obj, "inherited"), 1);
  JSValue *got = JS_GetProperty(ctx, obj, "inherited");
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, got), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, got), "from-proto");
  JS_FreeValue(ctx, got);

  /* 原型链查询：JS_GetPrototype 返回 proto */
  JSValue *p2 = JS_GetPrototype(ctx, obj);
  ASSERT_NE(p2, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, p2), JS_OBJECT);
  /* 同一原型对象：p2 与 proto 是同一内部对象 */
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  EXPECT_EQ(c->handles().get(static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(p2))),
            c->handles().get(static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(proto))));
  JS_FreeValue(ctx, p2);

  /* set 到 obj 自身：遮蔽原型（shadowing） */
  JSValue *sv = JS_CreateNumber(ctx, 99.0, nullptr);
  JS_SetProperty(ctx, obj, "inherited", sv);
  EXPECT_EQ(JS_ToNumber(ctx, JS_GetProperty(ctx, obj, "inherited")), 99.0);
  EXPECT_STREQ(JS_ToString(ctx, JS_GetProperty(ctx, proto, "inherited")), "from-proto");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 3. 数组：elements 分离存储 + length + 越界/空洞                             */
/* -------------------------------------------------------------------------- */

TEST(ObjectModel, ArrayElements) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *arr = JS_CreateArray(ctx, nullptr);
  ASSERT_NE(arr, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, arr), JS_OBJECT);
  EXPECT_EQ(JS_GetLength(ctx, arr), 0u);

  /* 顺序 set 元素 -> length 推导 */
  for (int i = 0; i < 3; ++i) {
    JSValue *e = JS_CreateInteger(ctx, i + 1, nullptr);
    JS_SetElement(ctx, arr, static_cast<size_t>(i), e);
  }
  EXPECT_EQ(JS_GetLength(ctx, arr), 3u);
  EXPECT_EQ(JS_ToNumber(ctx, JS_GetElement(ctx, arr, 0)), 1.0);
  EXPECT_EQ(JS_ToNumber(ctx, JS_GetElement(ctx, arr, 2)), 3.0);

  /* 越界 get -> undefined */
  JSValue *oob = JS_GetElement(ctx, arr, 100);
  ASSERT_NE(oob, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, oob), JS_UNDEFINED);
  JS_FreeValue(ctx, oob);

  /* 越界 set -> 自动扩容（空洞为 undefined） */
  JSValue *e = JS_CreateNumber(ctx, 9.0, nullptr);
  JS_SetElement(ctx, arr, 5, e);
  EXPECT_EQ(JS_GetLength(ctx, arr), 6u);
  JSValue *hole = JS_GetElement(ctx, arr, 3);
  ASSERT_NE(hole, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, hole), JS_UNDEFINED);
  JS_FreeValue(ctx, hole);
  EXPECT_EQ(JS_ToNumber(ctx, JS_GetElement(ctx, arr, 5)), 9.0);

  /* JS_SetLength 缩短/加长 */
  EXPECT_EQ(JS_SetLength(ctx, arr, 2), 1);
  EXPECT_EQ(JS_GetLength(ctx, arr), 2u);
  EXPECT_EQ(JS_SetLength(ctx, arr, 8), 1);
  EXPECT_EQ(JS_GetLength(ctx, arr), 8u);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 4. GC：对象引用边存活 + 循环引用回收                                        */
/* -------------------------------------------------------------------------- */

TEST(ObjectModel, ObjectRefsSurviveGc) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 根对象（经句柄持有）：属性指向子对象（子对象被引用边保护） */
  JSValue *root = JS_CreateObject(ctx, nullptr, nullptr);
  ASSERT_NE(root, nullptr);
  JSValue *child = JS_CreateObject(ctx, nullptr, nullptr);
  JS_SetProperty(ctx, root, "child", child);

  /* 临时对象（无根）：GC 后回收 */
  JSValue *tmp = JS_CreateObject(ctx, nullptr, nullptr);
  JS_FreeValue(ctx, tmp);

  JS_GC(ctx);
  /* root（句柄）+ child（root 的属性引用边）等应存活 */
  EXPECT_GT(c->heap().size(), 0u);
  EXPECT_EQ(JS_GetKind(ctx, root), JS_OBJECT);
  JSValue *got = JS_GetProperty(ctx, root, "child");
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, got), JS_OBJECT);
  JS_FreeValue(ctx, got);

  /* 释放 root 与 child 句柄 -> 二者都失去根 -> GC 回收 */
  JS_FreeValue(ctx, root);
  JS_FreeValue(ctx, child);
  JS_GC(ctx);
  EXPECT_EQ(c->heap().size(), 0u);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(ObjectModel, ArrayElementRefsSurviveGc) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 数组（句柄）持有元素对象（引用边） */
  JSValue *arr = JS_CreateArray(ctx, nullptr);
  ASSERT_NE(arr, nullptr);
  JSValue *elem = JS_CreateObject(ctx, nullptr, nullptr);
  JS_SetElement(ctx, arr, 0, elem);
  JS_FreeValue(ctx, elem); /* 元素句柄释放，但数组引用边保护 */

  JS_GC(ctx);
  JSValue *got = JS_GetElement(ctx, arr, 0);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, got), JS_OBJECT);
  JS_FreeValue(ctx, got);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(ObjectModel, CyclicRefsCollected) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 无根循环引用（obj <-> obj2）：mark-sweep 应回收（docs/gc.md §7.5） */
  JSValue *o1 = JS_CreateObject(ctx, nullptr, nullptr);
  JSValue *o2 = JS_CreateObject(ctx, nullptr, nullptr);
  JS_SetProperty(ctx, o1, "next", o2);
  JS_SetProperty(ctx, o2, "back", o1);
  JS_FreeValue(ctx, o1);
  JS_FreeValue(ctx, o2);

  /* 手动 GC：循环引用无根 -> 全部回收 */
  c->gc();
  EXPECT_EQ(c->heap().size(), 0u);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
