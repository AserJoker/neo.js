/*
 * GC 引擎单元测试（docs/gc.md §7 测试计划）。
 * 验证内部 Heap / HandleTable / mark-sweep 行为：
 *   1. 值存活：创建值 -> 手动 GC -> 值仍存活（根保护）。
 *   2. 作用域链回收：pushScope 绑定 -> popScope 后值成为垃圾 -> GC 回收。
 *   3. 句柄根保护：宿主经句柄持有 -> GC 存活；释放句柄 -> GC 回收。
 *   4. 阈值自动触发：分配超过阈值 -> 自动 GC（objects_ 不无限膨胀）。
 */

#include <gtest/gtest.h>

#include <cstdint>

#include "neojs/neojs.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/scope.h"
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
/* 1. 值存活：创建值 -> 手动 GC -> 值内容不变                                  */
/* -------------------------------------------------------------------------- */

TEST(GC, ValueSurvivesManualGc) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 创建值（经 Heap 登记） */
  neojs::JSValue *v = c->createValue(neojs::JSValue(42.0));
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(c->heap().size(), 1u);

  /* 手动 GC（未持有句柄/绑定的值应被回收） */
  c->gc();
  EXPECT_EQ(c->heap().size(), 0u); /* 无根 -> 回收 */

  /* 经句柄持有 -> GC 存活 */
  neojs::JSValue *v2 = c->createValue(neojs::JSValue(7.0));
  uint32_t h = c->handles().alloc(v2);
  ASSERT_NE(h, 0u);
  c->gc();
  EXPECT_EQ(c->heap().size(), 1u); /* 句柄是根 -> 存活 */
  EXPECT_EQ(c->handles().get(h), v2);
  EXPECT_EQ(c->handles().get(h)->toNumber(), 7.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 2. 作用域链回收：绑定值存活，pop 后成垃圾被回收                            */
/* -------------------------------------------------------------------------- */

TEST(GC, ScopeBoundValuesAreRoots) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSScope &root = c->currentScope();

  /* 根作用域绑定 x = 1（值经 Heap 登记，绑定是根） */
  neojs::JSValue *vx = c->createValue(neojs::JSValue(1.0));
  root.defineBinding("x", {vx, neojs::BindingKind::Var, true});

  /* 压栈绑定 y = 2 */
  c->pushScope();
  neojs::JSScope &child = c->currentScope();
  neojs::JSValue *vy = c->createValue(neojs::JSValue(2.0));
  child.defineBinding("y", {vy, neojs::BindingKind::Var, true});
  EXPECT_EQ(c->heap().size(), 2u);

  /* GC：两个绑定值都存活（作用域链是根集） */
  c->gc();
  EXPECT_EQ(c->heap().size(), 2u);
  EXPECT_EQ(root.findBinding("x")->value->toNumber(), 1.0);
  EXPECT_EQ(child.findBinding("y")->value->toNumber(), 2.0);

  /* pop 子作用域：y 绑定消失 -> 值成垃圾 */
  c->popScope();
  c->gc();
  EXPECT_EQ(c->heap().size(), 1u); /* 只剩 x */
  EXPECT_EQ(root.findBinding("x")->value->toNumber(), 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 3. 句柄根保护 + 释放后回收                                                 */
/* -------------------------------------------------------------------------- */

TEST(GC, HandleRootProtection) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 宿主经句柄持有值 -> GC 存活 */
  neojs::JSValue *v = c->createValue(neojs::JSValue(9.0));
  uint32_t h = c->handles().alloc(v);
  c->gc();
  EXPECT_EQ(c->heap().size(), 1u);
  EXPECT_NE(c->handles().get(h), nullptr);

  /* 释放句柄 -> 值失去根 -> GC 回收 */
  c->handles().free(h);
  c->gc();
  EXPECT_EQ(c->heap().size(), 0u);
  EXPECT_EQ(c->handles().get(h), nullptr);

  /* 句柄复用：释放后再次 alloc 复用槽位 */
  neojs::JSValue *v2 = c->createValue(neojs::JSValue(3.0));
  uint32_t h2 = c->handles().alloc(v2);
  EXPECT_EQ(h2, h); /* 复用释放的槽 */

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 4. 阈值自动触发：分配超过阈值 -> 自动 GC                                    */
/* -------------------------------------------------------------------------- */

TEST(GC, ThresholdAutoTrigger) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  /* 调低阈值便于测试（默认 1000） */
  c->heap().setGcThreshold(8);

  /* 创建 8 个值：第 8 个触发自动 GC（此时前 7 个无根，被回收）。
   * 第 8 个分配后 alloc_count_ 达到 8 -> collect() -> 前 7 个被回收。 */
  for (int i = 0; i < 7; ++i) {
    c->createValue(neojs::JSValue(static_cast<double>(i)));
  }
  EXPECT_EQ(c->heap().size(), 7u);
  EXPECT_EQ(c->heap().allocCount(), 7u);

  /* 第 8 个分配触发自动 GC：前 7 个无根被回收，只剩第 8 个（无根也活不到下次） */
  neojs::JSValue *v = c->createValue(neojs::JSValue(100.0));
  (void)v;
  /* 自动 GC 已发生：alloc_count_ 归零 */
  EXPECT_EQ(c->heap().allocCount(), 0u);
  /* 第 8 个也立即无根；但 heap 里可能仍有它（本次 GC 标记发生在分配时，
   * 该对象尚未登记或刚登记即被清除）。为确定性，手动 GC 确认归零。 */
  c->gc();
  EXPECT_EQ(c->heap().size(), 0u);

  /* 有根的不会因自动 GC 被误回收 */
  c->heap().setGcThreshold(3);
  neojs::JSValue *rooted = c->createValue(neojs::JSValue(1.0));
  uint32_t h = c->handles().alloc(rooted);
  c->createValue(neojs::JSValue(2.0));
  c->createValue(neojs::JSValue(3.0));
  /* 第 3 个分配触发自动 GC：句柄持有的 rooted 存活，无根的回收 */
  EXPECT_EQ(c->heap().size(), 1u);
  EXPECT_EQ(c->handles().get(h)->toNumber(), 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
