// PrimitiveWrapperBuiltins.cpp - Boolean/Number/String/Symbol 包装类注册实现
//
// bootstrap 顺序（依赖 Object/Function/Array 内建已就绪）：
//   1. Boolean.prototype / Number.prototype / String.prototype（均为包装对象，
//      [[PrimitiveValue]] 分别是 false / +0 / ""，ES2024 §20.3.3/§21.1.3/§22.1.3）
//   2. Symbol.prototype 复用 SymbolBuiltins 已建立的对象（规范上它是普通对象，
//      不是包装对象，ES2024 §20.4.3）
//   3. setWrapperPrototypes —— 此后 createPrimitiveWrapper 按原始值类型自动挂链
//   4. Boolean/Number/String 构造函数（替换自动生成的 prototype 属性）
//   5. 双向 constructor 引用
//   6. prototype.valueOf / toString / [Symbol.toStringTag]
//   7. 注册到 global
//
// 构造函数语义（ES2024 §20.3.1/§21.1.1/§22.1.1）：
//   Boolean(v) / new Boolean(v) —— call 返回 ToBoolean(v)，new 返回包装对象
//   Number(v)  / new Number(v)  —— call 返回 ToNumber(v)，new 返回包装对象
//   String(v)  / new String(v)  —— call 返回 ToString(v)，new 返回包装对象
//   Symbol 不是构造函数（new Symbol 报错），其包装对象只能由 Object(sym) 产生
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）

#include "core/PrimitiveWrapperBuiltins.h"

#include "core/BigIntOps.h"
#include "core/IContext.h"
#include "core/NumberBuiltins.h"
#include "core/StringBuiltins.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Boolean.h"
#include "core/types/Number.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <cmath>
#include <string>

namespace neo::core {

namespace {

// —— 构造函数 ——

// Boolean(value)：call → ToBoolean（原始值）
Value* booleanCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    bool b = argc > 0 ? detail::toBoolean(argv[0]) : false;
    return ctx->createBoolean(b);
}

// new Boolean(value) → 包装对象
Value* booleanConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    bool b = argc > 0 ? detail::toBoolean(argv[0]) : false;
    return ctx->createPrimitiveWrapper(ctx->createBoolean(b));
}

// Number(value)：call → ToNumeric 后转 Number（ES2024 §21.1.1.1）。
// BigInt 是特例：Number(1n) === 1（ToNumber 本身对 BigInt 抛错）。
Value* numberCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createNumber(0.0);
    Value* n = detail::toNumericValue(ctx, argv[0]);
    if (detail::isErrorValue(n)) return n;
    if (n->type()->kind() == TypeKind::BigInt) {
        return ctx->createNumber(detail::bigIntValue(n).toDouble());
    }
    return n;
}

// new Number(value) → 包装对象
Value* numberConstruct(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* n = numberCtor(ctx, self, argc, argv);
    if (detail::isErrorValue(n)) return n;
    return ctx->createPrimitiveWrapper(n);
}

// String(value)：call → ToString（原始值）。
// String(sym) 是 ToString 的特例：返回 "Symbol(description)"（ES2024 §22.1.1.1）；
// 其余走完整 ToString（§7.1.14，对象先 ToPrimitive）。
Value* stringCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createString("");
    if (argv[0]->type()->kind() == TypeKind::Symbol) return ctx->toString(argv[0]);
    return detail::toStringValue(ctx, argv[0]);
}

// String.raw(template, ...substitutions)：ES2024 §22.1.2.4。
// 按 raw 数组逐段拼接；raw[i] 之间插入 ToString(substitutions[i])。
Value* stringRaw(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0 || !detail::isObjectKind(argv[0]->type()->kind())) {
        return detail::typeError(ctx, "String.raw requires a template object");
    }
    Value* raw = ctx->getProperty(argv[0], "raw");
    if (detail::isErrorValue(raw)) return raw;
    TypeKind rawKind = raw->type()->kind();
    if (rawKind == TypeKind::Undefined || rawKind == TypeKind::Null) {
        return detail::typeError(ctx,
                                 "String.raw template raw is null or undefined");
    }
    Value* rawObject =
        detail::isObjectKind(rawKind) ? raw : ctx->createPrimitiveWrapper(raw);

    Value* lengthValue = ctx->getProperty(rawObject, "length");
    if (detail::isErrorValue(lengthValue)) return lengthValue;
    Value* lengthNumber = detail::toNumberValue(ctx, lengthValue);
    if (detail::isErrorValue(lengthNumber)) return lengthNumber;
    double length = static_cast<NumberData*>(lengthNumber->data())->value;
    if (std::isnan(length) || length <= 0.0) return ctx->createString("");
    if (length > 4294967295.0) length = 4294967295.0;
    const uint32_t count = static_cast<uint32_t>(length);

    std::string result;
    for (uint32_t i = 0; i < count; ++i) {
        Value* key = ctx->createString(std::to_string(i));
        Value* segment = ctx->getProperty(rawObject, key);
        if (detail::isErrorValue(segment)) return segment;
        Value* segmentString = detail::toStringValue(ctx, segment);
        if (detail::isErrorValue(segmentString)) return segmentString;
        result += static_cast<StringData*>(segmentString->data())->value;

        if (i + 1 < count) {
            if (static_cast<size_t>(i + 1) < argc) {
                Value* substitution =
                    detail::toStringValue(ctx, argv[i + 1]);
                if (detail::isErrorValue(substitution)) return substitution;
                result += static_cast<StringData*>(substitution->data())->value;
            }
        }
    }
    return ctx->createString(result);
}

