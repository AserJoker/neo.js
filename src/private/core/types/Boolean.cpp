// Boolean.cpp - BooleanType 行为方法实现
//
// boolean 参与运算（ES2024 §13.15.3 / §13.15.4）：
// add：两侧 ToPrimitive；任一为 string → 拼接（"true"+...），否则数值相加。
// sub/mul/div：两侧 ToPrimitive(NUMBER) 后 ToNumber，再算术。
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
    return addValues(ctx, a, b);
}

Value* BooleanType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '-', a, b);
}

Value* BooleanType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '*', a, b);
}

Value* BooleanType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '/', a, b);
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
Value* BooleanType::getProperty(IContext* ctx, Value* self, Value* key) {
    return primitiveGetProperty(ctx, self, key);
}
Value* BooleanType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    return primitiveSetProperty(ctx, self, key, value);
}
Value* BooleanType::hasProperty(IContext* ctx, Value* /*self*/, Value* /*key*/) {
    return ctx->createBoolean(false);
}
Value* BooleanType::deleteProperty(IContext* ctx, Value* /*self*/, Value* /*key*/) {
    return ctx->createBoolean(true);
}
Value* BooleanType::defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* /*value*/, bool /*writable*/, bool /*enumerable*/,
                          bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}


// 原型操作（ADR-018 §1）：基础类型无原型，读取返回 undefined，设置报错。
Value* BooleanType::getPrototype(IContext* ctx, Value* self) {
    return primitiveGetPrototype(ctx, self);
}
Value* BooleanType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return primitiveSetPrototype(ctx, self, proto);
}


// 构造调用（ADR-034）：基础类型不是构造函数。
Value* BooleanType::construct(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return notAConstructor(ctx, self);
}


// 访问器属性定义（ADR-018）：原始类型无属性容器。
Value* BooleanType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* /*getter*/,
                               Value* /*setter*/, bool /*enumerable*/, bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}

}  // namespace neo::core
