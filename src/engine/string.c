#include "engine/string.h"
#include "core/allocator.h"
#include "core/string.h"
#include "engine/variable.h"
#include <stdint.h>
#include <string.h>
static void neo_js_string_dispose(neo_allocator_t allocator,
                                  neo_js_string_t self) {
  neo_deinit_js_string(self, allocator);
}

neo_js_string_t neo_create_js_string(neo_allocator_t allocator,
                                     uint16_t *value) {
  neo_js_string_t string = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_string_t), neo_js_string_dispose);
  neo_init_js_string(string, allocator, value);
  return string;
}
void neo_init_js_string(neo_js_string_t self, neo_allocator_t allocator,
                        uint16_t *value) {
  neo_init_js_value(&self->super, allocator, NEO_JS_TYPE_STRING);
  size_t len = neo_string16_length(value);
  self->value =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (len + 1), NULL);
  memcpy(self->value, value, sizeof(uint16_t) * len);
  self->value[len] = 0;
}
void neo_deinit_js_string(neo_js_string_t self, neo_allocator_t allocator) {
  neo_allocator_free(allocator, self->value);
  neo_deinit_js_value(&self->super, allocator);
}
neo_js_value_t neo_js_string_to_value(neo_js_string_t self) {
  return &self->super;
}