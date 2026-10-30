/*
 * M5.5f 块作用域总验收：let/const 块级绑定 + TDZ + for 闭包（§8.1.1 / §13.7.4）。
 *
 * 覆盖（对照 docs/milestones.md M5.5f 待办）：
 *   - 块内 let/const：独立绑定 + 遮蔽外层同名 var/let
 *   - 块内 var 提升到函数作用域（§13.3.2.4）
 *   - TDZ：声明前读取/赋值抛 ReferenceError
 *   - const：赋值抛 TypeError；解构声明是初始化语义（允许写）
 *   - for 头 let/const：每次迭代新绑定（per-iteration），闭包捕获各迭代独立值
 *   - for-of/for-in 词法 left：同样 per-iteration 绑定
 *   - 顶层 let/const 不入全局对象（§13.3.2.4）；顶层 var 仍入全局
 *   - 重复声明（let/let、let/var 同作用域）报 SyntaxError
 *   - 词法绑定跨函数捕获（闭包读外层块 let）
 *   - 解构声明（let {a} = ... / const [x] = ...）
 *
 * 宿主模式：JS_Eval 求值，结果写入 globalThis.* 后断言（与 test_m55d.cc
 * 同辅助函数模式）。
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "neojs/neojs.h"

namespace {

/* 创建默认分配器 + ctx；调用方负责 JS_FreeContext 后 JS_FreeAllocator。 */
JSContext *makeCtx(JSAllocator **out_alloc) {
  JSAllocator *a = JS_NewAllocator(nullptr, nullptr, nullptr);
  EXPECT_NE(a, nullptr);
  JSContext *ctx = JS_NewContext(a);
  EXPECT_NE(ctx, nullptr);
  if (out_alloc != nullptr) *out_alloc = a;
  return ctx;
}

/* 从全局对象读属性值（返回句柄，调用方 FreeValue；属性不存在返回 NULL）。 */
JSValue *globalProp(JSContext *ctx, const char *name) {
  JSValue *g = JS_GetGlobalObject(ctx);
  JSValue *v = JS_GetProperty(ctx, g, name);
  JS_FreeValue(ctx, g);
  return v;
}

/* 断言全局对象属性为数值。 */
void expectGlobalNumber(JSContext *ctx, const char *name, double expected) {
  JSValue *v = globalProp(ctx, name);
  ASSERT_NE(v, nullptr) << "global property '" << name << "' missing";
  EXPECT_EQ(JS_GetKind(ctx, v), JS_NUMBER)
      << "global '" << name << "' kind, got: " << JS_ToString(ctx, v);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, v), expected) << "global '" << name << "'";
  JS_FreeValue(ctx, v);
}

/* 断言全局对象属性为字符串。 */
void expectGlobalString(JSContext *ctx, const char *name,
                        const std::string &expected) {
  JSValue *v = globalProp(ctx, name);
  ASSERT_NE(v, nullptr) << "global property '" << name << "' missing";
  EXPECT_EQ(JS_GetKind(ctx, v), JS_STRING)
      << "global '" << name << "' kind, got: " << JS_ToString(ctx, v);
  if (JS_GetKind(ctx, v) == JS_STRING) {
    EXPECT_EQ(std::string(JS_ToString(ctx, v)), expected)
        << "global '" << name << "'";
  }
  JS_FreeValue(ctx, v);
}

/* 断言全局对象属性为布尔值。 */
void expectGlobalBoolean(JSContext *ctx, const char *name, bool expected) {
  JSValue *v = globalProp(ctx, name);
  ASSERT_NE(v, nullptr) << "global property '" << name << "' missing";
  EXPECT_EQ(JS_GetKind(ctx, v), JS_BOOLEAN)
      << "global '" << name << "' kind, got: " << JS_ToString(ctx, v);
  EXPECT_EQ(JS_ToBoolean(ctx, v) != 0, expected) << "global '" << name << "'";
  JS_FreeValue(ctx, v);
}

/* 求值（断言无异常）并写入 globalThis，随后读断言。 */
void evalOk(JSContext *ctx, const char *src) {
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr) << "JS_Eval returned null for: " << src;
  EXPECT_NE(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "eval failed: " << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);
}

/* 求值并断言结果是 Exception（错误类型含 expect_substr）。 */
void evalExpectException(JSContext *ctx, const char *src,
                         const char *expect_substr) {
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr) << "JS_Eval returned null for: " << src;
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "expected exception for: " << src;
  if (JS_GetKind(ctx, r) == JS_EXCEPTION && expect_substr != nullptr) {
    std::string msg = JS_ToString(ctx, r);
    EXPECT_NE(msg.find(expect_substr), std::string::npos)
        << "exception message: " << msg;
  }
  JS_FreeValue(ctx, r);
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 块内 let/const：独立绑定 + 遮蔽                                                 */
/* -------------------------------------------------------------------------- */

