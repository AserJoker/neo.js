#ifndef NEOJS_RUNTIME_BUILTINS_H
#define NEOJS_RUNTIME_BUILTINS_H

/*
 * 内部 C++ 实现：内置对象注册（E4 最小语法绑定集）。
 *
 * 架构 §5.2 + §8 B0：globalThis 上注册语言核心内置（Symbol 构造器等），
 * 供运算符语义 / toPrimitive / 迭代协议使用。E4 只做「语法绑定所需」
 * 的最小集（用户确认），不做完整方法集。
 *
 * 设计：
 *   - well-known symbol 表（架构 §5.2）：Symbol.iterator / Symbol.toPrimitive /
 *     Symbol.toStringTag / Symbol.hasInstance 等预置表，惰性创建 + 进根集。
 *   - Symbol 构造器（Host 函数）注册到 globalThis，静态属性指向 well-known。
 *   - 后续 E4-2 装箱 / E4-4 运算符在此扩展注册。
 *
 * GC（docs/gc.md §4.1）：well-known 与内置函数都是根集可达（全局对象
 * 属性 + context 缓存），不回收。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#include <cstdint>

namespace neojs {

class JSContext;
class JSValue;

/* well-known symbol 键（架构 §5.2：供内置协议使用的最小集）。 */
enum class WellKnown : uint8_t {
  Iterator,      /* Symbol.iterator —— 迭代协议 */
  ToPrimitive,   /* Symbol.toPrimitive —— §7.1.1 转换钩子 */
  ToStringTag,   /* Symbol.toStringTag —— Object.prototype.toString */
  HasInstance,   /* Symbol.hasInstance —— instanceof 运算符 */
};

/* 注册 E4 内置对象到 globalThis（Symbol 构造器 + well-known 属性）。 */
void installBuiltins(JSContext *ctx);

/* 取 well-known symbol 值（惰性创建 + 缓存；根集保护）。 */
JSValue *wellKnownSymbol(JSContext *ctx, WellKnown wks);

/* 取 Object.prototype（M3.3 惰性创建 + 缓存 + 进根集）。
 * 普通对象字面量（vm NewObject）与包装对象原型的原型链都指向它。 */
JSValue *objectProto(JSContext *ctx);

/* 取 Array.prototype（M4.1 惰性创建 + 缓存 + 进根集）。
 * 数组字面量（vm NewArray）与 Array() 新建数组的原型链都指向它。 */
JSValue *arrayProto(JSContext *ctx);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_BUILTINS_H
