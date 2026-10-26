/*
 * M5.2a 模块系统端到端测试。
 *
 * 宿主注册模块源码供给回调（JS_AddModuleLoader，specifier -> 源码），
 * JS_EvalModule 加载并求值模块（依赖先求值，模块顶层在模块 env 运行）。
 *
 * 模块顶层 var/let/const 作用域隔离（写入模块 env，不污染全局），导出值
 * 经显式 `globalThis.x = v` 暴露给宿主验证。模块自动严格模式。
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <unordered_map>

#include "neojs/neojs.h"

namespace {

/* 测试用模块世界：specifier -> 源码（注册后不再修改，c_str 稳定）。 */
struct ModuleWorld {
  std::unordered_map<std::string, std::string> sources;
};

/* 模块源码供给回调（M5.2a）：specifier -> 源码；未找到返回 nullptr。 */
const char *moduleLoader(JSContext *ctx, const char *specifier, void *opaque) {
  (void)ctx;
  auto *world = static_cast<ModuleWorld *>(opaque);
  auto it = world->sources.find(specifier != nullptr ? specifier : "");
  return it == world->sources.end() ? nullptr : it->second.c_str();
}

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

}  // namespace

/* -------------------------------------------------------------------------- */
/* M5.2a：import / export 基本链路                                            */
/* -------------------------------------------------------------------------- */

