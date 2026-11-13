// sema/SemaContext.h - 语义分析共享状态
//
// 与 Walker 分离：Walker 只负责遍历与分派，作用域栈、严格性栈、错误列表
// 等语义状态由 SemaContext 持有，便于多遍 walk 共享与单测替换。
//
// 关联 ADR: 058（Sema Walker 与标注架构）、057（严格模式在 Sema）

#ifndef NEO_SEMA_SEMA_CONTEXT_H
#define NEO_SEMA_SEMA_CONTEXT_H

#include "frontend/Node.h"

#include <string>
#include <utility>
#include <vector>

namespace neo::sema {

struct SemaError {
    frontend::SourceLocation location;
    std::string message;
};

class SemaContext {
public:
    void error(frontend::SourceLocation location, std::string message) {
        errors_.push_back(SemaError{location, std::move(message)});
    }

    const std::vector<SemaError>& errors() const { return errors_; }
    bool ok() const { return errors_.empty(); }

private:
    std::vector<SemaError> errors_;
};

}  // namespace neo::sema

#endif  // NEO_SEMA_SEMA_CONTEXT_H
