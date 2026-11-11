// Error.cpp - ErrorType 行为方法 + ErrorData 实现
//
// Error 没有特化的属性语义：属性/原型操作全部委托 ObjectType
// （Data 继承 ObjectData，属性容器/原型链/访问器/环检测完全复用）。
// 运算走 ToPrimitive（与普通对象一致），call/construct 报错。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、019（赋值语义 §2）

#include "core/IContext.h"
#include "core/Value.h"
#include "core/types/Error.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

Value* ErrorType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::addValues(ctx, a, b);
}
Value* ErrorType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '-', a, b);
}
Value* ErrorType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '*', a, b);
}
Value* ErrorType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '/', a, b);
}
Value* ErrorType::toString(IContext* ctx, Value* self) {
    return detail::toStringValue(ctx, self);
}

Value* ErrorType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                       size_t /*argc*/, Value* /*argv*/[]) {
    return detail::notAFunction(ctx, self);
}

Value* ErrorType::construct(IContext* ctx, Value* self, size_t /*argc*/,
                            Value* /*argv*/[]) {
    return detail::notAConstructor(ctx, self);
}

Value* ErrorType::getProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->getProperty(ctx, self, key);
}
Value* ErrorType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    return getObjectType()->setProperty(ctx, self, key, value);
}
Value* ErrorType::hasProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->hasProperty(ctx, self, key);
}
Value* ErrorType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->deleteProperty(ctx, self, key);
}
Value* ErrorType::defineProperty(IContext* ctx, Value* self, Value* key, Value* value,
                                 bool writable, bool enumerable, bool configurable) {
    return getObjectType()->defineProperty(ctx, self, key, value, writable, enumerable,
                                           configurable);
}
Value* ErrorType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                                 Value* setter, bool enumerable, bool configurable) {
    return getObjectType()->defineAccessor(ctx, self, key, getter, setter, enumerable,
                                           configurable);
}
Value* ErrorType::getPrototype(IContext* ctx, Value* self) {
    return getObjectType()->getPrototype(ctx, self);
}
Value* ErrorType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return getObjectType()->setPrototype(ctx, self, proto);
}

// —— ErrorData ——

IData* ErrorData::clone(neo::infra::IAllocator* /*alloc*/) const {
    // Error 是引用语义（ADR-019 §2）：clone 不复制容器，ref++ 后返回 this。
    auto* self = const_cast<ErrorData*>(this);
    self->refInc();
    return self;
}

}  // namespace neo::core
