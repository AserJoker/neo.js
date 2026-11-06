// ErrorKind.h - 规范错误类型枚举（ES2024 §20.5）
//
// 引擎内部产生错误时用它指明规范要求的错误类型，由 Context::createErrorException
// 创建对应的 Error 对象（再按 ADR-006 包进 Exception 通道）。
//
// 范围与 ADR-038 一致：Error 基类 + 5 个一般性错误子类。
// EvalError 已不在 ES2024 规范中，不提供；AggregateError / SuppressedError 不在本阶段范围。
//
// 关联 ADR: 017（Exception 类型）、038（Error 体系）

#ifndef NEO_CORE_ERROR_KIND_H
#define NEO_CORE_ERROR_KIND_H

#include <cstdint>

namespace neo::core {

// 规范错误类型（与 global 上的构造函数名一一对应）
enum class ErrorKind : uint8_t {
    Error,
    TypeError,
    RangeError,
    ReferenceError,
    SyntaxError,
    URIError,
};

// 错误类型名（"Error" / "TypeError" / ...），同时是 global 上的构造函数名。
// 返回值指向静态存储期字符串。
const char* errorKindName(ErrorKind kind);

}  // namespace neo::core

#endif  // NEO_CORE_ERROR_KIND_H
