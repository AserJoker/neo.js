/*
 * 内部 C++ 实现：运算符语义（E4-3 最小语法绑定集）。
 *
 * 遵循 docs/coding-standards.md：
 *   #3 禁止 C++ 异常跨 C ABI 边界——本模块不 throw，错误经 ConversionResult
 *      （value=nullptr + error）传递（#4）。
 *   #4 JSValue 为一等公民，错误经 kind=Exception 值传递。
 *
 * 规范对照（ECMA-262）：
 *   §7.2.13  IsLessThan（Abstract Relational Comparison）
 *   §7.2.14  Strict Equality Comparison
 *   §7.2.15  Abstract Equality Comparison
 *   §7.3.20  in 运算符（HasProperty）
 *   §7.3.22  InstanceofOperator
 *   §13.5.3  typeof 运算符
 *   §13.15.2 加法运算符（+）
 */

#include "neojs/runtime/operators.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "neojs/runtime/context.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/object.h"
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

/* 取对象属性值（own 沿原型链）；未找到返回 nullptr（非错误）。 */
JSValue *getProp(JSObject *obj, const char *key) {
  if (obj == nullptr) return nullptr;
  return obj->getProperty(key);
}

/* BigInt 与 Number 精确相等比较（定义见下文 abstractEqualsImpl 之后）。 */
ConversionResult bigIntNumberEquals(JSContext *ctx, JSValue *big, double x);

/* -------------------------------------------------------------------------- */
/* 严格相等比较（§7.2.14）                                                    */
/* -------------------------------------------------------------------------- */

/* 同类型值比较：Number 用 IEEE（NaN!=NaN，+0==-0），String 按值，其余引用。 */
bool strictEqualsSameType(const JSValue *a, const JSValue *b) {
  switch (a->kind()) {
    case Kind::Undefined:
    case Kind::Null:
      return true;
    case Kind::Boolean:
      return a->toBoolean() == b->toBoolean();
    case Kind::Number:
      return a->toNumber() == b->toNumber(); /* NaN!=NaN；+0==-0 */
    case Kind::String:
      return a->toString() == b->toString();
    case Kind::BigInt:
      /* BigInt::equal（§6.1.6.2.13）：同符号同数字串。 */
      return a->bigIntNegative() == b->bigIntNegative() &&
             a->bigIntDigits() == b->bigIntDigits();
    case Kind::Symbol:
      return a == b; /* 同一 Symbol 值 */
    case Kind::Object:
      return a->object() == b->object(); /* 同一引用 */
    default:
      return false; /* Exception/Interrupt 不可达 */
  }
}

/* -------------------------------------------------------------------------- */
/* 抽象相等比较（§7.2.15）                                                    */
/* -------------------------------------------------------------------------- */