// new String(value) → 包装对象。
// 与 call 路径不同：Symbol 不走 SymbolDescriptiveString，而是 ToString 抛 TypeError
// （ES2024 §22.1.1.1 step 2.b：仅 NewTarget 为 undefined 时才特判 Symbol）。
Value* stringConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createPrimitiveWrapper(ctx->createString(""));
    Value* s = detail::toStringValue(ctx, argv[0]);
    if (detail::isErrorValue(s)) return s;
    return ctx->createPrimitiveWrapper(s);
}

// —— prototype 方法 ——

// 取 this 的原始值：this 是期望类型的原始值或对应包装对象。
// 否则返回 nullptr（调用方报 TypeError）。
Value* unwrapThis(Value* self, TypeKind want) {
    if (self == nullptr) return nullptr;
    if (self->type()->kind() == want) return self;
    if (self->type()->kind() == TypeKind::PrimitiveWrapper) {
        Value* p = wrapperPrimitiveValue(self);
        if (p != nullptr && p->type()->kind() == want) return p;
    }
    return nullptr;
}

Value* typeError(IContext* ctx, const char* method, const char* typeName) {
    std::string msg = std::string(method) + " requires that 'this' be a " + typeName;
    return detail::typeError(ctx, msg);
}

// Boolean.prototype.valueOf()
Value* booleanValueOf(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* p = unwrapThis(self, TypeKind::Boolean);
    if (p == nullptr) return typeError(ctx, "Boolean.prototype.valueOf", "Boolean");
    return p;
}

// Boolean.prototype.toString()
Value* booleanToString(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* p = unwrapThis(self, TypeKind::Boolean);
    if (p == nullptr) return typeError(ctx, "Boolean.prototype.toString", "Boolean");
    return ctx->createString(static_cast<BooleanData*>(p->data())->value ? "true" : "false");
}

// Number.prototype.valueOf()
Value* numberValueOf(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* p = unwrapThis(self, TypeKind::Number);
    if (p == nullptr) return typeError(ctx, "Number.prototype.valueOf", "Number");
    return p;
}

// Number.prototype.toString()
Value* numberToStringFn(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* p = unwrapThis(self, TypeKind::Number);
    if (p == nullptr) return typeError(ctx, "Number.prototype.toString", "Number");
    return ctx->toString(p);
}

// String.prototype.valueOf()
Value* stringValueOf(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* p = unwrapThis(self, TypeKind::String);
    if (p == nullptr) return typeError(ctx, "String.prototype.valueOf", "String");
    return p;
}

// String.prototype.toString()
Value* stringToStringFn(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* p = unwrapThis(self, TypeKind::String);
    if (p == nullptr) return typeError(ctx, "String.prototype.toString", "String");
    return p;
}

// Symbol.prototype.valueOf()
Value* symbolValueOf(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* p = unwrapThis(self, TypeKind::Symbol);
    if (p == nullptr) return typeError(ctx, "Symbol.prototype.valueOf", "Symbol");
    return p;
}

// 把值定义为 global object 属性（全局环境记录的对象记录）。
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->defineGlobal(name, v, true, false, true);
}

// 定义 prototype 方法（writable/不可枚举/可配置，与 ES 一致）
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    // 内建原型方法不可构造且无自有 prototype 属性（§20.3.3 等）。
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* f = ctx->createNativeFunction(fn, minArgc, {}, opts);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

