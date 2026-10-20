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

#include <algorithm>
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
    case Kind::BigInt:
      return v.bigIntDigits() == "0"; /* 0n -> falsey */
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

}  // namespace

/* -------------------------------------------------------------------------- */
/* BigInt 数值工具（M4.3）：十进制大数字符串的任意精度算术。                  */
/* 声明见 conversion.h（供 operators.cc 与内置复用）。                         */
/* -------------------------------------------------------------------------- */

void bigintAddDigit(std::string &s, unsigned v) {
  if (s == "0" && v == 0) return;
  int carry = static_cast<int>(v);
  for (int i = static_cast<int>(s.size()) - 1; i >= 0 && carry > 0; --i) {
    int d = (s[i] - '0') + carry;
    s[i] = static_cast<char>('0' + (d % 10));
    carry = d / 10;
  }
  if (carry > 0) s.insert(s.begin(), static_cast<char>('0' + carry));
}

void bigintMul(std::string &s, unsigned base) {
  int carry = 0;
  for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
    int d = (s[i] - '0') * static_cast<int>(base) + carry;
    s[i] = static_cast<char>('0' + (d % 10));
    carry = d / 10;
  }
  while (carry > 0) {
    s.insert(s.begin(), static_cast<char>('0' + (carry % 10)));
    carry /= 10;
  }
}

unsigned bigintDiv(std::string &s, unsigned base) {
  /* 从高位到低位逐位除以 base，余数传递到下一位。 */
  unsigned rem = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned cur = rem * 10u + static_cast<unsigned>(s[i] - '0');
    s[i] = static_cast<char>('0' + (cur / base));
    rem = cur % base;
  }
  /* 去掉结果前导 0（保留至少一位）。 */
  size_t p = s.find_first_not_of('0');
  if (p == std::string::npos) s = "0";
  else if (p > 0) s.erase(0, p);
  return rem;
}

bool bigintLess(const std::string &a, const std::string &b) {
  if (a.size() != b.size()) return a.size() < b.size();
  return a < b; /* 等长时字典序即数值序 */
}

void bigintSub(std::string &a, const std::string &b) {
  /* 前提：a >= b（调用方保证）。逐位相减借位。 */
  int borrow = 0;
  int i = static_cast<int>(a.size()) - 1;
  int j = static_cast<int>(b.size()) - 1;
  for (; j >= 0; --i, --j) {
    int d = (a[static_cast<size_t>(i)] - '0') - (b[static_cast<size_t>(j)] - '0') - borrow;
    if (d < 0) { d += 10; borrow = 1; } else { borrow = 0; }
    a[static_cast<size_t>(i)] = static_cast<char>('0' + d);
  }
  for (; borrow && i >= 0; --i) {
    int d = (a[static_cast<size_t>(i)] - '0') - borrow;
    if (d < 0) { d += 10; borrow = 1; } else { borrow = 0; }
    a[static_cast<size_t>(i)] = static_cast<char>('0' + d);
  }
  /* 去前导 0。 */
  size_t p = a.find_first_not_of('0');
  if (p == std::string::npos) a = "0";
  else if (p > 0) a.erase(0, p);
}

void bigintAdd(std::string &a, const std::string &b) {
  /* 从低位到高位逐位加，进位传递。b 可能比 a 长。 */
  int carry = 0;
  int i = static_cast<int>(a.size()) - 1;
  int j = static_cast<int>(b.size()) - 1;
  std::string out;
  out.reserve((a.size() > b.size() ? a.size() : b.size()) + 1);
  for (; i >= 0 || j >= 0 || carry; --i, --j) {
    int d = carry;
    if (i >= 0) d += a[static_cast<size_t>(i)] - '0';
    if (j >= 0) d += b[static_cast<size_t>(j)] - '0';
    out.push_back(static_cast<char>('0' + (d % 10)));
    carry = d / 10;
  }
  std::reverse(out.begin(), out.end());
  a = out;
}

