#include "engine/object.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "engine/string.h"
#include "engine/variable.h"
#include <stdbool.h>
#include <stddef.h>
static void neo_js_object_dispose(neo_allocator_t allocator,
                                  neo_js_object_t self) {
  neo_deinit_js_object(self, allocator);
}
neo_js_object_t neo_create_js_object(neo_allocator_t allocator,
                                     neo_js_variable_t prototype) {
  neo_js_object_t object = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_object_t), neo_js_object_dispose);
  neo_init_js_object(object, allocator, prototype);
  return object;
}
void neo_init_js_object(neo_js_object_t self, neo_allocator_t allocator,
                        neo_js_variable_t prototype) {
  neo_init_js_value(&self->super, allocator, NEO_JS_TYPE_OBJECT);
  self->prototype = prototype;
  neo_hash_map_initialize_t initialize = {
      .auto_free_key = false,
      .auto_free_value = true,
      .compare = (neo_compare_fn_t)neo_js_object_key_compare,
      .hash = (neo_hash_fn_t)neo_js_object_key_hash,
  };
  self->properties = neo_create_hash_map(allocator, &initialize);
  self->frozen = false;
  self->extensible = true;
  self->sealed = false;
}
void neo_deinit_js_object(neo_js_object_t self, neo_allocator_t allocator) {
  neo_deinit_js_value(&self->super, allocator);
  neo_allocator_free(allocator, self->properties);
}
neo_js_value_t neo_js_object_to_value(neo_js_object_t self) {
  return &self->super;
}
int32_t neo_js_object_key_compare(const neo_js_variable_t self,
                                  const neo_js_variable_t another,
                                  neo_js_context_t ctx) {
  if (self->value->type != another->value->type) {
    return self->value->type - another->value->type;
  }
  if (self->value->type == NEO_JS_TYPE_SYMBOL) {
    if (self->value == another->value) {
      return 0;
    }
    return (int32_t)(self->value - another->value);
  }
  neo_js_string_t str1 = (neo_js_string_t)self->value;
  neo_js_string_t str2 = (neo_js_string_t)another->value;
  for (;;) {
    if (*str1->value != *str2->value) {
      return *str1->value - *str2->value;
    }
    if (!*str1->value) {
      return 0;
    }
    str1++;
    str2++;
  }
  return 0;
}
uint32_t neo_js_object_key_hash(const neo_js_variable_t self, uint32_t max,
                                neo_js_context_t ctx) {
  if (self->value->type == NEO_JS_TYPE_SYMBOL) {
    return ((uint32_t)(ptrdiff_t)self->value) % max;
  }
  neo_js_string_t str = (neo_js_string_t)self->value;
  return neo_hash_sdb_utf16(str->value, max);
}
static void neo_js_object_property_dispose(neo_allocator_t allocator,
                                           neo_js_object_property_t self) {}
neo_js_object_property_t
neo_create_js_object_property(neo_allocator_t allocator) {
  neo_js_object_property_t property =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_object_property_t),
                          neo_js_object_property_dispose);
  property->configurable = false;
  property->enumable = false;
  property->get = NULL;
  property->set = NULL;
  property->value = NULL;
  property->writable = false;
  return property;
}