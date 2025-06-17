#include "js/std/function.h"
#include "js/context.h"
neo_js_variable_t neo_js_function_constructor(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              uint32_t argc,
                                              neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}