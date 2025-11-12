#include "engine/array.h"
#include "core/allocator.h"
#include "core/hash_map.h"
#include "engine/number.h"
#include "engine/object.h"
#include "engine/string.h"
#include "engine/value.h"
#include <stdint.h>
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
  neo_js_object_t object = &self->super;
  neo_js_value_t value = neo_js_array_to_value(self);
  value->type = NEO_JS_TYPE_ARRAY;
  neo_js_number_t length = neo_create_js_number(allocaotr, 0);
  const char *s = "length";
  uint16_t str[8];
  do {
    uint16_t *dst = str;
    while (*s) {
      *dst++ = *s++;
    }
    *dst = 0;
  } while (0);
  neo_js_string_t key = neo_create_js_string(allocaotr, str);
  neo_js_object_property_t prop = neo_create_js_object_property(allocaotr);
  prop->value = neo_js_number_to_value(length);
  prop->configurable = false;
  prop->enumable = false;
  prop->writable = true;
  prop->get = NULL;
  prop->set = NULL;
  neo_hash_map_set(object->properties, key, prop);
  neo_js_value_add_parent(&key->super, value);
  neo_js_value_add_parent(&length->super, value);
}
void neo_deinit_js_array(neo_js_array_t self, neo_allocator_t allocaotr) {
  neo_deinit_js_object(&self->super, allocaotr);
}
neo_js_value_t neo_js_array_to_value(neo_js_array_t self) {
  return neo_js_object_to_value(&self->super);
}