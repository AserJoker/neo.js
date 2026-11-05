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
    double n = static_cast<NumberData*>(self->data())->value;
    return ctx->createString(numberToString(n));
}

Value* NumberType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                        size_t /*argc*/, Value* /*argv*/[]) {
    return notAFunction(ctx, self);
}


// 属性操作（ADR-018）：基础类型无自有属性，读取返回 undefined，写入报错。
Value* NumberType::getProperty(IContext* ctx, Value* self, std::string_view key) {
    return primitiveGetProperty(ctx, self, key);
}
Value* NumberType::setProperty(IContext* ctx, Value* self, std::string_view key, Value* value) {
    return primitiveSetProperty(ctx, self, key, value);
}
bool NumberType::hasProperty(IContext* /*ctx*/, Value* /*self*/, std::string_view /*key*/) {
    return false;
}
Value* NumberType::deleteProperty(IContext* ctx, Value* /*self*/, std::string_view /*key*/) {
    return ctx->createBoolean(true);
}
Value* NumberType::defineProperty(IContext* ctx, Value* self, std::string_view key,
                          Value* /*value*/, bool /*writable*/, bool /*enumerable*/,
                          bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}


// 原型操作（ADR-018 §1）：基础类型无原型，读取返回 undefined，设置报错。
Value* NumberType::getPrototype(IContext* ctx, Value* self) {
    return primitiveGetPrototype(ctx, self);
}
Value* NumberType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return primitiveSetPrototype(ctx, self, proto);
}


// 构造调用（ADR-034）：基础类型不是构造函数。
Value* NumberType::construct(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return notAConstructor(ctx, self);
}

}  // namespace neo::core