void bigintMulStr(std::string &a, const std::string &b) {
  /* Schoolbook 乘法：a × b，逐位乘累加。O(n·m)。 */
  if (a == "0" || b == "0") { a = "0"; return; }
  std::string result(a.size() + b.size(), '0');
  for (int j = static_cast<int>(b.size()) - 1; j >= 0; --j) {
    int carry = 0;
    for (int i = static_cast<int>(a.size()) - 1; i >= 0; --i) {
      int bi = static_cast<int>(b[static_cast<size_t>(j)] - '0');
      int ai = static_cast<int>(a[static_cast<size_t>(i)] - '0');
      int pos = i + j + 1;
      int cur = static_cast<int>(result[static_cast<size_t>(pos)] - '0') +
                ai * bi + carry;
      result[static_cast<size_t>(pos)] = static_cast<char>('0' + (cur % 10));
      carry = cur / 10;
    }
    int pos = j; /* 进位写入更高位 */
    while (carry > 0) {
      int cur = static_cast<int>(result[static_cast<size_t>(pos)] - '0') + carry;
      result[static_cast<size_t>(pos)] = static_cast<char>('0' + (cur % 10));
      carry = cur / 10;
      --pos;
    }
  }
  size_t p = result.find_first_not_of('0');
  a = (p == std::string::npos) ? "0" : result.substr(p);
}

/* 有符号十进制大数加法：a += b。结果绝对值写回 a，符号写 outNeg。
 * 同号 -> 绝对值相加；异号 -> 绝对值相减（结果符号取绝对值大者）。 */
void bigintAddSigned(bool aNeg, std::string &a, bool bNeg,
                     const std::string &b, bool &outNeg) {
  if (aNeg == bNeg) {
    /* 同号：绝对值相加，符号不变。 */
    bigintAdd(a, b);
    outNeg = aNeg;
    return;
  }
  /* 异号：a + (-b)。
   * |a| >= |b| -> a - b，符号取 aNeg；否则 b - a，符号取 bNeg。 */
  if (!bigintLess(a, b)) {
    bigintSub(a, b);
    outNeg = aNeg;
  } else {
    std::string r = b;
    bigintSub(r, a);
    a = r;
    outNeg = bNeg;
  }
  if (a == "0") outNeg = false; /* -0 归一化为 0 */
}

void bigintSubSigned(bool aNeg, std::string &a, bool bNeg,
                     const std::string &b, bool &outNeg) {
  /* a - b = a + (-b)：翻转 b 符号后走加法。 */
  bigintAddSigned(aNeg, a, !bNeg, b, outNeg);
}

void bigintMulSigned(bool aNeg, std::string &a, bool bNeg,
                     const std::string &b, bool &outNeg) {
  bigintMulStr(a, b);
  outNeg = aNeg != bNeg && a != "0";
}

/* 十进制大数除法（绝对值）：a / b 商（向零截断）+ 余数。
 * 长除法（从高位逐位求商），O(n·m)。调用方保证 b != "0"。 */
std::string bigintDivMod(const std::string &a, const std::string &b,
                         std::string &rem) {
  rem.clear();
  std::string quot;
  quot.reserve(a.size());
  for (char ch : a) {
    /* 当前余数左移一位并加 digit：rem = rem*10 + digit（长除法）。 */
    if (rem != "0") bigintMul(rem, 10);
    bigintAddDigit(rem, static_cast<unsigned>(ch - '0'));
    /* 求这一位的商：累减 b 直到 rem < b（逐位除法）。 */
    unsigned q = 0;
    while (!bigintLess(rem, b)) {
      bigintSub(rem, b);
      ++q;
    }
    quot.push_back(static_cast<char>('0' + q));
  }
  /* 去前导 0（保留至少一位）。 */
  size_t p = quot.find_first_not_of('0');
  if (p == std::string::npos) quot = "0";
  else if (p > 0) quot.erase(0, p);
  if (rem.empty()) rem = "0";
  return quot;
}

void bigintPow(std::string &a, const std::string &b) {
  /* 二进制快速幂：a^b。0^0 = 1。b 为非负十进制串。 */
  std::string base = a;
  std::string exp = b;
  std::string result = "1";
  while (exp != "0") {
    /* 当前位为 1（exp 奇数）则乘入。 */
    unsigned parity = bigintDiv(exp, 2); /* exp /= 2，返回余数 */
    if (parity == 1) bigintMulStr(result, base);
    if (exp != "0") bigintMulStr(base, base);
  }
  a = result;
}

double bigintToNumber(const std::string &digits) {
  /* BigInt::toNumber（§6.1.6.2.11）：十进制串转 double。strtod 近似
   * （超精度舍入接受差距，与 Number 字面量同精度）。 */
  errno = 0;
  char *endp = nullptr;
  double d = std::strtod(digits.c_str(), &endp);
  if (endp == digits.c_str() || *endp != '\0') return NAN; /* 防御性 */
  return d;
}

