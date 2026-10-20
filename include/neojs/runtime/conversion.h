/*
 * 内部 C++ 实现：类型转换抽象运算（ECMA-262 抽象运算）。
 *
 * 遵循 docs/architecture.md §5.5 类型转换：
 *   完整实现 ToPrimitive / ToNumber / ToString / ToBoolean / ToObject /
 *   ToPropertyKey / ToNumeric / SameValueZero 等，作为 Values 模块核心 API，
 *   所有内置方法与运算符语义复用同一套。
 *
 * 遵循 docs/coding-standards.md：
 *   #3 禁止 C++ 异常跨 C ABI 边界——本模块不 throw，转换失败返回
 *      ConversionResult（携带 kind=Exception 语义）；
 *   #4 JSValue 为一等公民，错误经 Exception 值传递；
 *   #14 返回 JSValue 的接口永不返回 nullptr。
 *
 * 约定：转换函数接收 JSContext*（ToPrimitive 可能需要调用方法、产生中间值）。
 * 返回 ConversionResult：success 时 value 指向新创建的内部值（调用方负责
 * 持有——经句柄或绑定）；失败时 value == nullptr 且 error 非空。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_CONVERSION_H
#define NEOJS_RUNTIME_CONVERSION_H

#include <cstdint>
#include <string>

#include "neojs/runtime/value.h"

namespace neojs {

class JSContext;

/* 转换结果：成功时 value 非空；失败时 value==nullptr、error 描述失败原因。 */
struct ConversionResult {
  JSValue *value = nullptr;  /* 转换结果值（内部指针）；失败时 nullptr */
  std::string error;         /* 失败信息（如 "TypeError: ..."）；成功时空 */
  uint32_t tmp_handle = 0;   /* 返回值保护句柄（call() Script 分支用）；0 = 无 */

  bool ok() const { return value != nullptr; }
};

/* ToPrimitive hint（ECMA-262 §7.1.1）：
 *   default  -> 运算符默认（先 valueOf 后 toString）
 *   number   -> 先 valueOf 后 toString
 *   string   -> 先 toString 后 valueOf
 */
enum class Hint { Default, Number, String };

/* -------------------------------------------------------------------------- */
/* 抽象运算入口                                                               */
/* -------------------------------------------------------------------------- */

/* ToBoolean（§7.1.2）：纯函数，无失败。 */
bool toBoolean(const JSValue *v);

/* ToNumeric（§7.1.3）：ToPrimitive(number) 后 BigInt 返回自身，否则 ToNumber。
 * 返回 Number 或 BigInt 值。 */
ConversionResult toNumeric(JSContext *ctx, JSValue *v);

/* ToNumber（§7.1.4）：表 7。Symbol/BigInt -> TypeError；Object 经 ToPrimitive(number)。 */
ConversionResult toNumber(JSContext *ctx, JSValue *v);

/* ToString（§7.1.17）：表 8。Symbol -> TypeError；Object 经 ToPrimitive(string)。
 * Number 用 Number::toString 最短表示（§6.1.6.1.20）；
 * BigInt 用 BigInt::toString(10)（§6.1.6.2.21，十进制，负数带 "-"）。 */
ConversionResult toString(JSContext *ctx, JSValue *v);

/* ToBigInt（§7.1.13）：表 12。ToPrimitive(number) 后：
 * Boolean -> 0n/1n；BigInt 原样；String 经 StringToBigInt（非法 -> SyntaxError）；
 * Undefined/Null/Number/Symbol -> TypeError。 */
ConversionResult toBigInt(JSContext *ctx, JSValue *v);

/* ToPrimitive（§7.1.1）：hint 控制 OrdinaryToPrimitive 调用顺序。 */
ConversionResult toPrimitive(JSContext *ctx, JSValue *v, Hint hint = Hint::Default);

/* ToPropertyKey（§7.1.19）：ToPrimitive(string) 后返回 String 或 Symbol。 */
ConversionResult toPropertyKey(JSContext *ctx, JSValue *v);

/* ToObject（§7.1.13）：原始值装箱；Object 原样返回。 */
ConversionResult toObject(JSContext *ctx, JSValue *v);

/* Number::toString（§6.1.6.1.20）：最短十进制表示（"NaN"/"Infinity"/"0"）。
 * 纯工具函数（无 ctx 依赖），供转换/编译期数字键转字符串复用。 */
