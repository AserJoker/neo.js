/*
 * 内部 C++ 实现：类型转换抽象运算（ECMA-262 抽象运算）。
 *
 * 遵循 docs/coding-standards.md：
 *   #3 禁止 C++ 异常跨 C ABI 边界——本模块不 throw（标准库可能抛的
 *      bad_alloc 由分配器统一处理，不捕获）。
 *   #4 JSValue 为一等公民，错误经 kind=Exception 值传递（ConversionResult）。
 *
 * 规范对照（ECMA-262）：
 *   §7.1.1    ToPrimitive
 *   §7.1.2    ToBoolean
 *   §7.1.3    ToNumeric
 *   §7.1.4    ToNumber（表 7 + StringToNumber §7.1.4.1）
 *   §7.1.13   ToObject（表 14）
 *   §7.1.17   ToString（表 8）
 *   §7.1.19   ToPropertyKey
 *   §7.2.10   SameValue
 *   §7.2.11   SameValueZero
 *   §6.1.6.1.20 Number::toString（最短十进制表示）
 *
 * 注意：Symbol 转换语义（ToString/ToNumber 抛 TypeError、ToPropertyKey
 * 保留）依赖 Kind::Symbol（E2 引入类型；well-known 表在 E4）。
 * ToObject 装箱与 OrdinaryToPrimitive 的方法调用依赖内置对象与
 * Callable（E3/E4），当前阶段返回占位错误。
 */

#include "neojs/runtime/conversion.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "neojs/runtime/context.h"
#include "neojs/runtime/object.h"