namespace {

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
/* StringToBigInt（§7.1.14）：字符串 -> BigInt（失败返回空串）。               */
/* 接受：首尾空白；十进制整数（含 ±）；0x/0o/0b 非十进制整数。                */
/* 其余（小数/指数/非法字符/Infinity/NaN）-> 失败（undefined 语义）。         */
/* 结果串：符号并入（负数带 "-" 前缀）。供 operators.cc 与 ToBigInt 复用。     */
/* -------------------------------------------------------------------------- */

/* 非十进制数字串（去前缀与分隔符）-> 十进制大数字符串。 */
std::string bigintFromRadix(const std::string &body, int base) {
  std::string s = "0";
  for (size_t i = 0; i < body.size(); ++i) {
    char ch = body[i];
    int hv = -1;
    if (ch >= '0' && ch <= '9') hv = ch - '0';
    else if (ch >= 'a' && ch <= 'f') hv = ch - 'a' + 10;
    else if (ch >= 'A' && ch <= 'F') hv = ch - 'A' + 10;
    if (hv < 0 || hv >= base) return ""; /* 非法数字 -> 失败 */
    bigintMul(s, static_cast<unsigned>(base));
    bigintAddDigit(s, static_cast<unsigned>(hv));
  }
  return s;
}

std::string stringToBigInt(const std::string &str) {
  /* 1. 跳过首尾 WhiteSpace/LineTerminator。 */
  size_t i = 0;
  const size_t n = str.size();
  while (i < n) {
    int c = static_cast<unsigned char>(str[i]);
    if (isWhiteSpace(c) || isLineTerminator(c)) ++i;
    else break;
  }
  size_t end = n;
  while (end > i) {
    int c = static_cast<unsigned char>(str[end - 1]);
    if (isWhiteSpace(c) || isLineTerminator(c)) --end;
    else break;
  }
  if (i == end) return "0"; /* 空串（或全空白）-> 0 */

  std::string s = str.substr(i, end - i);
  const size_t m = s.size();
  bool neg = false;
  size_t start = 0;
  if (s[0] == '+' || s[0] == '-') {
    neg = (s[0] == '-');
    start = 1;
  }
  if (start >= m) return ""; /* 只有符号 -> 失败 */

  std::string digits;
  int base = 10;
  size_t body = start;
  if (m - start >= 2 && s[start] == '0') {
    char p = s[start + 1];
    if (p == 'x' || p == 'X') { base = 16; body = start + 2; }
    else if (p == 'o' || p == 'O') { base = 8; body = start + 2; }
    else if (p == 'b' || p == 'B') { base = 2; body = start + 2; }
  }
  /* 前缀后无数字 -> 失败（如 "0x"）。 */
  if (base != 10 && body >= m) return "";

  if (base == 10) {
    /* 十进制整数：仅 [0-9]，可含分隔符；不得含小数点/指数/其他。 */
    std::string clean;
    for (size_t k = body; k < m; ++k) {
      char ch = s[k];
      if (ch == '_') continue; /* 数字分隔符（§12.9.3.1） */
      if (ch < '0' || ch > '9') return ""; /* 小数点/e/Infinity/NaN 等 -> 失败 */
      clean.push_back(ch);
    }
    digits = clean;
  } else {
    digits = bigintFromRadix(s.substr(body), base);
    if (digits.empty()) return "";
  }

  /* 归一化：去前导 0（全 0 -> "0"）。 */
  size_t p = digits.find_first_not_of('0');
  if (p == std::string::npos) return "0";
  if (p > 0) digits.erase(0, p);
  return neg ? "-" + digits : digits; /* 符号并入结果串（负号前缀） */
}

/* -------------------------------------------------------------------------- */
/* Number::toString（§6.1.6.1.20）：最短十进制表示。                          */
/* 用 std::to_chars 求最短往返表示（Ryu 算法，C++17），再按 JS 记数法规则     */
/* 重排。to_chars 输出形如 "123" / "1.5" / "0.00123" / "1e+21" / "1e-07"。   */
/* 声明见 conversion.h（供编译器数字键转字符串复用）。                        */
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
/* Number.prototype 格式化工具（M4.3g，§21.1.3）                               */
/* -------------------------------------------------------------------------- */

/* 解析 numberToString(x) 的最短表示为数字串（供定点/精度格式化）。
 * 返回 (digits, exp, neg)：x = 0.digits × 10^exp（digits 为纯数字串，
 * 不含小数点/符号；exp 为规范指数 n；neg 为符号位）。仅有限非零值。 */
static void parseDigits(double x, std::string &digits, int &exp, bool &neg) {
  neg = x < 0;
  double mag = std::fabs(x);
  if (mag == 0.0) {
    digits = "0";
    exp = 0;
    return;
  }
  std::string s = numberToString(mag);
  size_t epos = s.find_first_of("eE");
  int sci = 0;
  if (epos != std::string::npos) {
    sci = std::strtol(s.c_str() + epos + 1, nullptr, 10);
    s = s.substr(0, epos);
  }
  size_t dot = s.find('.');
  std::string intpart = (dot == std::string::npos) ? s : s.substr(0, dot);
  std::string frac = (dot == std::string::npos) ? "" : s.substr(dot + 1);
  digits = intpart + frac;
  size_t p = digits.find_first_not_of('0');
  if (p == std::string::npos) {
    digits = "0";
    exp = 0;
    return;
  }
  digits = digits.substr(p);
  exp = sci + static_cast<int>(intpart.size()) - static_cast<int>(p);
}

/* 十进制数字串按保留位数四舍五入。
 * s 为纯数字串（无前导 0）；keep 为保留的有效位数。
 * 返回舍入后数字串；carry=true 表示产生了新位（如 999 -> 1000，返回 "1"）。 */
static std::string roundDigits(const std::string &s, int keep, bool &carry) {
  carry = false;
  if (keep >= static_cast<int>(s.size())) return s;
  if (keep <= 0) {
    if (s[0] >= '5') {
      carry = true;
      return "1";
    }
    return "";
  }
  std::string r = s.substr(0, static_cast<size_t>(keep));
  if (s[static_cast<size_t>(keep)] >= '5') {
    int i = keep - 1;
    while (i >= 0 && r[static_cast<size_t>(i)] == '9') {
      r[static_cast<size_t>(i)] = '0';
      --i;
    }
    if (i < 0) {
      /* 全 9 进位：9.99 -> 10.0，保留 keep 位（"1" + keep-1 个零）。 */
      r = "1" + std::string(static_cast<size_t>(keep - 1), '0');
      carry = true;
    } else {
      r[static_cast<size_t>(i)] = static_cast<char>(r[static_cast<size_t>(i)] + 1);
    }
  }
  return r;
}

/* 十进制数字串（无符号，最短往返）-> 按 radix（2-36）重排为任意进制数字串。
 * 用十进制大数工具逐位除 radix（bigintDiv 就地修改十进制串返回余数）。
 * 只支持非负整数；0 -> "0"。 */
static std::string decimalToRadix(std::string digits, int radix) {
  if (digits.empty() || digits == "0") return "0";
  const char *alphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string out;
  while (digits != "0") {
    unsigned rem = bigintDiv(digits, static_cast<unsigned>(radix));
    out.insert(out.begin(), alphabet[rem]);
  }
  return out;
}

std::string numberToStringRadix(double x, int radix) {
  /* 只支持整数：小数/非有限/±0 由调用方走十进制。 */
  if (std::isnan(x) || std::isinf(x) || x == 0.0) return "";
  if (x != std::trunc(x)) return "";
  bool neg = x < 0;
  double mag = std::fabs(x);
  char buf[32];
  auto res = std::to_chars(buf, buf + sizeof(buf), mag);
  std::string dec(buf, res.ptr);
  std::string r = decimalToRadix(dec, radix);
  return neg ? "-" + r : r;
}

std::string numberToFixed(double x, int digits) {
  if (std::isnan(x)) return "NaN";
  if (std::isinf(x)) return (x > 0) ? "Infinity" : "-Infinity";
  bool neg = x < 0;
  double mag = std::fabs(x);
  /* 绝对值 >= 1e21：规范步骤 7 回退 Number::toString。 */
  if (mag >= 1e21) return numberToString(x);
  /* 绝对值 < 1e-6：按 0 处理（保留符号，§21.1.3.5 步骤 5）。 */
  if (mag != 0.0 && mag < 1e-6) mag = 0.0;
  double factor = std::pow(10.0, digits);
  double scaled = std::round(mag * factor);
  std::string s;
  if (digits == 0) {
    if (std::fabs(scaled) < 1e15) {
      s = std::to_string(static_cast<long long>(scaled));
    } else {
      return numberToString(neg ? -scaled : scaled);
    }
  } else {
    long long whole = static_cast<long long>(scaled / factor);
    long long frac = static_cast<long long>(std::fmod(scaled, factor));
    s = std::to_string(whole) + ".";
    std::string f = std::to_string(frac);
    s += std::string(static_cast<size_t>(digits) - f.size(), '0') + f;
  }
  return neg ? "-" + s : s;
}

std::string numberToExponential(double x, int digits) {
  if (std::isnan(x)) return "NaN";
  if (std::isinf(x)) return (x > 0) ? "Infinity" : "-Infinity";
  bool neg = x < 0;
  double mag = std::fabs(x);
  if (mag == 0.0) {
    std::string s = (digits < 0) ? "0" : ("0." + std::string(static_cast<size_t>(digits), '0'));
    return (neg ? "-" : "") + s + "e+0";
  }
  std::string d;
  int e = 0;
  bool dneg = false;
  parseDigits(mag, d, e, dneg); /* mag>=0，dneg 恒 false；neg 由调用方维护 */
  --e; /* parseDigits 返回规范指数 n（0.d×10^n）；科学指数 e = n - 1 */
  int keep = (digits < 0) ? -1 : digits + 1; /* 有效位数 = 小数位 + 1 */
  std::string mant;
  if (keep < 0 || keep >= static_cast<int>(d.size())) {
    mant = d;
  } else {
    bool carry = false;
    mant = roundDigits(d, keep, carry);
    if (carry) ++e; /* roundDigits 进位已返回 keep 位（"1"+keep-1 个零） */
  }
  std::string mant_str = mant.substr(0, 1);
  std::string rest = mant.substr(1);
  if (digits >= 0) {
    if (rest.size() > static_cast<size_t>(digits)) rest = rest.substr(0, static_cast<size_t>(digits));
    rest += std::string(static_cast<size_t>(digits) - rest.size(), '0');
  }
  if (!rest.empty()) mant_str += "." + rest;
  std::string s = (neg ? "-" : "") + mant_str + "e";
  if (e >= 0) s += "+";
  s += std::to_string(e);
  return s;
}

std::string numberToPrecision(double x, int precision) {
  if (std::isnan(x)) return "NaN";
  if (std::isinf(x)) return (x > 0) ? "Infinity" : "-Infinity";
  bool neg = x < 0;
  double mag = std::fabs(x);
  if (mag == 0.0) {
    std::string s = "0." + std::string(static_cast<size_t>(precision - 1), '0');
    return (neg ? "-" : "") + s;
  }
  std::string d;
  int e = 0;
  bool dneg = false;
  parseDigits(mag, d, e, dneg); /* mag>=0，dneg 恒 false；neg 由调用方维护 */
  --e; /* parseDigits 返回规范指数 n（0.d×10^n）；科学指数 e = n - 1 */
  std::string sig;
  if (precision >= static_cast<int>(d.size())) {
    sig = d;
  } else {
    bool carry = false;
    sig = roundDigits(d, precision, carry);
    if (carry) ++e; /* roundDigits 进位已返回 precision 位 */
  }
  /* 记数法选择（§21.1.3.3 步骤 11-13）：
   *   e < -6 或 e >= 21 或 e >= precision 时用科学记数法；否则定点。 */
  if (e < -6 || e >= 21 || e >= precision) {
    std::string s = (neg ? "-" : "") + sig.substr(0, 1);
    if (sig.size() > 1) s += "." + sig.substr(1);
    s += "e";
    if (e >= 0) s += "+";
    s += std::to_string(e);
    return s;
  }
  /* 定点（§21.1.3.3 步骤 12-14）：m 为所需小数位数。 */
  int m = precision - e - 1;
  if (m < 0) m = 0;
  std::string s = (neg ? "-" : "");
  if (e >= 0) {
    /* 整数部分 e+1 位。 */
    s += sig.substr(0, static_cast<size_t>(e + 1));
    if (m > 0) {
      s += ".";
      /* 小数部分：sig 剩余字符，不足 m 位补零。 */
      size_t avail = (sig.size() > static_cast<size_t>(e + 1))
                         ? sig.size() - static_cast<size_t>(e + 1)
                         : 0;
      s += sig.substr(static_cast<size_t>(e + 1), avail);
      s += std::string(static_cast<size_t>(m) - avail, '0');
    }
  } else {
    /* e < 0：0.00...sig，小数总位数补到 m。 */
    s += "0.";
    s += std::string(static_cast<size_t>(-e - 1), '0');
    s += sig;
    int cur = (-e - 1) + static_cast<int>(sig.size());
    if (cur < m) s += std::string(static_cast<size_t>(m - cur), '0');
  }
  return s;
}

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
    case Kind::BigInt:
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
      return ok(v); /* ToNumber(Number) = 自身：返回原值不复制，
                    * 避免跨调用持有无根副本（低阈值 GC 下被回收） */
    case Kind::String:
      return ok(ctx->createValue(stringToNumber(v->toString())));
    case Kind::Object: {
      ConversionResult p = toPrimitive(ctx, v, Hint::Number);
      if (!p.ok()) return p;
      return toNumber(ctx, p.value);
    }
    case Kind::Symbol:
      return fail("TypeError: Cannot convert a Symbol value to a number");
    case Kind::BigInt:
      return fail("TypeError: Cannot convert a BigInt value to a number");
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
    case Kind::BigInt:
      /* BigInt::toString(10)（§6.1.6.2.21）：十进制数字串，负数带 "-"。 */
      return ok(ctx->createValue(v->bigIntNegative()
                                    ? "-" + v->bigIntDigits()
                                    : v->bigIntDigits()));
    case Kind::String:
      return ok(v);
    case Kind::Object: {
      ConversionResult p = toPrimitive(ctx, v, Hint::String);
      if (!p.ok()) return p;
      return toString(ctx, p.value);
    }
    case Kind::Symbol:
      return fail("TypeError: Cannot convert a Symbol value to a string");
    case Kind::Exception:
      /* 错误值：返回错误信息（供 JS_ToString 读取诊断消息）。 */
      return ok(ctx->createValue(v->errorInfo()));
    default:
      return ok(ctx->createValue(std::string("")));
  }
}

