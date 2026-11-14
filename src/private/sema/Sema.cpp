// sema/Sema.cpp - Sema 入口实现

#include "sema/Sema.h"

#include "sema/EarlyErrorPass.h"
#include "sema/ResolvePass.h"
#include "sema/ScopePass.h"

#include <utility>

namespace neo::sema {

SemaResult runSema(frontend::Node& program) {
    SemaContext context;
    AnnotationStore annotations;

    ScopeGraph graph = runScopePass(program, context);
    runResolvePass(program, graph, context, annotations);
    runEarlyErrorPass(program, context, annotations);

    SemaResult result;
    result.annotations = std::move(annotations);
    result.errors = context.errors();
    return result;
}

}  // namespace neo::sema
