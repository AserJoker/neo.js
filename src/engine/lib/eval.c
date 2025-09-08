#include "engine/lib/eval.h"
#include "compiler/parser.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "core/allocator.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include "runtime/vm.h"
#include <wchar.h>
NEO_JS_CFUNCTION(neo_js_eval) {
  neo_js_variable_t arg = NULL;
  if (argc) {
    arg = argv[0];
  } else {
    return neo_js_context_create_undefined(ctx);
  }
  if (neo_js_variable_get_type(arg)->kind != NEO_JS_TYPE_STRING) {
    return arg;
  }
  const char *src = neo_js_context_to_cstring(ctx, arg);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_ast_node_t node =
      TRY(neo_ast_parse_code(allocator, "<anonymouse_script>", src)) {
    return neo_js_context_create_compile_error(ctx);
  };
  neo_js_context_defer_free(ctx, node);
  neo_program_t program =
      TRY(neo_ast_write_node(allocator, "<anonymouse_script>", node)) {
    return neo_js_context_create_compile_error(ctx);
  };
  neo_js_context_defer_free(ctx, program);
  neo_js_vm_t vm = neo_create_js_vm(ctx, neo_js_context_create_undefined(ctx),
                                    neo_js_context_create_undefined(ctx), 0,
                                    neo_js_context_get_scope(ctx));
  neo_js_context_defer_free(ctx, vm);
  return neo_js_vm_exec(vm, program);
}