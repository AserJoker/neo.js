#include "runtime/object.h"
#include "engine/context.h"
#include "engine/variable.h"
#include <stdbool.h>
neo_js_variable_t neo_js_object_prototype = NULL;
neo_js_variable_t neo_js_object_class = NULL;

static neo_js_variable_t neo_js_object_constructor(neo_js_context_t ctx,
                                                   neo_js_variable_t self,
                                                   size_t argc,
                                                   neo_js_variable_t *argv) {
  return self;
}

void neo_initialize_js_object(neo_js_context_t ctx) {
  neo_js_object_class =
      neo_js_context_create_cfunction(ctx, neo_js_object_constructor, "Object");
  neo_js_variable_t key = neo_js_context_create_cstring(ctx, "prototype");
  neo_js_object_prototype =
      neo_js_variable_get_field(neo_js_object_class, ctx, key);
  key = neo_js_context_create_cstring(ctx, "constructor");
  neo_js_variable_def_field(neo_js_object_prototype, ctx, key,
                            neo_js_object_class, true, false, true);
}