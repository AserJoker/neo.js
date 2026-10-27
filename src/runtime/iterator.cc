/*
 * 内部 C++ 实现：迭代器协议抽象运算（ECMA-262 §7.4）。
 *
 * 实现 GetIterator / IteratorNext / IteratorStep / IteratorValue /
 * GetMethod。所有运算走统一 call() 入口（function.h），错误经
 * ConversionResult 传递。
 */

#include "neojs/runtime/iterator.h"

#include <string>

#include "neojs/runtime/builtins.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/object.h"
#include "neojs/runtime/value.h"

namespace neojs {

namespace {

/* 成功/失败结果构造（各 cc 模块内部辅助，与 conversion/operators 一致）。 */
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

}  // namespace

/* GetMethod（§7.3.11）：读 obj[key]，校验可调用。
 * 无方法（undefined/null/属性不存在）-> value=nullptr 非错误。 */
ConversionResult getMethod(JSContext *ctx, JSValue *obj, JSValue *key) {
  /* obj 必须是对象（或装箱后查属性）。 */
  JSObject *target = nullptr;
  if (obj->isObject()) {
    target = obj->object();
  } else if (obj->kind() == Kind::Undefined || obj->kind() == Kind::Null) {
    return fail("TypeError: Cannot read properties of undefined/null");
  } else {
    ConversionResult boxed = toObject(ctx, obj);
    if (!boxed.ok()) return boxed;
    target = boxed.value->object();
  }

  JSValue *method = nullptr;
  if (key->kind() == Kind::Symbol) {
    method = target->getSymbolProperty(key);
  } else {
    method = target->getProperty(key->toString());
  }
  if (method == nullptr || method->kind() == Kind::Undefined ||
      method->kind() == Kind::Null) {
    return ok(nullptr); /* 无方法（非错误） */
  }
  if (!isCallable(method)) {
    return fail("TypeError: property is not a function");
  }
  return ok(method);
}

/* GetIterator（§7.4.1）：obj[Symbol.iterator]() -> iterator 对象。 */
ConversionResult getIterator(JSContext *ctx, JSValue *obj) {
  /* Symbol.iterator well-known（E4-1 已注册）。 */
  JSValue *iter_sym = ctx->wellKnown(static_cast<int>(WellKnown::Iterator));
  if (iter_sym == nullptr) {
    return fail("Error: Symbol.iterator not registered");
  }
  ConversionResult method = getMethod(ctx, obj, iter_sym);
  if (!method.ok()) return method;
  if (method.value == nullptr) {
    return fail("TypeError: obj is not iterable");
  }
  ConversionResult it = call(ctx, method.value, obj, nullptr, 0);
  if (!it.ok()) return it;
  if (!it.value->isObject()) {
    return fail("TypeError: Iterator result is not an object");
  }
  return ok(it.value);
}

/* IteratorNext（§7.4.2）：call(iterator.next, iterator)。 */
ConversionResult iteratorNext(JSContext *ctx, JSValue *iterator) {
  JSValue *next = nullptr;
  if (iterator->isObject()) {
    next = iterator->object()->getProperty("next");
  }
  if (next == nullptr || !isCallable(next)) {
    return fail("TypeError: iterator.next is not a function");
  }
  ConversionResult r = call(ctx, next, iterator, nullptr, 0);
  if (!r.ok()) return r;
  if (!r.value->isObject()) {
    return fail("TypeError: Iterator result is not an object");
  }
  return ok(r.value);
}

/* IteratorStep（§7.4.5）：next() 后读 done。
 * done true -> value=nullptr（迭代结束，非错误）；否则返回结果对象。 */
ConversionResult iteratorStep(JSContext *ctx, JSValue *iterator) {
  ConversionResult r = iteratorNext(ctx, iterator);
  if (!r.ok()) return r;
  JSValue *done = r.value->object()->getProperty("done");
  if (done != nullptr && toBoolean(done)) {
    return ok(nullptr); /* 迭代完成 */
  }
  return ok(r.value);
}

/* IteratorValue（§7.4.4）：读迭代结果对象的 value。 */
ConversionResult iteratorValue(JSContext *ctx, JSValue *iterResult) {
  if (iterResult == nullptr || !iterResult->isObject()) {
    return fail("TypeError: iterator result is not an object");
  }
  JSValue *v = iterResult->object()->getProperty("value");
  return ok(v != nullptr ? v : ctx->createValue(Kind::Undefined));
}

/* 判断 completion 是否为 abrupt（throw 或内部错误）。
 * normal completion：value != nullptr（任意占位），error 空，thrown 空。
 * abrupt (throw)：thrown != nullptr。
 * abrupt (internal)：error 非空、thrown == nullptr、value == nullptr。 */
static bool isAbrupt(const ConversionResult &c) {
  return c.thrown != nullptr || !c.error.empty();
}

/* IteratorClose（§7.4.6）：见 iterator.h 注释。 */
ConversionResult iteratorClose(JSContext *ctx, JSValue *iterator,
                               ConversionResult completion) {
  if (iterator == nullptr || !iterator->isObject()) {
    /* 非对象迭代器：无法关闭，直接返回 completion。 */
    return completion;
  }

  /* 1. method = GetMethod(iterator, "return")。 */
  JSValue *return_key = ctx->createValue(std::string("return"));
  ConversionResult method_r = getMethod(ctx, iterator, return_key);
  if (!method_r.ok()) {
    /* GetMethod 自身失败（如属性不可调用）：§7.4.6 step 3-4 规定
     * 若原 completion 是 abrupt 则维持原 completion，否则返回此错误。 */
    if (isAbrupt(completion)) return completion;
    return method_r;
  }
  JSValue *method = method_r.value;
  if (method == nullptr) {
    /* 无 return 方法（undefined/null）：直接返回 completion。 */
    return completion;
  }

  /* 2. innerResult = Call(method, iterator)。 */
  ConversionResult inner = call(ctx, method, iterator, nullptr, 0);
  bool inner_abrupt = isAbrupt(inner);

  /* 3. 若原 completion 是 abrupt：维持原 completion（除非 inner 抛错优先）。
   *   §7.4.6 step 3.c：innerResult abrupt -> 返回 innerResult；否则返回 completion。 */
  if (isAbrupt(completion)) {
    if (inner_abrupt) return inner;
    return completion;
  }

  /* 4. 原 completion 是 normal：若 inner 是 abrupt，返回 inner。 */
  if (inner_abrupt) return inner;

  /* 5. 两者都 normal：innerResult.value 必须是 Object（§7.4.6 step 3.e）。
   * inner.value 可能为 nullptr（call 返回 ok(nullptr) 的特殊语义）——
   * 视为非对象，抛 TypeError。 */
  if (inner.value == nullptr || !inner.value->isObject()) {
    return fail("TypeError: iterator return() did not return an object");
  }

  /* 6. 返回 normal completion（保持原 completion 的 value 占位）。 */
  return completion;
}

}  // namespace neojs
