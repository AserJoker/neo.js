/*
 * C 对外接口（neojs.h）单元测试。
 * 验证规范 #2（命名/不透明指针）、#4（JSValue 一等公民 + kind）、
 * #5（内存管理/生命周期）、#9（一切从 ctx 出发）、#14（永不返回 nullptr）。
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "neojs/neojs.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/scope.h"
#include "neojs/runtime/value.h"

/* -------------------------------------------------------------------------- */
/* 测试辅助：按 #13 约定，先创建 allocator，再创建 ctx                          */
/* -------------------------------------------------------------------------- */

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
/* 生命周期                                                                    */
/* -------------------------------------------------------------------------- */

TEST(CApi, NewAndFreeContext) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(CApi, AllocatorLifecycle) {
  JSAllocator *a = JS_NewAllocator(nullptr, nullptr, nullptr);
  ASSERT_NE(a, nullptr);
  JS_FreeAllocator(a);
}

TEST(CApi, CustomAllocatorIsUsed) {
  static int alloc_calls = 0;
  static int free_calls = 0;
  alloc_calls = 0;
  free_calls = 0;

  auto my_alloc = [](size_t size, void *) -> void * {
    ++alloc_calls;
    return std::malloc(size);
  };
  auto my_free = [](void *ptr, void *) {
    ++free_calls;
    std::free(ptr);
  };

  JSAllocator *a = JS_NewAllocator(my_alloc, my_free, nullptr);
  ASSERT_NE(a, nullptr);

  /* ctx 使用自定义分配器；ctx 自身分配走它 */
  JSContext *ctx = JS_NewContext(a);
  ASSERT_NE(ctx, nullptr);
  ASSERT_GT(alloc_calls, 0);

  JSValue *v = JS_CreateNumber(ctx, 3.14, nullptr);
  ASSERT_NE(v, nullptr);
  JS_FreeValue(ctx, v);

  JS_FreeContext(ctx);
  ASSERT_GT(free_calls, 0);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 值构造与 kind（#4）                                                         */
/* -------------------------------------------------------------------------- */

TEST(CApi, CreateValuesAndKinds) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *u = JS_CreateUndefined(ctx, nullptr);
  ASSERT_NE(u, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, u), JS_UNDEFINED);

  JSValue *n = JS_CreateNull(ctx, nullptr);
  ASSERT_NE(n, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, n), JS_NULL);

  JSValue *b = JS_CreateBoolean(ctx, 1, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, b), JS_BOOLEAN);
  EXPECT_EQ(JS_ToBoolean(ctx, b), 1);

  JSValue *num = JS_CreateNumber(ctx, 42.0, nullptr);
  ASSERT_NE(num, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, num), JS_NUMBER);
  EXPECT_EQ(JS_ToNumber(ctx, num), 42.0);
  EXPECT_EQ(JS_ToInteger(ctx, num), 42);

  JS_FreeValue(ctx, num);
  JS_FreeValue(ctx, b);
  JS_FreeValue(ctx, n);
  JS_FreeValue(ctx, u);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* CreateX + name：创建作用域具名值 / 临时值（无孤立值原则）                    */
/* -------------------------------------------------------------------------- */

TEST(CApi, CreateNamedValueBindsToScope) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* name != NULL：创建并自动绑定到当前作用域（kind=Let） */
  JSValue *v = JS_CreateNumber(ctx, 7.0, "seven");
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, v), JS_NUMBER);
  EXPECT_EQ(JS_ToNumber(ctx, v), 7.0);

  /* 绑定生效：可沿作用域链 Load */
  JSValue *loaded = JS_LoadValue(ctx, "seven");
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(JS_ToNumber(ctx, loaded), 7.0);
  JS_FreeValue(ctx, loaded);

  /* name == NULL：临时值，不产生绑定 */
  JSValue *tmp = JS_CreateString(ctx, "temp", 4, nullptr);
  ASSERT_NE(tmp, nullptr);
  JSValue *missing = JS_LoadValue(ctx, "temp");
  ASSERT_NE(missing, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, missing), JS_EXCEPTION); /* 无此名绑定 */
  JS_FreeValue(ctx, missing);

  JS_FreeValue(ctx, tmp);
  JS_FreeValue(ctx, v);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 具名值经绑定持有：释放句柄后值仍存活（绑定是根集） */
TEST(CApi, NamedValueSurvivesHandleFree) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);

  JSValue *v = JS_CreateNumber(ctx, 9.0, "nine");
  ASSERT_NE(v, nullptr);
  JS_FreeValue(ctx, v); /* 释放句柄 */

  JS_GC(ctx); /* 值仍被绑定持有，存活 */
  EXPECT_EQ(c->heap().size(), 1u);
  EXPECT_EQ(JS_ToNumber(ctx, JS_LoadValue(ctx, "nine")), 9.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(CApi, CreateString) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *s = JS_CreateString(ctx, "hello", 5, nullptr);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, s), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, s), "hello");

  JS_FreeValue(ctx, s);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 错误通道（#4：kind = JS_EXCEPTION）                                        */
