// sema/Sema.h - Sema 入口：串联全部 pass 并输出标注
//
// 顺序遵循 ADR-058：
//   StrictnessPass → ScopePass → ResolvePass → ControlFlowPass →
//   ModulePass
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

SemaResult runSema(frontend::Node& program);

}  // namespace neo::sema

#endif  // NEO_SEMA_SEMA_H
