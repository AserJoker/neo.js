#include "engine/basetype/ref.h"
#include "engine/variable.h"
neo_js_type_t neo_get_js_ref_type() {
  static struct _neo_js_type_t type = {
      NEO_JS_TYPE_NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
  };
  return &type;
}

static void neo_js_ref_dispose(neo_allocator_t allocator, neo_js_ref_t self) {
  neo_js_value_dispose(allocator, &self->value);
}

neo_js_ref_t neo_create_js_ref(neo_allocator_t allocator,
                               neo_js_variable_t origin) {
  neo_js_ref_t ref = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_ref_t), neo_js_ref_dispose);
  neo_js_value_init(allocator, &ref->value);
  ref->value.type = neo_get_js_ref_type();
  if (origin) {
    ref->origin = neo_js_variable_get_handle(origin);
  } else {
    ref->origin = NULL;
  }
  return ref;
}

neo_js_ref_t neo_js_value_to_ref(neo_js_value_t value) {
  if (value->type->kind == NEO_JS_TYPE_REF) {
    return (neo_js_ref_t)value;
  }
  return NULL;
}