// Number.cpp - NumberType 行为方法实现（算术核心）
//
// number 参与运算：
// add：若右是 string → 调换让 string 主导 concat（JS + 规则：任一是 string → concat）；
//      否则 ToNumber(b) 后 a.value + bnum。
// sub/mul/div：ToNumber(b) 后算术（string 不主导算术，JS -/*/ 无 concat 分支）。
// toString：numberToString(a.value)。
//
// JS 除零规则（C++ double 自然产生）：1/0=Infinity, 0/0=NaN, -1/0=-Infinity。
//
// 关联 ADR: 016（Type 体系修订 §4/§5/§6）、017（透传）、006（异常即值）

#include "core/IContext.h"
#include "core/types/Number.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

using namespace detail;

Value* NumberType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    // JS + 规则：任一是 string → string concat（保持原左右顺序，a 在前）
    if (b->type()->kind() == TypeKind::String) {
        return ctx->createString(valueToString(a) + valueToString(b));
    }
    // 否则数值加法
    double lhs = static_cast<NumberData*>(a->data())->value;
    double rhs = toNumber(b);
    return ctx->createNumber(lhs + rhs);
}

Value* NumberType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    double lhs = static_cast<NumberData*>(a->data())->value;
    double rhs = toNumber(b);
    return ctx->createNumber(lhs - rhs);
}

Value* NumberType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    double lhs = static_cast<NumberData*>(a->data())->value;
    double rhs = toNumber(b);
    return ctx->createNumber(lhs * rhs);
}

Value* NumberType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    double lhs = static_cast<NumberData*>(a->data())->value;
    double rhs = toNumber(b);
    // JS 除零：1/0=Infinity, 0/0=NaN（C++ double 自然产生，无须特殊处理）
    return ctx->createNumber(lhs / rhs);
}

Value* NumberType::toString(IContext* ctx, Value* self) {
    if (Value* p = passthrough(self)) return p;  // NEO_NO_COVERAGE
    double n = static_cast<NumberData*>(self->data())->value;
    return ctx->createString(numberToString(n));
}

}  // namespace neo::core
