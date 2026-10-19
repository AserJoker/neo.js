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
 *   - Symbol 构造器（内置函数，JSBuiltinFn 内部指针语义）注册到 globalThis，
 *     静态属性指向 well-known。
 *
 * 规范对照（ECMA-262）：
 *   §20.4.2  Symbol 构造器（Symbol(description) -> 唯一 Symbol）
 *   §20.4.2.x well-known symbols 静态属性
 */

#include "neojs/runtime/builtins.h"

#include <string>

#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/object.h"
#include "neojs/runtime/value.h"

namespace neojs {

namespace {

/* 成功结果（conversion.cc 的 ok 在匿名命名空间，本模块独立定义）。 */
ConversionResult ok(JSValue *v) {
  ConversionResult r;
  r.value = v;
  return r;
}

/* Symbol 构造器（§20.4.1.1）：Symbol(description) -> 唯一 Symbol 值。
 * 无参数 -> 无描述符；有参数 -> ToString(description)（描述符非字符串时报错）。 */
ConversionResult symbolConstructor(JSContext *ctx, JSValue *, JSValue *const *args,
                                   size_t argc) {
  std::string desc;
  if (argc > 0 && args[0] != nullptr && args[0]->kind() != Kind::Undefined) {
    ConversionResult s = toString(ctx, args[0]);
    if (!s.ok()) return s; /* 描述符转换错误（如 Symbol 描述） */
    desc = s.value->toString();
  }
  JSValue *sym = ctx->createValue(Kind::Symbol);
  sym->setSymbolDescription(desc);
  return ok(sym);
}

}  // namespace

JSValue *wellKnownSymbol(JSContext *ctx, WellKnown wks) {
  int index = static_cast<int>(wks);
  if (ctx->wellKnown(index) != nullptr) return ctx->wellKnown(index); /* 已创建 */

  /* 描述符（供 Symbol.prototype.description / toString）。 */
  const char *desc = "";
  switch (wks) {
    case WellKnown::Iterator: desc = "Symbol.iterator"; break;
    case WellKnown::ToPrimitive: desc = "Symbol.toPrimitive"; break;
    case WellKnown::ToStringTag: desc = "Symbol.toStringTag"; break;
    case WellKnown::HasInstance: desc = "Symbol.hasInstance"; break;
  }

  JSValue *sym = ctx->createValue(Kind::Symbol);
  sym->setSymbolDescription(desc);
  ctx->setWellKnown(index, sym); /* 进根集（markRoots 遍历 well_known_） */
  return sym;
}

/* 注册 E4 内置对象到 globalThis。 */
void installBuiltins(JSContext *ctx) {
  /* Symbol 构造器：内置函数，经 Heap 创建 JSFunction + 包装 JSValue，
   * 挂到 globalThis（globalObject()）的 Symbol 属性。 */
  JSValue *global = ctx->globalObject();

  neojs::JSFunction *fn =
      ctx->heap().allocGCObject<neojs::JSFunction>(
          nullptr /* prototype */, symbolConstructor, ThisMode::Strict);
  neojs::JSValue *v = ctx->heap().allocGCObject<neojs::JSValue>();
  v->setObject(fn);
  fn->setHolder(v); /* 反向包装指针（与 createObject 一致） */
  global->object()->setProperty("Symbol", v);

  /* Symbol 构造器静态属性：Symbol.iterator 等 well-known（§20.4.2）。 */
  JSObject *sym_obj = fn; /* JSFunction 是 JSObject */
  sym_obj->setProperty("iterator", wellKnownSymbol(ctx, WellKnown::Iterator));
  sym_obj->setProperty("toPrimitive",
                       wellKnownSymbol(ctx, WellKnown::ToPrimitive));
  sym_obj->setProperty("toStringTag",
                       wellKnownSymbol(ctx, WellKnown::ToStringTag));
  sym_obj->setProperty("hasInstance",
                       wellKnownSymbol(ctx, WellKnown::HasInstance));
}

}  // namespace neojs
