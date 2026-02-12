#include "neojs/runtime/generator_function.h"
#include "neojs/engine/context.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"

NEO_JS_CFUNCTION(neo_js_generator_function_constructor) { return self; }
void neo_initialize_js_generator_function(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->generator_function_class = neo_js_context_create_cfunction(
      ctx, neo_js_generator_function_constructor, u"GeneratorFunction");
  constant->generator_function_prototype = neo_js_variable_get_field(
      constant->generator_function_class, ctx, constant->key_prototype);
  neo_js_variable_extends(constant->generator_function_class, ctx,
                          constant->function_class);
}