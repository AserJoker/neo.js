// Boolean.cpp - BooleanType 行为方法实现
//
// boolean 参与运算：ToNumber(true)=1, ToNumber(false)=0。
// add：若右是 string → 调换让 string 主导 concat（"true"+...）；否则 ToNumber 调换给 NumberType。
// sub/mul/div：ToNumber 调换给 NumberType。
// toString：返回 "true" / "false"。
//
// 关联 ADR: 016（Type 体系修订 §5/§6）、017（透传）、006（异常即值）

#include "core/IContext.h"
#include "core/types/Boolean.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

using namespace detail;

Value* BooleanType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    // + 规则：右是 string → string concat（保持原左右顺序，a 在前）
    if (b->type()->kind() == TypeKind::String) {
        return ctx->createString(valueToString(a) + valueToString(b));
    }
    // 否则 ToNumber(a) 调换给 NumberType
    Value* num = ctx->createNumber(toNumber(a));
    if (needPassthrough(num)) return num;  // NEO_NO_COVERAGE
    return ctx->add(num, b);
}

Value* BooleanType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    Value* num = ctx->createNumber(toNumber(a));
    if (needPassthrough(num)) return num;  // NEO_NO_COVERAGE
    return ctx->sub(num, b);
}

Value* BooleanType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    Value* num = ctx->createNumber(toNumber(a));
    if (needPassthrough(num)) return num;  // NEO_NO_COVERAGE
    return ctx->mul(num, b);
}

Value* BooleanType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    Value* num = ctx->createNumber(toNumber(a));
    if (needPassthrough(num)) return num;  // NEO_NO_COVERAGE
    return ctx->div(num, b);
}

Value* BooleanType::toString(IContext* ctx, Value* self) {
    if (Value* p = passthrough(self)) return p;  // NEO_NO_COVERAGE
    bool b = static_cast<BooleanData*>(self->data())->value;
    return ctx->createString(b ? "true" : "false");
}

}  // namespace neo::core
