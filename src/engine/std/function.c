#include "engine/std/function.h"
#include "engine/context.h"
neo_engine_variable_t
neo_engine_function_constructor(neo_engine_context_t ctx,
                                neo_engine_variable_t self, uint32_t argc,
                                neo_engine_variable_t *argv) {
  return neo_engine_context_create_undefined(ctx);
}