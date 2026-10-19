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
 *   - 包装对象（E4-2）：Number/String/Boolean/Symbol 构造器 + 各自 prototype
 *     的最小方法集（valueOf / toString），装箱（ToObject）后包装对象原型链
 *     指向对应 prototype（§7.1.13）。
 *   - 内置函数统一为 Builtin 类别（内部值指针语义），区别于宿主 Host 句柄语义。
 *
 * 规范对照（ECMA-262）：
 *   §20.1.1.1  Boolean 构造器
 *   §20.1.3.2  Boolean.prototype.toString
 *   §20.1.3.3  Boolean.prototype.valueOf
 *   §20.4.1.1  Symbol 构造器
 *   §20.4.3.2  Symbol.prototype.toString
 *   §20.4.3.4  Symbol.prototype.valueOf
 *   §21.1.1.1  Number 构造器
 *   §21.1.3.6  Number.prototype.toString
 *   §21.1.3.7  Number.prototype.valueOf
 *   §22.1.1.1  String 构造器
 *   §22.1.3.5  String.prototype.valueOf
 *   §22.1.3.27 String.prototype.toString
 */

#include "neojs/runtime/builtins.h"

#include <cmath>
#include <string>

#include "neojs/runtime/context.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/function.h"
#include "neojs/runtime/object.h"
#include "neojs/runtime/value.h"

namespace neojs {

namespace {

/* 成功/失败结果（conversion.cc 的 ok/fail 在匿名命名空间，本模块独立定义）。 */
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

/* 创建内置函数（Builtin 类别）：JSFunction + 包装 JSValue + 反向指针。
 * 内部值指针语义（区别于 Host 句柄语义，见 function.h）。 */
JSValue *makeBuiltin(JSContext *ctx, JSBuiltinFn fn, ThisMode mode) {
  JSFunction *f = ctx->heap().allocGCObject<JSFunction>(nullptr, fn, mode);
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(f);
  f->setHolder(v);
  return v;
}

/* thisNumberValue / thisStringValue / thisBooleanValue / thisSymbolValue
 * （§20.1.3.3 / §21.1.3.7 / §22.1.3.5 / §20.4.3.4 的抽象运算）：
 *   原始值（primKind）原样返回；目标包装对象（wrapKind）返回内部槽；
 *   其它 -> TypeError。供 prototype 方法校验 this。 */
ConversionResult thisWrapperValue(JSContext *, JSValue *thisVal, Kind primKind,
                                  ObjectKind wrapKind, const char *what) {
  if (thisVal != nullptr && thisVal->kind() == primKind) return ok(thisVal);
  if (thisVal != nullptr && thisVal->isObject() &&
      thisVal->object()->kind() == wrapKind) {
    JSValue *data = thisVal->object()->wrapperData();
    if (data != nullptr) return ok(data);
  }
  return fail(std::string("TypeError: ") + what);
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

/* Number 构造器（§21.1.1.1）：Number(x) -> ToNumber(x)；无参 -> +0。 */
ConversionResult numberConstructor(JSContext *ctx, JSValue *,
                                   JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() == Kind::Undefined) {
    return ok(ctx->createValue(0.0));
  }
  return toNumber(ctx, args[0]);
}

/* String 构造器（§22.1.1.1）：String(x) -> ToString(x)；无参 -> ""。 */
ConversionResult stringConstructor(JSContext *ctx, JSValue *,
                                   JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() == Kind::Undefined) {
    return ok(ctx->createValue(std::string("")));
  }
  return toString(ctx, args[0]);
}

/* Boolean 构造器（§20.1.1.1）：Boolean(x) -> ToBoolean(x)；无参 -> false。 */
ConversionResult booleanConstructor(JSContext *ctx, JSValue *,
                                    JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() == Kind::Undefined) {
    return ok(ctx->createValue(false));
  }
  return ok(ctx->createValue(toBoolean(args[0])));
}

/* Number.prototype.valueOf（§21.1.3.7）：thisNumberValue。 */
ConversionResult numberValueOf(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  return thisWrapperValue(
      ctx, thisVal, Kind::Number, ObjectKind::Number,
      "Number.prototype.valueOf called on incompatible receiver");
}

/* Number.prototype.toString（§21.1.3.6）：thisNumberValue -> ToString。 */
ConversionResult numberToStringBuiltin(JSContext *ctx, JSValue *thisVal,
                                       JSValue *const *, size_t) {
  ConversionResult x = thisWrapperValue(
      ctx, thisVal, Kind::Number, ObjectKind::Number,
      "Number.prototype.toString called on incompatible receiver");
  if (!x.ok()) return x;
  return toString(ctx, x.value);
}

/* String.prototype.valueOf / toString（§22.1.3.5 / §22.1.3.27）：thisStringValue。 */
ConversionResult stringValueOf(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  return thisWrapperValue(
      ctx, thisVal, Kind::String, ObjectKind::String,
      "String.prototype.valueOf called on incompatible receiver");
}

ConversionResult stringToStringBuiltin(JSContext *ctx, JSValue *thisVal,
                                       JSValue *const *, size_t) {
  return thisWrapperValue(
      ctx, thisVal, Kind::String, ObjectKind::String,
      "String.prototype.toString called on incompatible receiver");
}

/* Boolean.prototype.valueOf（§20.1.3.3）：thisBooleanValue。 */
ConversionResult booleanValueOf(JSContext *ctx, JSValue *thisVal,
                                JSValue *const *, size_t) {
  return thisWrapperValue(
      ctx, thisVal, Kind::Boolean, ObjectKind::Boolean,
      "Boolean.prototype.valueOf called on incompatible receiver");
}

