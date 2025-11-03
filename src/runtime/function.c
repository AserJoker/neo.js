#include "runtime/function.h"
#include "engine/context.h"
#include "engine/object.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/object.h"
#include <stdbool.h>

neo_js_variable_t neo_js_function_prototype = NULL;
neo_js_variable_t neo_js_function_class = NULL;
static neo_js_variable_t neo_js_function_constructor(neo_js_context_t ctx,
                                                     neo_js_variable_t self,
                                                     size_t argc,
                                                     neo_js_variable_t *argv) {
  return self;
}
void neo_initialize_js_function(neo_js_context_t ctx) {
  neo_js_function_class = neo_js_context_create_cfunction(
      ctx, neo_js_function_constructor, "Object");
  neo_js_variable_t key = neo_js_context_create_cstring(ctx, "prototype");
  neo_js_function_prototype =
      neo_js_variable_get_field(neo_js_function_class, ctx, key);
  // fix object class prototype
  {
    neo_js_object_t object = (neo_js_object_t)neo_js_object_class->value;
    neo_js_context_create_variable(ctx, object->prototype);
    neo_js_value_remove_parent(object->prototype, neo_js_object_class->value);
    object->prototype = neo_js_function_prototype->value;
    neo_js_value_add_parent(neo_js_function_prototype->value,
                            neo_js_object_class->value);
  }
}