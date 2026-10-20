/*
 * 内部 C++ 实现：内置对象注册（E4 最小语法绑定集）。
 *
 * 架构 §5.2 + §8 B0：globalThis 上注册语言核心内置（Symbol 构造器等），
 * 供运算符语义 / toPrimitive / 迭代协议使用。E4 只做「语法绑定所需」
 * 的最小集（用户确认），不做完整方法集。
 *
 * 设计：
 *   - well-known symbol 表（架构 §5.2）：Symbol.iterator / Symbol.toPrimitive /
 *     Symbol.toStringTag / Symbol.hasInstance 等预置表，惰性创建 + 进根集。
 *   - 包装对象（E4-2）：Number/String/Boolean/Symbol 构造器 + 各自 prototype
 *     的最小方法集（valueOf / toString），装箱（ToObject）后包装对象原型链
 *     指向对应 prototype（§7.1.13）。
 *   - 内置函数统一为 Builtin 类别（内部值指针语义），区别于宿主 Host 句柄语义。
 *
 * 规范对照（ECMA-262）：
 *   §20.1.1.1  Boolean 构造器
 *   §20.1.3.2  Boolean.prototype.toString
 *   §20.1.3.3  Boolean.prototype.valueOf
 *   §20.4.1.1  Symbol 构造器
 *   §20.4.3.2  Symbol.prototype.toString
 *   §20.4.3.4  Symbol.prototype.valueOf
 *   §21.1.1.1  Number 构造器
 *   §21.1.3.6  Number.prototype.toString
 *   §21.1.3.7  Number.prototype.valueOf
 *   §22.1.1.1  String 构造器
 *   §22.1.3.5  String.prototype.valueOf
 *   §22.1.3.27 String.prototype.toString
 */

#include "neojs/runtime/builtins.h"

#include <cmath>
#include <string>

#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/object.h"
#include "neojs/runtime/operators.h"
#include "neojs/runtime/value.h"

namespace neojs {

namespace {

/* 成功/失败结果（conversion.cc 的 ok/fail 在匿名命名空间，本模块独立定义）。 */
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

/* 创建内置函数（Builtin 类别）：JSFunction + 包装 JSValue + 反向指针。
 * 内部值指针语义（区别于 Host 句柄语义，见 function.h）。 */
JSValue *makeBuiltin(JSContext *ctx, JSBuiltinFn fn, ThisMode mode) {
  JSFunction *f = ctx->heap().allocGCObject<JSFunction>(nullptr, fn, mode);
  ctx->heap().pin(f); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(f);
  f->setHolder(v);
  ctx->heap().unpin(f);
  return v;
}

/* thisNumberValue / thisStringValue / thisBooleanValue / thisSymbolValue
 * （§20.1.3.3 / §21.1.3.7 / §22.1.3.5 / §20.4.3.4 的抽象运算）：
 *   原始值（primKind）原样返回；目标包装对象（wrapKind）返回内部槽；
 *   其它 -> TypeError。供 prototype 方法校验 this。 */
ConversionResult thisWrapperValue(JSContext *, JSValue *thisVal, Kind primKind,
                                  ObjectKind wrapKind, const char *what) {
  if (thisVal != nullptr && thisVal->kind() == primKind) return ok(thisVal);
  if (thisVal != nullptr && thisVal->isObject() &&
      thisVal->object()->kind() == wrapKind) {
    JSValue *data = thisVal->object()->wrapperData();
    if (data != nullptr) return ok(data);
  }
  return fail(std::string("TypeError: ") + what);
}

/* Symbol 构造器（§20.4.1.1）：Symbol(description) -> 唯一 Symbol 值。
 * 无参数 -> 无描述符；有参数 -> ToString(description)（描述符非字符串时报错）。 */
ConversionResult symbolConstructor(JSContext *ctx, JSValue *, JSValue *const *args,
                                   size_t argc) {
  std::string desc;
  if (argc > 0 && args[0] != nullptr && args[0]->kind() != Kind::Undefined) {
    ConversionResult s = toString(ctx, args[0]);
    if (!s.ok()) return s; /* 描述符转换错误（如 Symbol 描述） */
    desc = s.value->toString();
  }
  JSValue *sym = ctx->createValue(Kind::Symbol);
  sym->setSymbolDescription(desc);
  return ok(sym);
}

/* Number 构造器（§21.1.1.1）：Number(x) -> ToNumber(x)；无参 -> +0。 */
ConversionResult numberConstructor(JSContext *ctx, JSValue *,
                                   JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() == Kind::Undefined) {
    return ok(ctx->createValue(0.0));
  }
  return toNumber(ctx, args[0]);
}

/* String 构造器（§22.1.1.1）：String(x) -> ToString(x)；无参 -> ""。 */
ConversionResult stringConstructor(JSContext *ctx, JSValue *,
                                   JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() == Kind::Undefined) {
    return ok(ctx->createValue(std::string("")));
  }
  return toString(ctx, args[0]);
}

/* Boolean 构造器（§20.1.1.1）：Boolean(x) -> ToBoolean(x)；无参 -> false。 */
ConversionResult booleanConstructor(JSContext *ctx, JSValue *,
                                    JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() == Kind::Undefined) {
    return ok(ctx->createValue(false));
  }
  return ok(ctx->createValue(toBoolean(args[0])));
}

/* Object 构造器（§20.1.1）：Object(value) -> ToObject(value)（§20.1.1.1 步骤 1）。
 * 无参/undefined/null -> 新建普通对象（原型 Object.prototype）。
 * 参数已是对象 -> 原样返回；原始值 -> 装箱（ToObject）。 */
ConversionResult objectConstructor(JSContext *ctx, JSValue *,
                                   JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() == Kind::Undefined ||
      args[0]->kind() == Kind::Null) {
    /* 新建普通对象，原型链指向 Object.prototype（§20.1.1.1 步骤 2）。 */
    return ok(ctx->createObject(objectProto(ctx)->object()));
  }
  return toObject(ctx, args[0]);
}

