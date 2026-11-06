// SymbolBuiltins.cpp - Symbol 包装类注册实现（ES2024 §20.4，最小实现）
//
// Symbol(description)：
//   - 普通函数调用，返回新的 symbol（每次唯一）
//   - description 缺省或 undefined → 空描述；其他类型 ToString
// Symbol.prototype.toString()：
//   - this 是 symbol → "Symbol(description)"；否则 Exception
// Symbol.<well-known>：
//   - 15 个 well-known symbols 作为构造函数属性（writable=false/enumerable=false/
//     configurable=false，ES2024 §20.4.2.2）
// Symbol.prototype[Symbol.toStringTag] = "Symbol"（ES2024 §20.4.3.5）
// Symbol.for(key) / Symbol.keyFor(sym)：
//   - for 走 Context 全局注册表（同 key 永远返回同一 symbol）
//   - keyFor 反查 key，非 symbol 或未登记返回 undefined
//
// 本阶段最小实现：不处理 new Symbol() 报错、description getter、valueOf。
//
// 关联 ADR: 020（标准库组织）、016（Type 体系）

#include "core/SymbolBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/Symbol.h"
#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

namespace {

// Symbol(description) —— 返回新 symbol。
// description 走 ToString（ES2024 §20.4.1.1），可触发用户 toString。
Value* symbolCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    std::string desc;
    if (argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined) {
        Value* s = detail::toStringValue(ctx, argv[0]);
        if (detail::isErrorValue(s)) return s;
        desc = detail::valueToString(s);
    }
    return ctx->createSymbol(desc);
}

// Symbol.prototype.toString() —— this 必须是 symbol 或 Symbol 包装对象
Value* symbolToString(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* sym = self;
    if (sym != nullptr && sym->type()->kind() == TypeKind::PrimitiveWrapper) {
        sym = wrapperPrimitiveValue(sym);  // 拆 [[PrimitiveValue]]
    }
    if (sym == nullptr || sym->type()->kind() != TypeKind::Symbol) {
        return detail::typeError(
            ctx, "Symbol.prototype.toString requires that 'this' be a Symbol");
    }
    return ctx->toString(sym);
}

// Symbol.prototype[@@toPrimitive](hint) —— 返回 [[SymbolData]]（ES2024 §20.4.3.5）。
// hint 被忽略；this 须为 symbol 或 Symbol 包装对象。
Value* symbolToPrimitive(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* sym = self;
    if (sym != nullptr && sym->type()->kind() == TypeKind::PrimitiveWrapper) {
        sym = wrapperPrimitiveValue(sym);
    }
    if (sym == nullptr || sym->type()->kind() != TypeKind::Symbol) {
        return detail::typeError(
            ctx,
            "Symbol.prototype[Symbol.toPrimitive] requires that 'this' be a Symbol");
    }
    return sym;
}

// Symbol.for(key) —— 全局注册表查找/登记（ES2024 §20.4.2.1）。
// key 走 ToString（§20.4.2.1），可触发用户 toString。
Value* symbolFor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    std::string key;
    if (argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined) {
        Value* s = detail::toStringValue(ctx, argv[0]);
        if (detail::isErrorValue(s)) return s;
        key = detail::valueToString(s);
    }
    return ctx->symbolFor(key);
}

// Symbol.keyFor(sym) —— 反查注册表 key（ES2024 §20.4.2.6）
Value* symbolKeyFor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createUndefined();
    return ctx->symbolKeyFor(argv[0]);
}

// 把值挂到 global scope：add 跟踪 + bind 名字
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->global()->add(v);
    ctx->global()->bind(name, v);
}

}  // namespace

bool registerSymbolBuiltins(IContext* ctx) {
    // 1. Symbol 构造函数（普通函数，非构造函数语义）
    Value* symbolFn = ctx->createNativeFunction(symbolCtor, 0);
    if (symbolFn == ctx->oomValue()) return false;
    ctx->setFunctionName(symbolFn, "Symbol");

    // 2. Symbol.prototype：createNativeFunction 已自动创建，直接取用
    Value* proto = ctx->getProperty(symbolFn, "prototype");
    if (proto->type()->kind() != TypeKind::Object) return false;

    // 2a. prototype.toString
    Value* toStringFn = ctx->createNativeFunction(symbolToString, 0);
    if (toStringFn == ctx->oomValue()) return false;
    ctx->setFunctionName(toStringFn, "toString");
    if (ctx->defineProperty(proto, "toString", toStringFn, true, false, true)->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }

    // 2b. prototype[Symbol.toStringTag] = "Symbol"
    if (ctx->defineProperty(proto, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            ctx->createString("Symbol"), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // 2c. prototype[Symbol.toPrimitive]（ES2024 §20.4.3.5）
    Value* toPrimFn = ctx->createNativeFunction(symbolToPrimitive, 0);
    if (toPrimFn == ctx->oomValue()) return false;
    ctx->setFunctionName(toPrimFn, "[Symbol.toPrimitive]");
    if (ctx->defineProperty(proto, ctx->getWellKnownSymbol(WellKnownSymbol::ToPrimitive),
                            toPrimFn, false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // 3. Symbol.<well-known>（15 个，不可写/不可枚举/不可配置）
    for (size_t i = 0; i < kWellKnownSymbolCount; ++i) {
        auto which = static_cast<WellKnownSymbol>(i);
        Value* sym = ctx->getWellKnownSymbol(which);
        if (ctx->defineProperty(symbolFn, wellKnownSymbolName(which), sym,
                                false, false, false)
                ->type()->kind() == TypeKind::Exception) {
            return false;
        }
    }

    // 4. Symbol.for / Symbol.keyFor（不可写/不可枚举/可配置，ES2024 §20.4.2.2）
    // minArgc=0：无参时按 undefined 处理（ES 规范不要求参数个数）
    Value* forFn = ctx->createNativeFunction(symbolFor, 0);
    if (forFn == ctx->oomValue()) return false;
    ctx->setFunctionName(forFn, "for");
    if (ctx->defineProperty(symbolFn, "for", forFn, true, false, true)->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }

    Value* keyForFn = ctx->createNativeFunction(symbolKeyFor, 0);
    if (keyForFn == ctx->oomValue()) return false;
    ctx->setFunctionName(keyForFn, "keyFor");
    if (ctx->defineProperty(symbolFn, "keyFor", keyForFn, true, false, true)->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }

    // 5. 注册到 global
    bindGlobal(ctx, "Symbol", symbolFn);
    return true;
}

}  // namespace neo::core
