// sema/EarlyErrorPass.cpp - 统一早期错误 walk 实现

#include "sema/EarlyErrorPass.h"

#include "sema/ControlFlowPass.h"
#include "sema/ModulePass.h"
#include "sema/StrictnessPass.h"
#include "sema/Walker.h"

namespace neo::sema {

void runEarlyErrorPass(frontend::Node& program, SemaContext& context,
                       AnnotationStore& annotations) {
    Walker walker(annotations);
    walker.addAll(makeStrictnessVisitor());
    walker.addAll(makeControlFlowVisitor(annotations));
    walker.addAll(makeModuleVisitor(annotations));
    walker.walk(program, context);
}

}  // namespace neo::sema
