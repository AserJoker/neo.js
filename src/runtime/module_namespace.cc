/*
 * 内部 C++ 实现：模块 namespace 对象 + 全局动态 import() 内置（M5.2c-2）。
 *
 * 遵循 docs/architecture.md §8 B4（模块/宿主）与 ECMA-262：
 *   - §16.2.1.8  import(specifier)：编译为对全局内置函数 `import` 的调用。
 *   - §10.4.6    Module Namespace Exotic Objects：namespace 对象是模块导出
 *     的镜像——export_name -> 值（含 default）。本阶段用普通对象承载
 *     （属性不可配置/不可写/不可枚举的严格 exotic 语义留 M5.7/后续）。
 *   - §16.2.1.8  动态 import 返回 Promise：模块加载求值成功 resolve 为
 *     namespace 对象，失败 reject 为错误值。求值同步完成，resolve/reject
 *     经 Promise 决议闭包 + 微任务调度保证异步投递。
 *
 * 设计：
 *   - dynamicImport（全局 `import` 内置，Builtin 类别）：参数 specifier
 *     ToString -> loadAndEvaluateModule（依赖先求值 + 本模块执行）-> 成功
 *     遍历 record->exports() 收集导出镜像（findExport 解析 re-export/export *
 *     转发链到最终模块 env 读 live binding）-> 创建 Promise + 立即 resolve；
 *     失败创建 Promise + 立即 reject。
 *   - 复用 Promise 基础设施（createDerivedPromise / 决议闭包），保证
 *     import() 返回值与 Promise.resolve 同构（then/catch/await 可用）。
 *
 * GC 注意：namespace 对象经 promise result 持有（Promise 对象 gcMarkRefs
 * 标记 promise_result_），Promise 经返回值/微任务队列根集可达。
 */

#include "neojs/runtime/module_namespace.h"

#include <string>
#include <utility>

#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/environment.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/module.h"
#include "neojs/runtime/module_loader.h"
#include "neojs/runtime/object.h"
#include "neojs/runtime/promise.h"
#include "neojs/runtime/value.h"

