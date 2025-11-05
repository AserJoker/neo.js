#include "runtime/object.h"
#include "engine/context.h"
#include "engine/variable.h"
#include <stdbool.h>

NEO_JS_CFUNCTION(neo_js_object_constructor) { return self; }

void neo_initialize_js_object(neo_js_context_t ctx) {
  neo_js_constant_t *constant = neo_js_context_get_constant(ctx);
  constant->object_class =
      neo_js_context_create_cfunction(ctx, neo_js_object_constructor, "Object");
  constant->object_prototype = neo_js_variable_get_field(
      constant->object_class, ctx, constant->key_prototype);
  neo_js_variable_def_field(constant->object_prototype, ctx,
                            constant->key_constructor, constant->object_class,
                            true, false, true);
}