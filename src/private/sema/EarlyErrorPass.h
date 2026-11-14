// sema/EarlyErrorPass.h - 统一早期错误 walk
//
// StrictnessPass / ControlFlowPass / ModulePass 都是“上下文栈 + 早期错误”
// 的只读 pass，彼此没有数据依赖，只是共享 ResolvePass 产出的标注。
// 本 pass 在一次 walk 中注册三个 visitor，避免三次全树遍历。
//
// 各 visitor 仍可独立调用（runStrictnessPass 等）以便单测隔离。

#ifndef NEO_SEMA_EARLY_ERROR_PASS_H
#define NEO_SEMA_EARLY_ERROR_PASS_H

#include "frontend/Node.h"
#include "sema/AnnotationStore.h"
#include "sema/SemaContext.h"

namespace neo::sema {

void runEarlyErrorPass(frontend::Node& program, SemaContext& context,
                       AnnotationStore& annotations);

}  // namespace neo::sema

#endif  // NEO_SEMA_EARLY_ERROR_PASS_H
