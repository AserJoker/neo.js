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

}  // namespace neo::core
