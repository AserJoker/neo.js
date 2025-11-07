#include "engine/array.h"
#include "core/allocator.h"
#include "engine/object.h"
#include "engine/value.h"
static void neo_js_array_dispose(neo_allocator_t allocator,
                                 neo_js_array_t self) {
  neo_deinit_js_array(self, allocator);
}

neo_js_array_t neo_create_js_array(neo_allocator_t allocator,
                                   neo_js_value_t prototype) {
  neo_js_array_t array = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_array_t), neo_js_array_dispose);
  neo_init_js_array(array, allocator, prototype);
  return array;
}

void neo_init_js_array(neo_js_array_t self, neo_allocator_t allocaotr,
                       neo_js_value_t prototype) {
  neo_init_js_object(&self->super, allocaotr, prototype);
  neo_js_value_t value = neo_js_array_to_value(self);
  value->type = NEO_JS_TYPE_ARRAY;
}
void neo_deinit_js_array(neo_js_array_t self, neo_allocator_t allocaotr) {
  neo_deinit_js_object(&self->super, allocaotr);
}
neo_js_value_t neo_js_array_to_value(neo_js_array_t self) {
  return neo_js_object_to_value(&self->super);
}