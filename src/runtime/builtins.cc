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

#include <charconv>
#include <cmath>
#include <string>

#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/object.h"
#include "neojs/runtime/operators.h"
#include "neojs/runtime/utf16.h"
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

/* 前置声明：toIntegerOrInfinity 定义于 String 方法工具区（下方）。 */
double toIntegerOrInfinity(JSContext *ctx, JSValue *arg);

/* 创建内置函数（Builtin 类别）：JSFunction + 包装 JSValue + 反向指针。
 * 内部值指针语义（区别于 Host 句柄语义，见 function.h）。
 * 原型链 -> Function.prototype（惰性创建缓存）：内置函数同样可调
 * call/apply/bind（与 Script 函数一致，M4.2）。 */
JSValue *makeBuiltin(JSContext *ctx, JSBuiltinFn fn, ThisMode mode) {
  JSObject *fproto = nullptr;
  JSValue *fp = functionProto(ctx);
  if (fp != nullptr) fproto = fp->object();
  JSFunction *f = ctx->heap().allocGCObject<JSFunction>(fproto, fn, mode);
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

/* Number.prototype.toString（§21.1.3.6）：thisNumberValue -> ToString。
 * radix 参数（§6.1.6.1.1）：2-36 输出对应进制；缺省/10 走十进制最短表示；
 * 小数/非有限值/±0 一律十进制。radix 非法（<2 或 >36）-> RangeError。 */
ConversionResult numberToStringBuiltin(JSContext *ctx, JSValue *thisVal,
                                       JSValue *const *args, size_t argc) {
  ConversionResult x = thisWrapperValue(
      ctx, thisVal, Kind::Number, ObjectKind::Number,
      "Number.prototype.toString called on incompatible receiver");
  if (!x.ok()) return x;
  double d = x.value->toNumber();
  /* radix 缺参（argc==0 或 undefined）-> 十进制。 */
  if (argc > 0 && args[0] != nullptr && args[0]->kind() != Kind::Undefined) {
    double radix = toIntegerOrInfinity(ctx, args[0]);
    if (radix < 2.0 || radix > 36.0) {
      return fail("RangeError: toString() radix must be between 2 and 36");
    }
    if (radix != 10.0) {
      /* 非十进制：仅整数支持（§6.1.6.1.1 步骤 6：非有限值/±0/小数回退十进制）。 */
      if (!std::isnan(d) && !std::isinf(d) && d != 0.0 &&
          d == std::trunc(d)) {
        return ok(ctx->createValue(
            numberToStringRadix(d, static_cast<int>(radix))));
      }
    }
  }
  return toString(ctx, x.value);
}

/* -------------------------------------------------------------------------- */
/* Number.prototype 格式化方法（M4.3g，§21.1.3）                               */
/* -------------------------------------------------------------------------- */

/* Number.prototype.toFixed（§21.1.3.5）：thisNumberValue -> 定点记数法。
 * fractionDigits 缺参 -> 0；非整数 -> ToIntegerOrInfinity（截断）；
 * 越界（<0 或 >100）-> RangeError。 */
ConversionResult numberToFixedBuiltin(JSContext *ctx, JSValue *thisVal,
                                      JSValue *const *args, size_t argc) {
  ConversionResult x = thisWrapperValue(
      ctx, thisVal, Kind::Number, ObjectKind::Number,
      "Number.prototype.toFixed called on incompatible receiver");
  if (!x.ok()) return x;
  double d = x.value->toNumber();
  double f = toIntegerOrInfinity(ctx, (argc > 0) ? args[0] : nullptr);
  if (f < 0.0 || f > 100.0) {
    return fail("RangeError: toFixed() digits argument must be between 0 and 100");
  }
  return ok(ctx->createValue(numberToFixed(d, static_cast<int>(f))));
}

/* Number.prototype.toExponential（§21.1.3.2）：thisNumberValue -> 指数记数法。
 * fractionDigits 缺参 -> 不限制小数位（最短）；越界（<0 或 >100）-> RangeError。 */
ConversionResult numberToExponentialBuiltin(JSContext *ctx, JSValue *thisVal,
                                            JSValue *const *args, size_t argc) {
  ConversionResult x = thisWrapperValue(
      ctx, thisVal, Kind::Number, ObjectKind::Number,
      "Number.prototype.toExponential called on incompatible receiver");
  if (!x.ok()) return x;
  double d = x.value->toNumber();
  int digits = -1; /* 缺参标记 */
  if (argc > 0 && args[0] != nullptr && args[0]->kind() != Kind::Undefined) {
    double f = toIntegerOrInfinity(ctx, args[0]);
    if (f < 0.0 || f > 100.0) {
      return fail("RangeError: toExponential() argument must be between 0 and 100");
    }
    digits = static_cast<int>(f);
  }
  return ok(ctx->createValue(numberToExponential(d, digits)));
}

/* Number.prototype.toPrecision（§21.1.3.3）：thisNumberValue -> 按精度舍入。
 * precision 缺参 -> 最短十进制（toString）；越界（<1 或 >100）-> RangeError。 */
ConversionResult numberToPrecisionBuiltin(JSContext *ctx, JSValue *thisVal,
                                          JSValue *const *args, size_t argc) {
  ConversionResult x = thisWrapperValue(
      ctx, thisVal, Kind::Number, ObjectKind::Number,
      "Number.prototype.toPrecision called on incompatible receiver");
  if (!x.ok()) return x;
  double d = x.value->toNumber();
  if (argc == 0 || args[0] == nullptr || args[0]->kind() == Kind::Undefined) {
    return toString(ctx, x.value); /* 缺参 -> 最短十进制 */
  }
  double p = toIntegerOrInfinity(ctx, args[0]);
  if (p < 1.0 || p > 100.0) {
    return fail("RangeError: toPrecision() argument must be between 1 and 100");
  }
  return ok(ctx->createValue(numberToPrecision(d, static_cast<int>(p))));
}

/* Number.prototype.toLocaleString（§21.1.3.4）：本阶段无 locale，等价 toString。 */
ConversionResult numberToLocaleString(JSContext *ctx, JSValue *thisVal,
                                      JSValue *const *, size_t) {
  ConversionResult x = thisWrapperValue(
      ctx, thisVal, Kind::Number, ObjectKind::Number,
      "Number.prototype.toLocaleString called on incompatible receiver");
  if (!x.ok()) return x;
  return toString(ctx, x.value);
}

/* -------------------------------------------------------------------------- */
/* Number 静态方法（M4.3g，§21.1.2）                                           */
/* -------------------------------------------------------------------------- */

/* 本地空白判断（parseInt/parseFloat 前导剥离）：ASCII WhiteSpace ∪ LineTerminator。
 * 多字节 Unicode 空白 v1 简化不展开（与 conversion.cc 同级的已知差距）。 */
bool isJsSpace(int c) {
  switch (c) {
    case 0x09: /* TAB */
    case 0x0B: /* VT */
    case 0x0C: /* FF */
    case 0x20: /* SP */
    case 0xA0: /* NBSP */
    case 0xFEFF:
      return true;
    default:
      return false;
  }
}

bool isJsLineTerm(int c) {
  return c == 0x0A || c == 0x0D || c == 0x2028 || c == 0x2029;
}

/* Number.isFinite（§21.1.2.2）：仅 Number 且有限 -> true。 */
ConversionResult numberIsFinite(JSContext *ctx, JSValue *,
                                JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() != Kind::Number) {
    return ok(ctx->createValue(false));
  }
  double d = args[0]->toNumber();
  return ok(ctx->createValue(!std::isnan(d) && !std::isinf(d)));
}

/* Number.isInteger（§21.1.2.3）：仅 Number 且为整数（含 ±0）-> true。 */
ConversionResult numberIsInteger(JSContext *ctx, JSValue *,
                                 JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() != Kind::Number) {
    return ok(ctx->createValue(false));
  }
  double d = args[0]->toNumber();
  return ok(ctx->createValue(!std::isnan(d) && !std::isinf(d) &&
                             d == std::trunc(d)));
}

/* Number.isNaN（§21.1.2.4）：仅 Number 且 NaN -> true（不做类型转换）。 */
ConversionResult numberIsNaN(JSContext *ctx, JSValue *,
                             JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() != Kind::Number) {
    return ok(ctx->createValue(false));
  }
  return ok(ctx->createValue(std::isnan(args[0]->toNumber())));
}

/* Number.isSafeInteger（§21.1.2.5）：整数且在 [-2^53+1, 2^53-1] 内。 */
ConversionResult numberIsSafeInteger(JSContext *ctx, JSValue *,
                                     JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() != Kind::Number) {
    return ok(ctx->createValue(false));
  }
  double d = args[0]->toNumber();
  if (std::isnan(d) || std::isinf(d) || d != std::trunc(d)) {
    return ok(ctx->createValue(false));
  }
  const double maxSafe = 9007199254740991.0; /* 2^53 - 1 */
  return ok(ctx->createValue(std::fabs(d) <= maxSafe));
}

/* Number.parseFloat（§21.1.2.12 = §19.2.4 parseFloat）：
 * ToString 后解析前缀浮点数；空白剥离；首字符非法 -> NaN。 */
