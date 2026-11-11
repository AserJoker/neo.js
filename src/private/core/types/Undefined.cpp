// Undefined.cpp - UndefinedType 行为方法实现
//
// undefined 参与运算（ES2024 §13.15.3 / §13.15.4）：
// add：两侧 ToPrimitive；任一为 string → 拼接，否则数值相加（undefined → NaN）。
// sub/mul/div：两侧 ToPrimitive(NUMBER) 后 ToNumber（undefined → NaN），再算术。
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
    return addValues(ctx, a, b);
}

Value* UndefinedType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '-', a, b);
}

Value* UndefinedType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '*', a, b);
}

Value* UndefinedType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '/', a, b);
}

Value* UndefinedType::toString(IContext* ctx, Value* /*self*/) {
    return ctx->createString("undefined");
}

Value* UndefinedType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                           size_t /*argc*/, Value* /*argv*/[]) {
    return notAFunction(ctx, self);
}


// 属性操作（ADR-018）：基础类型无自有属性，读取返回 undefined，写入报错。
Value* UndefinedType::getProperty(IContext* ctx, Value* self, Value* key) {
    return primitiveGetProperty(ctx, self, key);
}
Value* UndefinedType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    return primitiveSetProperty(ctx, self, key, value);
}
Value* UndefinedType::hasProperty(IContext* ctx, Value* /*self*/, Value* /*key*/) {
    return ctx->createBoolean(false);
}
Value* UndefinedType::deleteProperty(IContext* ctx, Value* /*self*/, Value* /*key*/) {
    return ctx->createBoolean(true);
}
Value* UndefinedType::defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* /*value*/, bool /*writable*/, bool /*enumerable*/,
                          bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}


// 原型操作（ADR-018 §1）：基础类型无原型，读取返回 undefined，设置报错。
Value* UndefinedType::getPrototype(IContext* ctx, Value* self) {
    return primitiveGetPrototype(ctx, self);
}
Value* UndefinedType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return primitiveSetPrototype(ctx, self, proto);
}


// 构造调用（ADR-034）：基础类型不是构造函数。
Value* UndefinedType::construct(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return notAConstructor(ctx, self);
}


// 访问器属性定义（ADR-018）：原始类型无属性容器。
Value* UndefinedType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* /*getter*/,
                               Value* /*setter*/, bool /*enumerable*/, bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}

}  // namespace neo::core
