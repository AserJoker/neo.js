#include "neojs/runtime/suppressed_error.h"
#include "neojs/engine/context.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/error.h"
NEO_JS_CFUNCTION(neo_js_suppressed_error_constructor) {
  neo_js_variable_t message = neo_js_context_create_string(
      ctx, u"An error was suppressed during disposal");
  return neo_js_error_constructor(ctx, self, 1, &message);
}
void neo_initialize_js_suppressed_error(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->suppressed_error_class = neo_js_context_create_cfunction(
      ctx, neo_js_suppressed_error_constructor, u"SuppressedError");
  neo_js_variable_extends(constant->suppressed_error_class, ctx,
                          constant->error_class);
  neo_js_variable_t prototype = neo_js_variable_get_field(
      constant->suppressed_error_class, ctx, constant->key_prototype);
  neo_js_variable_t string =
      neo_js_context_create_string(ctx, u"SuppressedError");
  neo_js_variable_t key = neo_js_context_create_string(ctx, u"name");
  neo_js_variable_def_field(prototype, ctx, key, string, true, false, true);
}