TEST(BlockScope, BlockLetShadow) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 块内 let 遮蔽外层 var，块外读外层值。 */
  evalOk(ctx,
         "var x = 'outer';"
         "{ let x = 'inner'; globalThis.inner = x; }"
         "globalThis.outer = x;");
  expectGlobalString(ctx, "inner", "inner");
  expectGlobalString(ctx, "outer", "outer");

  /* 嵌套块遮蔽取中间层。 */
  evalOk(ctx,
         "let m = 1;"
         "{ let m = 2; { let m = 3; } globalThis.mid = m; }"
         "globalThis.out = m;");
  expectGlobalNumber(ctx, "mid", 2);
  expectGlobalNumber(ctx, "out", 1);

  /* 块内 let 重新赋值（非 const）。 */
  evalOk(ctx,
         "let y = 1;"
         "{ let y = 2; y = 9; globalThis.innerY = y; }"
         "globalThis.outerY = y;");
  expectGlobalNumber(ctx, "innerY", 9);
  expectGlobalNumber(ctx, "outerY", 1);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(BlockScope, BlockConst) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 块内 const 读取 + 遮蔽。 */
  evalOk(ctx,
         "const c = 1;"
         "{ const c = 9; globalThis.innerC = c; }"
         "globalThis.outerC = c;");
  expectGlobalNumber(ctx, "innerC", 9);
  expectGlobalNumber(ctx, "outerC", 1);

  /* const 赋值抛 TypeError。 */
  evalExpectException(ctx, "{ const c = 1; c = 2; }", "constant");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(BlockScope, BlockVarHoist) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 块内 var 提升到函数作用域：块外可见。 */
  evalOk(ctx,
         "{ var v = 1; }"
         "globalThis.v = v;");
  expectGlobalNumber(ctx, "v", 1);

  /* 块内 var 遮蔽：同名 var 复用同一槽（函数作用域）。 */
  evalOk(ctx,
         "var v = 5;"
         "{ var v = 6; }"
         "globalThis.v = v;");
  expectGlobalNumber(ctx, "v", 6);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* TDZ（暂时性死区）                                                              */
/* -------------------------------------------------------------------------- */

TEST(BlockScope, TDZ) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* let 声明前读取（无 catch）-> ReferenceError 传播。 */
  evalExpectException(ctx, "{ globalThis.t = x; let x = 1; }",
                      "before initialization");

  /* const 声明前读取（无 catch）-> ReferenceError 传播。 */
  evalExpectException(ctx, "{ globalThis.t = c; const c = 1; }",
                      "before initialization");

  /* TDZ 异常可被 catch 捕获（catch 在 let 声明后的同一块）。 */
  evalOk(ctx,
         "let caught = 'no';"
         "try { let z = z; } catch (e) { caught = 'yes'; }"
         "globalThis.caught = caught;");
  expectGlobalString(ctx, "caught", "yes");

  /* TDZ 赋值（let 声明前赋值）-> ReferenceError。 */
  evalExpectException(ctx, "{ x = 9; let x = 1; }", "before initialization");

  /* typeof 命中 TDZ 绑定同样抛 ReferenceError（§13.5.3.1：未初始化词法
   * 绑定与未声明变量不同——前者抛错，后者返回 "undefined"）。 */
  evalExpectException(ctx, "{ let r = (typeof z === 'undefined'); let z = 1; }",
                      "before initialization");

  /* 解构声明前读取（TDZ 同样覆盖解构绑定名）。 */
  evalExpectException(ctx, "{ globalThis.d = d; let { d } = { d: 1 }; }",
                      "before initialization");

  /* for 头 let 与 body let 遮蔽（非重名）：body 的 let i 遮蔽 for 头 i。 */
  evalOk(ctx,
         "for (let i = 0; i < 1; i++) { let i = 9; globalThis.shadow = i; }");
  expectGlobalNumber(ctx, "shadow", 9);
  /* for 头 i 在循环外不可见（块作用域，不泄漏到全局）。 */
  evalOk(ctx, "globalThis.after = (typeof i === 'undefined') ? 'hidden' : 'leak';");
  expectGlobalString(ctx, "after", "hidden");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* for 头 let/const：per-iteration 闭包绑定                                        */
/* -------------------------------------------------------------------------- */

