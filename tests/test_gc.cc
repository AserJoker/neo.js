/*
 * core 层 GC 里程碑测试（M2：value 生命周期管理）。
 *
 * 遵循 docs/scope-redesign.md §4（修正版）：
 *   - value 唯一 owner 是 Gc（全局表），scope 只持引用清单。
 *   - scope_ref-- 不 delete；delete 永远发生在 Gc::collect 扫描时。
 *     对象字段安全：延迟到 GC 统一处理，避免 scope 退出时中途悬垂。
 *   - 根集：scope_ref > 0 的 value 存活（collect 跳过）。
 *   - data 生命周期归 value 析构（refcount-- 归零 delete）。
 *   - 函数返回值过渡：create_value(res) 重建新 value 引用同 data。
 */

#include <gtest/gtest.h>

#include <string>

#include "neojs/core/context.h"
#include "neojs/core/data.h"
#include "neojs/core/gc.h"
#include "neojs/core/scope.h"
#include "neojs/core/value.h"

namespace {

using neojs::core::Context;
using neojs::core::Data;
using neojs::core::Gc;
using neojs::core::NumberData;
using neojs::core::Scope;
using neojs::core::Value;
using neojs::core::ValueKind;

/* -------------------------------------------------------------------------- */
/* Gc 全局表：createValue 入表，scope_ref 初始 0                             */
/* -------------------------------------------------------------------------- */

TEST(CoreGc, CreateValueEntersGlobalTable) {
  Gc gc;
  Value *v = gc.createValue(ValueKind::Number);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(v->scope_ref, 0); /* 未挂 scope：scope_ref=0 */
  EXPECT_EQ(gc.liveCount(), 1u);
  /* 无 scope 引用 → collect 回收 */
  gc.collect();
  EXPECT_EQ(gc.liveCount(), 0u);
}

TEST(CoreGc, AttachedValueSurvivesCollect) {
  Context ctx;
  Value *v = ctx.create_number(42); /* 挂 root_scope，scope_ref=1 */
  EXPECT_EQ(v->scope_ref, 1);
  EXPECT_EQ(ctx.gc_.liveCount(), 1u);
  ctx.gc_.collect();
  EXPECT_EQ(ctx.gc_.liveCount(), 1u); /* scope_ref>0 当根跳过 */
  /* 仍可访问 */
  EXPECT_EQ(static_cast<NumberData *>(v->data)->number, 42.0);
}

/* -------------------------------------------------------------------------- */
/* scope 退出只 scope_ref--，不 delete；collect 才回收                         */
/* -------------------------------------------------------------------------- */

TEST(CoreGc, ScopeExitDecrementsWithoutDelete) {
  Context ctx;
  ctx.push_scope();
  Value *v = ctx.create_number(7); /* 挂内层 scope，scope_ref=1 */
  EXPECT_EQ(v->scope_ref, 1);

  ctx.pop_scope(); /* 内层 scope 销毁：scope_ref 1→0，但不 delete */
  EXPECT_EQ(v->scope_ref, 0);
  /* value 仍存活：延迟回收，可安全访问（对象字段安全的前提） */
  EXPECT_EQ(static_cast<NumberData *>(v->data)->number, 7.0);

  /* collect 才真正回收 */
  ctx.gc_.collect();
  EXPECT_EQ(ctx.gc_.liveCount(), 0u);
}

TEST(CoreGc, SharedValueAcrossScopes) {
  Context ctx;
  Value *v = ctx.create_number(1); /* root_scope: scope_ref=1 */
  ctx.push_scope();
  ctx.attach(ctx.current_scope, v); /* 内层 scope 共享: scope_ref=2 */
  EXPECT_EQ(v->scope_ref, 2);

  ctx.pop_scope(); /* 内层销毁: 2→1，不 delete */
  EXPECT_EQ(v->scope_ref, 1);
  ctx.gc_.collect();
  EXPECT_EQ(ctx.gc_.liveCount(), 1u); /* root_scope 仍持有 */

  ctx.gc_.collect(); /* root_scope 常驻，ctx 析构时才回收 */
  EXPECT_EQ(ctx.gc_.liveCount(), 1u);
}

/* -------------------------------------------------------------------------- */
/* 函数返回值过渡：create_value(res) 重建新 value 引用同 data                  */
/* -------------------------------------------------------------------------- */

TEST(CoreGc, ReturnValueTransitionsData) {
  Context ctx;
  /* 模拟函数返回：value 在函数 scope 创建（scope_ref=1），返回后 scope 销毁。
   * 调用方（root_scope）重建 value 引用同 data，data 过渡存活。
   * 注意：caller 必须挂在调用方 scope（root_scope），不能挂函数 scope
   * （否则 pop 后 caller scope_ref 归零，collect 连 caller 一起回收）。 */
  Value *caller = ctx.create_value(ValueKind::Number); /* 挂 root_scope */

  ctx.push_scope();
  Value *res = ctx.create_number(99); /* 函数 scope: scope_ref=1 */
  Data *res_data = res->data;
  EXPECT_EQ(res_data->refcount, 1);

  /* 调用方重建：caller 引用同 data（data refcount++） */
  caller->data = res_data;
  res_data->refcount++;
  EXPECT_EQ(res_data->refcount, 2);

  /* 函数 scope 销毁：res scope_ref 1→0，不 delete（延迟回收） */
  ctx.pop_scope();
  EXPECT_EQ(res->scope_ref, 0);
  EXPECT_EQ(res_data->refcount, 2); /* res 值未回收，data 仍被两者持有 */

  /* collect 回收 res（value 释放，data refcount 2→1），data 仍被 caller 引用存活 */
  ctx.gc_.collect();
  EXPECT_EQ(res_data->refcount, 1);
  EXPECT_EQ(static_cast<NumberData *>(res_data)->number, 99.0);

  /* caller 仍可用 */
  EXPECT_EQ(caller->kind, ValueKind::Number);
  EXPECT_EQ(static_cast<NumberData *>(caller->data)->number, 99.0);
}

/* -------------------------------------------------------------------------- */
/* store：改 data 内容，data refcount 配对                                     */
/* -------------------------------------------------------------------------- */

TEST(CoreGc, StoreSwapsDataRefcount) {
  Context ctx;
  Value *a = ctx.create_number(10); /* data refcount=1 */
  Value *b = ctx.create_number(20); /* data refcount=1 */
  Data *a_data = a->data;
  Data *b_data = b->data;
  EXPECT_EQ(a_data->refcount, 1);
  EXPECT_EQ(b_data->refcount, 1);

  /* 需先 define 绑定，store 才有目标 */
  ctx.define("a", a);

  /* store a = b：a 的 data 换成 b 的 clone（深拷贝，refcount=1），旧 data 释放。
   * target（value 对象）本身不变，只是 data 顶替。 */
  EXPECT_TRUE(ctx.store("a", b));

  /* load("a") 拿到同一 value，data 已换为 b 的 clone（值 20，非 b_data 本体） */
  Value *loaded = ctx.load("a");
  EXPECT_EQ(loaded, a); /* value 对象不变 */
  EXPECT_EQ(static_cast<NumberData *>(loaded->data)->number, 20.0);
  EXPECT_NE(loaded->data, b_data); /* clone 深拷贝，不是同一 data */
  EXPECT_EQ(loaded->data->refcount, 1);

  /* b 原 data 不受影响；a 原 data（a_data）已 refcount 1→0 delete，不可再访问 */
  EXPECT_EQ(b_data->refcount, 1);
}

}  // namespace