/* -------------------------------------------------------------------------- */

TEST(CApi, EvalReturnsAstDumpInM11) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* M1.1：解析成功返回 AST dump 文本（String 值） */
  JSValue *r = JS_Eval(ctx, "1 + 2", 5);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_STRING);
  EXPECT_STRNE(JS_ToString(ctx, r), "");

  JS_FreeValue(ctx, r);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(CApi, EvalSyntaxErrorReturnsException) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 语法错误：返回 Exception（#4），非 nullptr */
  JSValue *r = JS_Eval(ctx, "1 +", 3);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION);

  JS_FreeValue(ctx, r);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(CApi, CreateError) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *err = JS_CreateError(ctx, "boom", nullptr);
  ASSERT_NE(err, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, err), JS_EXCEPTION);

  JS_FreeValue(ctx, err);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* #14：返回 JSValue 的接口永不返回 nullptr                                   */
/* -------------------------------------------------------------------------- */

TEST(CApi, ValueReturningApiNeverReturnsNull) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 各构造接口均返回非空 */
  EXPECT_NE(JS_CreateUndefined(ctx, nullptr), nullptr);
  EXPECT_NE(JS_CreateNull(ctx, nullptr), nullptr);
  EXPECT_NE(JS_CreateBoolean(ctx, 1, nullptr), nullptr);
  EXPECT_NE(JS_CreateNumber(ctx, 1.0, nullptr), nullptr);
  EXPECT_NE(JS_CreateInteger(ctx, 1, nullptr), nullptr);
  EXPECT_NE(JS_CreateString(ctx, "x", 1, nullptr), nullptr);
  EXPECT_NE(JS_CreateError(ctx, "x", nullptr), nullptr);
  EXPECT_NE(JS_Eval(ctx, "1", 1), nullptr);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 空指针健壮性                                                                */
/* -------------------------------------------------------------------------- */

TEST(CApi, NullHandling) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  EXPECT_EQ(JS_GetKind(ctx, nullptr), JS_UNDEFINED);
  EXPECT_EQ(JS_ToBoolean(ctx, nullptr), 0);
  EXPECT_EQ(JS_ToNumber(ctx, nullptr), 0.0);
  EXPECT_EQ(JS_ToInteger(ctx, nullptr), 0);
  EXPECT_EQ(JS_ToString(ctx, nullptr), nullptr);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 作用域压栈/弹栈：JS_PushScope / JS_PopScope（#9）                            */
/* -------------------------------------------------------------------------- */

TEST(CApi, ScopePushPop) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSScope &root = c->currentScope();
  EXPECT_EQ(root.parent(), nullptr);
  EXPECT_TRUE(root.children().empty());

  /* JS_PushScope：压入子作用域（parent 指向当前），成为 current_scope */
  JS_PushScope(ctx);
  neojs::JSScope &child = c->currentScope();
  EXPECT_EQ(child.parent(), &root);
  EXPECT_EQ(root.children().size(), 1u);
  EXPECT_EQ(root.children()[0], &child);

  /* 创建的值归属子作用域 */
  JSValue *v = JS_CreateNumber(ctx, 42.0, nullptr);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, v), JS_NUMBER);
  JS_FreeValue(ctx, v);

  /* 再压一层：parent/children 树加深 */
  JS_PushScope(ctx);
  neojs::JSScope &grand = c->currentScope();
  EXPECT_EQ(grand.parent(), &child);
  EXPECT_EQ(child.children().size(), 1u);
  EXPECT_EQ(child.children()[0], &grand);

  /* JS_PopScope：销毁当前作用域（回收归属值），回到父 */
  JS_PopScope(ctx);
  EXPECT_EQ(&c->currentScope(), &child);
  EXPECT_TRUE(child.children().empty());

  JS_PopScope(ctx);
  EXPECT_EQ(&c->currentScope(), &root);
  EXPECT_TRUE(root.children().empty());

  /* 根作用域不可弹栈 */
  JS_PopScope(ctx);
  EXPECT_EQ(&c->currentScope(), &root);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* name<->value 绑定 + JS_LoadValue 沿作用域链查找（#9）                       */
/* -------------------------------------------------------------------------- */