/* Object.getPrototypeOf（§20.1.2.9）：O 必须为对象；返回 [[Prototype]]。
 * 无原型 -> null。 */
ConversionResult objectGetPrototypeOf(JSContext *ctx, JSValue *,
                                      JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || !args[0]->isObject()) {
    return fail("TypeError: Object.getPrototypeOf called on non-object");
  }
  JSObject *proto = args[0]->object()->prototype();
  if (proto == nullptr) return ok(ctx->createValue(Kind::Null));
  if (proto->holder() == nullptr) return ok(ctx->createValue(Kind::Null));
  return ok(proto->holder());
}

/* Object.setPrototypeOf（§20.1.2.18）：O 必须为对象；proto 必须为 Object 或 null。
 * 返回 O。 */
ConversionResult objectSetPrototypeOf(JSContext *, JSValue *,
                                      JSValue *const *args, size_t argc) {
  if (argc < 2 || args[0] == nullptr || !args[0]->isObject()) {
    return fail("TypeError: Object.setPrototypeOf called on non-object");
  }
  JSValue *proto = args[1];
  if (proto != nullptr && proto->kind() != Kind::Null &&
      !proto->isObject()) {
    return fail("TypeError: Object.setPrototypeOf: prototype must be an object or null");
  }
  JSObject *target = args[0]->object();
  if (proto == nullptr || proto->kind() == Kind::Null) {
    target->setPrototype(nullptr);
  } else {
    /* 循环原型链防护：proto 自身（或链上）不得指向 target（§20.1.2.18 步骤 5）。
     * 若 proto 沿原型链可达 target 则拒绝，保持 target 原型不变。 */
    JSObject *cursor = proto->object();
    while (cursor != nullptr) {
      if (cursor == target) {
        return fail("TypeError: Cyclic __proto__ value");
      }
      cursor = cursor->prototype();
    }
    target->setPrototype(proto->object());
  }
  return ok(args[0]);
}

/* Object.create（§20.1.2.2）：新建对象，原型为 O（null 或 Object）。
 * 可选 properties 本阶段忽略（M4 Object.defineProperties 启用）。 */
ConversionResult objectCreate(JSContext *ctx, JSValue *,
                              JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr) {
    return fail("TypeError: Object.create requires an argument");
  }
  JSValue *proto = args[0];
  if (proto->kind() != Kind::Null && !proto->isObject()) {
    return fail("TypeError: Object prototype may only be an Object or null");
  }
  JSObject *p = (proto->kind() == Kind::Null) ? nullptr : proto->object();
  JSValue *obj = ctx->createObject(p);
  /* properties 参数（M4 Object.defineProperties 启用）。 */
  if (argc > 1 && args[1] != nullptr && args[1]->kind() != Kind::Undefined) {
    /* 本阶段忽略：未实现 defineProperties。 */
  }
  return ok(obj);
}

/* Number.prototype.valueOf（§21.1.3.7）：thisNumberValue。 */
ConversionResult numberValueOf(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  return thisWrapperValue(
      ctx, thisVal, Kind::Number, ObjectKind::Number,
      "Number.prototype.valueOf called on incompatible receiver");
}

/* Number.prototype.toString（§21.1.3.6）：thisNumberValue -> ToString。 */
ConversionResult numberToStringBuiltin(JSContext *ctx, JSValue *thisVal,
                                       JSValue *const *, size_t) {
  ConversionResult x = thisWrapperValue(
      ctx, thisVal, Kind::Number, ObjectKind::Number,
      "Number.prototype.toString called on incompatible receiver");
  if (!x.ok()) return x;
  return toString(ctx, x.value);
}

/* String.prototype.valueOf / toString（§22.1.3.5 / §22.1.3.27）：thisStringValue。 */
ConversionResult stringValueOf(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  return thisWrapperValue(
      ctx, thisVal, Kind::String, ObjectKind::String,
      "String.prototype.valueOf called on incompatible receiver");
}

ConversionResult stringToStringBuiltin(JSContext *ctx, JSValue *thisVal,
                                       JSValue *const *, size_t) {
  return thisWrapperValue(
      ctx, thisVal, Kind::String, ObjectKind::String,
      "String.prototype.toString called on incompatible receiver");
}

/* Boolean.prototype.valueOf（§20.1.3.3）：thisBooleanValue。 */
ConversionResult booleanValueOf(JSContext *ctx, JSValue *thisVal,
                                JSValue *const *, size_t) {
  return thisWrapperValue(
      ctx, thisVal, Kind::Boolean, ObjectKind::Boolean,
      "Boolean.prototype.valueOf called on incompatible receiver");
}

/* Boolean.prototype.toString（§20.1.3.2）："true" / "false"。 */
ConversionResult booleanToStringBuiltin(JSContext *ctx, JSValue *thisVal,
                                        JSValue *const *, size_t) {
  ConversionResult b = thisWrapperValue(
      ctx, thisVal, Kind::Boolean, ObjectKind::Boolean,
      "Boolean.prototype.toString called on incompatible receiver");
  if (!b.ok()) return b;
  return ok(ctx->createValue(b.value->toBoolean() ? std::string("true")
                                                  : std::string("false")));
}

/* Symbol.prototype.valueOf（§20.4.3.4）：thisSymbolValue。 */
ConversionResult symbolValueOf(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  return thisWrapperValue(
      ctx, thisVal, Kind::Symbol, ObjectKind::Symbol,
      "Symbol.prototype.valueOf called on incompatible receiver");
}

