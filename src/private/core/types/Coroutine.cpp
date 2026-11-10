// Coroutine.cpp - CoroutineType 行为方法 + CoroutineData 实现
//
// 协程没有特化的属性语义：属性/原型操作全部委托 ObjectType
// （Data 继承 ObjectData，属性容器/原型链/访问器/环检测完全复用）。
// 运算走 ToPrimitive（与普通对象一致），call/construct 报错。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、044（可达性标记）、045（协程抽象）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Coroutine.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

Value* CoroutineType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::addValues(ctx, a, b);
}
Value* CoroutineType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '-', a, b);
}
Value* CoroutineType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '*', a, b);
}
Value* CoroutineType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '/', a, b);
}
Value* CoroutineType::toString(IContext* ctx, Value* self) {
    return detail::toStringValue(ctx, self);
}

Value* CoroutineType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                           size_t /*argc*/, Value* /*argv*/[]) {
    return detail::notAFunction(ctx, self);
}

Value* CoroutineType::construct(IContext* ctx, Value* self, size_t /*argc*/,
                                Value* /*argv*/[]) {
    return detail::notAConstructor(ctx, self);
}

Value* CoroutineType::getProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->getProperty(ctx, self, key);
}
Value* CoroutineType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    return getObjectType()->setProperty(ctx, self, key, value);
}
bool CoroutineType::hasProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->hasProperty(ctx, self, key);
}
Value* CoroutineType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->deleteProperty(ctx, self, key);
}
Value* CoroutineType::defineProperty(IContext* ctx, Value* self, Value* key, Value* value,
                                     bool writable, bool enumerable, bool configurable) {
    return getObjectType()->defineProperty(ctx, self, key, value, writable, enumerable,
                                           configurable);
}
Value* CoroutineType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                                     Value* setter, bool enumerable, bool configurable) {
    return getObjectType()->defineAccessor(ctx, self, key, getter, setter, enumerable,
                                           configurable);
}
Value* CoroutineType::getPrototype(IContext* ctx, Value* self) {
    return getObjectType()->getPrototype(ctx, self);
}
Value* CoroutineType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return getObjectType()->setPrototype(ctx, self, proto);
}

// —— CoroutineData ——

// 析构：销毁仍持有的孤立执行作用域子树（与 FunctionData 的闭包作用域同构）。
// 挂起期间 execScope 是孤立子树，不在 global 树上；协程不可达后由 GC 回收本 data，
// 这里负责把它连同捕获值一起销毁（下一轮 GC 回收这些 Value）。
CoroutineData::~CoroutineData() {
    if (execScope != nullptr) {
        execScope->destroySelf();
        execScope = nullptr;
    }
}

void CoroutineData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (yielded != nullptr) visitor->visit(yielded);
    if (sentValue != nullptr) visitor->visit(sentValue);
    if (target != nullptr) visitor->visit(target);
    if (receiver != nullptr) visitor->visit(receiver);
    for (Value* v : initialArgs) {
        if (v != nullptr) visitor->visit(v);
    }
    if (vmState != nullptr) visitor->visit(vmState);
    // 孤立执行作用域不在 global 作用域树上，只能由持有它的协程对象主动标记
    // （ADR-044）。协程不可达时本方法不会被调用，作用域及其捕获值自然不被标记。
    if (execScope != nullptr) visitor->visitScope(execScope);
}

IData* CoroutineData::clone(neo::infra::IAllocator* /*alloc*/) const {
    // 协程是引用语义（ADR-019 §2）：clone 不复制容器，ref++ 后返回 this。
    auto* self = const_cast<CoroutineData*>(this);
    self->refInc();
    return self;
}

CoroutineData* asCoroutine(Value* v) {
    if (v == nullptr) return nullptr;
    TypeKind k = v->type()->kind();
    // AsyncGeneratorData 派生自 CoroutineData，协程 runner 可统一处理。
    if (k != TypeKind::Coroutine && k != TypeKind::AsyncGenerator) return nullptr;
    return static_cast<CoroutineData*>(v->data());
}

}  // namespace neo::core