TEST(CApi, LoadValueByScopeChain) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSScope &root = c->currentScope();

  /* 根作用域绑定 x = 1（值登记在 ctx，绑定定义在根作用域） */
  neojs::JSValue *vx = c->createValue(neojs::JSValue(1.0));
  root.defineBinding("x", {vx, neojs::BindingKind::Var, true});

  /* 压栈：子作用域绑定 y = 2 */
  JS_PushScope(ctx);
  neojs::JSScope &child = c->currentScope();
  neojs::JSValue *vy = c->createValue(neojs::JSValue(2.0));
  child.defineBinding("y", {vy, neojs::BindingKind::Var, true});

  /* 子作用域能沿链找到 x（父），也能找到 y（自身） */
  JSValue *lx = JS_LoadValue(ctx, "x");
  ASSERT_NE(lx, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, lx), JS_NUMBER);
  EXPECT_EQ(JS_ToNumber(ctx, lx), 1.0);

  JSValue *ly = JS_LoadValue(ctx, "y");
  ASSERT_NE(ly, nullptr);
  EXPECT_EQ(JS_ToNumber(ctx, ly), 2.0);

  /* 未找到：返回 Exception 值（#4/#14，非 nullptr） */
  JSValue *lz = JS_LoadValue(ctx, "z");
  ASSERT_NE(lz, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, lz), JS_EXCEPTION);

  /* 弹栈后回到根：y 不再可见，x 仍可见 */
  JS_PopScope(ctx);
  JSValue *ly2 = JS_LoadValue(ctx, "y");
  ASSERT_NE(ly2, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, ly2), JS_EXCEPTION);
  JSValue *lx2 = JS_LoadValue(ctx, "x");
  ASSERT_NE(lx2, nullptr);
  EXPECT_EQ(JS_ToNumber(ctx, lx2), 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* JS_DefineValue / JS_StoreValue（#9）                                        */
/* -------------------------------------------------------------------------- */

TEST(CApi, DefineAndStoreValue) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSScope &root = c->currentScope();

  /* 根作用域定义 counter = 0（kind=Var，对应 var 声明） */
  JSValue *v0 = JS_CreateNumber(ctx, 0.0, nullptr);
  JSValue *def = JS_DefineValue(ctx, "counter", v0, 0 /* Var */);
  ASSERT_NE(def, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, def), JS_NUMBER);
  EXPECT_EQ(JS_ToNumber(ctx, JS_LoadValue(ctx, "counter")), 0.0);

  /* 值登记在 ctx（P2），跨作用域 store 安全：子作用域赋值 counter = 5 */
  JSValue *v5 = JS_CreateNumber(ctx, 5.0, nullptr);
  JS_PushScope(ctx);
  JSValue *st = JS_StoreValue(ctx, "counter", v5);
  ASSERT_NE(st, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, st), JS_NUMBER);
  EXPECT_EQ(JS_ToNumber(ctx, JS_LoadValue(ctx, "counter")), 5.0);

  /* 子作用域更新后父作用域读到新值（赋值穿透作用域链） */
  JS_PopScope(ctx);
  EXPECT_EQ(JS_ToNumber(ctx, JS_LoadValue(ctx, "counter")), 5.0);
  /* 绑定存内部裸指针（G2/§6）：应等于 v5 句柄解出的内部对象 */
  neojs::JSValue *v5_internal = c->handles().get(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(v5)));
  ASSERT_NE(v5_internal, nullptr);
  EXPECT_EQ(root.findBinding("counter")->value, v5_internal);
  EXPECT_EQ(root.findBinding("counter")->value->toNumber(), 5.0);

  /* 未声明名 store：返回 Exception（ReferenceError） */
  JSValue *st2 = JS_StoreValue(ctx, "nope", JS_CreateNumber(ctx, 1.0, nullptr));
  ASSERT_NE(st2, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, st2), JS_EXCEPTION);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* JSBinding：kind 记录（M0 不校验）+ 未初始化绑定（TDZ 起点）                 */
/* -------------------------------------------------------------------------- */

TEST(CApi, BindingKindAndUninitialized) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
  neojs::JSScope &root = c->currentScope();

  /* const 定义：kind 被记录（M0 不校验，仍可 store） */
  JSValue *vc = JS_CreateNumber(ctx, 10.0, nullptr);
  JSValue *defc = JS_DefineValue(ctx, "c", vc, 2 /* Const */);
  ASSERT_NE(defc, nullptr);
  EXPECT_EQ(root.findBinding("c")->kind, neojs::BindingKind::Const);
  EXPECT_TRUE(root.findBinding("c")->initialized);

  /* M0 不校验 const：store 仍成功（M2 才拒绝） */
  JSValue *v2 = JS_CreateNumber(ctx, 20.0, nullptr);
  JSValue *st = JS_StoreValue(ctx, "c", v2);
  ASSERT_NE(st, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, st), JS_NUMBER);
  EXPECT_EQ(JS_ToNumber(ctx, JS_LoadValue(ctx, "c")), 20.0);

  /* value=nullptr 定义未初始化绑定（TDZ 起点，initialized=false） */
  JSValue *defu = JS_DefineValue(ctx, "u", nullptr, 1 /* Let */);
  EXPECT_EQ(defu, nullptr);
  EXPECT_EQ(root.findBinding("u")->kind, neojs::BindingKind::Let);
  EXPECT_FALSE(root.findBinding("u")->initialized);
  EXPECT_EQ(root.findBinding("u")->value, nullptr);

  /* M0 不校验 TDZ：load 未初始化绑定返回 ReferenceError（因为 value 为 nullptr） */
  JSValue *lu = JS_LoadValue(ctx, "u");
  ASSERT_NE(lu, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, lu), JS_EXCEPTION);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