/* Boolean.prototype.toString（§20.1.3.2）："true" / "false"。 */
ConversionResult booleanToStringBuiltin(JSContext *ctx, JSValue *thisVal,
                                        JSValue *const *, size_t) {
  ConversionResult b = thisWrapperValue(
      ctx, thisVal, Kind::Boolean, ObjectKind::Boolean,
      "Boolean.prototype.toString called on incompatible receiver");
  if (!b.ok()) return b;
  return ok(ctx->createValue(b.value->toBoolean() ? std::string("true")
                                                  : std::string("false")));
}

/* Symbol.prototype.valueOf（§20.4.3.4）：thisSymbolValue。 */
ConversionResult symbolValueOf(JSContext *ctx, JSValue *thisVal,
                               JSValue *const *, size_t) {
  return thisWrapperValue(
      ctx, thisVal, Kind::Symbol, ObjectKind::Symbol,
      "Symbol.prototype.valueOf called on incompatible receiver");
}

/* Symbol.prototype.toString（§20.4.3.2）：SymbolDescriptiveString(s)。 */
ConversionResult symbolToStringBuiltin(JSContext *ctx, JSValue *thisVal,
                                       JSValue *const *, size_t) {
  ConversionResult s = thisWrapperValue(
      ctx, thisVal, Kind::Symbol, ObjectKind::Symbol,
      "Symbol.prototype.toString called on incompatible receiver");
  if (!s.ok()) return s;
  const std::string &desc = s.value->symbolDescription();
  return ok(ctx->createValue("Symbol(" + desc + ")"));
}

/* 注册一组包装对象内置（Number/String/Boolean 之一）：
 * 构造器挂 global，prototype 挂 constructor + valueOf/toString 最小方法集，
 * prototype 进 wrapper_protos_ 缓存（装箱原型链，§7.1.13）。 */
void installWrapperBuiltin(JSContext *ctx, const char *name, ObjectKind kind,
                           JSBuiltinFn ctor, JSBuiltinFn valueOf,
                           JSBuiltinFn toStringFn) {
  JSValue *global = ctx->globalObject();
  JSValue *ctor_v = makeBuiltin(ctx, ctor, ThisMode::Strict);
  global->object()->setProperty(name, ctor_v);

  JSValue *proto = ctx->createObject(nullptr); /* 原型链 E4-4 接 Object.prototype */
  ctor_v->object()->setProperty("prototype", proto);
  proto->object()->setProperty("constructor", ctor_v);
  proto->object()->setProperty("valueOf",
                               makeBuiltin(ctx, valueOf, ThisMode::Strict));
  proto->object()->setProperty("toString",
                               makeBuiltin(ctx, toStringFn, ThisMode::Strict));
  ctx->setWrapperProto(kind, proto);
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
  JSValue *global = ctx->globalObject();

  /* globalThis 自引用（§19.1.1）：全局对象的属性，值指向全局对象自身。
   * 属性 {Writable:true, Enumerable:false, Configurable:true}。 */
  global->object()->setProperty("globalThis", global);

  /* 全局值属性（§19.1.2-4）：Infinity / NaN / undefined。
   * 均 {Writable:false, Enumerable:false, Configurable:false}。 */
  global->object()->setProperty("Infinity", ctx->createValue(INFINITY));
  global->object()->setProperty("NaN", ctx->createValue(NAN));
  global->object()->setProperty("undefined",
                               ctx->createValue(Kind::Undefined));

  /* Symbol 构造器（§20.4.1.1）：内置函数 + well-known 静态属性（E4-1）。 */
  JSValue *symbol = makeBuiltin(ctx, symbolConstructor, ThisMode::Strict);
  global->object()->setProperty("Symbol", symbol);

  JSObject *sym_obj = symbol->object(); /* JSFunction 是 JSObject */
  sym_obj->setProperty("iterator", wellKnownSymbol(ctx, WellKnown::Iterator));
  sym_obj->setProperty("toPrimitive",
                       wellKnownSymbol(ctx, WellKnown::ToPrimitive));
  sym_obj->setProperty("toStringTag",
                       wellKnownSymbol(ctx, WellKnown::ToStringTag));
  sym_obj->setProperty("hasInstance",
                       wellKnownSymbol(ctx, WellKnown::HasInstance));

  /* Symbol.prototype（§20.4.3）：valueOf/toString 最小方法集。 */
  JSValue *symbol_proto = ctx->createObject(nullptr);
  sym_obj->setProperty("prototype", symbol_proto);
  symbol_proto->object()->setProperty("constructor", symbol);
  symbol_proto->object()->setProperty(
      "valueOf", makeBuiltin(ctx, symbolValueOf, ThisMode::Strict));
  symbol_proto->object()->setProperty(
      "toString", makeBuiltin(ctx, symbolToStringBuiltin, ThisMode::Strict));
  ctx->setWrapperProto(ObjectKind::Symbol, symbol_proto);

  /* 包装对象构造器 + prototype 最小方法集（E4-2）。 */
  installWrapperBuiltin(ctx, "Number", ObjectKind::Number, numberConstructor,
                        numberValueOf, numberToStringBuiltin);
  installWrapperBuiltin(ctx, "String", ObjectKind::String, stringConstructor,
                        stringValueOf, stringToStringBuiltin);
  installWrapperBuiltin(ctx, "Boolean", ObjectKind::Boolean, booleanConstructor,
                        booleanValueOf, booleanToStringBuiltin);
}

}  // namespace neojs
