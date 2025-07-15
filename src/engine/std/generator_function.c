#include "engine/std/generator_function.h"
#include "engine/context.h"
neo_js_variable_t
neo_js_generator_function_constructor(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_generator_function_to_string(neo_js_context_t ctx,
                                                      neo_js_variable_t self,
                                                      uint32_t argc,
                                                      neo_js_variable_t *argv) {

  neo_js_function_t generator = neo_js_variable_to_function(self);
  if (neo_js_variable_get_type(self)->kind != NEO_TYPE_FUNCTION || !generator ||
      !generator->is_generator) {
    return neo_js_context_create_error(
        ctx, NEO_ERROR_TYPE,
        L" GeneratorFunction.prototype.toString requires that 'this' be a "
        L"GeneratorFunction");
  }
  neo_js_function_t func = neo_js_variable_to_function(self);
  return neo_js_context_create_string(ctx, func->source);
}