/* IsLooselyEqual（§7.2.13）：类型不同时按规范步骤 2-13 转换比较。 */
ConversionResult abstractEqualsImpl(JSContext *ctx, JSValue *x, JSValue *y) {
  if (x->kind() == y->kind()) {
    /* 同类型：严格相等语义（Number 用 IEEE）。 */
    return ok(ctx->createValue(strictEqualsSameType(x, y)));
  }

  /* 类型不同。规范 §7.2.13 步骤 2-13。 */
  switch (x->kind()) {
    case Kind::Undefined:
    case Kind::Null:
      /* null == undefined（及 undefined == null） */
      return ok(ctx->createValue(
          y->kind() == Kind::Undefined || y->kind() == Kind::Null));
    case Kind::Number:
      if (y->kind() == Kind::String) {
        /* x Number, y String：y ToNumber 后递归 */
        ConversionResult n = toNumber(ctx, y);
        if (!n.ok()) return n;
        return abstractEqualsImpl(ctx, x, n.value);
      }
      if (y->kind() == Kind::BigInt) {
        /* x Number, y BigInt（§7.2.13 步骤 13）：任一非有限 -> false；
         * 否则比较 x 与 BigInt 的精确值。 */
        return bigIntNumberEquals(ctx, y, x->toNumber());
      }
      if (y->kind() == Kind::Boolean) {
        /* x Number, y Boolean：y ToNumber 后递归 */
        ConversionResult n = toNumber(ctx, y);
        if (!n.ok()) return n;
        return abstractEqualsImpl(ctx, x, n.value);
      }
      if (y->kind() == Kind::Object) {
        /* x Number, y Object：y ToPrimitive 后递归 */
        ConversionResult p = toPrimitive(ctx, y, Hint::Number);
        if (!p.ok()) return p;
        return abstractEqualsImpl(ctx, x, p.value);
      }
      break;
    case Kind::BigInt:
      if (y->kind() == Kind::String) {
        /* x BigInt, y String（步骤 7）：y StringToBigInt，失败 -> false */
        std::string sy = stringToBigInt(y->toString());
        if (sy.empty()) return ok(ctx->createValue(false));
        bool neg = (sy[0] == '-');
        std::string digits = neg ? sy.substr(1) : sy;
        if (digits.empty()) digits = "0";
        JSValue bigY(false, digits);
        if (neg) bigY.setBigInt(true, digits);
        return ok(ctx->createValue(strictEqualsSameType(x, &bigY)));
      }
      if (y->kind() == Kind::Number) {
        return bigIntNumberEquals(ctx, x, y->toNumber());
      }
      if (y->kind() == Kind::Boolean) {
        /* x BigInt, y Boolean：y ToNumber 后递归（x 变 BigInt vs Number） */
        ConversionResult n = toNumber(ctx, y);
        if (!n.ok()) return n;
        return abstractEqualsImpl(ctx, x, n.value);
      }
      if (y->kind() == Kind::Object) {
        /* x BigInt, y Object：y ToPrimitive 后递归 */
        ConversionResult p = toPrimitive(ctx, y, Hint::Number);
        if (!p.ok()) return p;
        return abstractEqualsImpl(ctx, x, p.value);
      }
      break;
    case Kind::String:
      if (y->kind() == Kind::Number) {
        ConversionResult n = toNumber(ctx, x);
        if (!n.ok()) return n;
        return abstractEqualsImpl(ctx, n.value, y);
      }
      if (y->kind() == Kind::BigInt) {
        /* x String, y BigInt（步骤 8）：转置到 BigInt vs String */
        return abstractEqualsImpl(ctx, y, x);
      }
      if (y->kind() == Kind::Boolean) {
        ConversionResult n = toNumber(ctx, y);
        if (!n.ok()) return n;
        return abstractEqualsImpl(ctx, x, n.value);
      }
      if (y->kind() == Kind::Object) {
        ConversionResult p = toPrimitive(ctx, y, Hint::Number);
        if (!p.ok()) return p;
        return abstractEqualsImpl(ctx, x, p.value);
      }
      break;
    case Kind::Boolean:
      /* x Boolean：x ToNumber 后递归 */
      if (y->kind() == Kind::Number || y->kind() == Kind::String ||
          y->kind() == Kind::BigInt || y->kind() == Kind::Object) {
        ConversionResult n = toNumber(ctx, x);
        if (!n.ok()) return n;
        return abstractEqualsImpl(ctx, n.value, y);
      }
      break;
    case Kind::Object:
      /* x Object：x ToPrimitive(number) 后递归。 */
      if (y->kind() == Kind::Number || y->kind() == Kind::String ||
          y->kind() == Kind::BigInt || y->kind() == Kind::Boolean) {
        ConversionResult p = toPrimitive(ctx, x, Hint::Number);
        if (!p.ok()) return p;
        return abstractEqualsImpl(ctx, p.value, y);
      }
      break;
    case Kind::Symbol:
      /* Symbol 与对象比较：仅当同引用时相等（上面同类型已处理）；此处
       * Symbol vs 非对象原始值 -> false。 */
      if (y->kind() == Kind::Object) {
        return ok(ctx->createValue(x == y));
      }
      return ok(ctx->createValue(false));
    default:
      break;
  }

  /* 规范步骤 14：其余情况返回 false。 */
  return ok(ctx->createValue(false));
}

/* -------------------------------------------------------------------------- */
/* BigInt 与 Number 相等比较（§7.2.13 步骤 13）                                */
/* 任一非有限 -> false；否则比较 Number 与 BigInt 的精确数学值。              */
/* 方法：把非负整数 double 的精确整数值（IEEE 754：mant53 × 2^(exp-53)）用
 * 十进制字符串算术精确构造，再与 BigInt 十进制串逐位比较。完全精确，无
 * 精度损失（正确性优先，性能可接受）。                                      */