TEST(BlockScope, ForLetClosurePerIteration) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* for (let i...) 闭包捕获各自迭代值（§13.7.4.8）。 */
  evalOk(ctx,
         "var fns = [];"
         "for (let i = 0; i < 3; i++) { fns.push(function() { return i; }); }"
         "globalThis.c0 = fns[0]();"
         "globalThis.c1 = fns[1]();"
         "globalThis.c2 = fns[2]();");
  expectGlobalNumber(ctx, "c0", 0);
  expectGlobalNumber(ctx, "c1", 1);
  expectGlobalNumber(ctx, "c2", 2);

  /* 循环结束后 i 不可见（块作用域）。 */
  evalOk(ctx,
         "{ let i = 7; }"
         "globalThis.after = (typeof i === 'undefined') ? 'hidden' : 'leak';");
  expectGlobalString(ctx, "after", "hidden");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(BlockScope, ForConstClosurePerIteration) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* for (const x of ...) 闭包捕获各自元素。 */
  evalOk(ctx,
         "var fns = [];"
         "for (const v of [5, 6, 7]) { fns.push(function() { return v; }); }"
         "globalThis.s0 = fns[0]();"
         "globalThis.s1 = fns[1]();"
         "globalThis.s2 = fns[2]();");
  expectGlobalNumber(ctx, "s0", 5);
  expectGlobalNumber(ctx, "s1", 6);
  expectGlobalNumber(ctx, "s2", 7);

  /* for (const i in ...) 键 per-iteration。 */
  evalOk(ctx,
         "var ks = [];"
         "for (const k in { a: 1, b: 2 }) { ks.push(function() { return k; }); }"
         "globalThis.k0 = ks[0]();"
         "globalThis.k1 = ks[1]();");
  expectGlobalString(ctx, "k0", "a");
  expectGlobalString(ctx, "k1", "b");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(BlockScope, ForOfLexicalLeft) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* for-of let 声明 + 解构 const left。 */
  evalOk(ctx,
         "var s = 0;"
         "for (let x of [1, 2, 3]) { s = s + x; }"
         "globalThis.sum = s;");
  expectGlobalNumber(ctx, "sum", 6);

  /* for-of 解构 const left：const 允许初始写入（BindingInitialization）。 */
  evalOk(ctx,
         "var s2 = 0;"
         "for (const { a } of [{ a: 1 }, { a: 2 }]) { s2 = s2 + a; }"
         "globalThis.dsum = s2;");
  expectGlobalNumber(ctx, "dsum", 3);

  /* for-of 解构 let 数组模式。 */
  evalOk(ctx,
         "var s3 = 0;"
         "for (let [x, y] of [[1, 2], [3, 4]]) { s3 = s3 + x + y; }"
         "globalThis.psum = s3;");
  expectGlobalNumber(ctx, "psum", 10);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(BlockScope, ForInLexicalLeft) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* for-in let 键。 */
  evalOk(ctx,
         "var n = 0;"
         "for (let k in { a: 1, b: 2, c: 3 }) { n = n + 1; }"
         "globalThis.n = n;");
  expectGlobalNumber(ctx, "n", 3);

  /* for-in 赋值到词法绑定（外层 let）。 */
  evalOk(ctx,
         "let k2 = '';"
         "for (k2 in { x: 1 }) { }"
         "globalThis.k2 = k2;");
  expectGlobalString(ctx, "k2", "x");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 顶层 let/const：不入全局对象（§13.3.2.4）                                        */
/* -------------------------------------------------------------------------- */

