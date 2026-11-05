// String.cpp - StringType 行为方法实现（concat 核心）
//
// string 参与运算（ES2024 §13.15.3 / §13.15.4）：
// add：两侧 ToPrimitive（缺省 hint）；任一为 string → 拼接（string 主导）。
// sub/mul/div：两侧 ToPrimitive(NUMBER) 后 ToNumber，再算术。
// toString：返回等价 string Value（内容 = self.value）。
//
// 关联 ADR: 016（Type 体系修订 §4/§5/§6）、017（透传）、006（异常即值）

#include "core/IContext.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

using namespace detail;

Value* StringType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return addValues(ctx, a, b);
}

Value* StringType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '-', a, b);
}

Value* StringType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '*', a, b);
}

Value* StringType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '/', a, b);
}

Value* StringType::toString(IContext* ctx, Value* self) {
    const auto& s = static_cast<StringData*>(self->data())->value;
    return ctx->createString(s);
}

Value* StringType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                        size_t /*argc*/, Value* /*argv*/[]) {
    return notAFunction(ctx, self);
}


// 属性操作（ADR-018）：基础类型无自有属性，读取返回 undefined，写入报错。
Value* StringType::getProperty(IContext* ctx, Value* self, Value* key) {
    return primitiveGetProperty(ctx, self, key);
}
Value* StringType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    return primitiveSetProperty(ctx, self, key, value);
}
bool StringType::hasProperty(IContext* /*ctx*/, Value* /*self*/, Value* /*key*/) {
    return false;
}
Value* StringType::deleteProperty(IContext* ctx, Value* /*self*/, Value* /*key*/) {
    return ctx->createBoolean(true);
}
Value* StringType::defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* /*value*/, bool /*writable*/, bool /*enumerable*/,
                          bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}


// 原型操作（ADR-018 §1）：基础类型无原型，读取返回 undefined，设置报错。
Value* StringType::getPrototype(IContext* ctx, Value* self) {
    return primitiveGetPrototype(ctx, self);
}
Value* StringType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return primitiveSetPrototype(ctx, self, proto);
}


// 构造调用（ADR-034）：基础类型不是构造函数。
Value* StringType::construct(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return notAConstructor(ctx, self);
}


// 访问器属性定义（ADR-018）：原始类型无属性容器。
Value* StringType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* /*getter*/,
                               Value* /*setter*/, bool /*enumerable*/, bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}

}  // namespace neo::core