/* -------------------------------------------------------------------------- */

/* 非负整数 double -> 精确十进制串（调用方保证 d 为整数且 >= 0）。
 * d = frac * 2^exp（frexp，frac in [0.5,1)），frac 的 53 位尾数
 * mant53 = frac * 2^53（精确，2 的幂缩放），故 d = mant53 * 2^(exp-53)。 */
std::string exactDecimalOfNonNegInt(double d) {
  int exp = 0;
  double frac = std::frexp(d, &exp);
  uint64_t mant53 =
      static_cast<uint64_t>(frac * 9007199254740992.0); /* 2^53 */
  std::string s = std::to_string(mant53);
  int e = exp - 53;
  if (e > 0) {
    while (e-- > 0) bigintMul(s, 2); /* 最多 ~971 次，可接受 */
  } else if (e < 0) {
    int k = -e;
    while (k-- > 0) bigintDiv(s, 2);
    /* 余数应为 0（d 是整数）；商即精确值 */
  }
  return s;
}

/* 十进制数字串比较：返回 -1（a<b）/0/1。 */
int compareDecimal(const std::string &a, const std::string &b) {
  if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
  if (a == b) return 0;
  return a < b ? -1 : 1;
}

/* BigInt 与 Number 精确大小比较：返回 big < num（§7.2.12 步骤 14 语义）。
 * num 可为任意有限 double（含小数）。
 * 原理：BigInt 是整数，Number 是精确的 m·2^e。
 *   - e>=0（num 整数，可能超过 2^53）：构造 m·2^e 十进制串与 |big| 比较。
 *   - e<0（num 有小数或 2^|e| 整除 m）：num 非整数时，整数 b < num 当且仅当
 *     b <= floor(num)（num>0）或 b <= ceil(num)-1（num<0）；统一为
 *     b <= 向零截断方向。用 floor 的精确十进制串比较。 */
bool bigIntNumberLess(const JSValue *big, double num) {
  if (std::isnan(num)) return false; /* NaN 语义由调用方（返回 false/undefined） */
  bool bigNeg = big->bigIntNegative();
  bool numNeg = (num < 0);
  if (bigNeg != numNeg) return bigNeg; /* 负 < 正 */

  double absNum = std::fabs(num);
  bool numInt = (absNum == std::floor(absNum));
  const std::string &bs = big->bigIntDigits();

  if (numInt) {
    /* num 是整数：直接比较绝对值。 */
    std::string ns = exactDecimalOfNonNegInt(absNum);
    int c = compareDecimal(bs, ns);
    if (bigNeg) return c > 0;  /* 都负：|big|>|num| 则更小 */
    return c < 0;              /* 都正：|big|<|num| 则更小 */
  }

  /* num 非整数。 */
  double fl = std::floor(absNum);
  std::string fs = exactDecimalOfNonNegInt(fl);
  int c = compareDecimal(bs, fs);
  if (!bigNeg) {
    /* 正：big < num 当 |big| <= floor(num)（big 整数 < 非整 num）。 */
    return c <= 0;
  }
  /* 负：big < num 当 |big| >= ceil(num) = floor(num)+1。
   * |big| > floor -> true；|big| == floor -> false（如 -1 < -1.5 为 false）。 */
  return c > 0;
}

ConversionResult bigIntNumberEquals(JSContext *ctx, JSValue *big, double x) {
  /* 1. NaN / ±Infinity -> false。 */
  if (std::isnan(x) || std::isinf(x)) return ok(ctx->createValue(false));

  bool neg = big->bigIntNegative();
  std::string digits = big->bigIntDigits();
  /* 2. BigInt 为 0：x 必须为 +0/-0。 */
  if (digits == "0") return ok(ctx->createValue(x == 0.0));

  /* 3. 符号不同 -> false。 */
  if (neg != (x < 0)) return ok(ctx->createValue(false));

  /* 4. x 有小数部分 -> 不可能等于整数 BigInt。 */
  if (std::fabs(x) != std::floor(std::fabs(x))) {
    return ok(ctx->createValue(false));
  }

  /* 5. 精确构造 x 的整数绝对值十进制串，与 BigInt 数字串比较。 */
  std::string xs = exactDecimalOfNonNegInt(std::fabs(x));
  return ok(ctx->createValue(xs == digits));
}

