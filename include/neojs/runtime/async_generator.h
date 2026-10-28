/*
 * 内部 C++ 实现：AsyncGenerator 运行时（M5.5c-1，§27.6）。
 *
 * AsyncGenerator 是 async 与 generator 的交集：
 *   - 调用返回 AsyncGenerator 对象（state=suspended-start），不立即执行；
 *   - next/throw/return 返回 Promise（异步迭代器协议，§27.6.3.1/2/3），
 *     经 [[AsyncGeneratorQueue]] 请求队列逐个驱动执行（§27.6.3 AsyncGeneratorResume）；
 *   - yield 值是 AsyncYield：先 await yield 值，再挂起返回 {value,done:false} 的
 *     promise（§27.6.3.4 AsyncGeneratorYield）；
 *   - 函数完成/异常：resolve/reject 下一个请求的 promise（§27.6.3.3 step 9-10）。
 *
 * 复用 Generator 的现场机制：
 *   - AsyncGenerator 内嵌 AsyncFrame（gen_frame_ 槽，[[AsyncGeneratorContext]]），
 *     挂起时保存 VM 现场（pc/stack/env/this/pending）；
 *   - 状态复用 gen_state_（0 suspended-start / 1 suspended-yield / 2 executing
 *     / 3 completed，§27.6.2 AsyncGeneratorState）。
 *
 * 与同步 Generator 的差异：
 *   - 挂起是异步的：yield 挂起后，next() 返回的 promise 由请求队列持有，
 *     下一个请求入队后才经 AsyncGeneratorResume 恢复执行；
 *   - next/throw/return 不直接返回 {value,done}，而是返回 promise
 *     （resolve 为 {value,done} 对象，reject 为异常）。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_ASYNC_GENERATOR_H
#define NEOJS_RUNTIME_ASYNC_GENERATOR_H

#include <cstddef>
#include <vector>

#include "neojs/runtime/async.h"
#include "neojs/runtime/conversion.h"

namespace neojs {

class JSContext;
class JSFunction;
class JSValue;
class JSEnvironment;
class AsyncFrame;

/* AsyncGenerator 状态（§27.6.2 AsyncGeneratorState，整型存 gen_state_）。
 * 0 suspended-start：刚创建，未执行（首次 next 触发执行）
 * 1 suspended-yield：执行到 yield 挂起，等待下一个请求
 * 2 executing       ：正在执行（防重入：执行期间新请求入队）
 * 3 completed       ：函数已返回或抛错，后续 next 返回 {value:undefined,done:true} */
enum class AsyncGeneratorState : int {
  SuspendedStart = 0,
  SuspendedYield = 1,
  Executing = 2,
  Completed = 3,
};

/* 创建 async generator 调用（function.cc call() 分发：bc.is_async &&
 * bc.is_generator 时走此路径）：新建 AsyncGenerator 对象（state=
 * suspended-start），持有 fn/thisVal/args/env 用于首次 next() 触发执行
 * （§27.6.1.1 AsyncGeneratorStart 近似——不立即执行）。返回 AsyncGenerator
 * JSValue。 */
ConversionResult asyncGeneratorCall(JSContext *ctx, JSFunction *fn,
                                    JSValue *thisVal, JSValue *const *args,
                                    size_t argc);

/* AsyncGenerator.prototype.next(value) / throw(exception) / return(value)。
 * mode: 0 next / 1 throw / 2 return。
 * 返回 promise：入队 AsyncGeneratorRequest，队列空且状态非 executing 时
 * 立即驱动执行；否则等待前序请求完成（§27.6.3.1/2/3）。 */
ConversionResult asyncGeneratorProtoNext(JSContext *ctx, JSValue *self,
                                         JSValue *const *args, size_t argc,
                                         int mode);

/* AsyncGenerator.prototype[Symbol.asyncIterator]：返回 this（§27.6.3.4）。 */
ConversionResult asyncGeneratorSelfIterator(JSContext *ctx, JSValue *thisVal,
                                            JSValue *const *args, size_t argc);

/* AsyncYield 挂起（op_Yield 在 async generator 上下文调用，§27.6.3.4
 * AsyncGeneratorYield）：
 *   - resolve 当前请求（frame->async_request）的 capability {value,done:false}；
 *   - 保存 VM 现场到 AsyncGenerator 内嵌的 AsyncFrame（与 suspendYield 同构）；
 *   - 置 AsyncGeneratorState=SuspendedYield。
 * 返回 true 挂起成功（调用方置 frame.suspended 结束本帧）。
 * 注意：yield 值已由编译器在 yield 前插 Await 指令 await 过（AsyncYield 语义）。 */
