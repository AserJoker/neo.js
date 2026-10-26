/*
 * 内部 C++ 实现：模块 namespace 对象 + 全局动态 import() 内置（M5.2c-2）。
 *
 * 架构（docs/architecture.md §8 B4）：import(specifier) 编译为对全局内置
 * 函数 `import` 的调用；内置返回 Promise，模块求值成功 resolve 为 namespace
 * 对象（模块导出镜像，§10.4.6），失败 reject。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_MODULE_NAMESPACE_H
#define NEOJS_RUNTIME_MODULE_NAMESPACE_H

#include "neojs/runtime/conversion.h"

namespace neojs {

class JSContext;
class JSModuleRecord;
class JSValue;

/* 模块 namespace 对象（§10.4.6 Module Namespace Exotic Objects）：
 * 模块导出镜像——遍历 record->exports()，把 export_name -> live binding 值
 * 挂到普通对象（含 default 与 re-export/export * 转发）。 */
JSValue *createModuleNamespace(JSContext *ctx, JSModuleRecord *record);

/* 全局动态 import() 内置（§16.2.1.8）：import(specifier) -> Promise。 */
ConversionResult dynamicImport(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *args, size_t argc);

/* 注册全局 import 内置到 globalThis（installBuiltins 调用）。 */
void installModuleNamespaceBuiltins(JSContext *ctx);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_MODULE_NAMESPACE_H
