// Fatal.cpp - FatalType 行为方法实现（透传）
//
// fatal 类型（ADR-017 §4）：不可捕获致命错误，所有行为方法透传 self。
// Fatal 不参与计算——外部已拦截，若内部间接调用发生则立刻透传返回。
// 分派规则 a->type()->op(ctx,a,b)，故 add/sub/mul/div 的 self 即 a，透传 a。
//
// 关联 ADR: 016（Type 体系修订）、017（Fatal 透传契约 §4）

#include "core/types/Fatal.h"

namespace neo::core {

Value* FatalType::add(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* FatalType::sub(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* FatalType::mul(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* FatalType::div(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* FatalType::toString(IContext* /*ctx*/, Value* self) { return self; }

// call 透传 self（Fatal 不可捕获，ADR-017 §4）。
Value* FatalType::call(IContext* /*ctx*/, Value* self, Value* /*thisArg*/,
                       size_t /*argc*/, Value* /*argv*/[]) {
    return self;
}


// 属性操作：透传 self（异常/哨兵不参与属性操作）。
Value* FatalType::getProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/) {
    return self;
}
Value* FatalType::setProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/, Value* /*value*/) {
    return self;
}
bool FatalType::hasProperty(IContext* /*ctx*/, Value* /*self*/, std::string_view /*key*/) {
    return false;
}
Value* FatalType::deleteProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/) {
    return self;
}
Value* FatalType::defineProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/,
                          Value* /*value*/, bool /*writable*/, bool /*enumerable*/,
                          bool /*configurable*/) {
    return self;
}


// 原型操作：透传 self（异常/哨兵不参与属性操作）。
Value* FatalType::getPrototype(IContext* /*ctx*/, Value* self) {
    return self;
}
Value* FatalType::setPrototype(IContext* /*ctx*/, Value* self, Value* /*proto*/) {
    return self;
}

}  // namespace neo::core
