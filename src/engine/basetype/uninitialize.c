#include "engine/basetype/uninitialize.h"
#include "core/allocator.h"
#include "engine/type.h"

neo_js_type_t neo_get_js_uninitialize_type() {
  static struct _neo_js_type_t type = {0};
  type.kind = NEO_JS_TYPE_UNINITIALIZE;
  return &type;
}
static void neo_js_uninitialize_dispose(neo_allocator_t allocator,
                                        neo_js_uninitialize_t self) {
  neo_js_value_dispose(allocator, &self->value);
}

neo_js_uninitialize_t neo_create_js_uninitialize(neo_allocator_t allocator) {
  neo_js_uninitialize_t uninitialize =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_uninitialize_t),
                          neo_js_uninitialize_dispose);
  neo_js_value_init(allocator, &uninitialize->value);
  uninitialize->value.type = neo_get_js_uninitialize_type();
  return uninitialize;
}