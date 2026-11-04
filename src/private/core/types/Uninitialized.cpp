// Uninitialized.cpp - UninitializedType 行为方法实现（全透传）
//
// uninitialized 是 TDZ 哨兵：let/const 初始化前的变量槽位。不参与任何运算——
// 未来 VM 的 load 指令在读取变量时检测此类型并抛 ReferenceError。
// 所有行为方法返回 self（透传），仅为满足 IType 纯虚接口。
//
// 关联 ADR: 016（Type 体系修订）、030 阶段 5

#include "core/types/Uninitialized.h"

namespace neo::core {

Value* UninitializedType::add(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* UninitializedType::sub(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* UninitializedType::mul(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* UninitializedType::div(IContext* /*ctx*/, Value* a, Value* /*b*/) { return a; }
Value* UninitializedType::toString(IContext* /*ctx*/, Value* self) { return self; }

// call 透传 self（TDZ 哨兵不参与调用，进入前应已失败）。
Value* UninitializedType::call(IContext* /*ctx*/, Value* self, Value* /*thisArg*/,
                               size_t /*argc*/, Value* /*argv*/[]) {
    return self;
}


// 属性操作：透传 self（异常/哨兵不参与属性操作）。
Value* UninitializedType::getProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/) {
    return self;
}
Value* UninitializedType::setProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/, Value* /*value*/) {
    return self;
}
bool UninitializedType::hasProperty(IContext* /*ctx*/, Value* /*self*/, std::string_view /*key*/) {
    return false;
}
Value* UninitializedType::deleteProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/) {
    return self;
}
Value* UninitializedType::defineProperty(IContext* /*ctx*/, Value* self, std::string_view /*key*/,
                          Value* /*value*/, bool /*writable*/, bool /*enumerable*/,
                          bool /*configurable*/) {
    return self;
}

}  // namespace neo::core
