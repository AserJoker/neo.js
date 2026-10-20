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
  ctx->heap().pin(f); /* 分配保护（M3.3）：两段式创建中间窗口防误回收 */
  JSValue *v = ctx->heap().allocGCObject<JSValue>();
  v->setObject(f);
  f->setHolder(v);
  ctx->heap().unpin(f);
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

/* Object 构造器（§20.1.1）：Object(value) -> ToObject(value)（§20.1.1.1 步骤 1）。
 * 无参/undefined/null -> 新建普通对象（原型 Object.prototype）。
 * 参数已是对象 -> 原样返回；原始值 -> 装箱（ToObject）。 */
ConversionResult objectConstructor(JSContext *ctx, JSValue *,
                                   JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || args[0]->kind() == Kind::Undefined ||
      args[0]->kind() == Kind::Null) {
    /* 新建普通对象，原型链指向 Object.prototype（§20.1.1.1 步骤 2）。 */
    return ok(ctx->createObject(objectProto(ctx)->object()));
  }
  return toObject(ctx, args[0]);
}

/* Object.getPrototypeOf（§20.1.2.9）：O 必须为对象；返回 [[Prototype]]。
 * 无原型 -> null。 */
ConversionResult objectGetPrototypeOf(JSContext *ctx, JSValue *,
                                      JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr || !args[0]->isObject()) {
    return fail("TypeError: Object.getPrototypeOf called on non-object");
  }
  JSObject *proto = args[0]->object()->prototype();
  if (proto == nullptr) return ok(ctx->createValue(Kind::Null));
  if (proto->holder() == nullptr) return ok(ctx->createValue(Kind::Null));
  return ok(proto->holder());
}

/* Object.setPrototypeOf（§20.1.2.18）：O 必须为对象；proto 必须为 Object 或 null。
 * 返回 O。 */
ConversionResult objectSetPrototypeOf(JSContext *, JSValue *,
                                      JSValue *const *args, size_t argc) {
  if (argc < 2 || args[0] == nullptr || !args[0]->isObject()) {
    return fail("TypeError: Object.setPrototypeOf called on non-object");
  }
  JSValue *proto = args[1];
  if (proto != nullptr && proto->kind() != Kind::Null &&
      !proto->isObject()) {
    return fail("TypeError: Object.setPrototypeOf: prototype must be an object or null");
  }
  JSObject *target = args[0]->object();
  if (proto == nullptr || proto->kind() == Kind::Null) {
    target->setPrototype(nullptr);
  } else {
    /* 循环原型链防护：proto 自身（或链上）不得指向 target（§20.1.2.18 步骤 5）。
     * 若 proto 沿原型链可达 target 则拒绝，保持 target 原型不变。 */
    JSObject *cursor = proto->object();
    while (cursor != nullptr) {
      if (cursor == target) {
        return fail("TypeError: Cyclic __proto__ value");
      }
      cursor = cursor->prototype();
    }
    target->setPrototype(proto->object());
  }
  return ok(args[0]);
}

/* Object.create（§20.1.2.2）：新建对象，原型为 O（null 或 Object）。
 * 可选 properties 本阶段忽略（M4 Object.defineProperties 启用）。 */
ConversionResult objectCreate(JSContext *ctx, JSValue *,
                              JSValue *const *args, size_t argc) {
  if (argc == 0 || args[0] == nullptr) {
    return fail("TypeError: Object.create requires an argument");
  }
  JSValue *proto = args[0];
  if (proto->kind() != Kind::Null && !proto->isObject()) {
    return fail("TypeError: Object prototype may only be an Object or null");
  }
  JSObject *p = (proto->kind() == Kind::Null) ? nullptr : proto->object();
  JSValue *obj = ctx->createObject(p);
  /* properties 参数（M4 Object.defineProperties 启用）。 */
  if (argc > 1 && args[1] != nullptr && args[1]->kind() != Kind::Undefined) {
    /* 本阶段忽略：未实现 defineProperties。 */
  }
  return ok(obj);
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
 * prototype 进 wrapper_protos_ 缓存（装箱原型链，§7.1.13）。
 * 包装原型链指向 Object.prototype（§20.1.3：包装对象也是对象）。 */
void installWrapperBuiltin(JSContext *ctx, const char *name, ObjectKind kind,
                           JSBuiltinFn ctor, JSBuiltinFn valueOf,
                           JSBuiltinFn toStringFn) {
  JSValue *global = ctx->globalObject();
  JSValue *ctor_v = makeBuiltin(ctx, ctor, ThisMode::Strict);
  global->object()->setProperty(name, ctor_v);

  JSValue *proto = ctx->createObject(objectProto(ctx)->object()); /* 原型链 -> Object.prototype */
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

  /* Object 构造器（M3.3，§20.1.1）：先于包装对象建立 Object.prototype
   * 原型链根（包装对象/字面量/装箱都指向它）。 */
  JSValue *object = makeBuiltin(ctx, objectConstructor, ThisMode::Strict);
  global->object()->setProperty("Object", object);

  JSObject *object_fn = object->object(); /* JSFunction 是 JSObject */
  object_fn->setProperty("getPrototypeOf",
                         makeBuiltin(ctx, objectGetPrototypeOf, ThisMode::Strict));
  object_fn->setProperty("setPrototypeOf",
                         makeBuiltin(ctx, objectSetPrototypeOf, ThisMode::Strict));
  object_fn->setProperty("create",
                         makeBuiltin(ctx, objectCreate, ThisMode::Strict));

  /* Object.prototype（§20.1.3）：原型为 null；进 context 缓存（objectProto）。
   * 普通对象字面量 / 装箱对象 / 包装对象原型的原型链都汇聚于此。 */
  JSValue *object_proto = objectProto(ctx);
  object_fn->setProperty("prototype", object_proto);
  object_proto->object()->setProperty("constructor", object);

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
  JSValue *symbol_proto = ctx->createObject(objectProto(ctx)->object());
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

/* 取 Object.prototype（M3.3，惰性创建 + 缓存 + 进根集）。
 * 供 Object 构造器新建对象 / vm NewObject 字面量原型链使用。 */
JSValue *objectProto(JSContext *ctx) {
  JSValue *p = ctx->objectProto();
  if (p != nullptr) return p;
  p = ctx->createObject(nullptr); /* Object.prototype 原型为 null（§20.1.3） */
  ctx->setObjectProto(p);
  return p;
}

}  // namespace neojs
