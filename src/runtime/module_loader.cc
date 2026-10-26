#include "neojs/runtime/module_loader.h"

#include <utility>

#include "neojs/compiler/compiler.h"
#include "neojs/frontend/frontend.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/environment.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/module.h"
#include "neojs/runtime/vm.h"

namespace neojs {

namespace {

ConversionResult fail(const std::string &error) {
  ConversionResult r;
  r.error = error;
  return r;
}

ConversionResult ok(JSValue *v) {
  ConversionResult r;
  r.value = v;
  return r;
}

/* 判断 ConversionResult 是否失败（error 非空或 JS throw）。
 * 注意：不能用 ok()（value != nullptr）判断——模块求值成功返回
 * ok(nullptr)（模块无 completion value），value==nullptr 是成功。 */
bool isFailure(const ConversionResult &r) {
  return !r.error.empty() || r.thrown != nullptr;
}

}  // namespace

/* 加载模块记录（递归，不执行）：查表/取源码/parse/compile/注册依赖。
 * 返回 nullptr 时 error 已填。 */
JSModuleRecord *loadModule(JSContext *ctx, const std::string &specifier,
                           std::string *error) {
  /* 1. 查模块表：已加载（含正在求值的循环依赖）直接返回单例。 */
  JSModuleRecord *existing = ctx->findModule(specifier);
  if (existing != nullptr) return existing;

  /* 2. 模块源码供给回调：宿主提供 specifier -> 源码。 */
  JSModuleLoadFunc loader = ctx->moduleLoader();
  if (loader == nullptr) {
    if (error != nullptr)
      *error = "Error: no module loader registered (JS_AddModuleLoader)";
    return nullptr;
  }
  const char *source = loader(ctx, specifier.c_str(), ctx->moduleLoaderOpaque());
  if (source == nullptr) {
    if (error != nullptr)
      *error = "Error: cannot load module '" + specifier + "'";
    return nullptr;
  }

  /* 3. 解析（module=true：自动严格模式 + import/export 语法）。 */
  DiagnosticSink sink;
  AST *ast = parseSource(&ctx->allocator(), source,
                         /*源码长度：宿主返回 C 串，用 strlen（memory 经验：
                          * 硬编码长度易错）。*/
                         std::char_traits<char>::length(source), sink,
                         /*module=*/true);
  if (ast == nullptr) {
    if (error != nullptr) {
      *error = "SyntaxError: ";
      *error += sink.hasErrors() ? sink.diagnostics().front().message
                                 : "module parse failed";
    }
    return nullptr;
  }

  /* 4. 编译模块求值函数（Module Function，顶层代码在模块 env 运行）。 */
  Compiler compiler;
  CompileResult cr = compiler.compileModule(*ast);
  ctx->allocator().destroy(ast);
  if (!cr.ok) {
    if (error != nullptr) *error = "CompileError: " + cr.error;
    return nullptr;
  }

  /* 5. 创建模块记录 + 模块环境（parent=nullptr，顶层作用域隔离）。 */
  JSModuleRecord *record = ctx->heap().allocGCObject<JSModuleRecord>(specifier);
  ctx->heap().pin(record); /* 两段式创建保护：入模块表前防误回收 */
  JSEnvironment *module_env =
      ctx->heap().allocGCObject<JSEnvironment>(nullptr);
  record->setEnv(module_env);
  record->setFunctionBytecode(std::move(cr.fn));
  ctx->addModule(record); /* 入表（单例），此时才受根集保护 */

  /* 6. 递归加载依赖（§16.3.1.1 步骤 4-6：依赖先加载/求值）。
   *    注意：先入表再加载依赖——循环依赖中依赖方回查本模块时能拿到记录。 */
  for (const std::string &dep_spec : cr.module_deps) {
    if (dep_spec == specifier) continue; /* 自引用（M5.2b 细化） */
    std::string dep_error;
    JSModuleRecord *dep = loadModule(ctx, dep_spec, &dep_error);
    if (dep == nullptr) {
      if (error != nullptr) *error = dep_error;
      ctx->heap().unpin(record);
      return nullptr;
    }
    record->addDep(dep);
  }

  /* 7. 回填导出绑定表（编译期收集的 module_exports -> record->exports_）。
   *    - 本地导出（from_specifier 空）：export_name -> local_name。
   *    - re-export（from_specifier 非空）：export_name 转发自源模块记录。
   *    - export_all：export * from 'm'（运行期 findExport 沿链查源全表）。 */
  for (const ModuleExportEntry &e : cr.module_exports) {
    ExportBinding eb;
    eb.export_name = e.export_name;
    eb.local_name = e.local_name;
    if (e.export_all) {
      eb.export_name.clear(); /* export_all 标记：转发源模块全部具名导出 */
      eb.local_name.clear();
    }
    if (!e.from_specifier.empty()) {
      eb.from = ctx->findModule(e.from_specifier);
    }
    record->addExport(std::move(eb));
  }

  ctx->heap().unpin(record);
  return record;
}

/* 执行模块求值函数（顶层代码在模块 env 中运行）。
 * 状态机（§16.3.1.1，M5.2b）：Uninstantiated -> Evaluating -> Evaluated。
 *   - 进入即置 Evaluating：循环依赖（a -> b -> a）中，b 求值递归回 a 时
 *     看到 a 已是 Evaluating，直接短路返回（不重复执行）——依赖先求值
 *     的 DFS 在环上因此安全收敛。
 *   - 依赖先求值（§16.3.1.1 步骤 4-6）：链式依赖逐层展开；同一依赖被
 *     多个模块引用时 Evaluated 短路保证只求值一次。
 *   - 求值失败（依赖或本模块抛错）：置 Evaluated 表示已尝试过，避免后续
 *     JS_EvalModule 重入时卡在 Evaluating 短路返回（吞掉错误）。 */
ConversionResult evaluateModule(JSContext *ctx, JSModuleRecord *record) {
  if (record == nullptr) return fail("Error: null module record");

  /* 已求值 / 正在求值（循环依赖回查）短路。 */
  if (record->status() == ModuleStatus::Evaluated ||
      record->status() == ModuleStatus::Evaluating) {
    return ok(nullptr);
  }

  /* 进入求值：置 Evaluating，循环依赖回查时短路。 */
  record->setStatus(ModuleStatus::Evaluating);

  /* M5.2c：预置 import.meta 对象到模块 env 的隐藏绑定（惰性创建，含
   * url=specifier）。LoadImportMeta 沿 env 链查找（模块内嵌套函数闭包
   * 的 env 链最终指向本 env）。绑定值同时受 record->import_meta_ 与
   * env bindings 双引用保护（GC 幂等）。 */
  JSValue *meta = record->importMeta(ctx);
  if (meta != nullptr) record->env()->setBinding("__import_meta__", meta);

  /* 先递归求值依赖（依赖先求值，§16.3.1.1 步骤 4-6）。 */
  for (JSModuleRecord *dep : record->deps()) {
    if (dep == nullptr) continue;
    ConversionResult dr = evaluateModule(ctx, dep);
    if (isFailure(dr)) {
      record->setStatus(ModuleStatus::Evaluated); /* 失败：标记已尝试 */
      return dr;
    }
  }
  record->setStatus(ModuleStatus::Evaluated);

  const FunctionBytecode &fn = record->bytecode();
  /* 模块顶层代码在模块 env 中运行（与 JS_Eval 顶层同机制）：
   * LoadVar/StoreVar 读写模块 env 绑定，闭包沿 env 链捕获模块顶层变量。 */
  ConversionResult r = runBytecode(ctx, fn, nullptr, nullptr, 0, record->env());
  if (isFailure(r)) record->setStatus(ModuleStatus::Evaluated); /* 失败标记 */
  return r;
}

/* 加载并求值模块（M5.2a）：依赖先求值 + 本模块执行。
 * 返回 ConversionResult：成功 value=nullptr（模块无 completion value），
 * 失败 error/thrown 非空。调用方用 error/thrown 判断失败（勿用 ok()）。 */
ConversionResult loadAndEvaluateModule(JSContext *ctx,
                                       const std::string &specifier) {
  std::string error;
  JSModuleRecord *record = loadModule(ctx, specifier, &error);
  if (record == nullptr) return fail(error);

  /* 递归加载并求值依赖（依赖先求值，§16.3.1.1 步骤 4-6）。
   * 递归深度：deps 里再求值各自的 deps（链式依赖），每个模块 Evaluated
   * 短路保证只求值一次（单例）。 */
  ConversionResult dep_r = evaluateModule(ctx, record);
  if (isFailure(dep_r)) return dep_r;
  return ok(nullptr);
}

}  // namespace neojs