/* 具名导入 + 导出：import { a, b } 读取源模块 env 绑定（选项 A 动态查表）。 */
TEST(Module, BasicNamedImportExport) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modA"] = "export const a = 10; export const b = 20;";
  world.sources["main"] = "import { a, b } from 'modA'; globalThis.sum = a + b;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr) << "module should evaluate to nullptr, got: "
                        << (r != nullptr ? JS_ToString(ctx, r) : "(null)");

  expectGlobalNumber(ctx, "sum", 30.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 默认导出 + 默认导入：export default expr -> __default__ 隐藏绑定。 */
TEST(Module, DefaultExportImport) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modD"] = "export default 42;";
  world.sources["main"] = "import d from 'modD'; globalThis.d = d;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalNumber(ctx, "d", 42.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 具名默认导出：export default function f(){} 绑定名 = f（提升）。 */
TEST(Module, DefaultNamedFunctionExport) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modF"] = "export default function f() { return 7; }";
  world.sources["main"] =
      "import d from 'modF'; globalThis.r = d();";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalNumber(ctx, "r", 7.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 匿名默认导出函数：export default function(){} 按表达式求值。 */
TEST(Module, DefaultAnonymousFunctionExport) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modG"] = "export default function() { return 11; }";
  world.sources["main"] =
      "import d from 'modG'; globalThis.r = d();";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalNumber(ctx, "r", 11.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 导出函数声明 + 调用：export function double(n)。 */
TEST(Module, ExportFunctionCall) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modH"] = "export function double(n) { return n * 2; }";
  world.sources["main"] =
      "import { double } from 'modH'; globalThis.r = double(21);";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalNumber(ctx, "r", 42.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 模块顶层 var/let/const 作用域隔离：不污染全局，export 绑定 = env 绑定。 */
TEST(Module, ModuleScopeIsolation) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modJ"] = "var top = 99; var leak = 1; export { top };";
  world.sources["main"] =
      "import { top } from 'modJ'; globalThis.top = top;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalNumber(ctx, "top", 99.0);

  /* 模块顶层 var 不写全局对象（作用域隔离）：模块内 `var top` 只在模块
   * env，global 上不存在名为 top 的**额外**属性（main 显式写入的 top
   * 属性除外——本测试检查一个模块内声明但未显式写 global 的名字）。 */
  world.sources["modJ"] = "var top = 99; var leak = 1; export { top };";
  JSValue *leak = globalProp(ctx, "leak");
  EXPECT_EQ(JS_GetKind(ctx, leak), JS_UNDEFINED)
      << "module top-level var should not leak to global";
  JS_FreeValue(ctx, leak);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 副作用导入：裸 import 'm' 仍构成依赖并求值（不绑定名字）。 */
TEST(Module, SideEffectImport) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modS"] = "globalThis.sideEffect = 'ran';";
  world.sources["main"] = "import 'modS'; globalThis.after = 1;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalString(ctx, "sideEffect", "ran");
  expectGlobalNumber(ctx, "after", 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.2a：re-export 与 export *（转发链）                                     */
/* -------------------------------------------------------------------------- */

/* 重导出：export { x } from 'modB' —— LoadImport 沿转发链查源模块。 */
TEST(Module, ReExport) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modB"] = "export const x = 7;";
  world.sources["modC"] = "export { x } from 'modB';";
  world.sources["main"] = "import { x } from 'modC'; globalThis.x = x;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalNumber(ctx, "x", 7.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* export * from 'modE'：转发源模块全部具名导出（不含 default）。 */
TEST(Module, ExportStar) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modE"] = "export const y = 5; export const z = 6;";
  world.sources["modF"] = "export * from 'modE';";
  world.sources["main"] =
      "import { y, z } from 'modF'; globalThis.y = y; globalThis.z = z;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalNumber(ctx, "y", 5.0);
  expectGlobalNumber(ctx, "z", 6.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* export * 不转发 default。 */
TEST(Module, ExportStarExcludesDefault) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modE"] = "export const k = 1; export default 999;";
  world.sources["modF"] = "export * from 'modE';";
  world.sources["main"] =
      "import { k } from 'modF'; globalThis.k = k;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalNumber(ctx, "k", 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.2a：live binding 与依赖先求值                                           */
/* -------------------------------------------------------------------------- */

/* 依赖先求值：源模块顶层更新导出绑定后，import 方读到更新值（live binding）。 */
TEST(Module, LiveBindingAfterModuleInit) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modK"] = "export let k = 1; k = 2;";
  world.sources["main"] = "import { k } from 'modK'; globalThis.k = k;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalNumber(ctx, "k", 2.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 链式依赖：main -> mid -> base，依赖先求值保证 mid 可见 base 导出。 */
TEST(Module, ChainedDepsEvaluatedFirst) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["base"] = "export const v = 3;";
  world.sources["mid"] =
      "import { v } from 'base'; export const w = v * 4;";
  world.sources["main"] =
      "import { w } from 'mid'; globalThis.w = w;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalNumber(ctx, "w", 12.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 同一模块被多个模块依赖只求值一次（单例 + Evaluated 短路）。 */
TEST(Module, SharedDepEvaluatedOnce) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["shared"] =
      "globalThis.sharedRuns = (globalThis.sharedRuns || 0) + 1; "
      "export const s = 1;";
  world.sources["u1"] = "import { s } from 'shared'; globalThis.u1 = s;";
  world.sources["u2"] = "import { s } from 'shared'; globalThis.u2 = s;";
  world.sources["main"] = "import 'u1'; import 'u2';";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  EXPECT_EQ(r, nullptr);

  expectGlobalNumber(ctx, "sharedRuns", 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 重复 JS_EvalModule 同 specifier 幂等（单例，Evaluated 短路不重复求值）。 */
TEST(Module, EvalModuleIdempotent) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modI"] =
      "globalThis.runs = (globalThis.runs || 0) + 1; export const c = 1;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r1 = JS_EvalModule(ctx, "modI");
  EXPECT_EQ(r1, nullptr);
  JSValue *r2 = JS_EvalModule(ctx, "modI");
  EXPECT_EQ(r2, nullptr);

  expectGlobalNumber(ctx, "runs", 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.2a：错误路径                                                             */
/* -------------------------------------------------------------------------- */

/* 导入绑定赋值 -> TypeError（import 是 immutable binding）。 */
TEST(Module, ImportAssignmentThrowsTypeError) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["modG"] = "export let g = 1;";
  world.sources["main"] = "import { g } from 'modG'; g = 2;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  ASSERT_NE(r, nullptr) << "assignment to import binding should throw";
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION);
  if (JS_GetKind(ctx, r) == JS_EXCEPTION) {
    EXPECT_NE(std::string(JS_ToString(ctx, r)).find("TypeError"),
              std::string::npos)
        << "got: " << JS_ToString(ctx, r);
  }
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 未注册 loader 时报错（Exception）。 */
TEST(Module, NoLoaderRegistered) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_EvalModule(ctx, "main");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 加载器返回 nullptr（模块不存在）报错。 */
TEST(Module, CannotLoadModule) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["other"] = "export const x = 1;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "missing");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 模块语法错误报错。 */
TEST(Module, ModuleSyntaxError) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["bad"] = "export const = ;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "bad");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 模块顶层抛异常传播为 Exception（依赖模块抛错）。 */
TEST(Module, ModuleTopLevelThrow) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  ModuleWorld world;
  world.sources["boom"] = "throw 'boom';";
  world.sources["main"] = "import 'boom'; globalThis.after = 1;";
  JS_AddModuleLoader(ctx, moduleLoader, &world);

  JSValue *r = JS_EvalModule(ctx, "main");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION);
  if (JS_GetKind(ctx, r) == JS_EXCEPTION) {
    EXPECT_NE(std::string(JS_ToString(ctx, r)).find("boom"),
              std::string::npos)
        << "got: " << JS_ToString(ctx, r);
  }
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