/* Symbol.prototype.toString（§20.4.3.2）：SymbolDescriptiveString(s)。 */
ConversionResult symbolToStringBuiltin(JSContext *ctx, JSValue *thisVal,
                                       JSValue *const *, size_t) {
  ConversionResult s = thisWrapperValue(
      ctx, thisVal, Kind::Symbol, ObjectKind::Symbol,
      "Symbol.prototype.toString called on incompatible receiver");
  if (!s.ok()) return s;
  const std::string &desc = s.value->symbolDescription();
  return ok(ctx->createValue("Symbol(" + desc + ")"));
}

/* Array 构造器（M4.1，§23.1.1）：
 *   Array() / Array(len) / Array(...items)。
 * 无参或单个非数字参数 -> 新建空数组；单个 0..2^32-1 整数 -> 预分配 length；
 * 多参数 -> 依次填入元素。new 语义与直接调用一致。
 * 本阶段简化：不校验参数是数组/对象（Array.of/Array.from 留 M4.4）。 */
ConversionResult arrayConstructor(JSContext *ctx, JSValue *thisVal,
                                  JSValue *const *args, size_t argc) {
  JSValue *arr = ctx->createArray(0);
  JSObject *obj = arr->object();
  if (argc == 1) {
    JSValue *a = args[0];
    if (a != nullptr && a->kind() == Kind::Number) {
      double d = a->toNumber();
      if (d >= 0.0 && d == std::trunc(d) && d < 4294967296.0) {
        obj->resizeElements(static_cast<size_t>(d)); /* 预分配 length */
        return ok(arr);
      }
    }
  }
  for (size_t i = 0; i < argc; ++i) {
    obj->setElement(i, args[i]);
  }
  (void)thisVal;
  return ok(arr);
}

/* Array.isArray（§23.1.2.1）：arg 是否为 Array 对象。 */
ConversionResult arrayIsArray(JSContext *ctx, JSValue *,
                              JSValue *const *args, size_t argc) {
  bool is_arr = argc > 0 && args[0] != nullptr && args[0]->isObject() &&
                args[0]->object()->isArray();
  return ok(ctx->createValue(is_arr));
}

/* 取 this 为数组的公共校验（§23.1.3）：非数组对象抛 TypeError。 */
JSObject *arrayThis(JSValue *thisVal) {
  if (thisVal == nullptr || !thisVal->isObject() ||
      !thisVal->object()->isArray()) {
    return nullptr; /* 调用方据此 fail */
  }
  return thisVal->object();
}

/* Array.prototype.push（§23.1.3.25）：末尾追加元素，返回新 length。 */
ConversionResult arrayPush(JSContext *ctx, JSValue *thisVal,
                           JSValue *const *args, size_t argc) {
  JSObject *arr = arrayThis(thisVal);
  if (arr == nullptr) {
    return fail("TypeError: Array.prototype.push called on non-array");
  }
  size_t len = arr->length();
  for (size_t i = 0; i < argc; ++i) {
    arr->setElement(len + i, args[i]);
  }
  return ok(ctx->createValue(static_cast<double>(len + argc)));
}

/* Array.prototype.pop（§23.1.3.19）：删除并返回末尾元素；空数组返回 undefined。 */
ConversionResult arrayPop(JSContext *ctx, JSValue *thisVal,
                          JSValue *const *, size_t) {
  JSObject *arr = arrayThis(thisVal);
  if (arr == nullptr) {
    return fail("TypeError: Array.prototype.pop called on non-array");
  }
  size_t len = arr->length();
  if (len == 0) return ok(ctx->createValue(Kind::Undefined));
  JSValue *v = arr->getElement(len - 1);
  arr->resizeElements(len - 1);
  return ok(v != nullptr ? v : ctx->createValue(Kind::Undefined));
}

/* Array.prototype.indexOf（§23.1.3.15）：返回第一个匹配索引，未找到 -1。 */
ConversionResult arrayIndexOf(JSContext *ctx, JSValue *thisVal,
                              JSValue *const *args, size_t argc) {
  JSObject *arr = arrayThis(thisVal);
  if (arr == nullptr) {
    return fail("TypeError: Array.prototype.indexOf called on non-array");
  }
  size_t len = arr->length();
  JSValue *target = (argc > 0) ? args[0] : nullptr;
  size_t from = 0;
  if (argc > 1 && args[1] != nullptr) {
    double d = args[1]->toNumber();
    if (d < 0.0) {
      double r = static_cast<double>(len) + d;
      from = (r > 0.0) ? static_cast<size_t>(r) : 0;
    } else if (d >= static_cast<double>(len)) {
      return ok(ctx->createValue(-1.0));
    } else {
      from = static_cast<size_t>(d);
    }
  }
  for (size_t i = from; i < len; ++i) {
    JSValue *v = arr->getElement(i);
    /* 空洞跳过（严格相等对空洞无定义）。 */
    if (v == nullptr) continue;
    ConversionResult eq = strictEquals(ctx, target, v);
    if (!eq.ok()) return eq;
    if (eq.value->toBoolean()) {
      return ok(ctx->createValue(static_cast<double>(i)));
    }
  }
  return ok(ctx->createValue(-1.0));
}

/* Array.prototype.join（§23.1.3.14）：元素转字符串用分隔符连接；
 * null/undefined 元素转为空串。 */
ConversionResult arrayJoin(JSContext *ctx, JSValue *thisVal,
                           JSValue *const *args, size_t argc) {
  JSObject *arr = arrayThis(thisVal);
  if (arr == nullptr) {
    return fail("TypeError: Array.prototype.join called on non-array");
  }
  std::string sep = ",";
  if (argc > 0 && args[0] != nullptr &&
      args[0]->kind() != Kind::Undefined) {
    ConversionResult s = toString(ctx, args[0]);
    if (!s.ok()) return s;
    sep = s.value->toString();
  }
  size_t len = arr->length();
  std::string out;
  for (size_t i = 0; i < len; ++i) {
    if (i > 0) out += sep;
    JSValue *v = arr->getElement(i);
    if (v == nullptr || v->kind() == Kind::Null ||
        v->kind() == Kind::Undefined) {
      continue; /* 空串 */
    }
    ConversionResult s = toString(ctx, v);
    if (!s.ok()) return s;
    out += s.value->toString();
  }
  return ok(ctx->createValue(std::move(out)));
}

