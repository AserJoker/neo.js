// String.cpp - StringType 行为方法实现（concat 核心）
//
// string 参与运算：
// add：主导 concat——valueToString(a) + valueToString(b)（JS + 规则：任一是 string → concat）。
// sub/mul/div：ToNumber(a) op ToNumber(b)（string 不主导算术，-/*/ 无 concat 分支）。
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
    // 主导 concat：ToString(a) + ToString(b)
    std::string result = valueToString(a) + valueToString(b);
    return ctx->createString(result);
}

Value* StringType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    // string 不主导算术：ToNumber(a) - ToNumber(b)
    return ctx->createNumber(toNumber(a) - toNumber(b));
}

Value* StringType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return ctx->createNumber(toNumber(a) * toNumber(b));
}

Value* StringType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return ctx->createNumber(toNumber(a) / toNumber(b));
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
Value* StringType::getProperty(IContext* ctx, Value* self, std::string_view key) {
    return primitiveGetProperty(ctx, self, key);
}
Value* StringType::setProperty(IContext* ctx, Value* self, std::string_view key, Value* value) {
    return primitiveSetProperty(ctx, self, key, value);
}
bool StringType::hasProperty(IContext* /*ctx*/, Value* /*self*/, std::string_view /*key*/) {
    return false;
}
Value* StringType::deleteProperty(IContext* ctx, Value* /*self*/, std::string_view /*key*/) {
    return ctx->createBoolean(true);
}
Value* StringType::defineProperty(IContext* ctx, Value* self, std::string_view key,
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

}  // namespace neo::core
