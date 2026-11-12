// types/BigInt.cpp - BigIntType 行为方法
//
// 算术/比较/位运算的语义在 TypeBehavior.h / OperatorBehavior.h 的统一转换链里，
// 本文件只做类型分派入口，保证 `1n + 1n` 与 `1 + 1n` 走同一套规则。

#include "core/IContext.h"
#include "core/types/BigInt.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

using namespace detail;

Value* BigIntType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return addValues(ctx, a, b);
}

Value* BigIntType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '-', a, b);
}

Value* BigIntType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '*', a, b);
}

Value* BigIntType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '/', a, b);
}

Value* BigIntType::toString(IContext* ctx, Value* self) {
    return ctx->createString(static_cast<BigIntData*>(self->data())->value.toString());
}

Value* BigIntType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                        size_t /*argc*/, Value* /*argv*/[]) {
    return notAFunction(ctx, self);
}

Value* BigIntType::construct(IContext* ctx, Value* self, size_t /*argc*/,
                             Value* /*argv*/[]) {
    return notAConstructor(ctx, self);
}

// 基础类型无自有属性：读取返回 undefined，写入报错。
Value* BigIntType::getProperty(IContext* ctx, Value* self, Value* key) {
    return primitiveGetProperty(ctx, self, key);
}
Value* BigIntType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    return primitiveSetProperty(ctx, self, key, value);
}
Value* BigIntType::hasProperty(IContext* ctx, Value* /*self*/, Value* /*key*/) {
    return ctx->createBoolean(false);
}
Value* BigIntType::deleteProperty(IContext* ctx, Value* /*self*/, Value* /*key*/) {
    return ctx->createBoolean(true);
}
Value* BigIntType::defineProperty(IContext* ctx, Value* self, Value* key,
                                  Value* /*value*/, bool /*writable*/,
                                  bool /*enumerable*/, bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}
Value* BigIntType::defineAccessor(IContext* ctx, Value* self, Value* key,
                                  Value* /*getter*/, Value* /*setter*/,
                                  bool /*enumerable*/, bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}
Value* BigIntType::getPrototype(IContext* ctx, Value* self) {
    return primitiveGetPrototype(ctx, self);
}
Value* BigIntType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return primitiveSetPrototype(ctx, self, proto);
}

}  // namespace neo::core