/* Array.prototype.toString（§23.1.3.29）：委托 join()（无参）。 */
ConversionResult arrayToString(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  return arrayJoin(ctx, thisVal, nullptr, 0);
}

/* Array.prototype.reverse（§23.1.3.26）：原地反转，返回数组本身。 */
ConversionResult arrayReverse(JSContext *, JSValue *thisVal,
                              JSValue *const *, size_t) {
  JSObject *arr = arrayThis(thisVal);
  if (arr == nullptr) {
    return fail("TypeError: Array.prototype.reverse called on non-array");
  }
  size_t n = arr->length();
  for (size_t lo = 0, hi = n; lo < hi;) {
    --hi;
    if (lo >= hi) break;
    JSValue *a = arr->getElement(lo);
    JSValue *b = arr->getElement(hi);
    /* 空洞交换（保持空洞位置）。 */
    if (a == nullptr && b == nullptr) {
      /* both holes: no-op */
    } else if (a == nullptr) {
      arr->setElement(lo, b);
      arr->deleteElement(hi);
    } else if (b == nullptr) {
      arr->deleteElement(lo);
      arr->setElement(hi, a);
    } else {
      arr->setElement(lo, b);
      arr->setElement(hi, a);
    }
    ++lo;
  }
  return ok(thisVal);
}

/* Array.prototype.shift（§23.1.3.24）：删除并返回第一个元素；空数组返回 undefined。 */
ConversionResult arrayShift(JSContext *ctx, JSValue *thisVal,
                            JSValue *const *, size_t) {
  JSObject *arr = arrayThis(thisVal);
  if (arr == nullptr) {
    return fail("TypeError: Array.prototype.shift called on non-array");
  }
  size_t len = arr->length();
  if (len == 0) return ok(ctx->createValue(Kind::Undefined));
  JSValue *first = arr->getElement(0);
  for (size_t i = 1; i < len; ++i) {
    JSValue *v = arr->getElement(i);
    if (v != nullptr) {
      arr->setElement(i - 1, v);
    } else {
      arr->deleteElement(i - 1);
    }
  }
  arr->resizeElements(len - 1);
  return ok(first != nullptr ? first : ctx->createValue(Kind::Undefined));
}

/* Array.prototype.unshift（§23.1.3.31）：头部插入元素，返回新 length。 */
ConversionResult arrayUnshift(JSContext *ctx, JSValue *thisVal,
                              JSValue *const *args, size_t argc) {
  JSObject *arr = arrayThis(thisVal);
  if (arr == nullptr) {
    return fail("TypeError: Array.prototype.unshift called on non-array");
  }
  size_t len = arr->length();
  /* 先把现有元素后移 argc 位（从后往前避免覆盖）。 */
  for (size_t i = len; i > 0; --i) {
    JSValue *v = arr->getElement(i - 1);
    if (v != nullptr) {
      arr->setElement(i - 1 + argc, v);
    } else {
      arr->deleteElement(i - 1 + argc);
    }
  }
  for (size_t i = 0; i < argc; ++i) {
    arr->setElement(i, args[i]);
  }
  return ok(ctx->createValue(static_cast<double>(len + argc)));
}

/* -------------------------------------------------------------------------- */
/* Function 构造器 + Function.prototype（M4.2，§20.2）                        */
/* -------------------------------------------------------------------------- */

/* Function 构造器（§20.2.1）：Function(...args) -> 新函数。
 * 本阶段简化：忽略参数与函数体（Function() -> 空函数，调用返回 undefined）。
 * 完整实现（参数名 + 函数体解析编译）留 M4.3。 */
ConversionResult functionConstructor(JSContext *ctx, JSValue *thisVal,
                                     JSValue *const *, size_t) {
  (void)thisVal;
  /* 原型链 -> Function.prototype（M4.2）：动态创建的函数同样可调
   * call/apply/bind。 */
  JSObject *fproto = nullptr;
  JSValue *fp = functionProto(ctx);
  if (fp != nullptr) fproto = fp->object();
  JSFunction *f = ctx->heap().allocGCObject<JSFunction>(
      fproto, FunctionKind::Script, nullptr /* func */);
  f->setConstructor(true);
  ctx->heap().pin(f); /* 分配保护（M3.3） */
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(f);
  f->setHolder(v);
  ctx->heap().unpin(f);
  return ok(v);
}

/* Function.prototype.call（§20.2.3.4）：func.call(thisArg, ...args)。
 * thisArg 原样绑定；参数逐个传给 func。 */
ConversionResult functionCall(JSContext *ctx, JSValue *thisVal,
                              JSValue *const *args, size_t argc) {
  if (!isCallable(thisVal)) {
    return fail("TypeError: Function.prototype.call called on non-function");
  }
  JSValue *thisArg = (argc > 0) ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *const *call_args = (argc > 1) ? &args[1] : nullptr;
  return call(ctx, thisVal, thisArg, call_args, argc > 0 ? argc - 1 : 0);
}

/* Function.prototype.apply（§20.2.3.2）：func.apply(thisArg, argArray)。
 * argArray 为数组（或类数组）时展开；本阶段只支持真数组。 */
