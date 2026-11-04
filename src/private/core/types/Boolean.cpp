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
    return ctx->add(ctx->createNumber(toNumber(a)), b);
}

Value* BooleanType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return ctx->sub(ctx->createNumber(toNumber(a)), b);
}

Value* BooleanType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return ctx->mul(ctx->createNumber(toNumber(a)), b);
}

Value* BooleanType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return ctx->div(ctx->createNumber(toNumber(a)), b);
}

Value* BooleanType::toString(IContext* ctx, Value* self) {
    bool b = static_cast<BooleanData*>(self->data())->value;
    return ctx->createString(b ? "true" : "false");
}

Value* BooleanType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                         size_t /*argc*/, Value* /*argv*/[]) {
    return notAFunction(ctx, self);
}


// 属性操作（ADR-018）：基础类型无自有属性，读取返回 undefined，写入报错。
Value* BooleanType::getProperty(IContext* ctx, Value* self, std::string_view key) {
    return primitiveGetProperty(ctx, self, key);
}
Value* BooleanType::setProperty(IContext* ctx, Value* self, std::string_view key, Value* value) {
    return primitiveSetProperty(ctx, self, key, value);
}
bool BooleanType::hasProperty(IContext* /*ctx*/, Value* /*self*/, std::string_view /*key*/) {
    return false;
}
Value* BooleanType::deleteProperty(IContext* ctx, Value* /*self*/, std::string_view /*key*/) {
    return ctx->createBoolean(true);
}
Value* BooleanType::defineProperty(IContext* ctx, Value* self, std::string_view key,
                          Value* /*value*/, bool /*writable*/, bool /*enumerable*/,
                          bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}

}  // namespace neo::core
