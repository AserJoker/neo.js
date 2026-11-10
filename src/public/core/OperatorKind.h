// OperatorKind.h - Engine 运算符原语的种类与转换 hint
//
// VM 只负责取操作数与跳转；运算符的 JS 语义（ToPrimitive/ToNumber/ToBoolean、
// 相等/关系/位运算规则）由 Engine 统一实现，避免 VM 重复编排转换链。
//
// 关联 ADR: 016（Type 体系）、006（异常即值）

#ifndef NEO_CORE_OPERATOR_KIND_H
#define NEO_CORE_OPERATOR_KIND_H

#include <cstdint>

namespace neo::core {

// ToPrimitive 的 preferredType（ES2024 §7.1.1）。
// Default 与 Number 在 OrdinaryToPrimitive 中等价；仅 @@toPrimitive 的 hint 区分。
enum class ToPrimitiveHint : uint8_t {
    Default,
    Number,
    String,
};

// 二元运算符（不含赋值/逻辑短路——短路由 VM 用跳转实现）。
enum class BinaryOperator : uint8_t {
    Add,
    Sub,
    Mul,
    Div,
    Mod,  // %
    Exp,  // **

    StrictEqual,    // ===
    AbstractEqual,  // ==

    LessThan,           // <
    GreaterThan,        // >
    LessThanOrEqual,    // <=
    GreaterThanOrEqual, // >=

    BitAnd,            // &
    BitOr,             // |
    BitXor,            // ^
    ShiftLeft,         // <<
    ShiftRight,        // >>
    UnsignedShiftRight,// >>>

    In,          // key in obj
    InstanceOf,  // value instanceof ctor
};

// 一元运算符。
enum class UnaryOperator : uint8_t {
    Plus,        // +
    Minus,       // -
    LogicalNot,  // !
    BitNot,      // ~
    TypeOf,      // typeof
    Void,        // void
};

}  // namespace neo::core

#endif  // NEO_CORE_OPERATOR_KIND_H
