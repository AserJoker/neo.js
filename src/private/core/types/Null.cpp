// Null.cpp - NullType 行为方法实现
//
// null 参与运算：ToNumber(null)=+0。
// add：若右是 string → 调换让 string 主导 concat；否则 ToNumber(0) 调换给 NumberType。
// sub/mul/div：ToNumber(0) 调换给 NumberType。
// toString：返回 "null"。
//
// 关联 ADR: 016（Type 体系修订 §5/§6）、017（透传）、006（异常即值）

#include "core/IContext.h"
#include "core/types/Null.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

using namespace detail;

Value* NullType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    // + 规则：右是 string → string concat（保持原左右顺序，a 在前）
    if (b->type()->kind() == TypeKind::String) {
        return ctx->createString(valueToString(a) + valueToString(b));
    }
    return ctx->add(ctx->createNumber(0.0), b);
}

Value* NullType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return ctx->sub(ctx->createNumber(0.0), b);
}

Value* NullType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return ctx->mul(ctx->createNumber(0.0), b);
}

Value* NullType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return ctx->div(ctx->createNumber(0.0), b);
}

Value* NullType::toString(IContext* ctx, Value* /*self*/) {
    return ctx->createString("null");
}

Value* NullType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                      size_t /*argc*/, Value* /*argv*/[]) {
    return notAFunction(ctx, self);
}

}  // namespace neo::core