bool suspendAsyncYield(JSContext *ctx, JSValue *gen_v, JSFunction *fn,
                       size_t next_pc, std::vector<JSValue *> &stack,
                       JSEnvironment *env, JSValue *frame_this,
                       const ConversionResult &pending, bool has_pending,
                       uint32_t pending_handle, bool pending_is_return,
                       JSValue *yield_val);

/* resumeAsyncFrame（await 续延）在 async generator 上下文挂起后调用：
 * 若 AsyncGenerator 处于 SuspendedYield（op_Yield 挂起，请求已结算），清
 * frame->async_request 并驱动队列中下一个请求；若 Executing（await 挂起，
 * async_request 已由 suspendAwait 传播到新帧）则不动。 */
void asyncGeneratorAfterSuspend(JSContext *ctx, AsyncFrame *frame);

/* resumeAsyncFrame（await 续延）在 async generator 上下文完成/异常时调用：
 * 置 Completed，结算当前请求 capability（ok→resolve {value,done:true}，
 * 异常→reject），并结算队列中全部剩余请求（§27.6.3.5 AsyncGeneratorComplete）。
 * 返回原 ConversionResult（调用方丢弃——错误已进请求 promise，不冒泡）。 */
ConversionResult asyncGeneratorFrameSettled(JSContext *ctx, AsyncFrame *frame,
                                            ConversionResult result);

/* M5.5e async generator yield* 委托续延（AsyncDelegateResolve/Reject 调用）：
 * inner 方法结果 promise settle 后恢复委托状态机（非字节码续延）。
 *   result   resolve 时 iter_result（对象）；reject 时 rejection 值
 *   is_reject 是否 reject 分支
 * 按 frame->delegate_phase 分派：done→续延外层字节码（mode 0/2）、
 * done=false→AsyncGeneratorYield（先 await 产出值）、reject→异常注入外层。 */
ConversionResult asyncDelegateResume(JSContext *ctx, AsyncFrame *frame,
                                     JSValue *result, bool is_reject);

/* M5.5e op_AsyncYieldDelegate 调用的委托原语（vm.cc 使用）。 */
/* PromiseResolve 接合（x 是 Promise 直接用；否则包一层）。 */
JSValue *delegatePromiseResolve(JSContext *ctx, JSValue *x);
/* 读迭代结果 done（无 done 视作 false）。 */
bool delegateIterResultDone(JSValue *iter_result);
/* 读迭代结果 value（缺省 undefined）。 */
JSValue *delegateIterResultValue(JSContext *ctx, JSValue *iter_result);
/* 保存 VM 现场到 AsyncFrame（委托挂起基点）。 */
void saveAsyncFrameForDelegate(JSContext *ctx, AsyncFrame *af, JSFunction *fn,
                               size_t next_pc, std::vector<JSValue *> &stack,
                               JSEnvironment *env, JSValue *frame_this,
                               const ConversionResult &pending,
                               bool has_pending, uint32_t pending_handle,
                               bool pending_is_return);
/* await 一个 promise 并走委托状态机续延（非 Await 指令）。 */
ConversionResult asyncDelegateAwait(JSContext *ctx, AsyncFrame *af,
                                    JSValue *gen_v, JSValue *awaitable,
                                    AsyncDelegatePhase phase);
/* inner 产出值（done=false）：先 await 值再 AsyncGeneratorYield。 */
ConversionResult asyncDelegateYieldValue(JSContext *ctx, AsyncFrame *af,
                                         JSValue *gen_v, JSValue *iter_result);
/* AsyncGeneratorYield：resolve 当前请求 capability + 挂起（suspendAsyncYield 封装）。 */
ConversionResult asyncDelegateYield(JSContext *ctx, AsyncFrame *af,
                                    JSValue *gen_v, JSValue *value);

/* 取 AsyncGenerator.prototype（惰性创建 + 缓存 + 进根集，builtins 注册）。 */
JSValue *asyncGeneratorProto(JSContext *ctx);

/* 安装 AsyncGenerator.prototype 方法 + 全局 AsyncGenerator 构造器
 * + Symbol.toStringTag（builtins installBuiltins 调用，M5.5c-1）。 */
void installAsyncGeneratorBuiltins(JSContext *ctx);

/* AsyncGenerator 帧环境隐藏绑定名：与同步 generator 共用 kGeneratorBinding
 * （async.h 的 "__generator__"）。asyncGeneratorCall 写入 env 链首段，
 * op_Yield/op_Await 沿 env 链查找后按对象 kind 区分（isAsyncGenerator）。
 *
 * M5.5c-2 for await...of 的异步迭代抽象运算（GetAsyncIterator /
 * AsyncIteratorNext / AsyncIteratorClose / AsyncFromSyncIterator）声明在
 * async_iterator.h。 */

}  // namespace neojs

#endif  // NEOJS_RUNTIME_ASYNC_GENERATOR_H
