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

/* ToNumeric（§7.1.3）：当前仅 Number（BigInt 延后）。返回新 Number 值。 */
ConversionResult toNumeric(JSContext *ctx, JSValue *v);

/* ToNumber（§7.1.4）：表 7。Symbol -> TypeError；Object 经 ToPrimitive(number)。 */
ConversionResult toNumber(JSContext *ctx, JSValue *v);

/* ToString（§7.1.17）：表 8。Symbol -> TypeError；Object 经 ToPrimitive(string)。
 * Number 用 Number::toString 最短表示（§6.1.6.1.20）。 */
ConversionResult toString(JSContext *ctx, JSValue *v);

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

/* SameValue（§7.2.10）：用于 Object.is。区别：-0 与 +0 不等。 */
bool sameValue(const JSValue *a, const JSValue *b);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_CONVERSION_H
