#include "engine/boolean.h"
#include "core/allocator.h"
#include "engine/variable.h"
static void neo_js_boolean_dispose(neo_allocator_t allocator,
                                   neo_js_boolean_t self) {
  neo_deinit_js_boolean(self, allocator);
}
neo_js_boolean_t neo_create_js_boolean(neo_allocator_t allocator, bool value) {
  neo_js_boolean_t boolean = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_boolean_t), neo_js_boolean_dispose);
  neo_init_js_boolean(boolean, allocator, value);
  return boolean;
}
void neo_init_js_boolean(neo_js_boolean_t self, neo_allocator_t allocator,
                         bool value) {
  neo_init_js_value(&self->super, allocator, NEO_JS_TYPE_BOOLEAN);
  self->value = value;
}
void neo_deinit_js_boolean(neo_js_boolean_t self, neo_allocator_t allocator) {
  neo_deinit_js_value(&self->super, allocator);
}
neo_js_value_t neo_js_boolean_to_value(neo_js_boolean_t self) {
  return &self->super;
}