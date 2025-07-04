#include "engine/std/async_generator.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <string.h>
neo_js_variable_t neo_js_async_generator_constructor(neo_js_context_t ctx,
                                                     neo_js_variable_t self,
                                                     uint32_t argc,
                                                     neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_async_generator_next(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              uint32_t argc,
                                              neo_js_variable_t *argv) {

  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_async_generator_return(neo_js_context_t ctx,
                                                neo_js_variable_t self,
                                                uint32_t argc,
                                                neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_async_generator_throw(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv) {
  return neo_js_context_create_undefined(ctx);
}