// 建立单个包装构造函数：构造函数 + prototype 双向引用 + 方法 + 挂 global。
// proto 是已建好的包装原型对象；constructFn 是 new 路径（返回包装对象）。
bool registerWrapperCtor(IContext* ctx, const char* name, NativeFn ctor, NativeFn constructFn,
                         Value* proto, NativeFn valueOf, NativeFn toString,
                         const char* typeTag, Value** outFn = nullptr) {
    Value* fn = ctx->createNativeFunction(ctor, 0);
    if (fn == ctx->oomValue()) return false;
    if (outFn != nullptr) *outFn = fn;
    ctx->setFunctionName(fn, name);
    // 构造函数 length = 1（§20.3.2.1 等）；createNativeFunction 以 minArgc=0
    // 建过（构造调用允许零实参），此处按规范重定义。
    if (ctx->defineProperty(fn, "length", ctx->createNumber(1.0), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // call/construct 语义不同：new 走专用钩子返回包装对象
    ctx->setFunctionConstructor(fn, constructFn);
    // fn.prototype → proto（不可写/不可枚举/不可配置，ES2024 §20.3.2.1 等）
    if (ctx->defineProperty(fn, "prototype", proto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // proto.constructor → fn
    if (ctx->defineProperty(proto, "constructor", fn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, proto, "valueOf", valueOf, 0)) return false;
    if (!defineMethod(ctx, proto, "toString", toString, 0)) return false;
    // proto[Symbol.toStringTag] = typeTag（不可写/不可枚举/可配置）
    if (ctx->defineProperty(proto, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            ctx->createString(typeTag), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    bindGlobal(ctx, name, fn);
    return true;
}

}  // namespace

bool registerPrimitiveWrapperBuiltins(IContext* ctx) {
    // —— 1. Boolean/Number/String 原型（均为包装对象，持有对应原始值）——
    Value* booleanProto = ctx->createPrimitiveWrapper(ctx->createBoolean(false));
    if (booleanProto == ctx->oomValue()) return false;
    Value* numberProto = ctx->createPrimitiveWrapper(ctx->createNumber(0.0));
    if (numberProto == ctx->oomValue()) return false;
    Value* stringProto = ctx->createPrimitiveWrapper(ctx->createString(""));
    if (stringProto == ctx->oomValue()) return false;

    // 原型链：*Prototype → Object.prototype
    for (Value* proto : {booleanProto, numberProto, stringProto}) {
        if (ctx->setPrototype(proto, ctx->objectPrototype())->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }

    // —— 2. Symbol.prototype 复用 SymbolBuiltins 已建立的对象（普通对象，非包装）——
    // 注：SymbolBuiltins 在 Object.prototype 建立之前运行，故其 prototype 的
    // 原型指针为空，此处补挂 Object.prototype。
    Value* symbolFn = ctx->global()->lookup("Symbol");
    if (symbolFn == nullptr) return false;
    // Symbol 构造函数同样创建于 Function.prototype 之前，补挂其原型
    if (ctx->setPrototype(symbolFn, ctx->functionPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    Value* symbolProto = ctx->getProperty(symbolFn, "prototype");
    if (symbolProto->type()->kind() != TypeKind::Object) return false;
    if (ctx->setPrototype(symbolProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    // 补 valueOf（toString 与 toStringTag 已由 SymbolBuiltins 定义）
    if (!defineMethod(ctx, symbolProto, "valueOf", symbolValueOf, 0)) return false;

    // —— 3. 原型就绪：此后 createPrimitiveWrapper 自动挂链 ——
    ctx->setWrapperPrototypes(booleanProto, numberProto, stringProto, symbolProto);

    // —— 4-6. 三个包装构造函数 ——
    if (!registerWrapperCtor(ctx, "Boolean", booleanCtor, booleanConstruct, booleanProto,
                             booleanValueOf, booleanToString, "Boolean")) {
        return false;
    }
    Value* numberFn = nullptr;
    if (!registerWrapperCtor(ctx, "Number", numberCtor, numberConstruct, numberProto,
                             numberValueOf, numberToStringFn, "Number", &numberFn)) {
        return false;
    }
    if (!registerNumberBuiltins(ctx, numberFn, numberProto)) return false;
    Value* stringFn = nullptr;
    if (!registerWrapperCtor(ctx, "String", stringCtor, stringConstruct,
                             stringProto, stringValueOf, stringToStringFn,
                             "String", &stringFn)) {
        return false;
    }
    // String.prototype 常用方法（ES2024 §22.1.3）。
    if (!registerStringBuiltins(ctx, stringFn, stringProto)) return false;
    // String.raw（ES2024 §22.1.2.4）：不可写/不可枚举/可配置的静态方法。
    FunctionCreateOptions rawOpts;
    rawOpts.constructible = false;
    rawOpts.hasPrototypeProperty = false;
    Value* rawFn = ctx->createNativeFunction(stringRaw, 1, {}, rawOpts);
    if (rawFn == ctx->oomValue()) return false;
    ctx->setFunctionName(rawFn, "raw");
    if (ctx->defineProperty(stringFn, "raw", rawFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    return true;
}

}  // namespace neo::core