ConversionResult functionApply(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *args, size_t argc) {
  if (!isCallable(thisVal)) {
    return fail("TypeError: Function.prototype.apply called on non-function");
  }
  JSValue *thisArg = (argc > 0) ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *argArray = (argc > 1) ? args[1] : ctx->createValue(Kind::Undefined);
  if (argArray == nullptr || argArray->kind() == Kind::Undefined ||
      argArray->kind() == Kind::Null) {
    return call(ctx, thisVal, thisArg, nullptr, 0);
  }
  if (!argArray->isObject() || !argArray->object()->isArray()) {
    return fail("TypeError: CreateListFromArrayLike called on non-array");
  }
  JSObject *arr = argArray->object();
  size_t n = arr->length();
  std::vector<JSValue *> elems;
  elems.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    JSValue *e = arr->getElement(i);
    elems.push_back(e != nullptr ? e : ctx->createValue(Kind::Undefined));
  }
  return call(ctx, thisVal, thisArg, elems.empty() ? nullptr : elems.data(), n);
}

/* Function.prototype.bind（§20.2.3.5）：func.bind(thisArg, ...args)。
 * 返回新函数：调用时 this 固定为 thisArg，参数 = 绑定参数 + 调用参数。
 * 绑定函数为 FunctionKind::Bound（§20.2.3.5 BoundFunctionCreate）：
 * [[BoundTargetFunction]]/[[BoundThis]]/[[BoundArguments]] 存 JSFunction
 * 专用字段（GC 引用边），call() 统一转发（无需回调）。 */
ConversionResult functionBind(JSContext *ctx, JSValue *thisVal,
                              JSValue *const *args, size_t argc) {
  if (!isCallable(thisVal)) {
    return fail("TypeError: Function.prototype.bind called on non-function");
  }
  JSValue *thisArg = (argc > 0) ? args[0] : ctx->createValue(Kind::Undefined);
  size_t bound_argc = argc > 0 ? argc - 1 : 0;
  JSValue *const *bound_args = bound_argc > 0 ? &args[1] : nullptr;

  /* 创建 Bound 函数：JSFunction + 包装 JSValue + 绑定元数据。
   * 两段式创建窗口 pin 保护（M3.3）；setBoundData 前元数据无引用。 */
  JSFunction *f = ctx->heap().allocGCObject<JSFunction>(
      nullptr /* prototype */, FunctionKind::Bound, nullptr /* func */,
      ThisMode::Strict);
  ctx->heap().pin(f);
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(f);
  f->setHolder(v);
  ctx->heap().unpin(f);
  ctx->heap().pin(v); /* 分配保护：setBoundData 前防误回收 */
  std::vector<JSValue *> bargs;
  bargs.reserve(bound_argc);
  for (size_t i = 0; i < bound_argc; ++i) {
    bargs.push_back(bound_args[i]);
  }
  f->setBoundData(thisVal, thisArg, std::move(bargs));
  ctx->heap().unpin(v);
  return ok(v);
}

/* -------------------------------------------------------------------------- */
/* Error 家族（M4.2，§20.5）                                                  */
/* -------------------------------------------------------------------------- */

/* Error 构造器（§20.5.1.1）：Error(message) -> 新错误对象。
 * 简化：只存 message 属性；stack/Error.captureStackTrace 留 M4.3。
 * 与 call 语义一致（new 或直接调用都产生错误对象，标准要求 new 才有）。 */
ConversionResult errorConstructor(JSContext *ctx, JSValue *thisVal,
                                  JSValue *const *args, size_t argc) {
  /* new 语义：construct() 已创建 this（原型 = 构造器.prototype，含
   * toString/constructor/name/message），此处只需初始化 message + name。
   * 直接调用 Error(msg)（无 new）也产生错误对象（本阶段简化，标准要求
   * new 才有）：此时 thisVal 可能非对象，回退创建新对象（原型 Object.
   * prototype，拿不到 toString——M4.3 补全普通调用语义）。 */
  JSValue *v = thisVal;
  if (v == nullptr || !v->isObject()) {
    v = ctx->createObject(objectProto(ctx)->object());
  }
  ctx->heap().pin(v); /* 分配保护：setProperty/createValue 分配窗口防误回收 */
  if (argc > 0 && args[0] != nullptr && args[0]->kind() != Kind::Undefined) {
    ConversionResult s = toString(ctx, args[0]);
    if (!s.ok()) {
      ctx->heap().unpin(v);
      return s;
    }
    v->object()->setProperty("message", s.value);
  } else {
    v->object()->setProperty("message", ctx->createValue(std::string("")));
  }
  v->object()->setProperty("name", ctx->createValue(std::string("Error")));
  ctx->heap().unpin(v);
  return ok(v);
}

/* 子类构造器（TypeError/RangeError/ReferenceError/SyntaxError，§20.5.6-9）。
 * 与 Error 构造器一致，仅 name 不同。返回对象在覆盖 name 前 pin 保护
 * （createValue 分配窗口防误回收，M3.3 模式）。 */
ConversionResult errorSubclass(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *args, size_t argc,
                               const char *name) {
  ConversionResult r = errorConstructor(ctx, thisVal, args, argc);
  if (r.ok() && r.value != nullptr) {
    ctx->heap().pin(r.value);
    r.value->object()->setProperty("name", ctx->createValue(std::string(name)));
    ctx->heap().unpin(r.value);
  }
  return r;
}

ConversionResult errorTypeError(JSContext *ctx, JSValue *thisVal,
                                JSValue *const *args, size_t argc) {
  return errorSubclass(ctx, thisVal, args, argc, "TypeError");
}

ConversionResult errorRangeError(JSContext *ctx, JSValue *thisVal,
                                 JSValue *const *args, size_t argc) {
  return errorSubclass(ctx, thisVal, args, argc, "RangeError");
}

ConversionResult errorReferenceError(JSContext *ctx, JSValue *thisVal,
                                     JSValue *const *args, size_t argc) {
  return errorSubclass(ctx, thisVal, args, argc, "ReferenceError");
}

