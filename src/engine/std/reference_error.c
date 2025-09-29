#include "engine/std/reference_error.h"
#include "engine/context.h"
#include "engine/std/error.h"
#include "engine/type.h"

neo_js_variable_t neo_js_reference_error_constructor(neo_js_context_t ctx,
                                                     neo_js_variable_t self,
                                                     uint32_t argc,
                                                     neo_js_variable_t *argv) {
  if (neo_js_context_get_call_type(ctx) == NEO_JS_FUNCTION_CALL) {
    neo_js_variable_t constructor =
        neo_js_context_get_std(ctx).reference_error_constructor;
    return neo_js_context_construct(ctx, constructor, argc, argv);
  }
  neo_js_variable_t result = neo_js_error_constructor(ctx, self, argc, argv);
  neo_js_error_info_t info = neo_js_context_get_opaque(ctx, result, "info");
  info->type = "ReferenceError";
  return result;
}