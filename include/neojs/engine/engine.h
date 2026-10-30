/*
 * engine 层：基于 Context 的 value 运算模块（与 vm/编译器完全解耦）。
 *
 * 架构（docs/architecture.md + docs/scope-redesign.md）：
 *   - 字节码前（编译器）→ 字节码 → 字节码后（vm + engine）。
 *   - vm 解析字节码，调用本模块的接口完成语义；engine 提供几乎完整的语言特性。
 *   - engine 核心 = Context + Value；scope 一般情况下不可见（由 ctx 内部管理），
 *     本模块只暴露 ctx 前缀的 value 运算接口，不暴露 scope 操作。
 *
 * 约定：
 *   - 所有函数首参都是 Context&（运行时一切从 ctx 出发）。
 *   - 操作数/返回值都是 core::Value*（复用 core::Value，不新建 value 类型）。
 *   - 运算结果挂到 ctx 内部 scope（scope_ref=1），由 ctx/GC 管理生命周期。
 *   - 错误：返回 ValueKind::Exception 的 value（不抛 C++ 异常，见编码规范 #3/#4）。
 *   - 返回 value 的接口永不返回 nullptr（编码规范 #14）。
 *
 * M3 提供：创建 + 类型转换 + 一元/二元运算 + 比较 + 逻辑 + 异常通道。
 */

#ifndef NEOJS_ENGINE_ENGINE_H
#define NEOJS_ENGINE_ENGINE_H

#include <string>

#include "neojs/core/context.h"
#include "neojs/core/value.h"

namespace neojs {
namespace engine {

using core::Context;
using core::Value;

/* ========================================================================== */
/* 创建（全部挂 ctx 内部 scope，scope_ref=1）                                  */
/* ========================================================================== */

/* 创建数值 value（double）。 */
Value *create_number(Context &ctx, double n);

/* 创建布尔 value。 */
Value *create_boolean(Context &ctx, bool b);

/* 创建字符串 value。 */
Value *create_string(Context &ctx, const std::string &s);

/* 创建 undefined / null / TDZ 哨兵。 */
Value *create_undefined(Context &ctx);
Value *create_null(Context &ctx);
Value *create_uninitialized(Context &ctx);

/* ========================================================================== */
/* 类型转换（返回新 value；失败返回 exception value）                          */
/* ========================================================================== */

/* ToBoolean（§ToBoolean）：假值 = false/0/NaN/空串/null/undefined；余真。 */
Value *to_boolean(Context &ctx, Value *v);

/* ToNumber（§ToNumber）：number 原样；boolean→0/1；null→0；undefined→NaN；
 * string→数值解析（失败 NaN）；其余（uninitialized/exception 等）→ NaN 或异常。 */
Value *to_number(Context &ctx, Value *v);

/* ToString（§ToString）：string 原样；number→十进制串；boolean→"true"/"false"；
 * null→"null"；undefined→"undefined"。 */
Value *to_string(Context &ctx, Value *v);

/* ========================================================================== */
/* 一元运算（返回新 value）                                                    */
/* ========================================================================== */

/* 一元 +：ToNumber。 */
Value *unary_plus(Context &ctx, Value *v);

/* 一元 -：ToNumber 后取负（NaN 保持）。 */
Value *unary_minus(Context &ctx, Value *v);

/* 逻辑非 !：ToBoolean 后取反。 */
Value *logical_not(Context &ctx, Value *v);

/* 按位非 ~：ToNumber 取整（ToInt32）后按位取反。 */
Value *bitwise_not(Context &ctx, Value *v);

/* typeof：返回类型名字符串（"number"/"string"/"boolean"/"undefined"/
 * "object"/"function"/"symbol"/"bigint"；M3 只覆盖基础类型）。 */
Value *typeof_value(Context &ctx, Value *v);

/* void：求值但返回 undefined。 */
Value *void_value(Context &ctx, Value *v);

/* ========================================================================== */
/* 二元运算（返回新 value；操作数按 JS 语义转换）                               */
/* ========================================================================== */

/* 加法 +：number 相加 / 任一 string 则字符串拼接（§AdditiveExpression）。 */
Value *add(Context &ctx, Value *lhs, Value *rhs);

/* 减法/乘法/除法/取模：ToNumber 后运算。 */
Value *sub(Context &ctx, Value *lhs, Value *rhs);
Value *mul(Context &ctx, Value *lhs, Value *rhs);
Value *div(Context &ctx, Value *lhs, Value *rhs);
Value *mod(Context &ctx, Value *lhs, Value *rhs);

/* 指数 **：ToNumber 后 Math.pow。 */
Value *pow(Context &ctx, Value *lhs, Value *rhs);

/* 按位与/或/异或（ToInt32）。 */
Value *bit_and(Context &ctx, Value *lhs, Value *rhs);
Value *bit_or(Context &ctx, Value *lhs, Value *rhs);
Value *bit_xor(Context &ctx, Value *lhs, Value *rhs);

/* 移位（ToInt32 / ToUint32）。 */
Value *lshift(Context &ctx, Value *lhs, Value *rhs);
Value *rshift(Context &ctx, Value *lhs, Value *rhs);
Value *urshift(Context &ctx, Value *lhs, Value *rhs);

/* ========================================================================== */
/* 比较（返回布尔 value）                                                      */
/* ========================================================================== */

/* 抽象比较 < > <= >=（§RelationalExpression）：
 *   两个 string → 字典序；否则 ToNumber 比较。 */
Value *lt(Context &ctx, Value *lhs, Value *rhs);
Value *le(Context &ctx, Value *lhs, Value *rhs);
Value *gt(Context &ctx, Value *lhs, Value *rhs);
Value *ge(Context &ctx, Value *lhs, Value *rhs);

/* 抽象相等 ==（§EqualityExpression）：number/string/boolean 互相转换后比较。 */
Value *eq(Context &ctx, Value *lhs, Value *rhs);
Value *ne(Context &ctx, Value *lhs, Value *rhs);

/* 严格相等 ===（无类型转换）。 */
Value *strict_eq(Context &ctx, Value *lhs, Value *rhs);
Value *strict_ne(Context &ctx, Value *lhs, Value *rhs);

/* ========================================================================== */
/* 异常通道                                                                   */
/* ========================================================================== */

/* 创建异常 value（message 为错误文本）。调用方检查 kind==Exception。 */
Value *create_exception(Context &ctx, const std::string &message);

/* 判断 value 是否为异常。 */
bool is_exception(Value *v);

}  // namespace engine
}  // namespace neojs

#endif  // NEOJS_ENGINE_ENGINE_H
