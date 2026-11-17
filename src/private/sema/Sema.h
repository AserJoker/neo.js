// sema/Sema.h - Sema 入口：串联全部 pass 并输出标注
//
// 顺序遵循 ADR-058：
//   ScopePass → ResolvePass → EarlyErrorPass
//   （EarlyErrorPass 一次 walk 内执行 Strictness / ControlFlow / Module）
//
// ScopeGraph 等 scratch 不进入 SemaResult；Compiler 只消费 AnnotationStore
// 中的标注与错误列表。

#ifndef NEO_SEMA_SEMA_H
#define NEO_SEMA_SEMA_H

#include "frontend/Node.h"
#include "sema/AnnotationStore.h"
#include "sema/SemaContext.h"

#include <vector>

namespace neo::sema {

struct SemaResult {
    AnnotationStore annotations;
    std::vector<SemaError> errors;

    bool ok() const { return errors.empty(); }
};

struct SemaOptions {
    // 直接 eval 继承调用方严格模式；脚本/模块默认 false。
    bool inheritedStrict = false;
};

SemaResult runSema(frontend::Node& program, SemaOptions options = {});

}  // namespace neo::sema

#endif  // NEO_SEMA_SEMA_H