ConversionResult numberParseFloat(JSContext *ctx, JSValue *,
                                  JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr) {
    return ok(ctx->createValue(NAN));
  }
  ConversionResult s = toString(ctx, args[0]);
  if (!s.ok()) return s;
  const std::string &str = s.value->toString();
  /* 跳过前导空白（§19.2.4 步骤 3）。 */
  size_t i = 0;
  const size_t n = str.size();
  while (i < n) {
    int c = static_cast<unsigned char>(str[i]);
    if (isJsSpace(c) || isJsLineTerm(c)) {
      ++i;
    } else {
      break;
    }
  }
  if (i == n) return ok(ctx->createValue(NAN));
  /* 用 strtod 解析前缀（含 ±Infinity/0x 十六进制）。 */
  errno = 0;
  char *endp = nullptr;
  double d = std::strtod(str.c_str() + i, &endp);
  if (endp == str.c_str() + i) return ok(ctx->createValue(NAN));
  return ok(ctx->createValue(d));
}

/* Number.parseInt（§21.1.2.13 = §19.2.5 parseInt）：
 * ToString 后解析前缀整数；radix 2-36；空/首字符非法 -> NaN。 */
ConversionResult numberParseInt(JSContext *ctx, JSValue *,
                                JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr) {
    return ok(ctx->createValue(NAN));
  }
  ConversionResult s = toString(ctx, args[0]);
  if (!s.ok()) return s;
  const std::string &str = s.value->toString();
  size_t i = 0;
  const size_t n = str.size();
  while (i < n) {
    int c = static_cast<unsigned char>(str[i]);
    if (isJsSpace(c) || isJsLineTerm(c)) {
      ++i;
    } else {
      break;
    }
  }
  if (i == n) return ok(ctx->createValue(NAN));
  /* 符号。 */
  int sign = 1;
  if (str[i] == '+' || str[i] == '-') {
    if (str[i] == '-') sign = -1;
    ++i;
  }
  /* radix 解析（§19.2.5 步骤 5-9）：缺省/0/undefined -> 推断（0x -> 16，否则 10）。 */
  int radix = 10;
  if (argc > 1 && args[1] != nullptr && args[1]->kind() != Kind::Undefined) {
    double r = toIntegerOrInfinity(ctx, args[1]);
    if (r < 2.0 || r > 36.0) return ok(ctx->createValue(NAN));
    radix = static_cast<int>(r);
  }
  /* 0x 前缀推断（radix 缺省或 16 时）。 */
  if (i + 1 < n && str[i] == '0' && (str[i + 1] == 'x' || str[i + 1] == 'X')) {
    if (radix == 10) {
      radix = 16;
      i += 2;
    } else if (radix == 16) {
      i += 2;
    }
  }
  /* 解析前缀数字（radix 进制）。 */
  double value = 0.0;
  bool any = false;
  while (i < n) {
    int c = static_cast<unsigned char>(str[i]);
    int digit;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'z') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z') {
      digit = c - 'A' + 10;
    } else {
      break;
    }
    if (digit >= radix) break;
    value = value * radix + digit;
    any = true;
    ++i;
  }
  if (!any) return ok(ctx->createValue(NAN));
  return ok(ctx->createValue(sign * value));
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

/* -------------------------------------------------------------------------- */
/* String.prototype 方法补齐（M4.3f，§22.1.3）                                */
/* -------------------------------------------------------------------------- */

/* thisStringCoercible：RequireObjectCoercible(O) + ToString(O)。
 * 所有通用 String 方法的第一步（§22.1.3）：this 不必是 String，可转移
 * 到任意对象；undefined/null -> TypeError。返回 UTF-8 串副本。 */
ConversionResult thisStringCoercible(JSContext *ctx, JSValue *thisVal,
                                     std::string &out) {
  if (thisVal == nullptr || thisVal->kind() == Kind::Undefined ||
      thisVal->kind() == Kind::Null) {
    return fail("TypeError: Cannot convert undefined or null to object");
  }
  ConversionResult s = toString(ctx, thisVal);
  if (!s.ok()) return s;
  out = s.value->toString();
  return ok(s.value); /* 返回原始值便于复用（调用方取 out 拷贝） */
}

/* ToString(arg)，缺参（argc 边界 nullptr）按 undefined 处理（§22.1.3
 * 的 ToString(searchStr) 等步骤：缺参等价于传入 undefined）。
 * 内置函数经 function.cc 把缺参位置填充为 nullptr，需在此归一化。 */
ConversionResult toStringOrUndefined(JSContext *ctx, JSValue *arg) {
  if (arg == nullptr || arg->kind() == Kind::Undefined) {
    return ok(ctx->createValue(std::string("undefined")));
  }
  return toString(ctx, arg);
}

/* ToIntegerOrInfinity（§7.1.5）：ToNumber 后 NaN/±0 -> 0、±∞ -> ±∞、
 * 否则向零截断。供 at/charAt/.../repeat 等的索引/次数参数。 */
double toIntegerOrInfinity(JSContext *ctx, JSValue *arg) {
  if (arg == nullptr || arg->kind() == Kind::Undefined) return 0.0;
  ConversionResult n = toNumber(ctx, arg);
  if (!n.ok()) return 0.0; /* 转换失败（Symbol）：NaN 语义近似 0 */
  double d = n.value->toNumber();
  if (std::isnan(d) || d == 0.0) return 0.0;
  if (std::isinf(d)) return d;
  return std::trunc(d);
}

/* ToLength（§7.1.20）：ToIntegerOrInfinity 后 <=0 -> +0，否则
 * min(len, 2^53-1)。供 padStart/padEnd 的 maxLength。 */
double toLength(JSContext *ctx, JSValue *arg) {
  double d = toIntegerOrInfinity(ctx, arg);
  if (d <= 0.0) return 0.0;
  const double maxLen = 9007199254740991.0; /* 2^53 - 1 */
  return d > maxLen ? maxLen : d;
}

/* ToUint32（§7.1.7）：ToNumber 后取模 2^32（split 的 limit）。 */
uint32_t toUint32(JSContext *ctx, JSValue *arg) {
  double d = toIntegerOrInfinity(ctx, arg);
  double mod = std::fmod(d, 4294967296.0);
  if (mod < 0.0) mod += 4294967296.0;
  return static_cast<uint32_t>(mod);
}

/* GetSubstitution（§22.1.3.19.1，字符串替换模式）。captures 恒空（无正则）。 */
std::string substitution(const std::string &tmpl, const std::string &matched,
                         const std::string &str, std::size_t position) {
  std::string result;
  size_t i = 0;
  const size_t n = tmpl.size();
  while (i < n) {
    if (tmpl[i] != '$' || i + 1 >= n) {
      result += tmpl[i];
      ++i;
      continue;
    }
    char c = tmpl[i + 1];
    if (c == '$') {
      result += '$';
      i += 2;
    } else if (c == '&') {
      result += matched;
      i += 2;
    } else if (c == '`') {
      result += str.substr(0, utf16IndexToByte(str, position));
      i += 2;
    } else if (c == '\'') {
      std::string tail = utf16Substring(str, position + utf16Length(matched),
                                        utf16Length(str));
      result += tail;
      i += 2;
    } else if (c >= '0' && c <= '9') {
      /* 无捕获组：$n 原样保留（规范：index > captureLen 时按字面量）。 */
      result += tmpl[i];
      result += tmpl[i + 1];
      i += 2;
    } else {
      result += tmpl[i];
      ++i;
    }
  }
  return result;
}

/* String.prototype.at（§22.1.3.1）：相对索引（负数从尾数），越界 undefined。 */
ConversionResult stringAt(JSContext *ctx, JSValue *thisVal,
                          JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  double rel = toIntegerOrInfinity(ctx, (argc > 0) ? args[0] : nullptr);
  size_t len = utf16Length(s);
  double k = rel >= 0.0 ? rel : static_cast<double>(len) + rel;
  if (k < 0.0 || k >= static_cast<double>(len)) {
    return ok(ctx->createValue(Kind::Undefined));
  }
  return ok(ctx->createValue(utf16Substring(s, static_cast<size_t>(k),
                                            static_cast<size_t>(k) + 1)));
}

/* String.prototype.charAt（§22.1.3.2）：pos 处单 code unit 串；越界空串。 */
ConversionResult stringCharAt(JSContext *ctx, JSValue *thisVal,
                              JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  double pos = toIntegerOrInfinity(ctx, (argc > 0) ? args[0] : nullptr);
  size_t len = utf16Length(s);
  if (pos < 0.0 || pos >= static_cast<double>(len)) {
    return ok(ctx->createValue(std::string("")));
  }
  return ok(ctx->createValue(utf16Substring(s, static_cast<size_t>(pos),
                                            static_cast<size_t>(pos) + 1)));
}

/* String.prototype.charCodeAt（§22.1.3.3）：pos 处 code unit 数值；越界 NaN。 */
ConversionResult stringCharCodeAt(JSContext *ctx, JSValue *thisVal,
                                  JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  double pos = toIntegerOrInfinity(ctx, (argc > 0) ? args[0] : nullptr);
  size_t len = utf16Length(s);
  if (pos < 0.0 || pos >= static_cast<double>(len)) {
    return ok(ctx->createValue(NAN));
  }
  int32_t c = utf16CharCodeAt(s, static_cast<size_t>(pos));
  return ok(ctx->createValue(static_cast<double>(c)));
}

/* String.prototype.codePointAt（§22.1.3.4）：pos 处完整码点（代理对合并）；
 * 越界 undefined；孤立代理返回该 code unit 值。 */
