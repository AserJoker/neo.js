#include "runtime/async_generator_function.h"
#include "engine/context.h"
NEO_JS_CFUNCTION(neo_js_async_generator_function_constructor) { return self; }
void neo_initialize_js_async_generator_function(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->async_generator_function_class = neo_js_context_create_cfunction(
      ctx, neo_js_async_generator_function_constructor,
      "AsyncGeneratorFunction");
  constant->async_generator_function_prototype = neo_js_variable_get_field(
      constant->async_generator_function_class, ctx, constant->key_prototype);
  neo_js_variable_extends(constant->async_generator_function_class, ctx,
                          constant->function_class);
}