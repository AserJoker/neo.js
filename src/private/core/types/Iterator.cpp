// Iterator.cpp - IteratorObjectType 行为方法 + 两个迭代器 Data 实现
//
// 迭代器类型没有特化的属性语义：属性/原型操作全部委托 ObjectType
// （Data 继承 ObjectData，属性容器/原型链/访问器/环检测完全复用）。
// 运算走 ToPrimitive（与普通对象一致），call/construct 报错。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、019（赋值语义 §2）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/Iterator.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

// —— 内部槽访问 ——

Value* arrayIteratorIteratedObject(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::ArrayIterator) return nullptr;
    return static_cast<ArrayIteratorData*>(v->data())->iteratedObject;
}

IteratorWrapperData* asIteratorWrapper(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::IteratorWrapper) return nullptr;
    return static_cast<IteratorWrapperData*>(v->data());
}

// —— IteratorObjectType ——

Value* IteratorObjectType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::addValues(ctx, a, b);
}
Value* IteratorObjectType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '-', a, b);
}
Value* IteratorObjectType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '*', a, b);
}
Value* IteratorObjectType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '/', a, b);
}
Value* IteratorObjectType::toString(IContext* ctx, Value* self) {
    return detail::toStringValue(ctx, self);
}

Value* IteratorObjectType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                                size_t /*argc*/, Value* /*argv*/[]) {
    return detail::notAFunction(ctx, self);
}

Value* IteratorObjectType::construct(IContext* ctx, Value* self, size_t /*argc*/,
                                     Value* /*argv*/[]) {
    return detail::notAConstructor(ctx, self);
}

Value* IteratorObjectType::getProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->getProperty(ctx, self, key);
}
Value* IteratorObjectType::setProperty(IContext* ctx, Value* self, Value* key,
                                       Value* value) {
    return getObjectType()->setProperty(ctx, self, key, value);
}
Value* IteratorObjectType::hasProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->hasProperty(ctx, self, key);
}
Value* IteratorObjectType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->deleteProperty(ctx, self, key);
}
Value* IteratorObjectType::defineProperty(IContext* ctx, Value* self, Value* key,
                                          Value* value, bool writable, bool enumerable,
                                          bool configurable) {
    return getObjectType()->defineProperty(ctx, self, key, value, writable, enumerable,
                                           configurable);
}
Value* IteratorObjectType::defineAccessor(IContext* ctx, Value* self, Value* key,
                                          Value* getter, Value* setter, bool enumerable,
                                          bool configurable) {
    return getObjectType()->defineAccessor(ctx, self, key, getter, setter, enumerable,
                                           configurable);
}
Value* IteratorObjectType::getPrototype(IContext* ctx, Value* self) {
    return getObjectType()->getPrototype(ctx, self);
}
Value* IteratorObjectType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return getObjectType()->setPrototype(ctx, self, proto);
}

// —— ArrayIteratorData ——

void ArrayIteratorData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (iteratedObject != nullptr) visitor->visit(iteratedObject);
}

IData* ArrayIteratorData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<ArrayIteratorData*>(this);
    self->refInc();
    return self;
}

// —— IteratorWrapperData ——

void IteratorWrapperData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (iterated != nullptr) visitor->visit(iterated);
    if (nextMethod != nullptr) visitor->visit(nextMethod);
}

IData* IteratorWrapperData::clone(neo::infra::IAllocator* /*alloc*/) const {
    auto* self = const_cast<IteratorWrapperData*>(this);
    self->refInc();
    return self;
}

}  // namespace neo::core
