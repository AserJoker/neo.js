/*
 * 内部 C++ 实现：async/await 挂起帧（M5.4，§27.7）。
 *
 * async 函数执行到 await 且所等 promise 未决时挂起：整个 VM 现场
 * （操作数栈 + pc + 帧环境 + this + finally 挂起状态）从 C++ 栈转移到
 * 堆上的 AsyncFrame（GC 对象），await 的续延以微任务 job 形式入队。
 * promise settle 后 job 执行：重建现场、从挂起点继续执行字节码，
 * 直到下一个 await 挂起或函数完成（完成/异常 settle 外层 promise）。
 *
 * 设计（配合 feedback_vm_design.md：async/generator 全 env 持有）：
 *   - 局部变量直接入 env（LoadVar/StoreVar 读 env 绑定），操作数栈只存
 *     表达式中间值——挂起时只需拷贝操作数栈（表达式求值中途），局部
 *     变量状态全在 env 里，无需额外保存。
 *   - AsyncFrame 继承 JSObject（ObjectKind::AsyncFrame）：作为 JSValue 的
 *     object 载荷，可被 Bound 闭包作为捕获参数持有；GC 标记经多态派发
 *     到 AsyncFrame::gcMarkRefs（fn/env/frame_this/操作数栈元素逐边标记），
 *     挂起期间始终根可达（job 队列 -> Bound 闭包 -> 本帧）。
 *   - 外层 promise 经帧环境隐藏绑定 "__async_promise__" 传递（asyncCall
 *     写入，op_Await/续延沿 env 链读取）：嵌套 async 调用各自建帧环境，
 *     沿链查找先命中自身绑定，天然隔离。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_ASYNC_H
#define NEOJS_RUNTIME_ASYNC_H

#include <cstddef>
#include <vector>

#include "neojs/runtime/conversion.h"
#include "neojs/runtime/object.h"

namespace neojs {

class JSEnvironment;
class JSFunction;
class JSValue;

/* async 挂起帧（GC 对象，M5.4）。由微任务队列中的续延 job 持有
 * （Bound 闭包的捕获参数），故挂起期间始终根可达。继承 JSObject：
 * kind=ObjectKind::AsyncFrame，作为 JSValue 的 object 载荷。 */
class AsyncFrame : public JSObject {
 public:
  AsyncFrame();
  ~AsyncFrame() override = default;

  /* 恢复执行的函数对象（bytecode() 访问指令序列；GC 引用边）。 */
  JSFunction *fn = nullptr;
  /* 恢复点：下一条待执行指令（await 之后）。 */
  size_t pc = 0;
  /* 操作数栈副本（表达式中间值，含局部槽；元素为 GC 引用边）。 */
  std::vector<JSValue *> stack;
  /* 帧环境（闭包变量承载；GC 引用边）。 */
  JSEnvironment *env = nullptr;
  /* 当前帧 this（GC 引用边）。 */
  JSValue *frame_this = nullptr;
  /* finally 挂起异常状态（M5.1e-f 帧级状态，跨 await 挂起保留）。 */
  ConversionResult pending;
  bool has_pending = false;
  uint32_t pending_handle = 0;
  /* M5.5d：挂起的是 return completion（vs throw completion）。op_Yield
   * 挂起时由 VmFrame.pending_is_return 拷贝；恢复时回填到 runBytecode 的
   * VmFrame.pending_is_return，实现 generator.return() 在 finally 中 yield
   * 挂起后，恢复时仍按 return 语义完成（§27.5.3.3 step 5-7）。仅 generator/
   * async generator 用，async 不用。 */
  bool pending_is_return = false;
  /* 外层 promise（async 调用入口创建，完成/异常时 settle；GC 引用边）。
   * suspendAwait 时沿 env 链查找 "__async_promise__" 绑定写入。 */
  JSValue *promise = nullptr;
  /* M5.5a-3 generator：yield 值（op_Yield 弹栈顶后存此，供 resumeGenerator
   * 取出包装 {value, done:false}）。仅 generator 用，async 不用。GC 引用边。 */
  JSValue *yield_value = nullptr;
  /* M5.5a-4 generator yield* 委托：当前委托的 inner 迭代器对象。op_YieldDelegate
   * 经 getIterator 取得后存此；挂起期间持有防 GC 回收；恢复时 next/throw/return
   * 转发给 inner（§27.5.3.3 step 6-7）。委托完成后置 nullptr 退出委托模式。
   * 仅 generator 用，async 不用。GC 引用边。 */
  JSValue *delegate_iterator = nullptr;
  /* M5.5c-1 async generator：当前正在执行的请求（AsyncGeneratorRequest 包装
   * 对象）。asyncGeneratorResume 驱动执行前写入，op_Yield 挂起 async generator
   * 时 resolve 其 capability {value,done}，resumeAsyncFrame 完成/异常时
   * resolve/reject 其 capability。仅 async generator 用，async 不用。GC 引用边。 */
  JSValue *async_request = nullptr;