/* -------------------------------------------------------------------------- */
/* IsLessThan（§7.2.13 Abstract Relational Comparison）                        */
/* -------------------------------------------------------------------------- */

/* 数值比较：NaN 参与 -> undefined（外部转 false）。 */
bool numericLessThan(double x, double y) { return x < y; }

/* 字符串按码点比较（§7.2.13 步骤 4：逐个 UTF-16 code unit；v1 用字节
 * 字典序近似——UTF-8 字节序与 UTF-16 码点序对 BMP 一致，ASCII 正确）。 */
bool stringLessThan(const std::string &a, const std::string &b) { return a < b; }

/* BigInt 与 BigInt 比较：同符号 -> 按绝对值大小；异符号 -> 负数恒小。 */
bool bigintLessThan(const JSValue *a, const JSValue *b) {
  bool an = a->bigIntNegative();
  bool bn = b->bigIntNegative();
  if (an != bn) return an; /* 负数 < 正数 */
  if (an) {
    /* 都是负数：绝对值大的更小。 */
    return bigintLess(b->bigIntDigits(), a->bigIntDigits());
  }
  return bigintLess(a->bigIntDigits(), b->bigIntDigits());
}

ConversionResult isLessThanImpl(JSContext *ctx, JSValue *x, JSValue *y) {
  /* 1. ToPrimitive(number)。 */
  ConversionResult px = toPrimitive(ctx, x, Hint::Number);
  if (!px.ok()) return px;
  ConversionResult py = toPrimitive(ctx, y, Hint::Number);
  if (!py.ok()) return py;
  JSValue *a = px.value;
  JSValue *b = py.value;

  /* 2. 都是 String：按码点比较。 */
  if (a->kind() == Kind::String && b->kind() == Kind::String) {
    return ok(ctx->createValue(stringLessThan(a->toString(), b->toString())));
  }

  /* 3. BigInt 与 String（§7.2.12 步骤 4/5）：String 转 BigInt，失败 -> undefined。 */
  if (a->kind() == Kind::BigInt && b->kind() == Kind::String) {
    std::string sb = stringToBigInt(b->toString());
    if (sb.empty()) return ok(ctx->createValue(false)); /* undefined 语义 */
    bool neg = (sb[0] == '-');
    std::string digits = neg ? sb.substr(1) : sb;
    if (digits.empty()) digits = "0";
    JSValue bigB(false, digits);
    if (neg) bigB.setBigInt(true, digits);
    return ok(ctx->createValue(bigintLessThan(a, &bigB)));
  }
  if (a->kind() == Kind::String && b->kind() == Kind::BigInt) {
    std::string sa = stringToBigInt(a->toString());
    if (sa.empty()) return ok(ctx->createValue(false)); /* undefined 语义 */
    bool neg = (sa[0] == '-');
    std::string digits = neg ? sa.substr(1) : sa;
    if (digits.empty()) digits = "0";
    JSValue bigA(false, digits);
    if (neg) bigA.setBigInt(true, digits);
    return ok(ctx->createValue(bigintLessThan(&bigA, b)));
  }

  /* 4. 其余：ToNumeric 后比较。 */
  ConversionResult na = toNumeric(ctx, a);
  if (!na.ok()) return na;
  ConversionResult nb = toNumeric(ctx, b);
  if (!nb.ok()) return nb;
  JSValue *va = na.value;
  JSValue *vb = nb.value;

  /* 5. 同类型（Number vs Number，BigInt vs BigInt）。 */
  if (va->kind() == vb->kind()) {
    if (va->kind() == Kind::Number) {
      /* NaN 参与 -> undefined（运算符结果为 false）。 */
      return ok(ctx->createValue(numericLessThan(va->toNumber(), vb->toNumber())));
    }
    /* BigInt vs BigInt：BigInt::lessThan。 */
    return ok(ctx->createValue(bigintLessThan(va, vb)));
  }

  /* 6. 类型不同：BigInt vs Number（或反向）。§7.2.12 步骤 11-15。 */
  JSValue *big = va->kind() == Kind::BigInt ? va : vb;
  double num = va->kind() == Kind::Number ? va->toNumber() : vb->toNumber();
  bool bigIsA = (va->kind() == Kind::BigInt);

  /* 11. NaN -> undefined。 */
  if (std::isnan(num)) return ok(ctx->createValue(false));

  /* 12/13. BigInt 与 ±Infinity。 */
  if (std::isinf(num)) {
    if (num > 0) {
      /* BigInt < +Infinity 恒真；+Infinity < BigInt 恒假。 */
      return ok(ctx->createValue(bigIsA));
    }
    /* BigInt < -Infinity 恒假；-Infinity < BigInt 恒真。 */
    return ok(ctx->createValue(!bigIsA));
  }

  /* 14. 精确比较 BigInt 与 num（bigIntNumberLess 处理符号/小数/绝对值）。
   *     bigIsA=true：x=BigInt, y=num，结果 = big < num；
   *     bigIsA=false：x=num, y=BigInt，结果 = num < big = !(big < num) 且不相等
   *     ——用 bigIntNumberLess 求 big<num，再按方向取反。 */
  bool bigLess = bigIntNumberLess(big, num);
  if (bigIsA) return ok(ctx->createValue(bigLess));
  /* num < big：bigLess=false 时可能 num==big（false）或 num<big（true）。
   * 用相等性区分：num==big -> false；否则 num<big == !(big<num)。 */
  bool eq = false;
  /* num==big 当且仅当二者符号相同且绝对值十进制串相等。 */
  if ((num < 0) == big->bigIntNegative() &&
      !std::isnan(num) && !std::isinf(num)) {
    bool numInt = (std::fabs(num) == std::floor(std::fabs(num)));
    if (numInt) {
      std::string ns = exactDecimalOfNonNegInt(std::fabs(num));
      eq = (ns == big->bigIntDigits());
    }
  }
  return ok(ctx->createValue(!bigLess && !eq));
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                    */
/* -------------------------------------------------------------------------- */

ConversionResult strictEquals(JSContext *ctx, JSValue *x, JSValue *y) {
  if (x == nullptr || y == nullptr) {
    return ok(ctx->createValue(x == y)); /* null == null 语义由调用方定 */
  }
  if (x->kind() != y->kind()) return ok(ctx->createValue(false));
  return ok(ctx->createValue(strictEqualsSameType(x, y)));
}

ConversionResult abstractEquals(JSContext *ctx, JSValue *x, JSValue *y) {
  if (x == nullptr || y == nullptr) {
    return fail("TypeError: Cannot compare null value");
  }
  return abstractEqualsImpl(ctx, x, y);
}

ConversionResult isLessThan(JSContext *ctx, JSValue *x, JSValue *y) {
  if (x == nullptr || y == nullptr) {
    return fail("TypeError: Cannot compare null value");
  }
  return isLessThanImpl(ctx, x, y);
}

ConversionResult add(JSContext *ctx, JSValue *x, JSValue *y) {
  if (x == nullptr || y == nullptr) {
    return fail("TypeError: Cannot add null value");
  }
  /* §13.15.2：ToPrimitive(default) 后，任一 String -> 拼接。 */
  ConversionResult px = toPrimitive(ctx, x, Hint::Default);
  if (!px.ok()) return px;
  ConversionResult py = toPrimitive(ctx, y, Hint::Default);
  if (!py.ok()) return py;
  JSValue *a = px.value;
  JSValue *b = py.value;

  if (a->kind() == Kind::String || b->kind() == Kind::String) {
    /* 字符串拼接：非 String 一方 ToString。 */
    ConversionResult sa = toString(ctx, a);
    if (!sa.ok()) return sa;
    ConversionResult sb = toString(ctx, b);
    if (!sb.ok()) return sb;
    return ok(ctx->createValue(sa.value->toString() + sb.value->toString()));
  }

  /* 数值加法：ToNumeric 后相加。 */
  ConversionResult na = toNumeric(ctx, a);
  if (!na.ok()) return na;
  ConversionResult nb = toNumeric(ctx, b);
  if (!nb.ok()) return nb;
  JSValue *va = na.value;
  JSValue *vb = nb.value;
  if (va->kind() == Kind::BigInt || vb->kind() == Kind::BigInt) {
    /* §13.15.2 步骤 7：BigInt + Number 混用 -> TypeError。 */
    if (va->kind() != vb->kind()) {
      return fail("TypeError: Cannot mix BigInt and other types, use explicit conversions");
    }
    /* BigInt + BigInt：有符号大数加法。 */
    bool outNeg = false;
    std::string digits = va->bigIntDigits();
    bigintAddSigned(va->bigIntNegative(), digits, vb->bigIntNegative(),
                    vb->bigIntDigits(), outNeg);
    return ok(ctx->createValue(JSValue(outNeg, digits)));
  }
  return ok(ctx->createValue(va->toNumber() + vb->toNumber()));
}

ConversionResult typeOf(JSContext *ctx, JSValue *v) {
  if (v == nullptr) {
    return ok(ctx->createValue(std::string("undefined")));
  }
  switch (v->kind()) {
    case Kind::Undefined:
      return ok(ctx->createValue(std::string("undefined")));
    case Kind::Null:
      return ok(ctx->createValue(std::string("object"))); /* 历史遗留 */
    case Kind::Boolean:
      return ok(ctx->createValue(std::string("boolean")));
    case Kind::Number:
      return ok(ctx->createValue(std::string("number")));
    case Kind::String:
      return ok(ctx->createValue(std::string("string")));
    case Kind::Symbol:
      return ok(ctx->createValue(std::string("symbol")));
    case Kind::BigInt:
      return ok(ctx->createValue(std::string("bigint"))); /* §13.5.3 */
    case Kind::Object:
      /* 可调用对象 -> "function"；其余 -> "object"。 */
      return ok(ctx->createValue(std::string(
          isCallable(v) ? "function" : "object")));
    default:
      /* Exception/Interrupt 不可达。 */
      return ok(ctx->createValue(std::string("undefined")));
  }
}

ConversionResult instanceOf(JSContext *ctx, JSValue *x, JSValue *y) {
  /* §7.3.22：RHS 必须可调用。 */
  if (y == nullptr || !isCallable(y)) {
    return fail("TypeError: Right-hand side of 'instanceof' is not callable");
  }
  if (x == nullptr || !x->isObject()) {
    /* LHS 非对象 -> false（规范步骤 1：x 不是 Object 返回 false）。 */
    return ok(ctx->createValue(false));
  }

  /* 有 Symbol.hasInstance 则调用之（§7.3.22 步骤 3-6）。 */
  JSObject *rhs = y->object();
  JSValue *hasInst = getProp(rhs, "Symbol.hasInstance");
  /* 注意：Symbol.hasInstance 是 well-known symbol，属性键是 Symbol 值，
   * 当前对象模型属性键是 string——E4-3 阶段用字符串键占位，真正接入
   * Symbol 键属性在 E4-4 之后。此处先跳过 symbol 分支（RHS 无该属性
   * 时走 OrdinaryHasInstance）。 */
  (void)hasInst;

  /* OrdinaryHasInstance（§7.3.24）：LHS 沿原型链查找 RHS.prototype。 */
  JSValue *proto = getProp(rhs, "prototype");
  if (proto == nullptr || !proto->isObject()) {
    return fail("TypeError: Function has non-object prototype in instanceof check");
  }
  JSObject *needle = proto->object();

  /* 沿 LHS 原型链查找。 */
  JSObject *cursor = x->object()->prototype();
  while (cursor != nullptr) {
    if (cursor == needle) return ok(ctx->createValue(true));
    cursor = cursor->prototype();
  }
  return ok(ctx->createValue(false));
}

ConversionResult inOperator(JSContext *ctx, JSValue *x, JSValue *y) {
  /* §7.3.20：RHS 必须为 Object。 */
  if (y == nullptr || !y->isObject()) {
    return fail("TypeError: Right-hand side of 'in' is not an object");
  }
  /* LHS ToPropertyKey。 */
  ConversionResult key = toPropertyKey(ctx, x);
  if (!key.ok()) return key;

  /* HasProperty：沿原型链查找。 */
  JSObject *obj = y->object();
  if (key.value->kind() == Kind::Symbol) {
    /* Symbol 键：当前对象模型仅 string 键，Symbol 属性不存在（E4-4 后
     * 支持 Symbol 键）。 */
    return ok(ctx->createValue(false));
  }
  return ok(ctx->createValue(obj->hasProperty(key.value->toString())));
}

}  // namespace neojs