std::string numberToString(double x);

/* -------------------------------------------------------------------------- */
/* 值比较（§7.2）                                                              */
/* -------------------------------------------------------------------------- */

/* SameValueZero（§7.2.11）：用于 Map 键、Array.includes。
 * 区别：-0 与 +0 相等（不同于 SameValue）。 */
bool sameValueZero(const JSValue *a, const JSValue *b);

/* -------------------------------------------------------------------------- */
/* BigInt 数值工具（供运算符语义与内置复用，M4.3）                              */
/* -------------------------------------------------------------------------- */

/* 十进制大数字符串加法：s += v（v 为 0-9）。任意精度，原地修改。 */
void bigintAddDigit(std::string &s, unsigned v);

/* 十进制大数字符串乘法：s *= base（base 为 2-36）。任意精度，原地修改。 */
void bigintMul(std::string &s, unsigned base);

/* 十进制大数字符串除法：s /= base，返回余数（0..base-1）。任意精度，原地修改。 */
unsigned bigintDiv(std::string &s, unsigned base);

/* 十进制大数字符串比较：a < b（按数值，非字典序）。 */
bool bigintLess(const std::string &a, const std::string &b);

/* 十进制大数字符串减法：a -= b（前提 a >= b）。原地修改 a。 */
void bigintSub(std::string &a, const std::string &b);

/* 十进制大数字符串加法：a += b（b 为十进制数字串）。原地修改 a。 */
void bigintAdd(std::string &a, const std::string &b);

/* 十进制大数字符串乘法：a *= b（b 为十进制数字串）。任意精度。
 * 用 schoolbook 算法（逐位乘 + 移位加），v1 性能可接受。 */
void bigintMulStr(std::string &a, const std::string &b);

/* 十进制数字串 -> double（BigInt::toNumber，§6.1.6.2.11）。
 * 溢出 -> ±Infinity；v1 用 strtod 近似（超 17 位有效数字的舍入接受精度差距，
 * 与 Number 字面量精度同级）。 */
double bigintToNumber(const std::string &digits);

/* StringToBigInt（§7.1.14）：字符串 -> BigInt 十进制数字串；失败返回空串。
 * 接受：首尾空白；十进制整数（含 ±）；0x/0o/0b 非十进制整数；数字分隔符。
 * 其余（小数/指数/非法字符/Infinity/NaN）-> 失败。
 * 结果串符号并入（负数带 "-" 前缀）。供 operators.cc 与 ToBigInt 复用。 */
std::string stringToBigInt(const std::string &str);

/* 非十进制数字串（去前缀与分隔符）-> 十进制大数字符串（stringToBigInt 内部用）。 */
std::string bigintFromRadix(const std::string &body, int base);

/* 有符号十进制大数加法：a += b（a、b 为绝对值串，符号位分别 aNeg/bNeg）。
 * 结果绝对值写回 a，符号写 outNeg；结果 0 时 outNeg 恒 false。 */
void bigintAddSigned(bool aNeg, std::string &a, bool bNeg,
                     const std::string &b, bool &outNeg);

/* 有符号十进制大数减法：a -= b（a + (-b)）。语义同 bigintAddSigned。 */
void bigintSubSigned(bool aNeg, std::string &a, bool bNeg,
                     const std::string &b, bool &outNeg);

/* 有符号十进制大数乘法：a *= b。语义同 bigintAddSigned。 */
void bigintMulSigned(bool aNeg, std::string &a, bool bNeg,
                     const std::string &b, bool &outNeg);

/* 十进制大数除法（绝对值）：a / b 的商（向零截断）写返回值，余数写 rem。
 * 调用方保证 b != "0"（除零由运算符语义抛 RangeError）。 */
std::string bigintDivMod(const std::string &a, const std::string &b,
                         std::string &rem);

/* 十进制大数幂（绝对值）：a ^= b（b 为无符号十进制串，调用方保证非负）。
 * 0^0 = 1。二进制快速幂。 */
void bigintPow(std::string &a, const std::string &b);

/* SameValue（§7.2.10）：用于 Object.is。区别：-0 与 +0 不等。 */
bool sameValue(const JSValue *a, const JSValue *b);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_CONVERSION_H
