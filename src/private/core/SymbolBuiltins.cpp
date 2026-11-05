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
//
// 本阶段最小实现：不处理 new Symbol() 报错、description getter、valueOf、
// Symbol.for/keyFor（注册表后续）。
//
// 关联 ADR: 020（标准库组织）、016（Type 体系）

#include "core/SymbolBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Symbol.h"
#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

namespace {

// Symbol(description) —— 返回新 symbol
Value* symbolCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    std::string desc;
    if (argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined) {
        desc = detail::valueToString(argv[0]);
    }
    return ctx->createSymbol(desc);
}

// Symbol.prototype.toString() —— this 必须是 symbol
Value* symbolToString(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    if (self == nullptr || self->type()->kind() != TypeKind::Symbol) {
        return ctx->createException(ctx->createString("Symbol.prototype.toString requires that 'this' be a Symbol"));
    }
    return ctx->toString(self);
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

    // 4. 注册到 global
    bindGlobal(ctx, "Symbol", symbolFn);
    return true;
}

}  // namespace neo::core