ConversionResult errorSyntaxError(JSContext *ctx, JSValue *thisVal,
                                  JSValue *const *args, size_t argc) {
  return errorSubclass(ctx, thisVal, args, argc, "SyntaxError");
}

/* Error.prototype.toString（§20.5.3.4）："name: message"。 */
ConversionResult errorToString(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  if (thisVal == nullptr || !thisVal->isObject()) {
    return fail("TypeError: Error.prototype.toString called on non-object");
  }
  JSObject *o = thisVal->object();
  JSValue *name_v = o->getProperty("name");
  JSValue *msg_v = o->getProperty("message");
  std::string name = "Error";
  std::string msg;
  if (name_v != nullptr && name_v->kind() != Kind::Undefined) {
    ConversionResult s = toString(ctx, name_v);
    if (!s.ok()) return s;
    name = s.value->toString();
  }
  if (msg_v != nullptr && msg_v->kind() != Kind::Undefined) {
    ConversionResult s = toString(ctx, msg_v);
    if (!s.ok()) return s;
    msg = s.value->toString();
  }
  if (name.empty()) return ok(ctx->createValue(msg));
  if (msg.empty()) return ok(ctx->createValue(name));
  return ok(ctx->createValue(name + ": " + msg));
}

/* 注册 Error 构造器（§20.5）：
 *   name        全局名（Error/TypeError/...）
 *   ctor        构造器回调
 *   parentName  原型链父名（nullptr = Error，其余指向 Error.prototype）
 * 每个构造器：global[name] = ctor；ctor.prototype = 新错误原型
 *   {constructor: ctor, name, message: "", toString}；
 * 原型链：Error.prototype -> Object.prototype；子类 -> Error.prototype。 */
void installErrorBuiltin(JSContext *ctx, const char *name, JSBuiltinFn ctor,
                         const char *parentName) {
  JSValue *global = ctx->globalObject();
  JSValue *ctor_v = makeBuiltin(ctx, ctor, ThisMode::Strict);
  ctx->heap().pin(ctor_v); /* 分配保护：挂 global 前防误回收 */
  global->object()->setProperty(name, ctor_v);
  ctx->heap().unpin(ctor_v); /* 已入根（global 属性） */

  /* 原型对象（普通对象，非函数）。createObject 后未入根前，后续
   * setProperty / createValue / makeBuiltin 的分配可能触发 GC 误回收
   * （两段式窗口，M3.3 模式）：pin 包装器 JSValue（其 gcMarkRefs 传递
   * 标记内部 JSObject），挂到 ctor_v->prototype 后 unpin。 */
  JSValue *proto = ctx->createObject(objectProto(ctx)->object());
  ctx->heap().pin(proto);
  proto->object()->setProperty("constructor", ctor_v);
  proto->object()->setProperty("name", ctx->createValue(std::string(name)));
  proto->object()->setProperty("message", ctx->createValue(std::string("")));
  proto->object()->setProperty(
      "toString", makeBuiltin(ctx, errorToString, ThisMode::Strict));
  ctor_v->object()->setProperty("prototype", proto);
  ctx->heap().unpin(proto);

  /* 子类原型链：proto -> 父原型（Error.prototype 或 Object.prototype）。
   * 父原型在 installErrorBuiltin 中已建立（Error 先注册）。 */
  if (parentName != nullptr) {
    JSValue *parent_ctor = global->object()->getProperty(parentName);
    if (parent_ctor != nullptr && parent_ctor->isObject()) {
      JSValue *parent_proto = parent_ctor->object()->getProperty("prototype");
      if (parent_proto != nullptr && parent_proto->isObject()) {
        proto->object()->setPrototype(parent_proto->object());
      }
    }
  }
}

/* 注册一组包装对象内置（Number/String/Boolean 之一）：
 * 构造器挂 global，prototype 挂 constructor + valueOf/toString 最小方法集，
 * prototype 进 wrapper_protos_ 缓存（装箱原型链，§7.1.13）。
 * 包装原型链指向 Object.prototype（§20.1.3：包装对象也是对象）。 */
void installWrapperBuiltin(JSContext *ctx, const char *name, ObjectKind kind,
                           JSBuiltinFn ctor, JSBuiltinFn valueOf,
                           JSBuiltinFn toStringFn) {
  JSValue *global = ctx->globalObject();
  JSValue *ctor_v = makeBuiltin(ctx, ctor, ThisMode::Strict);
  ctx->heap().pin(ctor_v); /* 分配保护：挂 global 前防误回收 */
  global->object()->setProperty(name, ctor_v);
  ctx->heap().unpin(ctor_v); /* 已入根（global 属性） */

  JSValue *proto = ctx->createObject(objectProto(ctx)->object()); /* 原型链 -> Object.prototype */
  ctx->heap().pin(proto); /* 分配保护（M3.3）：入根前防误回收 */
  ctor_v->object()->setProperty("prototype", proto);
  proto->object()->setProperty("constructor", ctor_v);
  proto->object()->setProperty("valueOf",
                               makeBuiltin(ctx, valueOf, ThisMode::Strict));
  proto->object()->setProperty("toString",
                               makeBuiltin(ctx, toStringFn, ThisMode::Strict));
  ctx->setWrapperProto(kind, proto);
  ctx->heap().unpin(proto);
}

}  // namespace

JSValue *wellKnownSymbol(JSContext *ctx, WellKnown wks) {
  int index = static_cast<int>(wks);
  if (ctx->wellKnown(index) != nullptr) return ctx->wellKnown(index); /* 已创建 */

  /* 描述符（供 Symbol.prototype.description / toString）。 */
  const char *desc = "";
  switch (wks) {
    case WellKnown::Iterator: desc = "Symbol.iterator"; break;
    case WellKnown::ToPrimitive: desc = "Symbol.toPrimitive"; break;
    case WellKnown::ToStringTag: desc = "Symbol.toStringTag"; break;
    case WellKnown::HasInstance: desc = "Symbol.hasInstance"; break;
  }

  JSValue *sym = ctx->createValue(Kind::Symbol);
  sym->setSymbolDescription(desc);
  ctx->heap().pin(sym); /* 分配保护：进根集前防误回收 */
  ctx->setWellKnown(index, sym); /* 进根集（markRoots 遍历 well_known_） */
  ctx->heap().unpin(sym);
  return sym;
}

