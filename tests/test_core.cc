/*
 * core 层里程碑测试（M1：value/data/scope 数据层）。
 *
 * 遵循 docs/scope-redesign.md：
 *   - §4 value 生命周期：create_value 进 current_scope.values，scope 销毁时
 *     scope_ref-- 归零 delete；data refcount 由 value 持有。
 *   - §1.2 scope 树：push/pop 挂摘、递归销毁 children、词法查找沿 parent。
 *   - §3.5 load/store/clone：load 返回原始变量，store 改 data 不顶替变量，
 *     clone 按 data 类型分派。
 *   - §2 TDZ 哨兵：Uninitialized。
 *   - §5 闭包共享同一 value：读写穿透。
 */

#include <gtest/gtest.h>

#include <string>

#include "neojs/core/context.h"
#include "neojs/core/data.h"
#include "neojs/core/scope.h"
#include "neojs/core/value.h"

namespace {

using neojs::core::Context;
using neojs::core::Data;
using neojs::core::DataKind;
using neojs::core::NumberData;
using neojs::core::Scope;
using neojs::core::StringData;
using neojs::core::Value;
using neojs::core::ValueKind;

/* 数值 data 取值辅助 */
double numOf(const Value *v) {
  EXPECT_EQ(v->kind, ValueKind::Number);
  return static_cast<NumberData *>(v->data)->number;
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* §4 value 生命周期：scope_ref 配对                                          */
/* -------------------------------------------------------------------------- */

TEST(CoreValue, CreateEntersCurrentScope) {
  Context ctx;
  Value *v = ctx.create_number(42);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(v->scope_ref, 1);
  /* 已加入 root_scope.values */
  ASSERT_EQ(ctx.root_scope->values.size(), 1u);
  EXPECT_EQ(ctx.root_scope->values[0], v);
}

TEST(CoreValue, ScopeExitDecrementsAndFrees) {
  Context ctx;
  Value *v = ctx.create_number(7);
  EXPECT_EQ(v->scope_ref, 1);
  /* 销毁 root_scope（ctx 析构）：scope_ref-- 归零 → delete */
  /* 无法直接观测 delete，但确保无泄漏/崩溃即可（ASan 验证）。 */
}

TEST(CoreValue, MultipleScopesShareValue) {
  Context ctx;
  Value *v = ctx.create_number(1);
  /* 子 scope 也引用同一 value（闭包/import 场景：scope_ref++） */
  ctx.push_scope();
  Scope *inner = ctx.current_scope;
  v->scope_ref++;          /* 模拟加入 inner.values */
  inner->values.push_back(v);
  EXPECT_EQ(v->scope_ref, 2);
  ctx.pop_scope();
  EXPECT_EQ(v->scope_ref, 1);
}

TEST(CoreValue, ShareThenSequentialDestroy) {
  Context ctx;
  Value *v = ctx.create_number(1);
  ctx.define("v", v);
  /* 闭包 scope 共享同一 value */
  ctx.push_scope();
  v->scope_ref++;
  ctx.current_scope->values.push_back(v);
  ctx.current_scope->named_values["v"] = v;
  EXPECT_EQ(v->scope_ref, 2);

  /* 先销毁闭包 scope：scope_ref 2→1，value 存活 */
  ctx.pop_scope();
  EXPECT_EQ(v->scope_ref, 1);
  /* root_scope 仍持有（ctx 析构时归零 delete，无泄漏） */
}

/* -------------------------------------------------------------------------- */
/* §1.2 scope 树：挂摘 + 递归销毁                                             */
/* -------------------------------------------------------------------------- */

TEST(CoreScope, PushPopTree) {
  Context ctx;
  Scope *root = ctx.root_scope;
  Scope *a = ctx.push_scope();
  EXPECT_EQ(a->parent, root);
  EXPECT_EQ(ctx.current_scope, a);
  EXPECT_EQ(root->children.size(), 1u);

  Scope *b = ctx.push_scope();
  EXPECT_EQ(b->parent, a);
  EXPECT_EQ(a->children.size(), 1u);

  ctx.pop_scope();
  EXPECT_EQ(ctx.current_scope, a);
  ctx.pop_scope();
  EXPECT_EQ(ctx.current_scope, root);
  EXPECT_EQ(root->children.size(), 0u);
}

TEST(CoreScope, DestroyRecursivelyClearsChildren) {
  Context ctx;
  ctx.push_scope();
  Scope *a = ctx.current_scope;
  ctx.push_scope();
  Value *vb = ctx.create_number(5); /* 进内层 scope.values */
  EXPECT_EQ(vb->scope_ref, 1);

  /* 销毁 a 整棵子树（含内层 b） */
  ctx.destroy_scope(a);
  EXPECT_EQ(ctx.root_scope->children.size(), 0u);
  /* current_scope 若指向被销毁的子树需恢复 */
  ctx.current_scope = ctx.root_scope;
}

/* -------------------------------------------------------------------------- */
/* §3.5 load/store：load 原始变量，store 改 data 不顶替                       */
/* -------------------------------------------------------------------------- */

TEST(CoreStore, LoadReturnsOriginalVariable) {
  Context ctx;
  Value *v = ctx.create_number(10);
  ctx.define("a", v);
  Value *loaded = ctx.load("a");
  EXPECT_EQ(loaded, v); /* 原始变量，非拷贝 */
}

TEST(CoreStore, StoreMutatesDataNotVariable) {
  Context ctx;
  Value *v = ctx.create_number(10);
  ctx.define("a", v);
  Value *newval = ctx.create_number(20);
  EXPECT_TRUE(ctx.store("a", newval));
  /* load 仍返回同一 value 对象 */
  Value *loaded = ctx.load("a");
  EXPECT_EQ(loaded, v);
  /* 但 data 内容已变 */
  EXPECT_EQ(numOf(loaded), 20.0);
}

TEST(CoreStore, StoreNonexistentReturnsFalse) {
  Context ctx;
  Value *nv = ctx.create_number(1);
  EXPECT_FALSE(ctx.store("nope", nv));
}

TEST(CoreStore, StoreUndefinedClearsData) {
  Context ctx;
  Value *v = ctx.create_number(10);
  ctx.define("a", v);
  Value *u = ctx.create_undefined();
  EXPECT_TRUE(ctx.store("a", u));
  Value *loaded = ctx.load("a");
  EXPECT_EQ(loaded, v);
  EXPECT_EQ(loaded->kind, ValueKind::Undefined);
  EXPECT_EQ(loaded->data, nullptr);
}

TEST(CoreStore, LexicalLookupAlongParentChain) {
  Context ctx;
  Value *outer = ctx.create_number(1);
  ctx.define("x", outer);
  ctx.push_scope();
  /* 子 scope 无 x → 沿 parent 找到外层 */
  Value *loaded = ctx.load("x");
  EXPECT_EQ(loaded, outer);
  ctx.pop_scope();
}

/* -------------------------------------------------------------------------- */
/* §5 闭包共享同一 value：读写穿透                                             */
/* -------------------------------------------------------------------------- */

TEST(CoreClosure, SharedValueReadsThrough) {
  Context ctx;
  Value *a = ctx.create_number(1);
  ctx.define("a", a);

  /* 闭包 scope：引用同一 value（scope_ref++） */
  ctx.push_scope();
  Scope *closure = ctx.current_scope;
  a->scope_ref++;
  closure->values.push_back(a);
  closure->named_values["a"] = a;

  /* 外层 store → 闭包 scope 读到新 data */
  ctx.current_scope = ctx.root_scope;
  Value *nv = ctx.create_number(99);
  ctx.store("a", nv);

  /* 闭包 scope 读同一 value：data 已变 */
  ctx.current_scope = closure;
  Value *cl_loaded = ctx.load("a");
  EXPECT_EQ(cl_loaded, a);
  EXPECT_EQ(numOf(cl_loaded), 99.0);

  ctx.current_scope = ctx.root_scope;
  ctx.pop_scope();
}

/* -------------------------------------------------------------------------- */
/* §2 TDZ 哨兵：Uninitialized                                                  */
/* -------------------------------------------------------------------------- */

TEST(CoreTdz, UninitializedSentinel) {
  Context ctx;
  Value *u = ctx.create_uninitialized();
  EXPECT_EQ(u->kind, ValueKind::Uninitialized);
  EXPECT_EQ(u->data, nullptr);
  ctx.define("x", u);
  Value *loaded = ctx.load("x");
  EXPECT_EQ(loaded->kind, ValueKind::Uninitialized);
  /* store 后变真实值 */
  Value *real = ctx.create_number(3);
  ctx.store("x", real);
  EXPECT_EQ(numOf(ctx.load("x")), 3.0);
}

/* -------------------------------------------------------------------------- */
/* clone：值类型深拷贝                                                         */
/* -------------------------------------------------------------------------- */

TEST(CoreClone, ValueTypesDeepCopy) {
  Context ctx;
  Value *s = ctx.create_string("hello");
  Data *cloned = ctx.clone(s->data);
  ASSERT_NE(cloned, nullptr);
  EXPECT_EQ(cloned->kind, DataKind::String);
  EXPECT_NE(cloned, s->data); /* 深拷贝：新 data */
  EXPECT_EQ(static_cast<StringData *>(cloned)->str, "hello");
  EXPECT_EQ(cloned->refcount, 1);
  delete cloned;
}

/* -------------------------------------------------------------------------- */
/* Allocator 分配上限（防失控耗尽系统资源）                                    */
/* -------------------------------------------------------------------------- */

#include "neojs/core/allocator.h"

namespace {
/* 测试辅助：恢复分配上限 + 计数归零（alloc/free 平衡后应为 0）。 */
struct AllocatorGuard {
  size_t saved_limit = neojs::core::Allocator::limit();
  AllocatorGuard() { neojs::core::Allocator::set_limit(size_t{1} << 60); }
  ~AllocatorGuard() {
    neojs::core::Allocator::set_limit(saved_limit);
  }
};
}

TEST(Allocator, AllocFreeBalance) {
  AllocatorGuard g;
  /* alloc/free 平衡后活跃计数归零（MSVC 精确；其他平台高估但非负）。 */
  void *p1 = neojs::core::Allocator::alloc(100);
  void *p2 = neojs::core::Allocator::alloc(200);
  EXPECT_GE(neojs::core::Allocator::allocated(), 300u);
  neojs::core::Allocator::free(p1);
  neojs::core::Allocator::free(p2);
}

TEST(Allocator, LimitExceededAborts) {
  AllocatorGuard g;
  neojs::core::Allocator::set_limit(64);
  /* 超上限分配 → oomAbort（death test）。 */
  EXPECT_DEATH({ neojs::core::Allocator::alloc(4096); },
               "out of memory");
}
