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

}  // namespace neojs