ConversionResult toNumeric(JSContext *ctx, JSValue *v) {
  /* §7.1.3：ToPrimitive(number) 后，BigInt 返回自身，否则 ToNumber。 */
  if (v == nullptr) return fail("TypeError: Cannot convert undefined or null");
  ConversionResult p = toPrimitive(ctx, v, Hint::Number);
  if (!p.ok()) return p;
  if (p.value->kind() == Kind::BigInt) return ok(p.value);
  return toNumber(ctx, p.value);
}

ConversionResult toBigInt(JSContext *ctx, JSValue *v) {
  /* §7.1.13 ToBigInt：表 12。ToPrimitive(number) 后按类型转换。 */
  if (v == nullptr) return fail("TypeError: Cannot convert undefined or null");
  ConversionResult prim = toPrimitive(ctx, v, Hint::Number);
  if (!prim.ok()) return prim;
  switch (prim.value->kind()) {
    case Kind::Boolean:
      return ok(ctx->createValue(
          JSValue(false, prim.value->toBoolean() ? "1" : "0")));
    case Kind::BigInt:
      return ok(prim.value);
    case Kind::String: {
      std::string r = stringToBigInt(prim.value->toString());
      if (r.empty()) return fail("SyntaxError: Cannot convert to a BigInt");
      bool neg = (r[0] == '-');
      std::string digits = neg ? r.substr(1) : r;
      if (digits.empty()) digits = "0"; /* 防御性 */
      return ok(ctx->createValue(JSValue(neg, digits)));
    }
    case Kind::Undefined:
    case Kind::Null:
      return fail("TypeError: Cannot convert undefined or null to a BigInt");
    case Kind::Number:
      return fail("TypeError: Cannot convert a Number to a BigInt");
    case Kind::Symbol:
      return fail("TypeError: Cannot convert a Symbol value to a BigInt");
    default:
      /* Object 已 ToPrimitive 化；Exception/Interrupt 不可达。 */
      return fail("TypeError: Cannot convert to a BigInt");
  }
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
    case Kind::BigInt:
      /* M4.3e：BigInt 装箱（§7.1.13 表 14）-> 包装对象，原型链 BigInt.prototype。 */
      return ok(makeWrapper(ctx, ObjectKind::BigInt, v));
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
    case Kind::BigInt:
      /* BigInt::equal（§6.1.6.2.13）：同符号同数字串。 */
      return a->bigIntNegative() == b->bigIntNegative() &&
             a->bigIntDigits() == b->bigIntDigits();
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
