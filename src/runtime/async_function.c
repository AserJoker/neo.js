#include "neo.js/runtime/async_function.h"
#include "neo.js/engine/context.h"
#include "neo.js/engine/variable.h"
#include "neo.js/runtime/constant.h"

NEO_JS_CFUNCTION(neo_js_async_function_constructor) { return self; }
void neo_initialize_js_async_function(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->async_function_class = neo_js_context_create_cfunction(
      ctx, neo_js_async_function_constructor, "AsyncFunction");
  constant->async_function_prototype = neo_js_variable_get_field(
      constant->async_function_class, ctx, constant->key_prototype);
  neo_js_variable_extends(constant->async_function_class, ctx,
                          constant->function_class);
}