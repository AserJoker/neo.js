/*
 * 内部 C++ 实现：运算符语义（E4-3 最小语法绑定集）。
 *
 * 架构 §5.5：运算符语义全走 E1（对象模型）+ E2（类型转换）抽象运算，
 * 作为语法绑定所需的最小集（用户确认），供后续 Compiler 的字节码生成复用。
 *
 * 覆盖（ECMA-262）：
 *   §7.2.13  IsLessThan（Abstract Relational Comparison，< 运算符）
 *   §7.2.14  Strict Equality Comparison（=== / !==）
 *   §7.2.15  Abstract Equality Comparison（== / !=）
 *   §7.3.20  in 运算符（HasProperty）
 *   §7.3.22  InstanceofOperator（instanceof，走 Symbol.hasInstance）
 *   §13.5.3  typeof 运算符（Type(argument) 字符串）
 *   §13.15.2 加法运算符（+：数值加法或字符串拼接）
 *
 * 设计：
 *   - 所有运算符返回 ConversionResult（value=nullptr + error 表示异常，#3/#4）；
 *   - 严格相等/抽象相等返回 Boolean 值；`<` 返回 Boolean（NaN 语义为 false）；
 *   - `+` 返回 Number 或 String 值（ToPrimitive 后分类）；
 *   - typeof 返回 String 值（八种类型名 + 可调用对象 "function"）；
 *   - instanceof / in 返回 Boolean 值。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_OPERATORS_H
#define NEOJS_RUNTIME_OPERATORS_H

#include <cstddef>

#include "neojs/runtime/conversion.h"

namespace neojs {

class JSContext;
class JSValue;

/* 严格相等比较（§7.2.14）：x === y。返回 Boolean 值（无异常）。 */
ConversionResult strictEquals(JSContext *ctx, JSValue *x, JSValue *y);

/* 抽象相等比较（§7.2.15）：x == y。返回 Boolean 值。
 * 可抛 TypeError（Symbol 与对象比较、BigInt 与 Number 比较等）。 */
ConversionResult abstractEquals(JSContext *ctx, JSValue *x, JSValue *y);

/* 抽象关系比较 IsLessThan（§7.2.13）：x < y。返回 Boolean 值；
 * NaN 参与比较结果为 false（规范 undefined 语义）。 */
ConversionResult isLessThan(JSContext *ctx, JSValue *x, JSValue *y);

/* 加法运算符（§13.15.2）：x + y。ToPrimitive(default) 后：
 *   任一为 String -> 字符串拼接；否则 ToNumeric 数值加法。
 * 返回 Number 或 String 值。 */
ConversionResult add(JSContext *ctx, JSValue *x, JSValue *y);

/* typeof 运算符（§13.5.3）：返回类型名 String 值。
 * "undefined"/"object"/"boolean"/"number"/"string"/"symbol"/"function"；
 * 可调用对象返回 "function"。 */
ConversionResult typeOf(JSContext *ctx, JSValue *v);

/* instanceof 运算符（§7.3.22 InstanceofOperator）：
 *   x instanceof y。RHS 必须是可调用对象（否则 TypeError）；
 *   RHS 有 Symbol.hasInstance 则调用之，否则走 OrdinaryHasInstance
 *   （沿原型链查找 LHS 的 prototype 属性）。
 * 返回 Boolean 值。 */
ConversionResult instanceOf(JSContext *ctx, JSValue *x, JSValue *y);

/* in 运算符（§7.3.20 HasProperty）：x in y。
 * RHS 必须为 Object（原始值 -> TypeError）；返回 Boolean 值。 */
ConversionResult inOperator(JSContext *ctx, JSValue *x, JSValue *y);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_OPERATORS_H