/* 注册 E4 内置对象到 globalThis。 */
void installBuiltins(JSContext *ctx) {
  JSValue *global = ctx->globalObject();

  /* globalThis 自引用（§19.1.1）：全局对象的属性，值指向全局对象自身。
   * 属性 {Writable:true, Enumerable:false, Configurable:true}。 */
  global->object()->setProperty("globalThis", global);

  /* 全局值属性（§19.1.2-4）：Infinity / NaN / undefined。
   * 均 {Writable:false, Enumerable:false, Configurable:false}。 */
  global->object()->setProperty("Infinity", ctx->createValue(INFINITY));
  global->object()->setProperty("NaN", ctx->createValue(NAN));
  global->object()->setProperty("undefined",
                               ctx->createValue(Kind::Undefined));

  /* Object 构造器（M3.3，§20.1.1）：先于包装对象建立 Object.prototype
   * 原型链根（包装对象/字面量/装箱都指向它）。 */
  JSValue *object = makeBuiltin(ctx, objectConstructor, ThisMode::Strict);
  ctx->heap().pin(object); /* 分配保护：挂 global 前防误回收 */
  global->object()->setProperty("Object", object);
  ctx->heap().unpin(object);

  JSObject *object_fn = object->object(); /* JSFunction 是 JSObject */
  object_fn->setProperty("getPrototypeOf",
                         makeBuiltin(ctx, objectGetPrototypeOf, ThisMode::Strict));
  object_fn->setProperty("setPrototypeOf",
                         makeBuiltin(ctx, objectSetPrototypeOf, ThisMode::Strict));
  object_fn->setProperty("create",
                         makeBuiltin(ctx, objectCreate, ThisMode::Strict));

  /* Object.prototype（§20.1.3）：原型为 null；进 context 缓存（objectProto）。
   * 普通对象字面量 / 装箱对象 / 包装对象原型的原型链都汇聚于此。 */
  JSValue *object_proto = objectProto(ctx);
  object_fn->setProperty("prototype", object_proto);
  object_proto->object()->setProperty("constructor", object);

  /* Symbol 构造器（§20.4.1.1）：内置函数 + well-known 静态属性（E4-1）。 */
  JSValue *symbol = makeBuiltin(ctx, symbolConstructor, ThisMode::Strict);
  ctx->heap().pin(symbol); /* 分配保护：挂 global 前防误回收 */
  global->object()->setProperty("Symbol", symbol);
  ctx->heap().unpin(symbol);

  JSObject *sym_obj = symbol->object(); /* JSFunction 是 JSObject */
  sym_obj->setProperty("iterator", wellKnownSymbol(ctx, WellKnown::Iterator));
  sym_obj->setProperty("toPrimitive",
                       wellKnownSymbol(ctx, WellKnown::ToPrimitive));
  sym_obj->setProperty("toStringTag",
                       wellKnownSymbol(ctx, WellKnown::ToStringTag));
  sym_obj->setProperty("hasInstance",
                       wellKnownSymbol(ctx, WellKnown::HasInstance));

  /* Symbol.prototype（§20.4.3）：valueOf/toString 最小方法集。 */
  JSValue *symbol_proto = ctx->createObject(objectProto(ctx)->object());
  ctx->heap().pin(symbol_proto); /* 分配保护（M3.3）：入根前防误回收 */
  sym_obj->setProperty("prototype", symbol_proto);
  symbol_proto->object()->setProperty("constructor", symbol);
  symbol_proto->object()->setProperty(
      "valueOf", makeBuiltin(ctx, symbolValueOf, ThisMode::Strict));
  symbol_proto->object()->setProperty(
      "toString", makeBuiltin(ctx, symbolToStringBuiltin, ThisMode::Strict));
  ctx->setWrapperProto(ObjectKind::Symbol, symbol_proto);
  ctx->heap().unpin(symbol_proto);

  /* 包装对象构造器 + prototype 最小方法集（E4-2）。 */
  installWrapperBuiltin(ctx, "Number", ObjectKind::Number, numberConstructor,
                        numberValueOf, numberToStringBuiltin);
  installWrapperBuiltin(ctx, "String", ObjectKind::String, stringConstructor,
                        stringValueOf, stringToStringBuiltin);
  installWrapperBuiltin(ctx, "Boolean", ObjectKind::Boolean, booleanConstructor,
                        booleanValueOf, booleanToStringBuiltin);

  /* Array 构造器（M4.1，§23.1.1）：数组字面量/Array() 的原型链根。
   * 需在 createArray 首次被调用前注册（vm NewArray / 数组字面量）。 */
  JSValue *array = makeBuiltin(ctx, arrayConstructor, ThisMode::Strict);
  ctx->heap().pin(array); /* 分配保护：挂 global 前防误回收 */
  global->object()->setProperty("Array", array);
  ctx->heap().unpin(array);
  array->object()->setProperty("isArray",
                               makeBuiltin(ctx, arrayIsArray, ThisMode::Strict));

  /* Array.prototype（§23.1.3）：进 context 缓存（arrayProto）。
   * 数组字面量 / Array() 新建数组的原型链汇聚于此。 */
  JSValue *array_proto = arrayProto(ctx);
  ctx->heap().pin(array_proto); /* 分配保护（M3.3）：入根前防误回收 */
  array->object()->setProperty("prototype", array_proto);
  array_proto->object()->setProperty("constructor", array);
  array_proto->object()->setProperty("push",
                                     makeBuiltin(ctx, arrayPush, ThisMode::Strict));
  array_proto->object()->setProperty("pop",
                                     makeBuiltin(ctx, arrayPop, ThisMode::Strict));
  array_proto->object()->setProperty("shift",
                                     makeBuiltin(ctx, arrayShift, ThisMode::Strict));
  array_proto->object()->setProperty("unshift",
                                     makeBuiltin(ctx, arrayUnshift, ThisMode::Strict));
  array_proto->object()->setProperty("indexOf",
                                     makeBuiltin(ctx, arrayIndexOf, ThisMode::Strict));
  array_proto->object()->setProperty("join",
                                     makeBuiltin(ctx, arrayJoin, ThisMode::Strict));
  array_proto->object()->setProperty("toString",
                                     makeBuiltin(ctx, arrayToString, ThisMode::Strict));
  array_proto->object()->setProperty("reverse",
                                     makeBuiltin(ctx, arrayReverse, ThisMode::Strict));
  ctx->heap().unpin(array_proto); /* 已挂 ctor->prototype 入根（M3.3） */

  /* Function 构造器（M4.2，§20.2.1）：new Function(...) 动态创建函数。
   * 本阶段简化：支持无参/空函数体（Function() -> 空函数返回 undefined）。
   * Function.prototype 是 Object.prototype 的下级（§20.2.3）。 */
  JSValue *function = makeBuiltin(ctx, functionConstructor, ThisMode::Strict);
  ctx->heap().pin(function); /* 分配保护：挂 global 前防误回收 */
  global->object()->setProperty("Function", function);
  ctx->heap().unpin(function);
  JSObject *function_fn = function->object();

  /* Function.prototype（§20.2.3）：原型链 -> Object.prototype。
   * 经 functionProto 惰性创建 + 进 context 缓存（函数原型链根，供 Script/
   * Builtin 函数挂原型）。此处复用缓存，避免重复创建。 */
  JSValue *function_proto = functionProto(ctx);
  ctx->heap().pin(function_proto); /* 分配保护（M3.3）：入根前防误回收 */
  function_fn->setProperty("prototype", function_proto);
  function_proto->object()->setProperty("constructor", function);

  /* Function.prototype.call/apply/bind（M4.2，§20.2.3）。 */
  function_proto->object()->setProperty(
      "apply", makeBuiltin(ctx, functionApply, ThisMode::Strict));
  function_proto->object()->setProperty(
      "call", makeBuiltin(ctx, functionCall, ThisMode::Strict));
  function_proto->object()->setProperty(
      "bind", makeBuiltin(ctx, functionBind, ThisMode::Strict));
  ctx->heap().unpin(function_proto); /* 已挂 ctor->prototype 入根（M3.3） */

  /* Error 家族（M4.2，§20.5）：Error + TypeError/RangeError/ReferenceError/
   * SyntaxError。原型链 Error.prototype -> Object.prototype；
   * 子类 TypeError.prototype -> Error.prototype。 */
  installErrorBuiltin(ctx, "Error", errorConstructor, nullptr);
  installErrorBuiltin(ctx, "TypeError", errorTypeError, "Error");
  installErrorBuiltin(ctx, "RangeError", errorRangeError, "Error");
  installErrorBuiltin(ctx, "ReferenceError", errorReferenceError, "Error");
  installErrorBuiltin(ctx, "SyntaxError", errorSyntaxError, "Error");
}

