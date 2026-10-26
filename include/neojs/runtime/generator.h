/*
 * 内部 C++ 实现：generator 运行时（M5.5a-3，§27.5）。
 *
 * 复用 AsyncFrame 作通用挂起帧（用户确认设计：Generator 内嵌 AsyncFrame 指针）。
 *   - generatorCall：function* 调用入口。创建 Generator 对象（state=
 *     suspended-start），不立即执行（ES §27.5.5.1 GeneratorStart：g() 返回
 *     Generator，首次 next() 才执行函数体到首个 yield）。返回 Generator。
 *   - op_Yield 挂起：保存 VM 现场到 AsyncFrame（fn/pc/stack/env/this/pending），
 *     置 frame.suspended 结束本帧 runBytecode；resumeGenerator 检测后包装
 *     {value, done:false} 返回给 next() 调用方，置 state=suspended-yield。
 *   - resumeGenerator(next/throw/return)：从 AsyncFrame 重建现场续延到下一个
 *     yield 或函数完成；完成时 state=completed，next() 返回 {value:retval,done:true}。
 *
 * 与 async 的差异：
 *   - 挂起是同步的（不入队微任务，直接返回给 next 调用方）；
 *   - 恢复由 next/throw/return 显式触发（async 由 promise settle 触发）；
 *   - Generator 持 AsyncFrame 而非 promise；无隐藏绑定。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_GENERATOR_H
#define NEOJS_RUNTIME_GENERATOR_H

#include <cstddef>
#include <vector>

#include "neojs/runtime/conversion.h"

namespace neojs {

class JSEnvironment;
class JSFunction;
class JSValue;

/* Generator 状态（§27.5.5 [[GeneratorState]]，整型存储于 JSObject::gen_state_）。
 * 0 suspended-start：刚创建，未执行（首次 next 触发执行）
 * 1 suspended-yield：执行到 yield 挂起，可被 next/throw/return 恢复
 * 2 running        ：正在执行（防重入：next 期间再 next 抛 TypeError）
 * 3 completed      ：函数已返回或抛错，后续 next 返回 {value:undefined,done:true} */
enum class GeneratorState : int {
  SuspendedStart = 0,
  SuspendedYield = 1,
  Running = 2,
  Completed = 3,
};

/* 创建 generator 调用：新建 Generator 对象（state=suspended-start），持有 fn/
 * thisVal/args/env 用于首次 next() 触发执行。返回 Generator JSValue（§27.5.5.1
 * GeneratorStart 近似——不立即执行）。
 *   fn      为 generator Script 函数（bc.is_generator=true）
 *   thisVal 已解析的 this
 *   args/argc 调用参数（首次 next 执行时按名写入帧环境） */
ConversionResult generatorCall(JSContext *ctx, JSFunction *fn, JSValue *thisVal,
                               JSValue *const *args, size_t argc);

/* 恢复 Generator 执行（Generator.prototype.next/throw/return 内部入口）。
 *   gen_v     Generator JSValue
 *   value     恢复值（next：压栈；throw：异常注入；return：强制完成返回值）
 *   mode      0 next / 1 throw / 2 return
 * 返回 {value, done} 包装对象的 JSValue，或失败 ConversionResult。 */
ConversionResult resumeGenerator(JSContext *ctx, JSValue *gen_v,
                                 JSValue *value, int mode);

/* Generator.prototype.next(value) / throw(exception) / return(value) 实现。
 * mode: 0 next / 1 throw / 2 return。
 * 返回 ConversionResult：成功 value={value,done} 对象；失败（throw 注入抛出
 * 且未被捕、completed 状态 throw 等）走 thrown/error 路径，由 builtin 直接
 * 返回给 call()，经 op_Call 异常传播到 try/catch。 */
ConversionResult generatorProtoNext(JSContext *ctx, JSValue *self,
                                    JSValue *const *args, size_t argc, int mode);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_GENERATOR_H
