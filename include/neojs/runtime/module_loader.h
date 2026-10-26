/*
 * 内部 C++ 实现：模块加载器（M5.2a）。
 *
 * 架构（docs/architecture.md §8 B4）：JSContext 注册模块源码供给回调
 * （宿主提供 specifier -> 源码），加载器内部按需加载/求值。
 *
 * 流程（ECMA-262 §16.3 模块求值，M5.2a 简化为「依赖先求值」两阶段）：
 *   loadModule(ctx, specifier)   —— 递归加载：查模块表（单例）-> 回调取
 *     源码 -> parse(module=true) -> compileModule -> 先加载并求值依赖。
 *   evaluateModule(ctx, record)  —— 执行模块求值函数（Module Function），
 *     顶层代码在模块 env 中运行（作用域隔离，闭包捕获天然工作）。
 *
 * 依赖先求值（§16.3.1.1 步骤 4-6）：递归加载依赖，再按序求值。同一模块
 * 被多个模块依赖时只求值一次（模块表单例 + Evaluated 状态短路）。
 *
 * M5.2b 扩展点：循环依赖经状态机（Evaluating）处理，Live binding 由
 * LoadImport 运行期动态查源模块 env 保证（选项 A，本阶段即生效）。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_MODULE_LOADER_H
#define NEOJS_RUNTIME_MODULE_LOADER_H

#include <string>

#include "neojs/runtime/conversion.h"

namespace neojs {

class JSContext;
class JSModuleRecord;

/* 加载并求值模块（M5.2a）：
 *   1. 查模块表：已加载且 Evaluated -> 直接返回（单例）。
 *   2. 未加载：模块源码供给回调取源码 -> parse(module=true) ->
 *      compileModule -> 递归加载并求值依赖（先依赖后自身）。
 *   3. 执行模块求值函数（顶层代码在模块 env 运行）。
 * 返回 ConversionResult：成功 value=nullptr（模块无 completion value），
 * 失败 value=nullptr + error/thrown。 */
ConversionResult loadAndEvaluateModule(JSContext *ctx, const std::string &specifier);

/* 加载模块记录（递归，不执行）：查表/取源码/parse/compile，注册依赖。
 * 供 loadAndEvaluateModule 与 M5.2b 链接阶段复用。
 * 返回 nullptr 时 error 已填。 */
JSModuleRecord *loadModule(JSContext *ctx, const std::string &specifier,
                           std::string *error);

/* 执行模块求值函数（顶层代码在模块 env 中运行）。
 * 状态置 Evaluating（M5.2b 循环依赖）-> 执行 -> Evaluated。
 * 返回 ConversionResult（模块求值失败时异常）。 */
ConversionResult evaluateModule(JSContext *ctx, JSModuleRecord *record);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_MODULE_LOADER_H