/* 取 Object.prototype（M3.3，惰性创建 + 缓存 + 进根集）。
 * 供 Object 构造器新建对象 / vm NewObject 字面量原型链使用。 */
JSValue *objectProto(JSContext *ctx) {
  JSValue *p = ctx->objectProto();
  if (p != nullptr) return p;
  p = ctx->createObject(nullptr); /* Object.prototype 原型为 null（§20.1.3） */
  ctx->heap().pin(p); /* 分配保护：进根集前防误回收 */
  ctx->setObjectProto(p); /* 进根集（markRoots 遍历 object_proto_） */
  ctx->heap().unpin(p);
  return p;
}

/* 取 Function.prototype（M4.2，惰性创建 + 缓存 + 进根集）。
 * 供 Script/Builtin 函数创建时挂原型（函数方法 call/apply/bind 所在）。
 * 原型链 -> Object.prototype（§20.2.3）。 */
JSValue *functionProto(JSContext *ctx) {
  JSValue *p = ctx->functionProto();
  if (p != nullptr) return p;
  p = ctx->createObject(objectProto(ctx)->object());
  ctx->heap().pin(p); /* 分配保护：进根集前防误回收 */
  ctx->setFunctionProto(p); /* 进根集（markRoots 遍历 function_proto_） */
  ctx->heap().unpin(p);
  return p;
}

/* 取 Array.prototype（M4.1，惰性创建 + 缓存 + 进根集）。
 * 供 Array 构造器新建数组 / vm NewArray 字面量原型链使用。
 * 注意：Array.prototype 自身是数组对象（§23.1.3），但创建时不能走
 * createArray（会回调 arrayProto 造成无限递归）——直接构造空数组。 */
JSValue *arrayProto(JSContext *ctx) {
  JSValue *p = ctx->arrayProto();
  if (p != nullptr) return p;
  JSObject *arr = ctx->heap().allocGCObject<JSObject>(ObjectKind::Array, 0);
  ctx->heap().pin(arr); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(arr);
  arr->setHolder(v);
  ctx->heap().unpin(arr);
  ctx->heap().pin(v); /* 分配保护：进根集（array_proto_）前防误回收 */
  ctx->setArrayProto(v);
  ctx->heap().unpin(v);
  return v;
}

}  // namespace neojs