namespace neojs {

namespace {

/* 规范 §7.1.2 ToBoolean 判 falsey：undefined/null/false/+0/-0/NaN/空串。 */
bool isFalsey(const JSValue &v) {
  switch (v.kind()) {
    case Kind::Undefined:
    case Kind::Null:
      return true;
    case Kind::Boolean:
      return !v.toBoolean();
    case Kind::Number:
      return v.toNumber() == 0.0 || std::isnan(v.toNumber()); /* ±0 与 NaN */
    case Kind::String:
      return v.toString().empty();
    default:
      return false; /* Object / Symbol / Exception / Interrupt 均 truthy */
  }
}

/* 取对象属性值（own 沿原型链）；未找到返回 nullptr（非错误）。
 * ctx 当前未使用（属性读取不经 ctx）；E3 调用方法后启用。 */
JSValue *getProp(JSContext *ctx, JSValue *obj, const char *key) {
  (void)ctx;
  if (obj == nullptr || !obj->isObject()) return nullptr;
  return obj->object()->getProperty(key);
}

/* 创建装箱对象（E4-2）：包装对象原型链指向内置 prototype（若已注册）。 */
JSValue *makeWrapper(JSContext *ctx, ObjectKind kind, JSValue *data) {
  JSValue *w = ctx->createWrapper(kind, data);
  JSValue *proto = ctx->wrapperProto(kind);
  if (proto != nullptr && proto->isObject()) {
    w->object()->setPrototype(proto->object());
  }
  return w;
}

/* 失败结果（错误信息，value=nullptr）。 */
ConversionResult fail(const std::string &error) {
  ConversionResult r;
  r.error = error;
  return r;
}

/* 成功结果。 */
ConversionResult ok(JSValue *v) {
  ConversionResult r;
  r.value = v;
  return r;
}

/* -------------------------------------------------------------------------- */
/* StringToNumber（§7.1.4.1）：字符串解析为数字。                             */
/* 规范要点：跳过首尾 WhiteSpace/LineTerminator；空串（或全空白）-> +0；      */
/* 十六进制 0x/0X（无符号/小数点/指数）；十进制含指数；±Infinity；非法->NaN。 */
/* -------------------------------------------------------------------------- */

bool isWhiteSpace(int c) {
  /* 与 lexer.cc 的 u_isUWhiteSpace 白名单核心集合对齐（ASCII + 常见空白）。 */
  switch (c) {
    case 0x09: /* TAB */
    case 0x0B: /* VT */
    case 0x0C: /* FF */
    case 0x20: /* SP */
    case 0xA0: /* NBSP */
    case 0x1680:
    case 0x2000:
    case 0x2001:
    case 0x2002:
    case 0x2003:
    case 0x2004:
    case 0x2005:
    case 0x2006:
    case 0x2007:
    case 0x2008:
    case 0x2009:
    case 0x200A:
    case 0x202F:
    case 0x205F:
    case 0x3000:
    case 0xFEFF:
      return true;
    default:
      return false;
  }
}

bool isLineTerminator(int c) {
  return c == 0x0A || c == 0x0D || c == 0x2028 || c == 0x2029; /* LF CR LS PS */
}

double stringToNumber(const std::string &str) {
  /* 1. 跳过前导 WhiteSpace/LineTerminator（按字节处理 ASCII；多字节 Unicode
   *    空白 v1 简化不展开——与 lexer 的完整 ICU 白名单相比是已知差距）。 */
  size_t i = 0;
  const size_t n = str.size();
  while (i < n) {
    int c = static_cast<unsigned char>(str[i]);
    if (isWhiteSpace(c) || isLineTerminator(c)) {
      ++i;
    } else {
      break;
    }
  }
  /* 2. 跳过尾部空白。 */
  size_t end = n;
  while (end > i) {
    int c = static_cast<unsigned char>(str[end - 1]);
    if (isWhiteSpace(c) || isLineTerminator(c)) {
      --end;
    } else {
      break;
    }
  }
  if (i == end) return 0.0; /* 空串（或全空白）-> +0 */

  std::string s = str.substr(i, end - i);
  const size_t m = s.size();

  /* 3. ±Infinity。 */
  if (s == "Infinity" || s == "+Infinity") return INFINITY;
  if (s == "-Infinity") return -INFINITY;

  /* 4. 十六进制 0x/0X（规范：无符号、无小数点、无指数）。 */
  if (m >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    errno = 0;
    char *endp = nullptr;
    unsigned long long uv = std::strtoull(s.c_str() + 2, &endp, 16);
    if (endp == s.c_str() + 2 || *endp != '\0') return NAN; /* 无数字/残留 */
    if (errno == ERANGE) {
      /* 溢出：规范要求精确舍入到最近 double；v1 用 strtod 兜底（C99 hex
       * float 语义接近，接受精度差距）。 */
      return std::strtod(s.c_str(), nullptr);
    }
    return static_cast<double>(uv);
  }

  /* 5. 十进制（含符号/小数点/指数）。strtod 解析 + 全消耗校验。 */
  errno = 0;
  char *endp = nullptr;
  double d = std::strtod(s.c_str(), &endp);
  if (endp == s.c_str()) return NAN; /* 无数字前缀 */
  if (*endp != '\0') return NAN;     /* 未全消耗 */
  return d;
}

/* -------------------------------------------------------------------------- */
/* Number::toString（§6.1.6.1.20）：最短十进制表示。                          */
/* 用 std::to_chars 求最短往返表示（Ryu 算法，C++17），再按 JS 记数法规则     */
/* 重排。to_chars 输出形如 "123" / "1.5" / "0.00123" / "1e+21" / "1e-07"。   */
/* -------------------------------------------------------------------------- */

std::string numberToString(double x) {
  /* 1. NaN -> "NaN"；2. ±0 -> "0"；3. 负数递归；4. +Infinity。 */
  if (std::isnan(x)) return "NaN";
  if (x == 0.0) return "0";
  if (x < 0) return "-" + numberToString(-x);
  if (std::isinf(x)) return "Infinity";

  /* 5. 最短十进制表示：s × 10^(n-k)。k = 有效数字位数，n = 小数点位置。 */
  char buf[32];
  auto res = std::to_chars(buf, buf + sizeof(buf), x);
  std::string tmp(buf, res.ptr);

  /* 解析 to_chars 输出为 (digits, n, k)。 */
  size_t epos = tmp.find_first_of("eE");
  std::string mant = (epos == std::string::npos) ? tmp : tmp.substr(0, epos);
  int exp = 0;
  if (epos != std::string::npos) {
    exp = std::strtol(tmp.c_str() + epos + 1, nullptr, 10);
  }

  /* 完整数字串（去小数点）与小数点前位数（含前导 0）。 */
  std::string all;
  size_t dot = mant.find('.');
  std::string intpart = (dot == std::string::npos) ? mant : mant.substr(0, dot);
  std::string fracpart = (dot == std::string::npos) ? "" : mant.substr(dot + 1);
  all = intpart + fracpart;
  const int intlen = static_cast<int>(intpart.size()); /* 小数点前位数 */

  /* 第一个非零数字位置 p；有效数字 s = all[p..]，k = len。 */
  size_t p = all.find_first_not_of('0');
  if (p == std::string::npos) return "0"; /* 全 0（理论不可达，x != 0） */
  std::string s = all.substr(p);
  const int k = static_cast<int>(s.size());
  int n = intlen - static_cast<int>(p); /* 有效数字起点相对小数点的位置 */
  n += exp;                             /* 指数格式叠加 */

  /* 6. 记数法选择（§6.1.6.1.20 步骤 5-11 的等价形式）：
   *    a. k <= n <= 21       -> 整数（补 n-k 个 0）
   *    b. 0 < n <= 21        -> 带小数：前 n 位 . 剩余
   *    c. -6 < n <= 0        -> 纯小数：0. 后补 -n 个 0 + s
   *    d. 其余                -> 科学记数法 s[0][.s[1..]]e±(n-1) */
  if (k <= n && n <= 21) {
    return s + std::string(static_cast<size_t>(n - k), '0');
  }
  if (0 < n && n <= 21) {
    return s.substr(0, static_cast<size_t>(n)) + "." +
           s.substr(static_cast<size_t>(n));
  }
  if (-6 < n && n <= 0) {
    return "0." + std::string(static_cast<size_t>(-n), '0') + s;
  }
  /* 科学记数法。 */
  std::string out;
  out += s[0];
  if (k > 1) {
    out += ".";
    out += s.substr(1);
  }
  out += "e";
  int e = n - 1;
  if (e >= 0) out += "+";
  out += std::to_string(e);
  return out;
}

/* -------------------------------------------------------------------------- */
/* OrdinaryToPrimitive（§7.1.1.1）+ Symbol.toPrimitive（§7.1.1 步骤 1-5）     */
/* -------------------------------------------------------------------------- */

ConversionResult ordinaryToPrimitive(JSContext *ctx, JSValue *v, Hint hint) {
  /* v 是 Object（调用方保证）。按 hint 确定方法调用顺序。 */
  const char *first = (hint == Hint::String) ? "toString" : "valueOf";
  const char *second = (hint == Hint::String) ? "valueOf" : "toString";

  for (const char *name : {first, second}) {
    JSValue *m = getProp(ctx, v, name);
    if (m == nullptr) continue; /* 无此方法，尝试下一个 */
    /* 当前阶段无 Callable（E3 实现）：无法调用方法。
     * 此处为占位——E3 引入统一 Call 入口后，应调用 m(this) 并返回结果。
     * 现在跳过调用（等价于方法不存在），最终走 TypeError。 */
    (void)m;
    (void)ctx;
  }
  /* 两个方法都不可调用 -> TypeError。 */
  return fail("TypeError: Cannot convert object to primitive value");
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 抽象运算入口（公开接口，见 conversion.h）                                   */
/* -------------------------------------------------------------------------- */

bool toBoolean(const JSValue *v) {
  if (v == nullptr) return false;
  return !isFalsey(*v);
}

ConversionResult toPrimitive(JSContext *ctx, JSValue *v, Hint hint) {
  if (v == nullptr) return fail("TypeError: Cannot convert undefined or null");
  /* 原始值直接返回自身（含 Symbol：ToPrimitive 不转 Symbol）。 */
  switch (v->kind()) {
    case Kind::Undefined:
    case Kind::Null:
    case Kind::Boolean:
    case Kind::Number:
    case Kind::String:
    case Kind::Symbol:
      return ok(v);
    default:
      break;
  }
  /* Object：规范先查 @@toPrimitive（Symbol.well-known，E4 接入）。
   * 当前阶段无内置 Symbol，直接走 OrdinaryToPrimitive。 */
  return ordinaryToPrimitive(ctx, v, hint);
}

ConversionResult toNumber(JSContext *ctx, JSValue *v) {
  if (v == nullptr) return fail("TypeError: Cannot convert undefined or null");
  switch (v->kind()) {
    case Kind::Undefined:
      return ok(ctx->createValue(NAN));
    case Kind::Null:
      return ok(ctx->createValue(0.0));
    case Kind::Boolean:
      return ok(ctx->createValue(v->toBoolean() ? 1.0 : 0.0));
    case Kind::Number:
      return ok(ctx->createValue(v->toNumber()));
    case Kind::String:
      return ok(ctx->createValue(stringToNumber(v->toString())));
    case Kind::Object: {
      ConversionResult p = toPrimitive(ctx, v, Hint::Number);
      if (!p.ok()) return p;
      return toNumber(ctx, p.value);
    }
    case Kind::Symbol:
      return fail("TypeError: Cannot convert a Symbol value to a number");
    default:
      /* Exception / Interrupt 不可达（转换入口只收合法值）。 */
      return ok(ctx->createValue(NAN));
  }
}

ConversionResult toString(JSContext *ctx, JSValue *v) {
  if (v == nullptr) return fail("TypeError: Cannot convert undefined or null");
  switch (v->kind()) {
    case Kind::Undefined:
      return ok(ctx->createValue(std::string("undefined")));
    case Kind::Null:
      return ok(ctx->createValue(std::string("null")));
    case Kind::Boolean:
      return ok(ctx->createValue(v->toBoolean() ? std::string("true")
                                                : std::string("false")));
    case Kind::Number:
      return ok(ctx->createValue(numberToString(v->toNumber())));
    case Kind::String:
      return ok(v);
    case Kind::Object: {
      ConversionResult p = toPrimitive(ctx, v, Hint::String);
      if (!p.ok()) return p;
      return toString(ctx, p.value);
    }
    case Kind::Symbol:
      return fail("TypeError: Cannot convert a Symbol value to a string");
    default:
      return ok(ctx->createValue(std::string("")));
  }
}

ConversionResult toNumeric(JSContext *ctx, JSValue *v) {
  /* 当前仅 Number（BigInt 延后；BigInt 引入后此处返回 BigInt 原值）。 */
  return toNumber(ctx, v);
}

ConversionResult toPropertyKey(JSContext *ctx, JSValue *v) {
  /* §7.1.19：ToPrimitive(string) 后，Symbol 保留，其余转 String。 */
  ConversionResult p = toPrimitive(ctx, v, Hint::String);
  if (!p.ok()) return p;
  if (p.value->kind() == Kind::Symbol) return ok(p.value);
  return toString(ctx, p.value);
}

ConversionResult toObject(JSContext *ctx, JSValue *v) {
  if (v == nullptr) return fail("TypeError: Cannot convert undefined or null");
  switch (v->kind()) {
    case Kind::Undefined:
    case Kind::Null:
      return fail("TypeError: Cannot convert undefined or null to object");
    case Kind::Object:
      return ok(v); /* 已是对象，原样返回 */
    case Kind::Boolean:
      return ok(makeWrapper(ctx, ObjectKind::Boolean, v));
    case Kind::Number:
      return ok(makeWrapper(ctx, ObjectKind::Number, v));
    case Kind::String:
      return ok(makeWrapper(ctx, ObjectKind::String, v));
    case Kind::Symbol:
      return ok(makeWrapper(ctx, ObjectKind::Symbol, v));
    default:
      break;
  }
  /* Exception / Interrupt 不可达（转换入口只收合法值）。 */
  return fail("TypeError: Cannot convert to object");
}

/* -------------------------------------------------------------------------- */
/* SameValue / SameValueZero（§7.2.10 / §7.2.11）                              */
/* -------------------------------------------------------------------------- */

bool sameValueImpl(const JSValue *a, const JSValue *b, bool zeroEqual) {
  if (a->kind() != b->kind()) return false;
  switch (a->kind()) {
    case Kind::Undefined:
    case Kind::Null:
      return true;
    case Kind::Boolean:
      return a->toBoolean() == b->toBoolean();
    case Kind::Number: {
      double x = a->toNumber();
      double y = b->toNumber();
      if (std::isnan(x) && std::isnan(y)) return true; /* NaN == NaN */
      if (x == 0.0 && y == 0.0) return zeroEqual;      /* ±0 按需判等 */
      return x == y;
    }
    case Kind::String:
      return a->toString() == b->toString();
    case Kind::Object:
      return a->object() == b->object(); /* 同一引用 */
    case Kind::Symbol:
      return a == b; /* 同一 Symbol 值（引用相等） */
    default:
      return false;
  }
}

bool sameValueZero(const JSValue *a, const JSValue *b) {
  if (a == nullptr || b == nullptr) return a == b;
  return sameValueImpl(a, b, true);
}

bool sameValue(const JSValue *a, const JSValue *b) {
  if (a == nullptr || b == nullptr) return a == b;
  return sameValueImpl(a, b, false);
}

}  // namespace neojs
