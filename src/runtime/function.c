#include "runtime/function.h"
#include "engine/context.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include <stdbool.h>

NEO_JS_CFUNCTION(neo_js_function_constructor) { return self; }
void neo_initialize_js_function(neo_js_context_t ctx) {
  neo_js_scope_t root_scope = neo_js_context_get_root_scope(ctx);
  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);
  constant->function_class = neo_js_context_create_cfunction(
      ctx, neo_js_function_constructor, "Object");
  constant->function_prototype = neo_js_variable_get_field(
      constant->function_class, ctx, constant->key_prototype);
  neo_js_variable_def_field(constant->function_prototype, ctx,
                            constant->key_constructor, constant->function_class,
                            true, false, true);
  // fix object class prototype
  {
    neo_js_variable_set_prototype_of(constant->object_class, ctx,
                                     constant->function_prototype);
  }
  neo_js_scope_set_variable(root_scope, constant->function_prototype, NULL);
  neo_js_scope_set_variable(root_scope, constant->function_class, NULL);
}