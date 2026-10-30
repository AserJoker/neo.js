#include "neojs/engine/engine.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace neojs {
namespace engine {

namespace {

using core::BooleanData;
using core::NumberData;
using core::StringData;
using core::ValueKind;

double number_of(Value *v) {
  return static_cast<NumberData *>(v->data)->number;
}

const std::string &string_of(Value *v) {
  return static_cast<StringData *>(v->data)->str;
}

double to_number_impl(Value *v) {
  switch (v->kind) {
    case ValueKind::Number:
      return number_of(v);
    case ValueKind::Boolean:
      return static_cast<BooleanData *>(v->data)->value ? 1.0 : 0.0;
    case ValueKind::Null:
      return 0.0;
    case ValueKind::Undefined:
      return std::numeric_limits<double>::quiet_NaN();
    case ValueKind::String: {
      const std::string &s = string_of(v);
      const char *start = s.c_str();
      const char *end = start + s.size();
      auto is_ws = [](char c) {
        switch (c) {
          case ' ': case '\t': case '\n': case '\r': case '\v': case '\f':
            return true;
          default:
            return false;
        }
      };
      while (start < end && is_ws(*start)) ++start;
      while (end > start && is_ws(*(end - 1))) --end;
      if (start == end) return 0.0;
      std::string trimmed(start, end);
      char *parse_end = nullptr;
      double result = std::strtod(trimmed.c_str(), &parse_end);
      if (parse_end != trimmed.c_str() + trimmed.size()) {
        return std::numeric_limits<double>::quiet_NaN();
      }
      return result;
    }
    default:
      return std::numeric_limits<double>::quiet_NaN();
  }
}

int32_t to_int32_impl(double d) {
  if (std::isnan(d) || std::isinf(d) || d == 0.0) return 0;
  double truncated = std::trunc(d);
  double modulo = std::fmod(truncated, 4294967296.0);
  if (modulo < 0) modulo += 4294967296.0;
  if (modulo >= 2147483648.0) modulo -= 4294967296.0;
  return static_cast<int32_t>(modulo);
}

uint32_t to_uint32_impl(double d) {
  return static_cast<uint32_t>(to_int32_impl(d));
}

std::string to_string_impl(Value *v) {
  switch (v->kind) {
    case ValueKind::String:
      return string_of(v);
    case ValueKind::Number: {
      double d = number_of(v);
      if (std::isnan(d)) return "NaN";
      if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
      if (d == 0.0) return "0";
      if (d == std::trunc(d) && std::abs(d) < 1e21) {
        long long ll = static_cast<long long>(d);
        if (static_cast<double>(ll) == d) return std::to_string(ll);
      }
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.17g", d);
      return std::string(buf);
    }
    case ValueKind::Boolean:
      return static_cast<BooleanData *>(v->data)->value ? "true" : "false";
    case ValueKind::Null:
      return "null";
    case ValueKind::Undefined:
      return "undefined";
    default:
      return "";
  }
}

bool to_boolean_impl(Value *v) {
  switch (v->kind) {
    case ValueKind::Boolean:
      return static_cast<BooleanData *>(v->data)->value;
    case ValueKind::Number: {
      double d = number_of(v);
      return d != 0.0 && !std::isnan(d);
    }
    case ValueKind::String:
      return !string_of(v).empty();
    case ValueKind::Null:
    case ValueKind::Undefined:
      return false;
    default:
      return true;
  }
}

double abstract_relational(Value *lhs, Value *rhs) {
  if (lhs->kind == ValueKind::String && rhs->kind == ValueKind::String) {
    const std::string &a = string_of(lhs);
    const std::string &b = string_of(rhs);
    if (a < b) return -1.0;
    if (a > b) return 1.0;
    return 0.0;
  }
  double a = to_number_impl(lhs);
  double b = to_number_impl(rhs);
  if (std::isnan(a) || std::isnan(b)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (a < b) return -1.0;
  if (a > b) return 1.0;
  return 0.0;
}

bool strict_equals(Value *lhs, Value *rhs) {
  if (lhs->kind != rhs->kind) return false;
  switch (lhs->kind) {
    case ValueKind::Number: {
      double a = number_of(lhs);
      double b = number_of(rhs);
      if (std::isnan(a) || std::isnan(b)) return false;
      return a == b;
    }
    case ValueKind::String:
      return string_of(lhs) == string_of(rhs);
    case ValueKind::Boolean:
      return static_cast<BooleanData *>(lhs->data)->value ==
             static_cast<BooleanData *>(rhs->data)->value;
    case ValueKind::Null:
    case ValueKind::Undefined:
      return true;
    default:
      return lhs->data == rhs->data;
  }
}

bool abstract_equals(Value *lhs, Value *rhs) {
  if (lhs->kind == rhs->kind) return strict_equals(lhs, rhs);
  if ((lhs->kind == ValueKind::Null && rhs->kind == ValueKind::Undefined) ||
      (lhs->kind == ValueKind::Undefined && rhs->kind == ValueKind::Null)) {
    return true;
  }
  if (lhs->kind == ValueKind::Number && rhs->kind == ValueKind::String) {
    return number_of(lhs) == to_number_impl(rhs);
  }
  if (lhs->kind == ValueKind::String && rhs->kind == ValueKind::Number) {
    return to_number_impl(lhs) == number_of(rhs);
  }
  if (lhs->kind == ValueKind::Boolean) {
    /* boolean → ToNumber 后与 rhs 抽象相等（rhs 非 boolean/string 则纯数值比） */
    double lb = to_number_impl(lhs);
    if (rhs->kind == ValueKind::Number) return lb == number_of(rhs);
    if (rhs->kind == ValueKind::String) return lb == to_number_impl(rhs);
    if (rhs->kind == ValueKind::Boolean) return lb == to_number_impl(rhs);
    return false;
  }
  if (rhs->kind == ValueKind::Boolean) {
    double rb = to_number_impl(rhs);
    if (lhs->kind == ValueKind::Number) return number_of(lhs) == rb;
    if (lhs->kind == ValueKind::String) return to_number_impl(lhs) == rb;
    if (lhs->kind == ValueKind::Boolean) return to_number_impl(lhs) == rb;
    return false;
  }
  return false;
}

}  // namespace

/* ========================================================================== */
/* 创建                                                                       */
/* ========================================================================== */

Value *create_number(Context &ctx, double n) { return ctx.create_number(n); }
Value *create_boolean(Context &ctx, bool b) { return ctx.create_boolean(b); }
Value *create_string(Context &ctx, const std::string &s) {
  return ctx.create_string(s);
}
Value *create_undefined(Context &ctx) { return ctx.create_undefined(); }
Value *create_null(Context &ctx) { return ctx.create_null(); }
Value *create_uninitialized(Context &ctx) { return ctx.create_uninitialized(); }

/* ========================================================================== */
/* 类型转换                                                                   */
/* ========================================================================== */

Value *to_boolean(Context &ctx, Value *v) {
  return create_boolean(ctx, to_boolean_impl(v));
}

Value *to_number(Context &ctx, Value *v) {
  return create_number(ctx, to_number_impl(v));
}

Value *to_string(Context &ctx, Value *v) {
  return create_string(ctx, to_string_impl(v));
}

/* ========================================================================== */
/* 一元运算                                                                   */
/* ========================================================================== */

Value *unary_plus(Context &ctx, Value *v) { return to_number(ctx, v); }

Value *unary_minus(Context &ctx, Value *v) {
  double d = to_number_impl(v);
  return create_number(ctx, -d);
}

Value *logical_not(Context &ctx, Value *v) {
  return create_boolean(ctx, !to_boolean_impl(v));
}

Value *bitwise_not(Context &ctx, Value *v) {
  int32_t i = to_int32_impl(to_number_impl(v));
  return create_number(ctx, static_cast<double>(~i));
}

Value *typeof_value(Context &ctx, Value *v) {
  const char *type = "object";
  switch (v->kind) {
    case ValueKind::Number: type = "number"; break;
    case ValueKind::String: type = "string"; break;
    case ValueKind::Boolean: type = "boolean"; break;
    case ValueKind::Undefined: type = "undefined"; break;
    case ValueKind::Uninitialized: type = "undefined"; break;
    default: type = "object"; break; /* null 等 */
  }
  return create_string(ctx, type);
}

Value *void_value(Context &ctx, Value * /*v*/) { return create_undefined(ctx); }

/* ========================================================================== */
/* 二元运算                                                                   */
/* ========================================================================== */

Value *add(Context &ctx, Value *lhs, Value *rhs) {
  if (lhs->kind == ValueKind::String || rhs->kind == ValueKind::String) {
    return create_string(ctx, to_string_impl(lhs) + to_string_impl(rhs));
  }
  return create_number(ctx, to_number_impl(lhs) + to_number_impl(rhs));
}

Value *sub(Context &ctx, Value *lhs, Value *rhs) {
  return create_number(ctx, to_number_impl(lhs) - to_number_impl(rhs));
}

Value *mul(Context &ctx, Value *lhs, Value *rhs) {
  return create_number(ctx, to_number_impl(lhs) * to_number_impl(rhs));
}

Value *div(Context &ctx, Value *lhs, Value *rhs) {
  return create_number(ctx, to_number_impl(lhs) / to_number_impl(rhs));
}

Value *mod(Context &ctx, Value *lhs, Value *rhs) {
  return create_number(ctx, std::fmod(to_number_impl(lhs), to_number_impl(rhs)));
}

Value *pow(Context &ctx, Value *lhs, Value *rhs) {
  return create_number(ctx, std::pow(to_number_impl(lhs), to_number_impl(rhs)));
}

Value *bit_and(Context &ctx, Value *lhs, Value *rhs) {
  int32_t a = to_int32_impl(to_number_impl(lhs));
  int32_t b = to_int32_impl(to_number_impl(rhs));
  return create_number(ctx, static_cast<double>(a & b));
}

Value *bit_or(Context &ctx, Value *lhs, Value *rhs) {
  int32_t a = to_int32_impl(to_number_impl(lhs));
  int32_t b = to_int32_impl(to_number_impl(rhs));
  return create_number(ctx, static_cast<double>(a | b));
}

Value *bit_xor(Context &ctx, Value *lhs, Value *rhs) {
  int32_t a = to_int32_impl(to_number_impl(lhs));
  int32_t b = to_int32_impl(to_number_impl(rhs));
  return create_number(ctx, static_cast<double>(a ^ b));
}

Value *lshift(Context &ctx, Value *lhs, Value *rhs) {
  int32_t a = to_int32_impl(to_number_impl(lhs));
  uint32_t b = static_cast<uint32_t>(to_number_impl(rhs)) & 0x1F;
  return create_number(ctx, static_cast<double>(a << b));
}

Value *rshift(Context &ctx, Value *lhs, Value *rhs) {
  int32_t a = to_int32_impl(to_number_impl(lhs));
  uint32_t b = static_cast<uint32_t>(to_number_impl(rhs)) & 0x1F;
  return create_number(ctx, static_cast<double>(a >> b));
}

Value *urshift(Context &ctx, Value *lhs, Value *rhs) {
  uint32_t a = to_uint32_impl(to_number_impl(lhs));
  uint32_t b = static_cast<uint32_t>(to_number_impl(rhs)) & 0x1F;
  return create_number(ctx, static_cast<double>(a >> b));
}

/* ========================================================================== */
/* 比较                                                                       */
/* ========================================================================== */

Value *lt(Context &ctx, Value *lhs, Value *rhs) {
  return create_boolean(ctx, abstract_relational(lhs, rhs) == -1.0);
}

Value *le(Context &ctx, Value *lhs, Value *rhs) {
  double r = abstract_relational(lhs, rhs);
  return create_boolean(ctx, r == -1.0 || r == 0.0); /* NaN → false */
}

Value *gt(Context &ctx, Value *lhs, Value *rhs) {
  return create_boolean(ctx, abstract_relational(rhs, lhs) == -1.0);
}

Value *ge(Context &ctx, Value *lhs, Value *rhs) {
  double r = abstract_relational(rhs, lhs);
  return create_boolean(ctx, r == -1.0 || r == 0.0);
}

Value *eq(Context &ctx, Value *lhs, Value *rhs) {
  return create_boolean(ctx, abstract_equals(lhs, rhs));
}

Value *ne(Context &ctx, Value *lhs, Value *rhs) {
  return create_boolean(ctx, !abstract_equals(lhs, rhs));
}

Value *strict_eq(Context &ctx, Value *lhs, Value *rhs) {
  return create_boolean(ctx, strict_equals(lhs, rhs));
}

Value *strict_ne(Context &ctx, Value *lhs, Value *rhs) {
  return create_boolean(ctx, !strict_equals(lhs, rhs));
}

/* ========================================================================== */
/* 逻辑运算                                                                   */
/* ========================================================================== */

Value *logical_and(Context &ctx, Value *lhs, Value *rhs) {
  (void)ctx;
  return to_boolean_impl(lhs) ? rhs : lhs;
}

Value *logical_or(Context &ctx, Value *lhs, Value *rhs) {
  (void)ctx;
  return to_boolean_impl(lhs) ? lhs : rhs;
}

Value *nullish(Context &ctx, Value *lhs, Value *rhs) {
  (void)ctx;
  if (lhs->kind == ValueKind::Null || lhs->kind == ValueKind::Undefined) {
    return rhs;
  }
  return lhs;
}

/* ========================================================================== */
/* 异常通道                                                                   */
/* ========================================================================== */

Value *create_exception(Context &ctx, const std::string &message) {
  /* 异常 value：kind=Exception，data 存消息文本（复用 StringData）。
   * 后续可扩展为 ErrorData{name,message}。 */
  Value *v = ctx.create_value(ValueKind::Exception);
  StringData *d = new StringData();
  d->refcount = 1;
  d->str = message;
  v->data = d;
  return v;
}

bool is_exception(Value *v) { return v->kind == ValueKind::Exception; }

}  // namespace engine
}  // namespace neojs
