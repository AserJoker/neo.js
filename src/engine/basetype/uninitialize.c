#include "engine/basetype/uninitialize.h"
#include "core/allocator.h"
#include "engine/type.h"

neo_js_type_t neo_get_js_uninitialize_type() {
  static struct _neo_js_type_t type = {0};
  type.kind = NEO_TYPE_UNINITIALIZE;
  return &type;
}

neo_js_uninitialize_t neo_create_js_uninitialize(neo_allocator_t allocator) {
  neo_js_uninitialize_t uninitialize = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_uninitialize_t), NULL);
  uninitialize->value.ref = 0;
  uninitialize->value.type = neo_get_js_uninitialize_type();
  return uninitialize;
}