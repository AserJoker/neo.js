// OperatorPrimitives.cpp - Engine 转换/运算符原语（VM 可调用入口）
//
// 这些方法把 TypeBehavior / OperatorBehavior 的语义收敛为 IContext 原语：
// VM 只按字节码取操作数，转换顺序、错误通道、位运算规则全部由 Engine 落实。
//
// 关联 ADR: 016（Type 体系）、006（异常即值）

#include "core/Context.h"
#include "core/OperatorBehavior.h"

namespace neo::core {

bool Context::toBoolean(Value* v) {
    return detail::toBoolean(v);
}

Value* Context::toNumber(Value* v) {
    return detail::toNumberValue(this, v);
}

Value* Context::toPrimitive(Value* v, ToPrimitiveHint hint) {
    detail::PreferredType preferred = detail::PreferredType::Default;
    switch (hint) {
        case ToPrimitiveHint::Default:
            preferred = detail::PreferredType::Default;
            break;
        case ToPrimitiveHint::Number:
            preferred = detail::PreferredType::Number;
            break;
        case ToPrimitiveHint::String:
            preferred = detail::PreferredType::String;
            break;
    }
    return detail::toPrimitive(this, v, preferred);
}

Value* Context::toPropertyKey(Value* v) {
    return detail::toPropertyKeyValue(this, v);
}

Value* Context::binaryOp(BinaryOperator op, Value* a, Value* b) {
    return detail::binaryOpValues(this, op, a, b);
}

Value* Context::unaryOp(UnaryOperator op, Value* v) {
    return detail::unaryOpValues(this, op, v);
}

}  // namespace neo::core
