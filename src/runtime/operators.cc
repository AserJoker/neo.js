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

/* AbstractEqualityComparison(x, y)：类型不同时按规范表 12 转换比较。
 * 当前无 BigInt：Number/String/Boolean 与 Object 的比较经 ToPrimitive。 */
ConversionResult abstractEqualsImpl(JSContext *ctx, JSValue *x, JSValue *y) {
  if (x->kind() == y->kind()) {
    /* 同类型：严格相等语义（Number 用 IEEE）。 */
    return ok(ctx->createValue(strictEqualsSameType(x, y)));
  }

  /* 类型不同。规范 §7.2.15 步骤 2-9（表 12）。 */
  switch (x->kind()) {
    case Kind::Undefined:
    case Kind::Null:
      /* null == undefined（及 undefined == null） */
      return ok(ctx->createValue(
          y->kind() == Kind::Undefined || y->kind() == Kind::Null));
    case Kind::Number:
      if (y->kind() == Kind::String) {
        /* x 是 Number，y 是 String：y 转数字后比较 */
        ConversionResult n = toNumber(ctx, y);
        if (!n.ok()) return n;
        return ok(ctx->createValue(x->toNumber() == n.value->toNumber()));
      }
      if (y->kind() == Kind::Boolean) {
        /* x 是 Number，y 是 Boolean：y 转数字 */
        ConversionResult n = toNumber(ctx, y);
        if (!n.ok()) return n;
        return ok(ctx->createValue(x->toNumber() == n.value->toNumber()));
      }
      if (y->kind() == Kind::Object) {
        /* x 是 Number，y 是 Object：y ToPrimitive(number) 后递归 */
        ConversionResult p = toPrimitive(ctx, y, Hint::Number);
        if (!p.ok()) return p;
        return abstractEqualsImpl(ctx, x, p.value);
      }
      break;
    case Kind::String:
      if (y->kind() == Kind::Number) {
        ConversionResult n = toNumber(ctx, x);
        if (!n.ok()) return n;
        return ok(ctx->createValue(n.value->toNumber() == y->toNumber()));
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
      /* x 是 Boolean：x 转数字后递归（此时 x 变 Number）。 */
      if (y->kind() == Kind::Number || y->kind() == Kind::String ||
          y->kind() == Kind::Object) {
        ConversionResult n = toNumber(ctx, x);
        if (!n.ok()) return n;
        return abstractEqualsImpl(ctx, n.value, y);
      }
      break;
    case Kind::Object:
      /* x 是 Object：x ToPrimitive(number) 后递归。 */
      if (y->kind() == Kind::Number || y->kind() == Kind::String ||
          y->kind() == Kind::Boolean) {
        ConversionResult p = toPrimitive(ctx, x, Hint::Number);
        if (!p.ok()) return p;
        return abstractEqualsImpl(ctx, p.value, y);
      }
      break;
    case Kind::Symbol:
      /* Symbol 与对象比较：仅当同引用时相等（上面同类型已处理）；此处
       * Symbol vs 非对象原始值 -> false（规范步骤 6/8 对 Symbol 的处理：
       * Symbol 与 String/Number/Boolean 比较均 false）。 */
      if (y->kind() == Kind::Object) {
        return ok(ctx->createValue(x == y));
      }
      return ok(ctx->createValue(false));
    default:
      break;
  }

  /* 规范步骤 10：其余情况返回 false。 */
  return ok(ctx->createValue(false));
}

/* -------------------------------------------------------------------------- */
/* IsLessThan（§7.2.13 Abstract Relational Comparison）                        */
/* -------------------------------------------------------------------------- */

/* 数值比较：NaN 参与 -> undefined（外部转 false）。 */
bool numericLessThan(double x, double y) { return x < y; }

/* 字符串按码点比较（§7.2.13 步骤 4：逐个 UTF-16 code unit；v1 用字节
 * 字典序近似——UTF-8 字节序与 UTF-16 码点序对 BMP 一致，ASCII 正确）。 */
bool stringLessThan(const std::string &a, const std::string &b) { return a < b; }

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

  /* 3. 其余：ToNumeric 后数值比较。 */
  ConversionResult na = toNumeric(ctx, a);
  if (!na.ok()) return na;
  ConversionResult nb = toNumeric(ctx, b);
  if (!nb.ok()) return nb;
  double da = na.value->toNumber();
  double db = nb.value->toNumber();
  /* 4. NaN 参与 -> undefined（运算符结果为 false）。 */
  return ok(ctx->createValue(numericLessThan(da, db)));
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
  return ok(ctx->createValue(na.value->toNumber() + nb.value->toNumber()));
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
