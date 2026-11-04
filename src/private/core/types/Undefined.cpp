// Undefined.cpp - UndefinedType 行为方法实现
//
// undefined 参与运算：ToNumber(undefined)=NaN。
// add：若右是 string → 调换让 string 主导 concat；否则 ToNumber(NaN) 调换给 NumberType。
// sub/mul/div：ToNumber(NaN) 调换给 NumberType（结果恒 NaN）。
// toString：返回 "undefined"。
//
// 关联 ADR: 016（Type 体系修订 §5/§6）、017（透传）、006（异常即值）

#include "core/IContext.h"
#include "core/types/Undefined.h"
#include "core/types/TypeBehavior.h"

#include <cmath>

namespace neo::core {

using namespace detail;

Value* UndefinedType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    // + 规则：右是 string → string concat（保持原左右顺序，a 在前）
    if (b->type()->kind() == TypeKind::String) {
        return ctx->createString(valueToString(a) + valueToString(b));
    }
    // 否则 ToNumber(undefined)=NaN，调换给 NumberType
    return ctx->add(ctx->createNumber(std::nan("")), b);
}

Value* UndefinedType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return ctx->sub(ctx->createNumber(std::nan("")), b);
}

Value* UndefinedType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return ctx->mul(ctx->createNumber(std::nan("")), b);
}

Value* UndefinedType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return ctx->div(ctx->createNumber(std::nan("")), b);
}

Value* UndefinedType::toString(IContext* ctx, Value* /*self*/) {
    return ctx->createString("undefined");
}

}  // namespace neo::core
