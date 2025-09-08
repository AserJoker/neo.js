#include "engine/std/syntax_error.h"
#include "engine/context.h"
#include "engine/std/error.h"
#include "engine/type.h"

neo_js_variable_t neo_js_syntax_error_constructor(neo_js_context_t ctx,
                                                  neo_js_variable_t self,
                                                  uint32_t argc,
                                                  neo_js_variable_t *argv) {
  if (neo_js_context_get_call_type(ctx) == NEO_JS_FUNCTION_CALL) {
    neo_js_variable_t constructor =
        neo_js_context_get_std(ctx).syntax_error_constructor;
    neo_js_variable_t prototype = neo_js_context_get_field(
        ctx, constructor, neo_js_context_create_string(ctx, "prototype"), NULL);
    self = neo_js_context_create_object(ctx, prototype);
    neo_js_context_set_field(ctx, self,
                             neo_js_context_create_string(ctx, "constructor"),
                             constructor, NULL);
  }
  neo_js_variable_t result = neo_js_error_constructor(ctx, self, argc, argv);
  neo_js_error_info_t info = neo_js_context_get_opaque(ctx, result, "info");
  info->type = "SyntaxError";
  return result;
}