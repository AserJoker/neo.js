// sema/Sema.cpp - Sema 入口实现

#include "sema/Sema.h"

#include "sema/ControlFlowPass.h"
#include "sema/ModulePass.h"
#include "sema/ResolvePass.h"
#include "sema/ScopePass.h"
#include "sema/StrictnessPass.h"

#include <utility>

namespace neo::sema {

SemaResult runSema(frontend::Node& program) {
    SemaContext context;
    AnnotationStore annotations;

    runStrictnessPass(program, context, annotations);

    ScopeGraph graph = runScopePass(program, context);
    runResolvePass(program, graph, context, annotations);
    runControlFlowPass(program, context, annotations);
    runModulePass(program, context, annotations);

    SemaResult result;
    result.annotations = std::move(annotations);
    result.errors = context.errors();
    return result;
}

}  // namespace neo::sema
