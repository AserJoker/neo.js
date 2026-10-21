/*
 * 内部 C++ 实现：迭代器协议抽象运算（ECMA-262 §7.4）。
 *
 * 架构 §7.4：GetIterator / IteratorNext / IteratorStep / IteratorValue
 * 等抽象运算，供 for-of、数组/字符串/Map/Set 内建迭代器、展开语法复用。
 *
 * 遵循 docs/coding-standards.md：
 *   #3 禁止 C++ 异常跨 C ABI 边界——失败返回 ConversionResult；
 *   #4 JSValue 为一等公民，错误经 Exception 值传递；
 *   #14 返回 JSValue 的接口永不返回 nullptr。
 *
 * 本模块只实现协议骨架（调用 Symbol.iterator / next / 读 done / value），
 * 具体集合的内建迭代器对象（ArrayIterator/MapIterator 等）在 builtins 侧。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_ITERATOR_H
#define NEOJS_RUNTIME_ITERATOR_H

#include <cstddef>

#include "neojs/runtime/conversion.h"

namespace neojs {

class JSContext;
class JSValue;

/* GetIterator（§7.4.1，无 hint 简化版）：
 *   obj[Symbol.iterator] -> method（必须是可调用，否则 TypeError）；
 *   call(method, obj) -> iterator（必须是对象，否则 TypeError）。
 * 返回迭代器对象（内部指针）。 */
ConversionResult getIterator(JSContext *ctx, JSValue *obj);

/* IteratorNext（§7.4.2，无 value 简化版）：
 *   call(iterator.next, iterator) -> 结果必须为对象（否则 TypeError）。
 * 返回迭代结果对象。 */
ConversionResult iteratorNext(JSContext *ctx, JSValue *iterator);

/* IteratorStep（§7.4.5）：IteratorNext 后读 done。
 *   done 为 true -> 返回 value=nullptr 表示迭代完成（非错误）；
 *   done 为 false/undefined -> 返回 value=iteratorResult（继续迭代）。
 * 调用方通过 result.value==nullptr 判断迭代结束。 */
ConversionResult iteratorStep(JSContext *ctx, JSValue *iterator);

/* IteratorValue（§7.4.4）：读迭代结果对象的 value 属性。 */
ConversionResult iteratorValue(JSContext *ctx, JSValue *iterResult);

/* GetMethod（§7.3.11）：读对象属性并校验为可调用。
 *   属性不存在/undefined/null -> 返回 value=nullptr（无方法，非错误）；
 *   存在但不可调用 -> TypeError。供 Symbol.iterator / return 等获取。 */
ConversionResult getMethod(JSContext *ctx, JSValue *obj, JSValue *key);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_ITERATOR_H
