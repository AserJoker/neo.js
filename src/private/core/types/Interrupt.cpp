// Interrupt.cpp - InterruptType 行为方法（透传）+ InterruptData GC/clone
//
// Interrupt 是控制信号值（ADR-046），不参与运算/属性/调用：
// 所有行为方法透传 self。分派规则 a->type()->op(ctx,a,b)，故 self 即 a。
//
// 关联 ADR: 016（类型体系）、017（Interrupt 规划）、046（Interrupt 控制信号）

#include "core/types/Interrupt.h"

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Value.h"

namespace neo::core {

Value* InterruptType::add(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* InterruptType::sub(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* InterruptType::mul(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* InterruptType::div(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* InterruptType::toString(IContext* /*ctx*/, Value* self) { return self; }

Value* InterruptType::call(IContext* /*ctx*/, Value* self, Value* /*thisArg*/,
                           size_t /*argc*/, Value* /*argv*/[]) {
    return self;
}

Value* InterruptType::getProperty(IContext* /*ctx*/, Value* self, Value* /*key*/) {
    return self;
}
Value* InterruptType::setProperty(IContext* /*ctx*/, Value* self, Value* /*key*/,
                                  Value* /*value*/) {
    return self;
}
Value* InterruptType::hasProperty(IContext* ctx, Value* /*self*/, Value* /*key*/) {
    return ctx->createBoolean(false);
}
Value* InterruptType::deleteProperty(IContext* /*ctx*/, Value* self, Value* /*key*/) {
    return self;
}
Value* InterruptType::defineProperty(IContext* /*ctx*/, Value* self, Value* /*key*/,
                                     Value* /*value*/, bool /*writable*/,
                                     bool /*enumerable*/, bool /*configurable*/) {
    return self;
}
Value* InterruptType::defineAccessor(IContext* /*ctx*/, Value* self, Value* /*key*/,
                                     Value* /*getter*/, Value* /*setter*/,
                                     bool /*enumerable*/, bool /*configurable*/) {
    return self;
}
Value* InterruptType::getPrototype(IContext* /*ctx*/, Value* self) { return self; }
Value* InterruptType::setPrototype(IContext* /*ctx*/, Value* self, Value* /*proto*/) {
    return self;
}
Value* InterruptType::construct(IContext* /*ctx*/, Value* self, size_t /*argc*/,
                                Value* /*argv*/[]) {
    return self;
}

void InterruptData::enumerateChildren(IGCVisitor* visitor) {
    // value 永远不为 nullptr（无载荷时用 undefined Value 表示）
    if (value != nullptr) visitor->visit(value);
}

IData* InterruptData::clone(neo::infra::IAllocator* /*alloc*/) const {
    // 引用语义（ADR-019 §2）：clone 不复制，ref++ 后返回 this
    auto* self = const_cast<InterruptData*>(this);
    self->refInc();
    return self;
}

InterruptData* asInterrupt(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::Interrupt) return nullptr;
    return static_cast<InterruptData*>(v->data());
}

}  // namespace neo::core