ConversionResult stringCodePointAt(JSContext *ctx, JSValue *thisVal,
                                   JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  double pos = toIntegerOrInfinity(ctx, (argc > 0) ? args[0] : nullptr);
  size_t len = utf16Length(s);
  if (pos < 0.0 || pos >= static_cast<double>(len)) {
    return ok(ctx->createValue(Kind::Undefined));
  }
  size_t i = static_cast<size_t>(pos);
  int32_t hi = utf16CharCodeAt(s, i);
  if (hi >= 0xD800 && hi <= 0xDBFF && i + 1 < len) {
    int32_t lo = utf16CharCodeAt(s, i + 1);
    if (lo >= 0xDC00 && lo <= 0xDFFF) {
      int32_t cp = 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00);
      return ok(ctx->createValue(static_cast<double>(cp)));
    }
  }
  return ok(ctx->createValue(static_cast<double>(hi)));
}

/* String.prototype.concat（§22.1.3.5）：this 串拼接全部参数。 */
ConversionResult stringConcat(JSContext *ctx, JSValue *thisVal,
                              JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  for (size_t i = 0; i < argc; ++i) {
    ConversionResult t = toStringOrUndefined(ctx, args[i]);
    if (!t.ok()) return t;
    s += t.value->toString();
  }
  return ok(ctx->createValue(std::move(s)));
}

/* String.prototype.endsWith（§22.1.3.7）：S 以 searchStr 结尾（endPosition 处截断）。 */
ConversionResult stringEndsWith(JSContext *ctx, JSValue *thisVal,
                                JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  ConversionResult search = toStringOrUndefined(ctx, (argc > 0) ? args[0] : nullptr);
  if (!search.ok()) return search;
  const std::string &needle = search.value->toString();
  size_t len = utf16Length(s);
  double pos = (argc > 1 && args[1] != nullptr &&
                args[1]->kind() != Kind::Undefined)
                   ? toIntegerOrInfinity(ctx, args[1])
                   : static_cast<double>(len);
  double end = pos < 0.0 ? 0.0 : (pos > static_cast<double>(len)
                                      ? static_cast<double>(len)
                                      : pos);
  size_t searchLen = utf16Length(needle);
  if (searchLen == 0) return ok(ctx->createValue(true));
  if (static_cast<double>(searchLen) > end) {
    return ok(ctx->createValue(false));
  }
  size_t start = static_cast<size_t>(end) - searchLen;
  std::string sub = utf16Substring(s, start, static_cast<size_t>(end));
  return ok(ctx->createValue(sub == needle));
}

/* String.prototype.includes（§22.1.3.8）：S 在 position 起包含 searchStr。 */
ConversionResult stringIncludes(JSContext *ctx, JSValue *thisVal,
                                JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  ConversionResult search = toStringOrUndefined(ctx, (argc > 0) ? args[0] : nullptr);
  if (!search.ok()) return search;
  const std::string &needle = search.value->toString();
  double pos = toIntegerOrInfinity(ctx, (argc > 1) ? args[1] : nullptr);
  size_t len = utf16Length(s);
  double start = pos < 0.0 ? 0.0 : (pos > static_cast<double>(len)
                                        ? static_cast<double>(len)
                                        : pos);
  size_t idx = utf16IndexOf(s, needle, static_cast<size_t>(start));
  return ok(ctx->createValue(idx != std::string::npos));
}

/* String.prototype.indexOf（§22.1.3.9）：S 中 searchStr 首次出现索引；未找到 -1。 */
ConversionResult stringIndexOf(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  ConversionResult search = toStringOrUndefined(ctx, (argc > 0) ? args[0] : nullptr);
  if (!search.ok()) return search;
  const std::string &needle = search.value->toString();
  double pos = toIntegerOrInfinity(ctx, (argc > 1) ? args[1] : nullptr);
  size_t len = utf16Length(s);
  double start = pos < 0.0 ? 0.0 : (pos > static_cast<double>(len)
                                        ? static_cast<double>(len)
                                        : pos);
  size_t idx = utf16IndexOf(s, needle, static_cast<size_t>(start));
  if (idx == std::string::npos) return ok(ctx->createValue(-1.0));
  return ok(ctx->createValue(static_cast<double>(idx)));
}

/* String.prototype.lastIndexOf（§22.1.3.11）：searchStr 最后出现位置；
 * position 默认 +∞（从尾搜）；ToNumber 语义（undefined -> NaN -> +∞）。 */
ConversionResult stringLastIndexOf(JSContext *ctx, JSValue *thisVal,
                                   JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  ConversionResult search = toStringOrUndefined(ctx, (argc > 0) ? args[0] : nullptr);
  if (!search.ok()) return search;
  const std::string &needle = search.value->toString();
  double numPos = NAN;
  if (argc > 1 && args[1] != nullptr && args[1]->kind() != Kind::Undefined) {
    ConversionResult n = toNumber(ctx, args[1]);
    if (!n.ok()) return n;
    numPos = n.value->toNumber();
  }
  double pos = std::isnan(numPos) ? INFINITY : std::trunc(numPos);
  size_t len = utf16Length(s);
  size_t searchLen = utf16Length(needle);
  if (len < searchLen) return ok(ctx->createValue(-1.0));
  double maxStart = static_cast<double>(len - searchLen);
  double startD = pos < 0.0 ? 0.0 : (pos > maxStart ? maxStart : pos);
  size_t idx = utf16LastIndexOf(s, needle, static_cast<size_t>(startD));
  if (idx == std::string::npos) return ok(ctx->createValue(-1.0));
  return ok(ctx->createValue(static_cast<double>(idx)));
}

/* String.prototype.padStart/padEnd（§22.1.3.17/.16）：填充到 maxLength。 */
ConversionResult stringPad(JSContext *ctx, JSValue *thisVal, bool start,
                           JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  double intMax = toLength(ctx, (argc > 0) ? args[0] : nullptr);
  size_t stringLen = utf16Length(s);
  if (intMax <= static_cast<double>(stringLen)) return ok(ctx->createValue(s));
  std::string fill = " ";
  if (argc > 1 && args[1] != nullptr && args[1]->kind() != Kind::Undefined) {
    ConversionResult f = toString(ctx, args[1]);
    if (!f.ok()) return f;
    fill = f.value->toString();
  }
  if (fill.empty()) return ok(ctx->createValue(s));
  size_t fillLen = static_cast<size_t>(intMax) - stringLen;
  size_t fillUnits = utf16Length(fill);
  if (fillUnits == 0) return ok(ctx->createValue(s));
  /* 截断填充串到 fillLen code units（§22.1.3.17.2 步骤 5）。 */
  std::string filler;
  size_t remaining = fillLen;
  while (remaining > 0) {
    size_t take = std::min(remaining, fillUnits);
    filler += utf16Substring(fill, 0, take);
    remaining -= take;
  }
  return ok(ctx->createValue(start ? filler + s : s + filler));
}

ConversionResult stringPadStart(JSContext *ctx, JSValue *thisVal,
                                JSValue *const *args, size_t argc) {
  return stringPad(ctx, thisVal, true, args, argc);
}

ConversionResult stringPadEnd(JSContext *ctx, JSValue *thisVal,
                              JSValue *const *args, size_t argc) {
  return stringPad(ctx, thisVal, false, args, argc);
}

/* String.prototype.repeat（§22.1.3.18）：count 份 S 拼接；负数/∞ -> RangeError。 */
ConversionResult stringRepeat(JSContext *ctx, JSValue *thisVal,
                              JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  double n = toIntegerOrInfinity(ctx, (argc > 0) ? args[0] : nullptr);
  if (n < 0.0 || std::isinf(n)) {
    return fail("RangeError: Invalid count value");
  }
  if (n == 0.0) return ok(ctx->createValue(std::string("")));
  std::string out;
  out.reserve(s.size() * static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) out += s;
  return ok(ctx->createValue(std::move(out)));
}

/* String.prototype.replace（§22.1.3.19）：首处替换。
 * v1 简化：不做 Symbol.replace 方法分派（RegExp 未实现）；
 * replaceValue 支持字符串（含 $ 替换模式）；函数替换本阶段按 ToString
 * 处理（与 V8 有差异，标注已知差距——函数转源码串）。 */
ConversionResult stringReplace(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  /* searchValue：本阶段只处理字符串（RegExp 未实现 -> ToString 简化）。 */
  ConversionResult search = toStringOrUndefined(ctx, (argc > 0) ? args[0] : nullptr);
  if (!search.ok()) return search;
  const std::string &needle = search.value->toString();
  ConversionResult repl =
      toStringOrUndefined(ctx, (argc > 1) ? args[1] : nullptr);
  if (!repl.ok()) return repl;
  const std::string &templateStr = repl.value->toString();
  if (needle.empty()) {
    /* 空 search：匹配 S 开头（position 0），替换后接原串。 */
    std::string out = substitution(templateStr, needle, s, 0);
    out += s;
    return ok(ctx->createValue(std::move(out)));
  }
  size_t pos = utf16IndexOf(s, needle, 0);
  if (pos == std::string::npos) return ok(ctx->createValue(s));
  size_t b0 = utf16IndexToByte(s, pos);
  std::string preceding = s.substr(0, b0);
  std::string following = utf16Substring(s, pos + utf16Length(needle),
                                         utf16Length(s));
  std::string replacement =
      substitution(templateStr, needle, s, static_cast<size_t>(pos));
  return ok(ctx->createValue(preceding + replacement + following));
}

