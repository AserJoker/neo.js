#include "neojs/engine/number.h"
#include "neojs/core/allocator.h"
static void neo_js_number_dispose(neo_allocator_t allocator,
                                  neo_js_number_t self) {
  neo_deinit_js_number(self, allocator);
}

void neo_init_js_number(neo_js_number_t self, neo_allocator_t allocator,
                        double value) {
  neo_init_js_value(&self->super, allocator, NEO_JS_TYPE_NUMBER);
  self->value = value;
}
void neo_deinit_js_number(neo_js_number_t self, neo_allocator_t allocator) {
  neo_deinit_js_value(&self->super, allocator);
}
neo_js_number_t neo_create_js_number(neo_allocator_t allocator, double value) {
  neo_js_number_t number = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_number_t), neo_js_number_dispose);
  neo_init_js_number(number, allocator, value);
  return number;
}

neo_js_value_t neo_js_number_to_value(neo_js_number_t self) {
  return &self->super;
}