TEST(BlockScope, TopLevelLexicalNotGlobal) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 顶层 let/const 不入全局对象；var 仍入全局。 */
  evalOk(ctx,
         "let lg = 1;"
         "const cg = 2;"
         "var vg = 3;"
         "globalThis.letIsGlobal = ('lg' in globalThis);"
         "globalThis.constIsGlobal = ('cg' in globalThis);"
         "globalThis.varIsGlobal = ('vg' in globalThis);");
  expectGlobalBoolean(ctx, "letIsGlobal", false);
  expectGlobalBoolean(ctx, "constIsGlobal", false);
  expectGlobalBoolean(ctx, "varIsGlobal", true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(BlockScope, TopLevelLexicalReadAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 顶层 let 读取/赋值/闭包捕获。 */
  evalOk(ctx,
         "let g = 42;"
         "globalThis.g1 = g;"
         "g = 7;"
         "globalThis.g2 = g;"
         "globalThis.g3 = (function() { return g; })();");
  expectGlobalNumber(ctx, "g1", 42);
  expectGlobalNumber(ctx, "g2", 7);
  expectGlobalNumber(ctx, "g3", 7);

  /* 顶层 const 读取 + 赋值抛错。 */
  evalOk(ctx, "const c = 5; globalThis.c1 = c;");
  expectGlobalNumber(ctx, "c1", 5);
  evalExpectException(ctx, "const c2 = 5; c2 = 6;", "constant");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 重复声明与跨函数捕获                                                            */
/* -------------------------------------------------------------------------- */

TEST(BlockScope, RedeclarationErrors) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 同作用域 let 重复声明 -> SyntaxError。 */
  evalExpectException(ctx, "{ let a = 1; let a = 2; }", "already been declared");

  /* let 与 var 同作用域重名（块内）-> SyntaxError。 */
  evalExpectException(ctx, "{ let b = 1; var b; }", nullptr);

  /* 不同块作用域同名合法。 */
  evalOk(ctx,
         "{ let c = 1; }"
         "{ let c = 2; }"
         "globalThis.dup = 1;");
  expectGlobalNumber(ctx, "dup", 1);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(BlockScope, CrossFunctionCapture) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 闭包捕获外层块 let（块内定义函数读取块 let）。 */
  evalOk(ctx,
         "var get = null;"
         "{ let secret = 42; get = function() { return secret; }; }"
         "globalThis.secret = get();");
  expectGlobalNumber(ctx, "secret", 42);

  /* 闭包捕获 for-let（每次迭代独立值已覆盖）。 */

  /* 块内函数捕获块 let 且外层同名 var 被遮蔽。 */
  evalOk(ctx,
         "var val = 'var';"
         "var fn;"
         "{ let val = 'let'; fn = function() { return val; }; }"
         "globalThis.captured = fn();");
  expectGlobalString(ctx, "captured", "let");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 解构声明                                                                        */
/* -------------------------------------------------------------------------- */

TEST(BlockScope, DestructuringDeclaration) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* let 对象解构。 */
  evalOk(ctx,
         "let { p, q } = { p: 10, q: 20 };"
         "globalThis.p = p;"
         "globalThis.q = q;");
  expectGlobalNumber(ctx, "p", 10);
  expectGlobalNumber(ctx, "q", 20);

  /* const 数组解构。 */
  evalOk(ctx,
         "const [x, y] = [1, 2];"
         "globalThis.x = x;"
         "globalThis.y = y;");
  expectGlobalNumber(ctx, "x", 1);
  expectGlobalNumber(ctx, "y", 2);

  /* let 解构 + 默认值。 */
  evalOk(ctx,
         "let { m = 5 } = {};"
         "let [n = 6] = [];"
         "globalThis.m = m;"
         "globalThis.n = n;");
  expectGlobalNumber(ctx, "m", 5);
  expectGlobalNumber(ctx, "n", 6);

  /* const 解构声明后赋值 -> TypeError（const 语义）。 */
  evalOk(ctx,
         "let { c } = { c: 1 };"
         "globalThis.cInit = c;");
  expectGlobalNumber(ctx, "cInit", 1);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 复合：块 + 循环 + 闭包 + 异常                                                  */
/* -------------------------------------------------------------------------- */

TEST(BlockScope, Composite) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 块内 for-let 闭包 + 求和。 */
  evalOk(ctx,
         "var fns = [];"
         "for (let i = 0; i < 4; i++) {"
         "  let j = i * 10;"
         "  fns.push(function() { return i + j; });"
         "}"
         "globalThis.sum = fns[0]() + fns[1]() + fns[2]() + fns[3]();");
  expectGlobalNumber(ctx, "sum", 0 + 11 + 22 + 33);

  /* 块内 let 与 var 混用，异常路径清理块环境。 */
  evalOk(ctx,
         "var reached = 0;"
         "try {"
         "  let t = 1;"
         "  reached = reached + t;"
         "  throw new Error('x');"
         "} catch (e) {"
         "  let t = 2;"
         "  reached = reached + t;"
         "}"
         "globalThis.reached = reached;");
  expectGlobalNumber(ctx, "reached", 3);

  /* 块内 let + finally。 */
  evalOk(ctx,
         "var seq = '';"
         "try { let s = 'A'; seq = seq + s; }"
         "finally { seq = seq + 'F'; }"
         "globalThis.seq = seq;");
  expectGlobalString(ctx, "seq", "AF");

  /* if 子块 let 独立绑定。 */
  evalOk(ctx,
         "let r = 0;"
         "if (true) { let q = 7; r = q; }"
         "globalThis.r = r;");
  expectGlobalNumber(ctx, "r", 7);

  /* try/catch 各自块 let 独立（不冲突）。catch 块内 let t 遮蔽外层。 */
  evalOk(ctx,
         "var caughtT = 'no';"
         "try { let t = 1; throw 2; }"
         "catch (e) { let t = 9; caughtT = t; }"
         "globalThis.t = caughtT;");
  expectGlobalNumber(ctx, "t", 9);

  /* 块内 let 不泄漏为词法绑定：块外同名 typeof 返回 "undefined"（未声明）。 */
  evalOk(ctx,
         "{ let hidden2 = 1; }"
         "globalThis.leakCheck = (typeof hidden2 === 'undefined') ? 'hidden' : 'leak';");
  expectGlobalString(ctx, "leakCheck", "hidden");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