/* String.prototype.slice（§22.1.3.22）：负索引从尾数；end 默认串尾。 */
ConversionResult stringSlice(JSContext *ctx, JSValue *thisVal,
                             JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  size_t len = utf16Length(s);
  double intStart = toIntegerOrInfinity(ctx, (argc > 0) ? args[0] : nullptr);
  double fromD;
  if (std::isinf(intStart) && intStart < 0) {
    fromD = 0.0;
  } else if (intStart < 0.0) {
    fromD = std::max(static_cast<double>(len) + intStart, 0.0);
  } else {
    fromD = std::min(intStart, static_cast<double>(len));
  }
  double intEnd = (argc > 1 && args[1] != nullptr &&
                   args[1]->kind() != Kind::Undefined)
                      ? toIntegerOrInfinity(ctx, args[1])
                      : static_cast<double>(len);
  double toD;
  if (std::isinf(intEnd) && intEnd < 0) {
    toD = 0.0;
  } else if (intEnd < 0.0) {
    toD = std::max(static_cast<double>(len) + intEnd, 0.0);
  } else {
    toD = std::min(intEnd, static_cast<double>(len));
  }
  if (fromD >= toD) return ok(ctx->createValue(std::string("")));
  return ok(ctx->createValue(utf16Substring(
      s, static_cast<size_t>(fromD), static_cast<size_t>(toD))));
}

/* String.prototype.split（§22.1.3.23）：按分隔符切分。
 * v1：仅字符串分隔符（RegExp 未实现）；空分隔符 -> 单 code unit 元素；
 * limit 按 ToUint32；separator undefined -> [S]。 */
ConversionResult stringSplit(JSContext *ctx, JSValue *thisVal,
                             JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  /* limit（§22.1.3.23 步骤 5）：undefined -> 2^32-1，否则 ToUint32。 */
  uint32_t lim = 0xFFFFFFFFu;
  if (argc > 1 && args[1] != nullptr && args[1]->kind() != Kind::Undefined) {
    lim = toUint32(ctx, args[1]);
  }
  JSValue *arr = ctx->createArray(0);
  size_t len = utf16Length(s);
  /* separator undefined -> [S]（步骤 8）。 */
  bool sepUndef = argc == 0 || args[0] == nullptr ||
                  args[0]->kind() == Kind::Undefined;
  if (lim == 0) return ok(arr); /* 空结果 */
  if (sepUndef) {
    if (s.empty()) return ok(arr); /* 空串 split undefined -> [ ] */
    arr->object()->setElement(0, ctx->createValue(s));
    return ok(arr);
  }
  ConversionResult sep = toString(ctx, args[0]);
  if (!sep.ok()) return sep;
  const std::string &sepStr = sep.value->toString();
  size_t sepLen = utf16Length(sepStr);
  if (s.empty()) {
    /* 空串：分隔符为空也匹配 -> 空结果；否则 [""]。 */
    if (sepLen == 0) return ok(arr);
    arr->object()->setElement(0, ctx->createValue(std::string("")));
    return ok(arr);
  }
  if (sepLen == 0) {
    /* 空分隔符：拆成单 code unit（步骤 10，head 限 lim 个）。 */
    size_t outLen = std::min(static_cast<size_t>(lim), len);
    for (size_t i = 0; i < outLen; ++i) {
      arr->object()->setElement(
          i, ctx->createValue(utf16Substring(s, i, i + 1)));
    }
    return ok(arr);
  }
  size_t i = 0;
  size_t count = 0;
  size_t j = utf16IndexOf(s, sepStr, 0);
  while (j != std::string::npos) {
    arr->object()->setElement(
        count++, ctx->createValue(utf16Substring(s, i, j)));
    if (count == lim) return ok(arr);
    i = j + sepLen;
    j = utf16IndexOf(s, sepStr, i);
  }
  arr->object()->setElement(
      count, ctx->createValue(utf16Substring(s, i, len)));
  return ok(arr);
}

/* String.prototype.startsWith（§22.1.3.24）：S 从 position 起以 searchStr 开头。 */
ConversionResult stringStartsWith(JSContext *ctx, JSValue *thisVal,
                                  JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  ConversionResult search = toStringOrUndefined(ctx, (argc > 0) ? args[0] : nullptr);
  if (!search.ok()) return search;
  const std::string &needle = search.value->toString();
  double pos = (argc > 1 && args[1] != nullptr &&
                args[1]->kind() != Kind::Undefined)
                   ? toIntegerOrInfinity(ctx, args[1])
                   : 0.0;
  size_t len = utf16Length(s);
  double start = pos < 0.0 ? 0.0 : (pos > static_cast<double>(len)
                                        ? static_cast<double>(len)
                                        : pos);
  size_t searchLen = utf16Length(needle);
  if (searchLen == 0) return ok(ctx->createValue(true));
  double end = start + static_cast<double>(searchLen);
  if (end > static_cast<double>(len)) return ok(ctx->createValue(false));
  std::string sub = utf16Substring(s, static_cast<size_t>(start),
                                   static_cast<size_t>(end));
  return ok(ctx->createValue(sub == needle));
}

/* String.prototype.substring（§22.1.3.25）：夹取 + start>end 交换。 */
ConversionResult stringSubstring(JSContext *ctx, JSValue *thisVal,
                                 JSValue *const *args, size_t argc) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  size_t len = utf16Length(s);
  double intStart = toIntegerOrInfinity(ctx, (argc > 0) ? args[0] : nullptr);
  double intEnd = (argc > 1 && args[1] != nullptr &&
                   args[1]->kind() != Kind::Undefined)
                      ? toIntegerOrInfinity(ctx, args[1])
                      : static_cast<double>(len);
  auto clamp = [&](double d) {
    if (d < 0.0) return 0.0;
    if (d > static_cast<double>(len)) return static_cast<double>(len);
    return d;
  };
  double fs = clamp(intStart);
  double fe = clamp(intEnd);
  double from = std::min(fs, fe);
  double to = std::max(fs, fe);
  return ok(ctx->createValue(
      utf16Substring(s, static_cast<size_t>(from), static_cast<size_t>(to))));
}

/* String.prototype.toLowerCase/toUpperCase（§22.1.3.28/.30）：
 * Unicode Default Case Conversion（ICU root locale）。 */
ConversionResult stringToLowerCase(JSContext *ctx, JSValue *thisVal,
                                   JSValue *const *, size_t) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  return ok(ctx->createValue(utf16ToLower(s)));
}

ConversionResult stringToUpperCase(JSContext *ctx, JSValue *thisVal,
                                   JSValue *const *, size_t) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  return ok(ctx->createValue(utf16ToUpper(s)));
}

/* TrimString（§22.1.3.32.1）：按 UTF-16 code unit 判断空白。 */
ConversionResult stringTrim(JSContext *ctx, JSValue *thisVal, bool start,
                            bool end) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  size_t len = utf16Length(s);
  size_t lo = 0;
  size_t hi = len;
  if (start) {
    while (lo < hi && utf16IsWhiteSpace(
                          static_cast<uint16_t>(utf16CharCodeAt(s, lo)))) {
      ++lo;
    }
  }
  if (end) {
    while (hi > lo &&
           utf16IsWhiteSpace(
               static_cast<uint16_t>(utf16CharCodeAt(s, hi - 1)))) {
      --hi;
    }
  }
  return ok(ctx->createValue(utf16Substring(s, lo, hi)));
}

ConversionResult stringTrimAll(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  return stringTrim(ctx, thisVal, true, true);
}

ConversionResult stringTrimStart(JSContext *ctx, JSValue *thisVal,
                                 JSValue *const *, size_t) {
  return stringTrim(ctx, thisVal, true, false);
}

ConversionResult stringTrimEnd(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  return stringTrim(ctx, thisVal, false, true);
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

/* -------------------------------------------------------------------------- */
/* BigInt 构造器 + BigInt.prototype（M4.3e，§21.2）                            */
/* -------------------------------------------------------------------------- */

/* BigInt 构造器（§21.2.1.1）：BigInt(value) 按步骤执行：
 *   1. new 调用（NewTarget 非 undefined）-> TypeError（BigInt 不是构造器）。
 *   2. ToPrimitive(value, NUMBER)。
 *   3. 结果是 Number -> NumberToBigInt（非整数 -> RangeError）。
 *   4. 否则 ToBigInt（Boolean/BigInt/String；Undefined/Null/Symbol/Number -> TypeError）。
 * 无参/undefined -> TypeError（ToBigInt(undefined)）。 */
ConversionResult bigIntConstructor(JSContext *ctx, JSValue *thisVal,
                                   JSValue *const *args, size_t argc) {
  if (thisVal != nullptr && thisVal->isObject()) {
    /* construct() 传入新对象 this：new BigInt() 禁止（§21.2.1 步骤 1）。 */
    return fail("TypeError: BigInt is not a constructor");
  }
  JSValue *arg = (argc > 0) ? args[0] : nullptr;
  /* ToPrimitive(value, NUMBER)。 */
  ConversionResult prim = toPrimitive(ctx, arg, Hint::Number);
  if (!prim.ok()) return prim;
  if (prim.value->kind() == Kind::Number) {
    /* NumberToBigInt（§21.2.1.1.1）：非整数 -> RangeError。
     * 整数 Number -> 精确 BigInt（double 整数精确）。 */
    double d = prim.value->toNumber();
    if (std::isnan(d) || std::isinf(d) || std::trunc(d) != d) {
      return fail("RangeError: The number " + std::to_string(d) +
                  " cannot be converted to a BigInt because it is not an integer");
    }
    bool neg = d < 0.0;
    double mag = neg ? -d : d;
    /* double -> 十进制整数串（无指数，无小数点）。 */
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), mag);
    std::string digits(buf, res.ptr);
    return ok(ctx->createValue(JSValue(neg, digits)));
  }
  return toBigInt(ctx, prim.value);
}

