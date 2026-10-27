/*
 * 内部 C++ 实现：异步迭代抽象运算（M5.5c-2，§14.7.5 + §27.1.4）。
 *
 * 实现 for await...of 的运行时支持：
 *   - GetAsyncIterator（§14.7.5.1）：优先取 iterable[Symbol.asyncIterator]()；
 *     无则回退同步 iterable[Symbol.iterator]() 并包装成 AsyncFromSyncIterator
 *     （§27.1.4）。
 *   - AsyncIteratorNext（§14.7.5.3）：async iterator.next() + PromiseResolve
 *     接合，返回结果 promise（由调用方经 Await 指令挂起/恢复）。
 *   - AsyncIteratorClose（§14.7.5.4）：async iterator.return() + PromiseResolve
 *     接合，返回结果 promise（由调用方经 Await 指令挂起/恢复）。
 *   - AsyncFromSyncIterator 对象（§27.1.4.1/2/3）：包装同步迭代器，next/
 *     return/throw 返回 promise（内部同步调用 + resolve 结果对象）。
 *
 * 设计（配合 for-await 编译器发射）：
 *   - 指令 GetAsyncIterator / AsyncIteratorNext / AsyncIteratorClose 都不挂起——
 *     next/close 只做同步调用 + PromiseResolve 接合，把结果 promise 压栈，
 *     由后续 Await 指令统一挂起/恢复（await 挂起机制天然复用）。
 *   - AsyncFromSyncIterator 的 next/return/throw 是原生 Builtin 方法（挂共享
 *     prototype），经 this 读内部槽 [[SyncIterator]]（复用 iter_target_）。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_ASYNC_ITERATOR_H
#define NEOJS_RUNTIME_ASYNC_ITERATOR_H

#include <cstddef>

#include "neojs/runtime/conversion.h"

namespace neojs {

class JSContext;
class JSValue;

/* GetAsyncIterator（§14.7.5.1）：iterable -> async iterator。
 *   1. method = GetMethod(iterable, Symbol.asyncIterator)
 *   2. method 存在（callable）-> Call -> iterator（须对象，否则 TypeError）
 *   3. method 不存在 -> 回退同步：GetMethod(iterable, Symbol.iterator) 得
 *      syncIterator（须对象），包装成 AsyncFromSyncIterator（§27.1.4）。
 * 返回 async iterator。 */
ConversionResult getAsyncIterator(JSContext *ctx, JSValue *iterable);

/* AsyncIteratorNext（§14.7.5.3）：async iterator 下一步。
 *   1. next = GetMethod(iterator, "next")（非 callable -> TypeError）
 *   2. nextResult = Call(next, iterator)（返回 promise 或 thenable）
 *   3. PromiseResolve 接合（nextResult 非 promise 则包一层）
 * 返回已接合的 promise（不挂起；由调用方经 Await 指令挂起/恢复）。 */
ConversionResult asyncIteratorNext(JSContext *ctx, JSValue *iterator);

/* AsyncIteratorClose（§14.7.5.4）：异步关闭 async iterator。
 *   1. returnMethod = GetMethod(iterator, "return")
 *   2. 无 return 方法 -> 返回已 resolve 的 promise（占位 undefined）
 *   3. 有 -> returnResult = Call(returnMethod, iterator)
 *      PromiseResolve 接合后返回。
 * 返回 promise（不挂起；由调用方经 Await 指令挂起/恢复）。 */
ConversionResult asyncIteratorClose(JSContext *ctx, JSValue *iterator);

/* AsyncFromSyncIterator（§27.1.4）：包装同步迭代器为异步迭代器。
 * 对象内部槽 [[SyncIterator]]（复用 iter_target_），原型链挂
 * AsyncFromSyncIterator.prototype（next/return/throw 返回 promise）。
 * GetAsyncIterator 回退路径调用。 */
JSValue *createAsyncFromSyncIterator(JSContext *ctx, JSValue *syncIterator);

/* AsyncFromSyncIterator.prototype.next(value)（§27.1.4.1）：sync next()
 * 同步调用，resolve promise {value, done}。Builtin 函数。 */
ConversionResult asyncFromSyncNext(JSContext *ctx, JSValue *thisVal,
                                   JSValue *const *args, size_t argc);

/* AsyncFromSyncIterator.prototype.return(value)（§27.1.4.2）：sync return
 * 方法存在则调用 + resolve；无则 resolve {value, done:true}。Builtin 函数。 */
ConversionResult asyncFromSyncReturn(JSContext *ctx, JSValue *thisVal,
                                     JSValue *const *args, size_t argc);

/* AsyncFromSyncIterator.prototype.throw(value)（§27.1.4.3）：sync throw
 * 方法存在则调用 + resolve；无则 reject TypeError。Builtin 函数。 */
ConversionResult asyncFromSyncThrow(JSContext *ctx, JSValue *thisVal,
                                    JSValue *const *args, size_t argc);

/* 取 AsyncFromSyncIterator.prototype（惰性创建 + 缓存 + 进根集）。 */
JSValue *asyncFromSyncIteratorProto(JSContext *ctx);

/* 安装 AsyncFromSyncIterator.prototype 方法（next/return/throw，builtins
 * installBuiltins 调用，M5.5c-2）。不暴露全局构造器（规范无
 * %AsyncFromSyncIterator%）。 */
void installAsyncFromSyncIteratorBuiltins(JSContext *ctx);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_ASYNC_ITERATOR_H
