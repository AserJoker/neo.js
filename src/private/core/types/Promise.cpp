// Promise.cpp - PromiseType 行为方法 + PromiseData 实现
//
// Promise 没有特化的属性语义：属性/原型操作全部委托 ObjectType
// （Data 继承 ObjectData，属性容器/原型链/访问器/环检测完全复用）。
// 运算走 ToPrimitive（与普通对象一致），call/construct 报错。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、019（赋值语义 §2）、043（Promise）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/Promise.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

Value* PromiseType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::addValues(ctx, a, b);
}
Value* PromiseType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '-', a, b);
}
Value* PromiseType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '*', a, b);
}
Value* PromiseType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '/', a, b);
}
Value* PromiseType::toString(IContext* ctx, Value* self) {
    return detail::toStringValue(ctx, self);
}

Value* PromiseType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                         size_t /*argc*/, Value* /*argv*/[]) {
    return detail::notAFunction(ctx, self);
}

Value* PromiseType::construct(IContext* ctx, Value* self, size_t /*argc*/,
                              Value* /*argv*/[]) {
    return detail::notAConstructor(ctx, self);
}

Value* PromiseType::getProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->getProperty(ctx, self, key);
}
Value* PromiseType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    return getObjectType()->setProperty(ctx, self, key, value);
}
Value* PromiseType::hasProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->hasProperty(ctx, self, key);
}
Value* PromiseType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->deleteProperty(ctx, self, key);
}
Value* PromiseType::defineProperty(IContext* ctx, Value* self, Value* key, Value* value,
                                   bool writable, bool enumerable, bool configurable) {
    return getObjectType()->defineProperty(ctx, self, key, value, writable, enumerable,
                                           configurable);
}
Value* PromiseType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                                   Value* setter, bool enumerable, bool configurable) {
    return getObjectType()->defineAccessor(ctx, self, key, getter, setter, enumerable,
                                           configurable);
}
Value* PromiseType::getPrototype(IContext* ctx, Value* self) {
    return getObjectType()->getPrototype(ctx, self);
}
Value* PromiseType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return getObjectType()->setPrototype(ctx, self, proto);
}

// —— PromiseData ——

void PromiseData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (result != nullptr) visitor->visit(result);
    if (resolveFn != nullptr) visitor->visit(resolveFn);
    if (rejectFn != nullptr) visitor->visit(rejectFn);
    for (const PromiseReaction& r : reactions) {
        if (r.capability.promise != nullptr) visitor->visit(r.capability.promise);
        if (r.capability.resolve != nullptr) visitor->visit(r.capability.resolve);
        if (r.capability.reject != nullptr) visitor->visit(r.capability.reject);
        if (r.handler != nullptr) visitor->visit(r.handler);
    }
}

IData* PromiseData::clone(neo::infra::IAllocator* /*alloc*/) const {
    // Promise 是引用语义（ADR-019 §2）：clone 不复制容器，ref++ 后返回 this。
    auto* self = const_cast<PromiseData*>(this);
    self->refInc();
    return self;
}

PromiseData* asPromise(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::Promise) return nullptr;
    return static_cast<PromiseData*>(v->data());
}

}  // namespace neo::core