/* BigInt.prototype.valueOf（§21.2.3.4）：thisBigIntValue。 */
ConversionResult bigIntValueOf(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  return thisWrapperValue(
      ctx, thisVal, Kind::BigInt, ObjectKind::BigInt,
      "BigInt.prototype.valueOf called on incompatible receiver");
}

/* BigInt.prototype.toString（§21.2.3.3）：thisBigIntValue -> radix 基数字符串。
 * radix 缺省 10；2..36 合法，其余 RangeError。本阶段只实现十进制（10）
 * 与非十进制报错（简化，M4.3e）。 */
ConversionResult bigIntToStringBuiltin(JSContext *ctx, JSValue *thisVal,
                                       JSValue *const *args, size_t argc) {
  ConversionResult x = thisWrapperValue(
      ctx, thisVal, Kind::BigInt, ObjectKind::BigInt,
      "BigInt.prototype.toString called on incompatible receiver");
  if (!x.ok()) return x;
  int radix = 10;
  if (argc > 0 && args[0] != nullptr && args[0]->kind() != Kind::Undefined) {
    ConversionResult n = toNumber(ctx, args[0]);
    if (!n.ok()) return n;
    double rd = n.value->toNumber();
    if (rd < 2.0 || rd > 36.0 || std::trunc(rd) != rd) {
      return fail("RangeError: toString() radix must be an integer between 2 and 36");
    }
    radix = static_cast<int>(rd);
  }
  /* 本阶段只实现十进制（10）；非十进制报错（简化，M4.3e）。 */
  if (radix != 10) {
    return fail("Error: BigInt.prototype.toString with radix other than 10 not implemented");
  }
  return toString(ctx, x.value); /* BigInt::toString(10)，负数带 "-" */
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
/* Map 构造器 + Map.prototype（M4.4b，§24.1）                                  */
/* -------------------------------------------------------------------------- */

/* 取 this 为 Map 的公共校验（§24.1.3）：非 Map 对象抛 TypeError。 */
JSObject *mapThis(JSValue *thisVal) {
  if (thisVal == nullptr || !thisVal->isObject() ||
      !thisVal->object()->isMap()) {
    return nullptr;
  }
  return thisVal->object();
}

/* Map 构造器（§24.1.1.1）：new Map(iterable)。
 * 本阶段迭代器协议未启用：iterable 为数组/类数组时按
 * [key, value] 对逐项插入；否则忽略（空 Map）。
 * 注意：construct() 预建的 this 是普通对象，此处直接创建并返回
 * 真正的 Map 对象（construct 步骤 4 采纳对象返回值）。 */
ConversionResult mapConstructor(JSContext *ctx, JSValue *, JSValue *const *args,
                                size_t argc) {
  JSValue *map = ctx->createMap();
  JSObject *obj = map->object();
  if (argc > 0 && args[0] != nullptr && args[0]->kind() != Kind::Undefined &&
      args[0]->isObject() && args[0]->object()->isArray()) {
    /* 数组 iterable：每项为 [key, value] 对。 */
    JSObject *items = args[0]->object();
    size_t n = items->length();
    for (size_t i = 0; i < n; ++i) {
      JSValue *pair = items->getElement(i);
      if (pair == nullptr || !pair->isObject() ||
          !pair->object()->isArray()) {
        return fail("TypeError: Iterator value is not an entry object");
      }
      JSObject *p = pair->object();
      JSValue *k = p->getElement(0);
      JSValue *v = p->getElement(1);
      obj->collectionSet(k != nullptr ? k : ctx->createValue(Kind::Undefined),
                         v != nullptr ? v : ctx->createValue(Kind::Undefined));
    }
  }
  return ok(map);
}

/* Map.prototype.set（§24.1.3.24）：设置键值对，返回 Map 自身。 */
ConversionResult mapSet(JSContext *ctx, JSValue *thisVal, JSValue *const *args,
                        size_t argc) {
  JSObject *m = mapThis(thisVal);
  if (m == nullptr) return fail("TypeError: Map.prototype.set called on non-map");
  JSValue *key = (argc > 0) ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *val = (argc > 1) ? args[1] : ctx->createValue(Kind::Undefined);
  m->collectionSet(key, val);
  return ok(thisVal);
}

/* Map.prototype.get（§24.1.3.9）：返回键对应值，未命中 undefined。 */
ConversionResult mapGet(JSContext *ctx, JSValue *thisVal, JSValue *const *args,
                        size_t argc) {
  JSObject *m = mapThis(thisVal);
  if (m == nullptr) return fail("TypeError: Map.prototype.get called on non-map");
  JSValue *key = (argc > 0) ? args[0] : ctx->createValue(Kind::Undefined);
  JSValue *v = m->collectionGet(key);
  return ok(v != nullptr ? v : ctx->createValue(Kind::Undefined));
}

/* Map.prototype.has（§24.1.3.10）：是否包含键。 */
ConversionResult mapHas(JSContext *ctx, JSValue *thisVal, JSValue *const *args,
                        size_t argc) {
  JSObject *m = mapThis(thisVal);
  if (m == nullptr) return fail("TypeError: Map.prototype.has called on non-map");
  JSValue *key = (argc > 0) ? args[0] : ctx->createValue(Kind::Undefined);
  return ok(ctx->createValue(m->collectionHas(key)));
}

/* Map.prototype.delete（§24.1.3.4）：删除键，返回是否删除。 */
ConversionResult mapDelete(JSContext *ctx, JSValue *thisVal,
                           JSValue *const *args, size_t argc) {
  JSObject *m = mapThis(thisVal);
  if (m == nullptr) {
    return fail("TypeError: Map.prototype.delete called on non-map");
  }
  JSValue *key = (argc > 0) ? args[0] : ctx->createValue(Kind::Undefined);
  return ok(ctx->createValue(m->collectionDelete(key)));
}

/* Map.prototype.clear（§24.1.3.2）：清空所有条目。 */
ConversionResult mapClear(JSContext *ctx, JSValue *thisVal, JSValue *const *,
                          size_t) {
  JSObject *m = mapThis(thisVal);
  if (m == nullptr) {
    return fail("TypeError: Map.prototype.clear called on non-map");
  }
  m->collectionClear();
  return ok(ctx->createValue(Kind::Undefined));
}

/* Map.prototype.size（§24.1.3.13）：条目数。 */
ConversionResult mapSizeGetter(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  JSObject *m = mapThis(thisVal);
  if (m == nullptr) {
    return fail("TypeError: Map.prototype.size called on non-map");
  }
  return ok(ctx->createValue(static_cast<double>(m->collectionSize())));
}

/* Map.prototype.forEach（§24.1.3.7）：按插入序对每项调用
 * callbackfn(value, key, map)。 */
ConversionResult mapForEach(JSContext *ctx, JSValue *thisVal,
                            JSValue *const *args, size_t argc) {
  JSObject *m = mapThis(thisVal);
  if (m == nullptr) {
    return fail("TypeError: Map.prototype.forEach called on non-map");
  }
  if (argc == 0 || args[0] == nullptr || !isCallable(args[0])) {
    return fail("TypeError: callback is not a function");
  }
  JSValue *cb = args[0];
  JSValue *thisArg = (argc > 1) ? args[1] : ctx->createValue(Kind::Undefined);
  /* 快照条目（回调可能修改 Map，规范按快照迭代）。 */
  std::vector<MapEntry> snapshot = m->collectionEntries();
  for (const MapEntry &e : snapshot) {
    JSValue *cbargs[3] = {e.value, e.key, thisVal};
    ConversionResult r = call(ctx, cb, thisArg, cbargs, 3);
    if (!r.ok()) return r;
  }
  return ok(ctx->createValue(Kind::Undefined));
}

/* -------------------------------------------------------------------------- */
/* Set 构造器 + Set.prototype（M4.4c，§24.2）                                  */
/* -------------------------------------------------------------------------- */

/* 取 this 为 Set 的公共校验（§24.2.3）：非 Set 对象抛 TypeError。 */
JSObject *setThis(JSValue *thisVal) {
  if (thisVal == nullptr || !thisVal->isObject() ||
      !thisVal->object()->isSet()) {
    return nullptr;
  }
  return thisVal->object();
}

/* Set 构造器（§24.2.1.1）：new Set(iterable)。
 * 本阶段迭代器协议未启用：iterable 为数组时逐项 add；否则忽略（空 Set）。
 * construct() 预建的 this 是普通对象，直接创建并返回真正的 Set 对象。 */
ConversionResult setConstructor(JSContext *ctx, JSValue *, JSValue *const *args,
                                size_t argc) {
  JSValue *set = ctx->createSet();
  JSObject *obj = set->object();
  if (argc > 0 && args[0] != nullptr && args[0]->kind() != Kind::Undefined &&
      args[0]->isObject() && args[0]->object()->isArray()) {
    /* 数组 iterable：逐项 add。 */
    JSObject *items = args[0]->object();
    size_t n = items->length();
    for (size_t i = 0; i < n; ++i) {
      JSValue *item = items->getElement(i);
      obj->collectionSet(
          item != nullptr ? item : ctx->createValue(Kind::Undefined),
          ctx->createValue(Kind::Undefined));
    }
  }
  return ok(set);
}

/* Set.prototype.add（§24.2.3.1）：加入元素，返回 Set 自身。 */
ConversionResult setAdd(JSContext *ctx, JSValue *thisVal, JSValue *const *args,
                        size_t argc) {
  JSObject *s = setThis(thisVal);
  if (s == nullptr) return fail("TypeError: Set.prototype.add called on non-set");
  JSValue *key = (argc > 0) ? args[0] : ctx->createValue(Kind::Undefined);
  s->collectionSet(key, ctx->createValue(Kind::Undefined));
  return ok(thisVal);
}

/* Set.prototype.has（§24.2.3.9）：是否包含元素。 */
ConversionResult setHas(JSContext *ctx, JSValue *thisVal, JSValue *const *args,
                        size_t argc) {
  JSObject *s = setThis(thisVal);
  if (s == nullptr) return fail("TypeError: Set.prototype.has called on non-set");
  JSValue *key = (argc > 0) ? args[0] : ctx->createValue(Kind::Undefined);
  return ok(ctx->createValue(s->collectionHas(key)));
}

/* Set.prototype.delete（§24.2.3.4）：删除元素，返回是否删除。 */
ConversionResult setDelete(JSContext *ctx, JSValue *thisVal,
                           JSValue *const *args, size_t argc) {
  JSObject *s = setThis(thisVal);
  if (s == nullptr) {
    return fail("TypeError: Set.prototype.delete called on non-set");
  }
  JSValue *key = (argc > 0) ? args[0] : ctx->createValue(Kind::Undefined);
  return ok(ctx->createValue(s->collectionDelete(key)));
}

/* Set.prototype.clear（§24.2.3.2）：清空所有元素。 */
ConversionResult setClear(JSContext *ctx, JSValue *thisVal, JSValue *const *,
                          size_t) {
  JSObject *s = setThis(thisVal);
  if (s == nullptr) {
    return fail("TypeError: Set.prototype.clear called on non-set");
  }
  s->collectionClear();
  return ok(ctx->createValue(Kind::Undefined));
}

/* Set.prototype.size（§24.2.3.10）：元素数。 */
ConversionResult setSizeGetter(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  JSObject *s = setThis(thisVal);
  if (s == nullptr) {
    return fail("TypeError: Set.prototype.size called on non-set");
  }
  return ok(ctx->createValue(static_cast<double>(s->collectionSize())));
}

/* Set.prototype.forEach（§24.2.3.6）：按插入序对每项调用
 * callbackfn(value, key, set)，key 与 value 相同。 */
ConversionResult setForEach(JSContext *ctx, JSValue *thisVal,
                            JSValue *const *args, size_t argc) {
  JSObject *s = setThis(thisVal);
  if (s == nullptr) {
    return fail("TypeError: Set.prototype.forEach called on non-set");
  }
  if (argc == 0 || args[0] == nullptr || !isCallable(args[0])) {
    return fail("TypeError: callback is not a function");
  }
  JSValue *cb = args[0];
  JSValue *thisArg = (argc > 1) ? args[1] : ctx->createValue(Kind::Undefined);
  /* 快照条目（回调可能修改 Set，规范按快照迭代）。 */
  std::vector<MapEntry> snapshot = s->collectionEntries();
  for (const MapEntry &e : snapshot) {
    JSValue *cbargs[3] = {e.key, e.key, thisVal};
    ConversionResult r = call(ctx, cb, thisArg, cbargs, 3);
    if (!r.ok()) return r;
  }
  return ok(ctx->createValue(Kind::Undefined));
}

/* -------------------------------------------------------------------------- */
/* 内建迭代器（M4.4f，§23.1.5/§23.2.5/§24.1.5/§24.2.5）                     */
/* -------------------------------------------------------------------------- */

/* 创建迭代结果对象 { value, done }（§7.4.7 CreateIterResultObject）。
 * 普通对象，原型 Object.prototype。 */
JSValue *createIterResult(JSContext *ctx, JSValue *value, bool done) {
  JSValue *r = ctx->createObject(objectProto(ctx)->object());
  r->object()->setProperty("value", value);
  r->object()->setProperty("done", ctx->createValue(done));
  return r;
}

/* 创建内建迭代器对象：kind 为 Array/String/Map/Set 之一，target 为被迭代
 * 对象（GC 引用边，经 iter_target_ 槽标记），index 为初始推进索引。
 * 两段式创建（pin/unpin 分配保护，docs/gc.md G1）。 */
JSValue *createCollectionIterator(JSContext *ctx, ObjectKind kind,
                                  JSValue *target, size_t index) {
  JSObject *iter = ctx->heap().allocGCObject<JSObject>(kind, 0);
  ctx->heap().pin(iter); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(iter);
  iter->setHolder(v);
  iter->setIterTarget(target);
  iter->setIterIndex(index);
  ctx->heap().unpin(iter);
  return v;
}

/* 公共校验：this 必须是对应 kind 的迭代器对象。 */
JSObject *iteratorThis(JSValue *thisVal, ObjectKind kind) {
  if (thisVal == nullptr || !thisVal->isObject() ||
      thisVal->object()->kind() != kind) {
    return nullptr;
  }
  return thisVal->object();
}

/* Array Iterator next（§23.1.5.2.1）：读 target[index]，推进 index；
 * 越界（含空洞——规范空洞按 undefined 产出）返回 {value, done:true}。
 * 空洞元素（elements 中 nullptr）产出 undefined（§23.1.5.2.1 步骤 8 的
 * Get 返回 undefined）。 */
ConversionResult arrayIteratorNext(JSContext *ctx, JSValue *thisVal,
                                   JSValue *const *, size_t) {
  JSObject *it = iteratorThis(thisVal, ObjectKind::ArrayIterator);
  if (it == nullptr) {
    return fail("TypeError: Array Iterator next called on non-iterator");
  }
  JSValue *target = it->iterTarget();
  if (target == nullptr || !target->isObject() || !target->object()->isArray()) {
    return fail("TypeError: Array Iterator target is not an array");
  }
  JSObject *arr = target->object();
  size_t index = it->iterIndex();
  if (index >= arr->length()) {
    return ok(createIterResult(ctx, ctx->createValue(Kind::Undefined), true));
  }
  it->setIterIndex(index + 1);
  JSValue *elem = arr->getElement(index);
  return ok(createIterResult(
      ctx, elem != nullptr ? elem : ctx->createValue(Kind::Undefined), false));
}

/* String Iterator next（§22.1.5.1.1）：按 UTF-16 code point 推进。
 * target 为原始字符串值（[[IteratedString]]）；每次产出完整码点
 * （代理对合并），孤立代理产出该 code unit 单字符。 */
ConversionResult stringIteratorNext(JSContext *ctx, JSValue *thisVal,
                                    JSValue *const *, size_t) {
  JSObject *it = iteratorThis(thisVal, ObjectKind::StringIterator);
  if (it == nullptr) {
    return fail("TypeError: String Iterator next called on non-iterator");
  }
  JSValue *target = it->iterTarget();
  if (target == nullptr || !target->isString()) {
    return fail("TypeError: String Iterator target is not a string");
  }
  const std::string &s = target->toString();
  size_t len = utf16Length(s);
  size_t index = it->iterIndex();
  if (index >= len) {
    return ok(createIterResult(ctx, ctx->createValue(Kind::Undefined), true));
  }
  /* 完整码点：代理对高代理时吞掉低代理（孤立代理保持单 code unit）。 */
  int32_t hi = utf16CharCodeAt(s, index);
  size_t next = index + 1;
  if (hi >= 0xD800 && hi <= 0xDBFF && next < len) {
    int32_t lo = utf16CharCodeAt(s, next);
    if (lo >= 0xDC00 && lo <= 0xDFFF) next = index + 2;
  }
  it->setIterIndex(next);
  JSValue *ch =
      ctx->createValue(utf16Substring(s, index, next));
  return ok(createIterResult(ctx, ch, false));
}

/* Map Iterator next（§24.1.5.2.1）：读 entries[index]，推进 index。
 * kind 区分 values/entries：entries 产出 [key, value] 数组。
 * Map Iterator 的 kind 由 iter_index_ 高 8 位编码（低位为条目索引）。
 * 本阶段简化：iter_index_ 直接存条目索引，kind 由创建时区分 ——
 * values/keys 产出对应值，entries 产出 [key, value] 数组。 */
ConversionResult mapIteratorNext(JSContext *ctx, JSValue *thisVal,
                                 JSValue *const *, size_t) {
  JSObject *it = iteratorThis(thisVal, ObjectKind::MapIterator);
  if (it == nullptr) {
    return fail("TypeError: Map Iterator next called on non-iterator");
  }
  JSValue *target = it->iterTarget();
  if (target == nullptr || !target->isObject() || !target->object()->isMap()) {
    return fail("TypeError: Map Iterator target is not a map");
  }
  JSObject *m = target->object();
  /* 快照（迭代期间修改 Map 的语义：规范允许但结果不定；按当前条目迭代）。 */
  const std::vector<MapEntry> &entries = m->collectionEntries();
  size_t index = it->iterIndex();
  if (index >= entries.size()) {
    return ok(createIterResult(ctx, ctx->createValue(Kind::Undefined), true));
  }
  it->setIterIndex(index + 1);
  const MapEntry &e = entries[index];
  return ok(createIterResult(ctx, e.value, false));
}

/* Set Iterator next（§24.2.5.2.1）：读 entries[index]（value 即 key）。 */
ConversionResult setIteratorNext(JSContext *ctx, JSValue *thisVal,
                                 JSValue *const *, size_t) {
  JSObject *it = iteratorThis(thisVal, ObjectKind::SetIterator);
  if (it == nullptr) {
    return fail("TypeError: Set Iterator next called on non-iterator");
  }
  JSValue *target = it->iterTarget();
  if (target == nullptr || !target->isObject() || !target->object()->isSet()) {
    return fail("TypeError: Set Iterator target is not a set");
  }
  JSObject *s = target->object();
  const std::vector<MapEntry> &entries = s->collectionEntries();
  size_t index = it->iterIndex();
  if (index >= entries.size()) {
    return ok(createIterResult(ctx, ctx->createValue(Kind::Undefined), true));
  }
  it->setIterIndex(index + 1);
  return ok(createIterResult(ctx, entries[index].key, false));
}

/* 创建迭代器 + 挂 next 方法的公共步骤：返回带 next 的内建迭代器对象。
 * next 方法为 Builtin 函数（原型 Function.prototype）。 */
JSValue *createIteratorWithNext(JSContext *ctx, ObjectKind kind,
                                JSValue *target, size_t index,
                                JSBuiltinFn next_fn) {
  JSValue *iter = createCollectionIterator(ctx, kind, target, index);
  ctx->heap().pin(iter); /* 分配保护：挂 next 前防误回收 */
  iter->object()->setProperty("next",
                              makeBuiltin(ctx, next_fn, ThisMode::Strict));
  ctx->heap().unpin(iter);
  return iter;
}

/* Array.prototype[Symbol.iterator]（§23.1.3.34）：返回 Array Iterator。
 * 本阶段简化：支持真数组（数组迭代）；类数组对象留后续。 */
ConversionResult arrayValuesBuiltin(JSContext *ctx, JSValue *thisVal,
                                    JSValue *const *, size_t) {
  if (thisVal == nullptr || !thisVal->isObject() ||
      !thisVal->object()->isArray()) {
    return fail("TypeError: Array.prototype[Symbol.iterator] called on non-array");
  }
  return ok(createIteratorWithNext(ctx, ObjectKind::ArrayIterator, thisVal, 0,
                                   arrayIteratorNext));
}

/* String.prototype[Symbol.iterator]（§22.1.3.32）：返回 String Iterator。
 * this 可为原始字符串值（ToObject 装箱后迭代原始值）。 */
ConversionResult stringIteratorBuiltin(JSContext *ctx, JSValue *thisVal,
                                       JSValue *const *, size_t) {
  std::string s;
  ConversionResult r = thisStringCoercible(ctx, thisVal, s);
  if (!r.ok()) return r;
  /* 目标存原始字符串值（[[IteratedString]]），stringIteratorNext 直接读。 */
  JSValue *str = ctx->createValue(s);
  return ok(createIteratorWithNext(ctx, ObjectKind::StringIterator, str, 0,
                                   stringIteratorNext));
}

/* Map.prototype.keys（§24.1.3.9）：返回 Map 键迭代器。
 * 本阶段 Map Iterator 只产 value（简化 entries 数组）；keys/values
 * 均产出对应值。后续迭代器 kind 细化留 M5。 */
ConversionResult mapKeysBuiltin(JSContext *ctx, JSValue *thisVal,
                                JSValue *const *, size_t) {
  JSObject *m = mapThis(thisVal);
  if (m == nullptr) {
    return fail("TypeError: Map.prototype.keys called on non-map");
  }
  return ok(createIteratorWithNext(ctx, ObjectKind::MapIterator, thisVal, 0,
                                   mapIteratorNext));
}

/* Map.prototype.values（§24.1.3.11）：返回 Map 值迭代器。 */
ConversionResult mapValuesBuiltin(JSContext *ctx, JSValue *thisVal,
                                  JSValue *const *, size_t) {
  JSObject *m = mapThis(thisVal);
  if (m == nullptr) {
    return fail("TypeError: Map.prototype.values called on non-map");
  }
  return ok(createIteratorWithNext(ctx, ObjectKind::MapIterator, thisVal, 0,
                                   mapIteratorNext));
}

/* Map.prototype.entries（§24.1.3.5）：返回 Map 条目迭代器。
 * 简化：条目迭代器同样产出 value（与 keys/values 相同）；
 * entries 的 [key, value] 数组产出留后续。 */
ConversionResult mapEntriesBuiltin(JSContext *ctx, JSValue *thisVal,
                                   JSValue *const *, size_t) {
  JSObject *m = mapThis(thisVal);
  if (m == nullptr) {
    return fail("TypeError: Map.prototype.entries called on non-map");
  }
  return ok(createIteratorWithNext(ctx, ObjectKind::MapIterator, thisVal, 0,
                                   mapIteratorNext));
}

/* Set.prototype.values（§24.2.3.11）：返回 Set 迭代器（值即键）。 */
ConversionResult setValuesBuiltin(JSContext *ctx, JSValue *thisVal,
                                  JSValue *const *, size_t) {
  JSObject *s = setThis(thisVal);
  if (s == nullptr) {
    return fail("TypeError: Set.prototype.values called on non-set");
  }
  return ok(createIteratorWithNext(ctx, ObjectKind::SetIterator, thisVal, 0,
                                   setIteratorNext));
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
  /* M4.3f：new String()/Number()/Boolean()/BigInt() 应返回对应包装对象
   * （String exotic object 等）。标记构造器包装 kind，construct() 据此创建。 */
  static_cast<JSFunction *>(ctor_v->object())->setConstructWrapperKind(kind);
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

  /* Number.prototype 方法补齐（M4.3g，§21.1.3）：toFixed/toExponential/
   * toPrecision/toLocaleString。toString 已支持 radix（numberToStringBuiltin）。 */
  JSValue *number_proto = ctx->wrapperProto(ObjectKind::Number);
  if (number_proto != nullptr && number_proto->isObject()) {
    JSObject *np = number_proto->object();
    np->setProperty("toFixed",
                    makeBuiltin(ctx, numberToFixedBuiltin, ThisMode::Strict));
    np->setProperty("toExponential",
                    makeBuiltin(ctx, numberToExponentialBuiltin, ThisMode::Strict));
    np->setProperty("toPrecision",
                    makeBuiltin(ctx, numberToPrecisionBuiltin, ThisMode::Strict));
    np->setProperty("toLocaleString",
                    makeBuiltin(ctx, numberToLocaleString, ThisMode::Strict));
  }

  /* Number 静态方法（M4.3g，§21.1.2）：isFinite/isInteger/isNaN/isSafeInteger/
   * parseFloat/parseInt + 常量属性。 */
  JSValue *number_ctor = ctx->globalObject()->object()->getProperty("Number");
  if (number_ctor != nullptr && number_ctor->isObject()) {
    JSObject *nf = number_ctor->object();
    nf->setProperty("isFinite", makeBuiltin(ctx, numberIsFinite, ThisMode::Strict));
    nf->setProperty("isInteger",
                    makeBuiltin(ctx, numberIsInteger, ThisMode::Strict));
    nf->setProperty("isNaN", makeBuiltin(ctx, numberIsNaN, ThisMode::Strict));
    nf->setProperty("isSafeInteger",
                    makeBuiltin(ctx, numberIsSafeInteger, ThisMode::Strict));
    nf->setProperty("parseFloat",
                    makeBuiltin(ctx, numberParseFloat, ThisMode::Strict));
    nf->setProperty("parseInt",
                    makeBuiltin(ctx, numberParseInt, ThisMode::Strict));
    /* 常量属性（§21.1.2.1/§21.1.2.6-15）。 */
    nf->setProperty("EPSILON", ctx->createValue(2.220446049250313e-16));
    nf->setProperty("MAX_SAFE_INTEGER", ctx->createValue(9007199254740991.0));
    nf->setProperty("MAX_VALUE", ctx->createValue(1.7976931348623157e+308));
    nf->setProperty("MIN_SAFE_INTEGER", ctx->createValue(-9007199254740991.0));
    nf->setProperty("MIN_VALUE", ctx->createValue(5e-324));
    nf->setProperty("NaN", ctx->createValue(NAN));
    nf->setProperty("NEGATIVE_INFINITY", ctx->createValue(-INFINITY));
    nf->setProperty("POSITIVE_INFINITY", ctx->createValue(INFINITY));
  }

  /* String.prototype 方法补齐（M4.3f，§22.1.3）：在 installWrapperBuiltin
   * 创建的 String.prototype 上注册全部字符串方法。原型经 wrapper_protos_
   * 缓存（装箱原型链 + 'str'.method() 方法调用都经此解析）。 */
  JSValue *string_proto = ctx->wrapperProto(ObjectKind::String);
  if (string_proto != nullptr && string_proto->isObject()) {
    JSObject *sp = string_proto->object();
    sp->setProperty("at", makeBuiltin(ctx, stringAt, ThisMode::Strict));
    sp->setProperty("charAt", makeBuiltin(ctx, stringCharAt, ThisMode::Strict));
    sp->setProperty("charCodeAt",
                    makeBuiltin(ctx, stringCharCodeAt, ThisMode::Strict));
    sp->setProperty("codePointAt",
                    makeBuiltin(ctx, stringCodePointAt, ThisMode::Strict));
    sp->setProperty("concat", makeBuiltin(ctx, stringConcat, ThisMode::Strict));
    sp->setProperty("endsWith",
                    makeBuiltin(ctx, stringEndsWith, ThisMode::Strict));
    sp->setProperty("includes",
                    makeBuiltin(ctx, stringIncludes, ThisMode::Strict));
    sp->setProperty("indexOf",
                    makeBuiltin(ctx, stringIndexOf, ThisMode::Strict));
    sp->setProperty("lastIndexOf",
                    makeBuiltin(ctx, stringLastIndexOf, ThisMode::Strict));
    sp->setProperty("padEnd",
                    makeBuiltin(ctx, stringPadEnd, ThisMode::Strict));
    sp->setProperty("padStart",
                    makeBuiltin(ctx, stringPadStart, ThisMode::Strict));
    sp->setProperty("repeat", makeBuiltin(ctx, stringRepeat, ThisMode::Strict));
    sp->setProperty("replace",
                    makeBuiltin(ctx, stringReplace, ThisMode::Strict));
    sp->setProperty("slice", makeBuiltin(ctx, stringSlice, ThisMode::Strict));
    sp->setProperty("split", makeBuiltin(ctx, stringSplit, ThisMode::Strict));
    sp->setProperty("startsWith",
                    makeBuiltin(ctx, stringStartsWith, ThisMode::Strict));
    sp->setProperty("substring",
                    makeBuiltin(ctx, stringSubstring, ThisMode::Strict));
    sp->setProperty("toLowerCase",
                    makeBuiltin(ctx, stringToLowerCase, ThisMode::Strict));
    sp->setProperty("toUpperCase",
                    makeBuiltin(ctx, stringToUpperCase, ThisMode::Strict));
    sp->setProperty("trim", makeBuiltin(ctx, stringTrimAll, ThisMode::Strict));
    sp->setProperty("trimStart",
                    makeBuiltin(ctx, stringTrimStart, ThisMode::Strict));
    sp->setProperty("trimEnd",
                    makeBuiltin(ctx, stringTrimEnd, ThisMode::Strict));
    /* String.prototype[Symbol.iterator]（M4.4f，§22.1.3.32）：字符串迭代。 */
    sp->setSymbolProperty(
        wellKnownSymbol(ctx, WellKnown::Iterator),
        makeBuiltin(ctx, stringIteratorBuiltin, ThisMode::Strict));
  }
  installWrapperBuiltin(ctx, "Boolean", ObjectKind::Boolean, booleanConstructor,
                        booleanValueOf, booleanToStringBuiltin);

  /* BigInt 构造器（M4.3e，§21.2.1）：BigInt(x) -> ToBigInt(x)；
   * new BigInt() 抛 TypeError（非构造器，仅可调用）。
   * 复用 installWrapperBuiltin：BigInt.prototype 挂 constructor/valueOf/toString，
   * 原型链 -> Object.prototype，进 wrapper_protos_ 缓存（装箱原型链）。 */
  installWrapperBuiltin(ctx, "BigInt", ObjectKind::BigInt, bigIntConstructor,
                        bigIntValueOf, bigIntToStringBuiltin);

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
  /* Array.prototype[Symbol.iterator]（M4.4f，§23.1.3.34）：数组迭代。 */
  array_proto->object()->setSymbolProperty(
      wellKnownSymbol(ctx, WellKnown::Iterator),
      makeBuiltin(ctx, arrayValuesBuiltin, ThisMode::Strict));
  ctx->heap().unpin(array_proto); /* 已挂 ctor->prototype 入根（M3.3） */

  /* Map 构造器（M4.4b，§24.1.1）：new Map(iterable)。
   * Map.prototype（§24.1.3）挂 set/get/has/delete/clear/size/forEach。 */
  JSValue *map = makeBuiltin(ctx, mapConstructor, ThisMode::Strict);
  ctx->heap().pin(map); /* 分配保护：挂 global 前防误回收 */
  global->object()->setProperty("Map", map);
  ctx->heap().unpin(map);

  JSValue *map_proto = mapProto(ctx);
  ctx->heap().pin(map_proto); /* 分配保护（M3.3）：入根前防误回收 */
  map->object()->setProperty("prototype", map_proto);
  map_proto->object()->setProperty("constructor", map);
  map_proto->object()->setProperty(
      "set", makeBuiltin(ctx, mapSet, ThisMode::Strict));
  map_proto->object()->setProperty(
      "get", makeBuiltin(ctx, mapGet, ThisMode::Strict));
  map_proto->object()->setProperty(
      "has", makeBuiltin(ctx, mapHas, ThisMode::Strict));
  map_proto->object()->setProperty(
      "delete", makeBuiltin(ctx, mapDelete, ThisMode::Strict));
  map_proto->object()->setProperty(
      "clear", makeBuiltin(ctx, mapClear, ThisMode::Strict));
  map_proto->object()->setProperty(
      "size", makeBuiltin(ctx, mapSizeGetter, ThisMode::Strict));
  map_proto->object()->setProperty(
      "forEach", makeBuiltin(ctx, mapForEach, ThisMode::Strict));
  /* Map.prototype 迭代器（M4.4f，§24.1.3）：keys/values/entries +
   * [Symbol.iterator]（别名 entries，§24.1.3.11）。 */
  map_proto->object()->setProperty(
      "keys", makeBuiltin(ctx, mapKeysBuiltin, ThisMode::Strict));
  map_proto->object()->setProperty(
      "values", makeBuiltin(ctx, mapValuesBuiltin, ThisMode::Strict));
  map_proto->object()->setProperty(
      "entries", makeBuiltin(ctx, mapEntriesBuiltin, ThisMode::Strict));
  map_proto->object()->setSymbolProperty(
      wellKnownSymbol(ctx, WellKnown::Iterator),
      makeBuiltin(ctx, mapEntriesBuiltin, ThisMode::Strict));
  ctx->heap().unpin(map_proto); /* 已挂 ctor->prototype 入根（M3.3） */

  /* Set 构造器（M4.4c，§24.2.1）：new Set(iterable)。
   * Set.prototype（§24.2.3）挂 add/has/delete/clear/size/forEach。 */
  JSValue *set = makeBuiltin(ctx, setConstructor, ThisMode::Strict);
  ctx->heap().pin(set); /* 分配保护：挂 global 前防误回收 */
  global->object()->setProperty("Set", set);
  ctx->heap().unpin(set);

  JSValue *set_proto = setProto(ctx);
  ctx->heap().pin(set_proto); /* 分配保护（M3.3）：入根前防误回收 */
  set->object()->setProperty("prototype", set_proto);
  set_proto->object()->setProperty("constructor", set);
  set_proto->object()->setProperty(
      "add", makeBuiltin(ctx, setAdd, ThisMode::Strict));
  set_proto->object()->setProperty(
      "has", makeBuiltin(ctx, setHas, ThisMode::Strict));
  set_proto->object()->setProperty(
      "delete", makeBuiltin(ctx, setDelete, ThisMode::Strict));
  set_proto->object()->setProperty(
      "clear", makeBuiltin(ctx, setClear, ThisMode::Strict));
  set_proto->object()->setProperty(
      "size", makeBuiltin(ctx, setSizeGetter, ThisMode::Strict));
  set_proto->object()->setProperty(
      "forEach", makeBuiltin(ctx, setForEach, ThisMode::Strict));
  /* Set.prototype 迭代器（M4.4f，§24.2.3）：values + [Symbol.iterator]
   * （别名 values，§24.2.3.11）。keys/entries 与 values 同义（Set 值即键）。 */
  set_proto->object()->setProperty(
      "values", makeBuiltin(ctx, setValuesBuiltin, ThisMode::Strict));
  set_proto->object()->setSymbolProperty(
      wellKnownSymbol(ctx, WellKnown::Iterator),
      makeBuiltin(ctx, setValuesBuiltin, ThisMode::Strict));
  ctx->heap().unpin(set_proto); /* 已挂 ctor->prototype 入根（M3.3） */

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

/* 取 Map.prototype（M4.4a，惰性创建 + 缓存 + 进根集）。
 * new Map() 新建 Map 的原型链根（set/get/has 等所在）。
 * 原型链 -> Object.prototype（§24.1.3）。 */
JSValue *mapProto(JSContext *ctx) {
  JSValue *p = ctx->mapProto();
  if (p != nullptr) return p;
  p = ctx->createObject(objectProto(ctx)->object());
  ctx->heap().pin(p); /* 分配保护（M3.3）：进根集前防误回收 */
  ctx->setMapProto(p); /* 进根集（markRoots 遍历 map_proto_） */
  ctx->heap().unpin(p);
  return p;
}

/* 取 Set.prototype（M4.4a，惰性创建 + 缓存 + 进根集）。
 * new Set() 新建 Set 的原型链根（add/has 等所在）。
 * 原型链 -> Object.prototype（§24.2.3）。 */
JSValue *setProto(JSContext *ctx) {
  JSValue *p = ctx->setProto();
  if (p != nullptr) return p;
  p = ctx->createObject(objectProto(ctx)->object());
  ctx->heap().pin(p); /* 分配保护（M3.3）：进根集前防误回收 */
  ctx->setSetProto(p); /* 进根集（markRoots 遍历 set_proto_） */
  ctx->heap().unpin(p);
  return p;
}

}  // namespace neojs
