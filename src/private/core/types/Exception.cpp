// types/exception.cpp - ExceptionData::enumerateChildren 实现
//
// 向 GC 报告持有的子 value（被抛出的错误对象）。child 永远不为 nullptr
// （不携带错误对象时用 undefined Value 表示），故无条件 visit。
//
// 关联 ADR: 015（data 持子 value 而非子 data，无环）、017（Exception 类型）

#include "core/types/Exception.h"

#include "core/IGCEngine.h"     // IGCVisitor 完整定义
#include "core/Value.h"  // Value 完整定义

namespace neo::core {

void ExceptionData::enumerateChildren(IGCVisitor* visitor) {
    // child 永远不为 nullptr（不携带错误对象时用 undefined Value 表示）。
    visitor->visit(child);
}

// Exception 行为方法：透传 self（ADR-006 异常即值）。
// Exception 不参与计算——外部已拦截，若内部间接调用发生则立刻透传返回。
// 但 Fatal 优先于 Exception（ADR-017 §4）：若 b 是 Fatal，透传 b。
// 分派规则 a->type()->op(ctx,a,b)，故 add/sub/mul/div 的 self 即 a（Exception）。
Value* ExceptionType::add(IContext* /*ctx*/, Value* a, Value* b) {
    if (b->type()->kind() == TypeKind::Fatal) return b;
    return a;
}
Value* ExceptionType::sub(IContext* /*ctx*/, Value* a, Value* b) {
    if (b->type()->kind() == TypeKind::Fatal) return b;
    return a;
}
Value* ExceptionType::mul(IContext* /*ctx*/, Value* a, Value* b) {
    if (b->type()->kind() == TypeKind::Fatal) return b;
    return a;
}
Value* ExceptionType::div(IContext* /*ctx*/, Value* a, Value* b) {
    if (b->type()->kind() == TypeKind::Fatal) return b;
    return a;
}
Value* ExceptionType::toString(IContext* /*ctx*/, Value* self) { return self; }

// call 透传 self（异常即值，ADR-006）——被调用的 Exception 不执行，直接透传。
Value* ExceptionType::call(IContext* /*ctx*/, Value* self, Value* /*thisArg*/,
                           size_t /*argc*/, Value* /*argv*/[]) {
    return self;
}


// 属性操作：透传 self（异常/哨兵不参与属性操作）。
Value* ExceptionType::getProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/) {
    return self;
}
Value* ExceptionType::setProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/, Value* /*value*/) {
    return self;
}
bool ExceptionType::hasProperty(IContext* /*ctx*/, Value* /*self*/, std::string_view /*key*/) {
    return false;
}
Value* ExceptionType::deleteProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/) {
    return self;
}
Value* ExceptionType::defineProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/,
                          Value* /*value*/, bool /*writable*/, bool /*enumerable*/,
                          bool /*configurable*/) {
    return self;
}

}  // namespace neo::core
