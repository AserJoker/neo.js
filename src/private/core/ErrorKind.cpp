// ErrorKind.cpp - 规范错误类型名映射
//
// 关联 ADR: 038（Error 体系）、039（错误类型整改）

#include "core/ErrorKind.h"

namespace neo::core {

const char* errorKindName(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::Error:          return "Error";
        case ErrorKind::TypeError:      return "TypeError";
        case ErrorKind::RangeError:     return "RangeError";
        case ErrorKind::ReferenceError: return "ReferenceError";
        case ErrorKind::SyntaxError:    return "SyntaxError";
        case ErrorKind::URIError:       return "URIError";
    }
    return "Error";
}

}  // namespace neo::core
