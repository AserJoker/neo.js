#include "engine/std/async_function.h"
#include "engine/context.h"
neo_js_variable_t neo_js_async_function_constructor(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    uint32_t argc,
                                                    neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_async_function_to_string(neo_js_context_t ctx,
                                                  neo_js_variable_t self,
                                                  uint32_t argc,
                                                  neo_js_variable_t *argv) {

  neo_js_function_t generator = neo_js_variable_to_function(self);
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_FUNCTION ||
      !generator || !generator->is_generator) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        L" AsyncFunction.prototype.toString requires that 'this' be a "
        L"AsyncFunction");
  }
  neo_js_function_t func = neo_js_variable_to_function(self);
  return neo_js_context_create_string(ctx, func->source);
}

void neo_js_context_init_std_async_function(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).async_function_constructor,
      neo_js_context_create_string(ctx, L"prototype"), NULL);
  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, L"toString"),
      neo_js_context_create_cfunction(ctx, L"toString",
                                      neo_js_async_function_to_string),
      true, false, true);
}