  /* 引用边标记：fn/env/frame_this/promise + 操作数栈每个元素。 */
  void gcMarkRefs(Heap *heap) override;
};

/* 创建包装 AsyncFrame 的 JSValue（两段式：frame 已分配，包 JSValue）。 */
JSValue *wrapAsyncFrame(JSContext *ctx, AsyncFrame *frame);

/* 取 AsyncFrame（v 的 object 载荷须为 AsyncFrame；否则返回 nullptr）。 */
AsyncFrame *asAsyncFrame(JSValue *v);

/* 从 AsyncFrame 重建现场并继续执行到下一个 await 挂起或函数完成。
 *   result    恢复值（AwaitResolve：await 的 fulfillment value 压栈；
 *             AwaitReject：await 抛出的 rejection 值——经异常路径传播）
 *   is_reject 是否 reject 分支（AwaitReject）
 * 返回 ConversionResult：
 *   - suspended=true  又挂起（新 AsyncFrame 已入队），函数未完成；
 *   - ok()            函数完整执行，value 为函数返回值（此时外层 promise
 *                     已 settle）；
 *   - error/thrown    执行中抛错（外层 promise 已 reject，错误向上传播
 *                     给 job 调用方——JS_RunJobs 报未捕获）。 */
ConversionResult resumeAsyncFrame(JSContext *ctx, AsyncFrame *frame,
                                  JSValue *result, bool is_reject);

/* 创建 async 调用：新建外层 promise，执行 fn 字节码到首个 await 挂起
 * 或完成，返回 promise（§27.7.5.3 AsyncFunctionStart 近似）。
 *   fn 为 async Script 函数；thisVal 已解析。 */
ConversionResult asyncCall(JSContext *ctx, JSFunction *fn, JSValue *thisVal,
                           JSValue *const *args, size_t argc);

/* async 帧环境隐藏绑定名（外层 promise 传递，op_Await/续延沿链读取）。 */
constexpr const char *kAsyncPromiseBinding = "__async_promise__";

/* await 挂起（op_Await 调用，M5.4）：保存 VM 现场到堆上 AsyncFrame，
 * 把 AwaitResolve/AwaitReject 续延 reaction 挂到 awaitable 上（§27.7.4.1
 * Await 步骤 9：awaitable.then(awaitResolve, awaitReject)，Bound 闭包捕获
 * AsyncFrame）。awaitable settle 时经 PromiseReactionJob 调用 handler 并传
 * value/reason。
 *   fn          当前 async 函数对象（bytecode() 恢复用）
 *   next_pc     await 之后下一条指令
 *   stack       当前操作数栈（拷贝进 AsyncFrame，含 await 操作数）
 *   env         当前帧环境（沿链查找外层 promise + 恢复时重建）
 *   frame_this  当前帧 this
 *   pending/has_pending/pending_handle  finally 挂起异常状态（帧级）
 *   awaitable   待 await 的 promise（PromiseResolve 接合后的对象）
 * 返回 true 表示挂起成功（调用方置 frame.suspended 结束本帧）；
 * 失败返回 false（内部错误，调用方按异常处理）。 */
bool suspendAwait(JSContext *ctx, JSFunction *fn, size_t next_pc,
                  std::vector<JSValue *> &stack, JSEnvironment *env,
                  JSValue *frame_this, const ConversionResult &pending,
                  bool has_pending, uint32_t pending_handle,
                  JSValue *awaitable);

/* M5.5a-3 generator yield 挂起（op_Yield 调用）：保存 VM 现场到 Generator
 * 对象内嵌的 AsyncFrame（pc=下一条，stack/env/this/pending），弹栈顶 yield 值
 * 存 frame->yield_value，置 frame.suspended 结束本帧 runBytecode。调用方
 * （resumeGenerator）检测 suspended 后包装 {value, done:false} 返回给 next()。
 *   gen        当前 generator 对象（沿 env 隐藏绑定查找，见下）
 *   fn         当前 generator 函数对象
 *   next_pc    yield 之后下一条指令
 *   stack      当前操作数栈（拷贝进 AsyncFrame，yield 值已弹栈）
 *   env        当前帧环境
 *   frame_this 当前帧 this
 *   pending/has_pending/pending_handle  finally 挂起异常状态（帧级）
 *   yield_val  yield 值（栈顶已弹出，存入 frame->yield_value）
 * 返回 true 挂起成功（调用方置 frame.suspended）；false 内部错误。 */
bool suspendYield(JSContext *ctx, JSValue *gen, JSFunction *fn, size_t next_pc,
                  std::vector<JSValue *> &stack, JSEnvironment *env,
                  JSValue *frame_this, const ConversionResult &pending,
                  bool has_pending, uint32_t pending_handle,
                  bool pending_is_return, JSValue *yield_val);

/* generator 帧环境隐藏绑定名（generator 对象传递，op_Yield 沿链读取）。
 * generatorCall 在 env 链首段写入，op_Yield 沿链查找当前 generator。 */
constexpr const char *kGeneratorBinding = "__generator__";

}  // namespace neojs

#endif  // NEOJS_RUNTIME_ASYNC_H
