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
 * 遵循 docs/coding-standards.md：#3 禁止 C++ 异常跨 C ABI 边界——失败返回
 * ConversionResult；#14 返回 JSValue 的接口永不返回 nullptr。
 */

#include "neojs/runtime/async_iterator.h"

#include <utility>
#include <vector>

#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/iterator.h"
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

ConversionResult fail(const std::string &error) {
  ConversionResult r;
  r.error = error;
  return r;
}

/* PromiseResolve 接合（§14.7.5.3 step 6 / Await 同构）：x 是 Promise 直接用；
 * 否则包一层 pending promise 并同步 resolve（resolve 后 reaction 经微任务
 * 入队——Await 看到仍 pending 会挂起，微任务驱动续延，符合规范异步时序）。
 * 返回 promise（GC 引用边，调用方压栈受根集保护）。 */
JSValue *promiseResolve(JSContext *ctx, JSValue *x) {
  if (x != nullptr && x->isObject() && x->object()->isPromise()) {
    return x;
  }
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：决议前防误回收 */
  JSValue *argv[1] = {x != nullptr ? x : ctx->createValue(Kind::Undefined)};
  ConversionResult r =
      call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
  ctx->heap().unpin(p);
  if (!r.ok()) {
    /* resolve 失败（极不可能）：reject promise，返回仍为 promise。 */
    (void)r;
  }
  return p;
}

/* 创建 {value, done} 迭代器结果对象（AsyncFromSyncIterator 方法决议值）。 */
JSValue *makeIterResult(JSContext *ctx, JSValue *value, bool done) {
  JSValue *r = ctx->createObject(objectProto(ctx)->object());
  r->object()->setProperty("value",
                           value != nullptr ? value
                                            : ctx->createValue(Kind::Undefined));
  r->object()->setProperty("done", ctx->createValue(done));
  return r;
}

/* 调同步迭代器方法并包装结果：call(method, iterator, [value]) 返回的结果
 * 对象 -> resolve promise {value: r.value, done: r.done}（§27.1.4.1
 * AsyncFromSyncIteratorContinuation 近似——同步迭代器 next() 同步返回结果，
 * 直接 resolve；经微任务 settle 派生 promise）。返回 promise。 */
ConversionResult syncIteratorCallAndResolve(JSContext *ctx, JSValue *sync_iter,
                                            JSValue *method, JSValue *value) {
  JSValue *argv[1] = {value != nullptr ? value : ctx->createValue(Kind::Undefined)};
  ConversionResult r = call(ctx, method, sync_iter, argv, 1);
  if (!r.ok()) return r;
  if (r.value == nullptr || !r.value->isObject()) {
    return fail("TypeError: Iterator result is not an object");
  }
  JSValue *val = r.value->object()->getProperty("value");
  JSValue *done = r.value->object()->getProperty("done");
  JSValue *resolve = nullptr, *reject = nullptr;
  JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
  ctx->heap().pin(p); /* 分配保护：决议前防误回收 */
  JSValue *argv2[1] = {makeIterResult(
      ctx, val != nullptr ? val : ctx->createValue(Kind::Undefined),
      done != nullptr && toBoolean(done))};
  ConversionResult rr =
      call(ctx, resolve, ctx->createValue(Kind::Undefined), argv2, 1);
  ctx->heap().unpin(p);
  if (!rr.ok()) {
    /* resolve 抛错（极不可能）：reject promise。 */
    JSValue *reason = rr.thrown;
    if (reason == nullptr) {
      reason = ctx->createValue(Kind::Exception);
      reason->setErrorInfo(rr.error);
    }
    JSValue *argv3[1] = {reason};
    (void)call(ctx, reject, ctx->createValue(Kind::Undefined), argv3, 1);
  }
  return ok(p);
}

/* 取同步迭代器的方法（"next"/"return"/"throw"），无则返回 value=nullptr
 * （非错误）。经 getMethod 抽象运算（iterator.h）。 */