namespace neojs {

namespace {

ConversionResult ok(JSValue *v) {
  ConversionResult r;
  r.value = v;
  return r;
}

/* 创建内置函数（Builtin 类别）：JSFunction + 包装 JSValue + 反向指针。
 * 原型链 -> Function.prototype（惰性创建缓存）。各模块匿名命名空间独立
 * 定义（builtins.cc/promise.cc 同模式）。 */
JSValue *makeBuiltin(JSContext *ctx, JSBuiltinFn fn, ThisMode mode) {
  JSObject *fproto = nullptr;
  JSValue *fp = functionProto(ctx);
  if (fp != nullptr) fproto = fp->object();
  JSFunction *f = ctx->heap().allocGCObject<JSFunction>(fproto, fn, mode);
  ctx->heap().pin(f); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(f);
  f->setHolder(v);
  ctx->heap().unpin(f);
  return v;
}

/* 取 Object.prototype 原型（模块 namespace 对象的原型链根）。 */
JSObject *objectProtoOf(JSContext *ctx) {
  JSValue *op = objectProto(ctx);
  return op != nullptr ? op->object() : nullptr;
}

/* 解析导出名到最终模块 + 本地绑定名，读取 live binding 值。
 * 返回 true 且 *out != nullptr 表示成功；false = 导出不存在（或值未定，
 * 如模块尚未求值）。 */
bool readExportValue(JSContext *ctx, JSModuleRecord *record,
                     const std::string &export_name, JSValue **out) {
  (void)ctx; /* 当前未使用：属性读取/绑定查找不经 ctx */
  if (out != nullptr) *out = nullptr;
  if (record == nullptr) return false;
  std::string local_name;
  JSModuleRecord *resolved = nullptr;
  if (!record->findExport(export_name, &local_name, &resolved)) return false;
  if (resolved == nullptr || resolved->env() == nullptr) return false;
  JSValue *v = resolved->env()->getBinding(local_name);
  if (v == nullptr) return false;
  if (out != nullptr) *out = v;
  return true;
}

}  // namespace

/* 模块 namespace 对象（M5.2c-2）：模块导出镜像。
 * 遍历 record->exports()，对每条本地导出/re-export/export * 条目解析出
 * 实际导出名 + 值，挂到 namespace 对象。export * from 'm'（export_name 空）
 * 转发源模块全部具名导出（不含 default，§16.4.6）。 */
JSValue *createModuleNamespace(JSContext *ctx, JSModuleRecord *record) {
  if (record == nullptr) return nullptr;
  JSValue *ns = ctx->createObject(objectProtoOf(ctx));
  ctx->heap().pin(ns); /* 分配保护（M3.3）：属性挂载前防误回收 */
  for (const ExportBinding &eb : record->exports()) {
    if (eb.export_name.empty() && eb.from != nullptr) {
      /* export * from 'm'：转发源模块全部具名导出（不含 default）。 */
      for (const ExportBinding &seb : eb.from->exports()) {
        if (seb.export_name.empty() || seb.export_name == "default") continue;
        JSValue *v = nullptr;
        if (readExportValue(ctx, eb.from, seb.export_name, &v) && v != nullptr) {
          ns->object()->setProperty(seb.export_name, v);
        }
      }
      continue;
    }
    if (eb.export_name.empty()) continue; /* 非 export_all 的空名防御 */
    JSValue *v = nullptr;
    if (readExportValue(ctx, record, eb.export_name, &v) && v != nullptr) {
      ns->object()->setProperty(eb.export_name, v);
    }
  }
  ctx->heap().unpin(ns);
  return ns;
}

/* 全局动态 import() 内置（M5.2c-2，§16.2.1.8）：
 *   import(specifier) -> Promise
 * 步骤：
 *   1. specifier 参数 ToString（缺省 -> "undefined"）。
 *   2. loadAndEvaluateModule：加载（parse/compile/依赖）+ 求值（模块顶层在
 *      模块 env 运行）。成功无 completion value；失败 error/thrown 非空。
 *   3. 成功：创建 namespace 对象（导出镜像），创建 Promise + resolve(ns)。
 *      失败：创建 Promise + reject(错误值)（JS throw 值优先，内部错误转
 *      Exception）。
 * 求值同步完成，resolve/reject 经 Promise 决议闭包 + 微任务投递。 */
ConversionResult dynamicImport(JSContext *ctx, JSValue *, JSValue *const *args,
                               size_t argc) {
  JSValue *spec_arg = argc > 0 ? args[0] : ctx->createValue(Kind::Undefined);
  ConversionResult spec_r = toString(ctx, spec_arg);
  if (!spec_r.ok()) {
    /* ToString 失败（如 Symbol）：直接 reject 该错误。 */
    JSValue *resolve = nullptr, *reject = nullptr;
    JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
    ctx->heap().pin(p); /* 分配保护：reject 前防误回收 */
    JSValue *err = ctx->createValue(Kind::Exception);
    err->setErrorInfo(spec_r.error);
    JSValue *argv[1] = {err};
    ConversionResult r =
        call(ctx, reject, ctx->createValue(Kind::Undefined), argv, 1);
    ctx->heap().unpin(p);
    if (!r.ok()) return r;
    return ok(p);
  }

  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：resolve/reject 前防误回收 */

  ConversionResult load_r = loadAndEvaluateModule(ctx, spec_r.value->toString());
  if (load_r.error.empty() && load_r.thrown == nullptr) {
    /* 成功：resolve(namespace)。 */
    JSModuleRecord *record = ctx->findModule(spec_r.value->toString());
    JSValue *ns = createModuleNamespace(ctx, record);
    if (ns == nullptr) ns = ctx->createObject(objectProtoOf(ctx)); /* 防御 */
    JSValue *argv[1] = {ns};
    ConversionResult r =
        call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
    ctx->heap().unpin(p);
    if (!r.ok()) return r;
    return ok(p);
  }

  /* 失败：reject(错误值)。JS throw 值优先用其字符串表示（toString 失败则
   * 退化为 Exception 值）。 */
  JSValue *err = nullptr;
  if (load_r.thrown != nullptr) {
    ConversionResult ts = toString(ctx, load_r.thrown);
    if (ts.ok()) {
      err = ctx->createValue(Kind::Exception);
      err->setErrorInfo(ts.value->toString());
    } else {
      err = ctx->createValue(Kind::Exception);
      err->setErrorInfo(ts.error);
    }
  } else {
    err = ctx->createValue(Kind::Exception);
    err->setErrorInfo(load_r.error);
  }
  JSValue *argv[1] = {err};
  ConversionResult r = call(ctx, reject, ctx->createValue(Kind::Undefined), argv, 1);
  ctx->heap().unpin(p);
  if (!r.ok()) return r;
  return ok(p);
}

/* 注册全局动态 import 内置（installBuiltins 调用）：
 *   globalThis.import = dynamicImport（Builtin，严格模式）。
 * 注意：`import` 是保留字，无法用 JS 源码声明，只能宿主注册。 */
void installModuleNamespaceBuiltins(JSContext *ctx) {
  JSValue *global = ctx->globalObject();
  JSValue *fn = makeBuiltin(ctx, dynamicImport, ThisMode::Strict);
  ctx->heap().pin(fn); /* 分配保护：挂 global 前防误回收 */
  global->object()->setProperty("import", fn);
  ctx->heap().unpin(fn);
}

}  // namespace neojs