ConversionResult syncIterMethod(JSContext *ctx, JSValue *sync_iter,
                                const char *name) {
  JSValue *key = ctx->createValue(std::string(name));
  ConversionResult m = getMethod(ctx, sync_iter, key);
  if (!m.ok()) return m;
  return ok(m.value);
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 异步迭代抽象运算（§14.7.5）                                                 */
/* -------------------------------------------------------------------------- */

/* GetAsyncIterator（§14.7.5.1）：
 *   1. method = GetMethod(iterable, Symbol.asyncIterator)
 *   2. method 存在 -> Call -> iterator（须对象）；替换为 iterator。
 *   3. method 不存在 -> 回退同步：
 *      syncMethod = GetMethod(iterable, Symbol.iterator)
 *      syncIterator = Call(syncMethod, iterable)（须对象）
 *      -> 包装 AsyncFromSyncIterator（§27.1.4）。
 * 返回 async iterator。 */
ConversionResult getAsyncIterator(JSContext *ctx, JSValue *iterable) {
  JSValue *async_sym = ctx->wellKnown(static_cast<int>(WellKnown::AsyncIterator));
  if (async_sym == nullptr) {
    return fail("Error: Symbol.asyncIterator not registered");
  }
  ConversionResult method_r = getMethod(ctx, iterable, async_sym);
  /* getMethod 对「无方法」返回 value=nullptr（非错误）；err/thrown 非空才是真错误。 */
  if (method_r.value == nullptr &&
      (!method_r.error.empty() || method_r.thrown != nullptr))
    return method_r;
  if (method_r.value != nullptr) {
    /* 有 Symbol.asyncIterator 方法：直接调用。 */
    ConversionResult it = call(ctx, method_r.value, iterable, nullptr, 0);
    if (!it.ok()) return it;
    if (it.value == nullptr || !it.value->isObject()) {
      return fail("TypeError: Iterator result is not an object");
    }
    return ok(it.value);
  }

  /* 回退同步迭代器（§14.7.5.1 step 7-9）。 */
  JSValue *iter_sym = ctx->wellKnown(static_cast<int>(WellKnown::Iterator));
  if (iter_sym == nullptr) {
    return fail("Error: Symbol.iterator not registered");
  }
  ConversionResult sync_method_r = getMethod(ctx, iterable, iter_sym);
  if (sync_method_r.value == nullptr &&
      (!sync_method_r.error.empty() || sync_method_r.thrown != nullptr))
    return sync_method_r;
  if (sync_method_r.value == nullptr) {
    return fail("TypeError: obj is not async iterable");
  }
  ConversionResult sync_it = call(ctx, sync_method_r.value, iterable, nullptr, 0);
  if (!sync_it.ok()) return sync_it;
  if (sync_it.value == nullptr || !sync_it.value->isObject()) {
    return fail("TypeError: Iterator result is not an object");
  }
  /* 包装成 AsyncFromSyncIterator。 */
  return ok(createAsyncFromSyncIterator(ctx, sync_it.value));
}

/* AsyncIteratorNext（§14.7.5.3）：async iterator 下一步。
 *   1. next = GetMethod(iterator, "next")（非 callable -> TypeError）
 *   2. nextResult = Call(next, iterator)
 *   3. PromiseResolve 接合 -> 返回结果 promise（不挂起；Await 指令处理）。 */
ConversionResult asyncIteratorNext(JSContext *ctx, JSValue *iterator) {
  JSValue *key = ctx->createValue(std::string("next"));
  ConversionResult m = getMethod(ctx, iterator, key);
  if (m.value == nullptr &&
      (!m.error.empty() || m.thrown != nullptr))
    return m;
  if (m.value == nullptr) {
    return fail("TypeError: iterator.next is not a function");
  }
  ConversionResult next_r = call(ctx, m.value, iterator, nullptr, 0);
  if (!next_r.ok()) return next_r;
  return ok(promiseResolve(ctx, next_r.value));
}

/* AsyncIteratorClose（§14.7.5.4）：异步关闭。
 *   1. returnMethod = GetMethod(iterator, "return")
 *   2. 无 return 方法 -> 返回已 resolve 的 promise（占位 undefined，后续
 *      Await 直接继续）。
 *   3. 有 -> returnResult = Call(returnMethod, iterator) + PromiseResolve。
 * 返回 promise（不挂起；Await 指令处理）。
 * 注意：规范对 returnResult 须为对象的校验在 Await 恢复后由调用方语义
 * 承担（本实现 v1 简化：不额外校验对象性，RethrowPending 维持原 completion）。 */
ConversionResult asyncIteratorClose(JSContext *ctx, JSValue *iterator) {
  JSValue *return_key = ctx->createValue(std::string("return"));
  ConversionResult m = getMethod(ctx, iterator, return_key);
  if (m.value == nullptr &&
      (!m.error.empty() || m.thrown != nullptr))
    return m;
  if (m.value == nullptr) {
    /* 无 return 方法：resolve undefined 占位（§14.7.5.4 step 3）。 */
    return ok(promiseResolve(ctx, ctx->createValue(Kind::Undefined)));
  }
  ConversionResult return_r = call(ctx, m.value, iterator, nullptr, 0);
  if (!return_r.ok()) return return_r;
  return ok(promiseResolve(ctx, return_r.value));
}

/* -------------------------------------------------------------------------- */
/* AsyncFromSyncIterator（§27.1.4）                                             */
/* -------------------------------------------------------------------------- */

/* 创建 AsyncFromSyncIterator 对象：kind=AsyncFromSyncIterator，[[SyncIterator]]
 * = syncIterator（复用 iter_target_），原型链挂 AsyncFromSyncIterator.prototype。 */
JSValue *createAsyncFromSyncIterator(JSContext *ctx, JSValue *syncIterator) {
  JSObject *proto = nullptr;
  JSValue *p = asyncFromSyncIteratorProto(ctx);
  if (p != nullptr) proto = p->object();
  JSObject *obj = ctx->heap().allocGCObject<JSObject>(
      ObjectKind::AsyncFromSyncIterator, 0);
  if (proto != nullptr) obj->setPrototype(proto);
  ctx->heap().pin(obj); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  obj->setSyncIterator(syncIterator);
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(obj);
  obj->setHolder(v);
  ctx->heap().unpin(obj);
  return v;
}

/* AsyncFromSyncIterator.prototype.next(value)（§27.1.4.1）：
 *   sync next() 同步调用，resolve promise {value, done}。 */
ConversionResult asyncFromSyncNext(JSContext *ctx, JSValue *thisVal,
                                   JSValue *const *args, size_t argc) {
  if (thisVal == nullptr || !thisVal->isObject() ||
      !thisVal->object()->isAsyncFromSyncIterator()) {
    return fail("TypeError: not an AsyncFromSyncIterator");
  }
  JSValue *sync_iter = thisVal->object()->syncIterator();
  if (sync_iter == nullptr) {
    return fail("TypeError: invalid AsyncFromSyncIterator");
  }
  ConversionResult m = syncIterMethod(ctx, sync_iter, "next");
  if (!m.ok()) return m;
  if (m.value == nullptr) {
    /* 同步迭代器无 next（异常）：reject（§27.1.4.1 step 3）。 */
    return fail("TypeError: sync iterator.next is not a function");
  }
  JSValue *value =
      (argc > 0 && args[0] != nullptr) ? args[0] : ctx->createValue(Kind::Undefined);
  return syncIteratorCallAndResolve(ctx, sync_iter, m.value, value);
}

/* AsyncFromSyncIterator.prototype.return(value)（§27.1.4.2）：
 *   sync return 方法存在 -> 调用 + resolve；无 -> resolve {value, done:true}。 */
ConversionResult asyncFromSyncReturn(JSContext *ctx, JSValue *thisVal,
                                     JSValue *const *args, size_t argc) {
  if (thisVal == nullptr || !thisVal->isObject() ||
      !thisVal->object()->isAsyncFromSyncIterator()) {
    return fail("TypeError: not an AsyncFromSyncIterator");
  }
  JSValue *sync_iter = thisVal->object()->syncIterator();
  if (sync_iter == nullptr) {
    return fail("TypeError: invalid AsyncFromSyncIterator");
  }
  JSValue *value =
      (argc > 0 && args[0] != nullptr) ? args[0] : ctx->createValue(Kind::Undefined);
  ConversionResult m = syncIterMethod(ctx, sync_iter, "return");
  if (!m.ok()) return m;
  if (m.value == nullptr) {
    /* 无 return 方法：resolve {value, done:true}（§27.1.4.2 step 4）。 */
    JSValue *resolve = nullptr, *reject = nullptr;
    JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
    ctx->heap().pin(p); /* 分配保护：决议前防误回收 */
    JSValue *argv[1] = {makeIterResult(ctx, value, true)};
    ConversionResult rr =
        call(ctx, resolve, ctx->createValue(Kind::Undefined), argv, 1);
    ctx->heap().unpin(p);
    (void)rr;
    return ok(p);
  }
  return syncIteratorCallAndResolve(ctx, sync_iter, m.value, value);
}

/* AsyncFromSyncIterator.prototype.throw(value)（§27.1.4.3）：
 *   sync throw 方法存在 -> 调用 + resolve；无 -> reject TypeError。 */
ConversionResult asyncFromSyncThrow(JSContext *ctx, JSValue *thisVal,
                                    JSValue *const *args, size_t argc) {
  if (thisVal == nullptr || !thisVal->isObject() ||
      !thisVal->object()->isAsyncFromSyncIterator()) {
    return fail("TypeError: not an AsyncFromSyncIterator");
  }
  JSValue *sync_iter = thisVal->object()->syncIterator();
  if (sync_iter == nullptr) {
    return fail("TypeError: invalid AsyncFromSyncIterator");
  }
  JSValue *value =
      (argc > 0 && args[0] != nullptr) ? args[0] : ctx->createValue(Kind::Undefined);
  ConversionResult m = syncIterMethod(ctx, sync_iter, "throw");
  if (!m.ok()) return m;
  if (m.value == nullptr) {
    /* 无 throw 方法：reject TypeError（§27.1.4.3 step 4）。 */
    JSValue *resolve = nullptr, *reject = nullptr;
    JSValue *p = createDerivedPromise(ctx, &resolve, &reject);
    ctx->heap().pin(p); /* 分配保护：决议前防误回收 */
    JSValue *argv[1] = {ctx->createValue(std::string(
        "TypeError: sync iterator.throw is not a function"))};
    ConversionResult rr =
        call(ctx, reject, ctx->createValue(Kind::Undefined), argv, 1);
    ctx->heap().unpin(p);
    (void)rr;
    return ok(p);
  }
  return syncIteratorCallAndResolve(ctx, sync_iter, m.value, value);
}

/* 取 AsyncFromSyncIterator.prototype（惰性创建 + 缓存 + 进根集）。
 * 方法注册在 installAsyncFromSyncIteratorBuiltins（builtins.cc，M5.5c-2）。 */
JSValue *asyncFromSyncIteratorProto(JSContext *ctx) {
  JSValue *p = ctx->asyncFromSyncIteratorProto();
  if (p != nullptr) return p;
  p = ctx->createObject(objectProto(ctx)->object());
  ctx->heap().pin(p); /* 分配保护（M3.3）：进根集前防误回收 */
  ctx->setAsyncFromSyncIteratorProto(p); /* 进根集（markRoots 遍历） */
  ctx->heap().unpin(p);
  return p;
}

}  // namespace neojs
