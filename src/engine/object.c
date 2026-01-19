#include "neojs/engine/object.h"
#include "neojs/core/allocator.h"
#include "neojs/core/common.h"
#include "neojs/core/hash.h"
#include "neojs/core/hash_map.h"
#include "neojs/core/list.h"
#include "neojs/engine/string.h"
#include "neojs/engine/value.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static void neo_js_object_dispose(neo_allocator_t allocator,
                                  neo_js_object_t self) {
  neo_deinit_js_object(self, allocator);
}
neo_js_object_t neo_create_js_object(neo_allocator_t allocator,
                                     neo_js_value_t prototype) {
  neo_js_object_t object = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_object_t), neo_js_object_dispose);
  neo_init_js_object(object, allocator, prototype);
  return object;
}

void neo_init_js_object(neo_js_object_t self, neo_allocator_t allocator,
                        neo_js_value_t prototype) {
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
  self->keys = neo_create_list(allocator, NULL);
  self->clazz = NULL;
  initialize.compare = (neo_compare_fn_t)strcmp;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  initialize.auto_free_key = true;
  initialize.auto_free_value = false;
  self->internals = neo_create_hash_map(allocator, &initialize);
  initialize.auto_free_key = false;
  initialize.auto_free_value = true;
  initialize.hash = NULL;
  initialize.compare = NULL;
  self->privites = neo_create_hash_map(allocator, &initialize);
  neo_js_value_add_parent(prototype, neo_js_object_to_value(self));
}
void neo_deinit_js_object(neo_js_object_t self, neo_allocator_t allocator) {
  neo_allocator_free(allocator, self->privites);
  neo_allocator_free(allocator, self->keys);
  neo_allocator_free(allocator, self->properties);
  neo_allocator_free(allocator, self->internals);
  neo_deinit_js_value(&self->super, allocator);
}
neo_js_value_t neo_js_object_to_value(neo_js_object_t self) {
  return &self->super;
}
int32_t neo_js_object_key_compare(const neo_js_value_t self,
                                  const neo_js_value_t another) {
  if (self->type != another->type) {
    return self->type - another->type;
  }
  if (self->type == NEO_JS_TYPE_SYMBOL) {
    if (self == another) {
      return 0;
    }
    return (int32_t)(self - another);
  }
  const uint16_t *str1 = ((neo_js_string_t)self)->value;
  const uint16_t *str2 = ((neo_js_string_t)another)->value;
  for (;;) {
    if (*str1 != *str2) {
      return *str1 - *str2;
    }
    if (!*str1) {
      return 0;
    }
    str1++;
    str2++;
  }
  return 0;
}
uint32_t neo_js_object_key_hash(const neo_js_value_t self, uint32_t max) {
  if (self->type == NEO_JS_TYPE_SYMBOL) {
    return ((uint32_t)(ptrdiff_t)self) % max;
  }
  neo_js_string_t str = (neo_js_string_t)self;
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
neo_js_object_private_t
neo_create_js_object_private(neo_allocator_t allocator) {
  neo_js_object_private_t pri = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_object_private_t), NULL);
  pri->get = NULL;
  pri->set = NULL;
  pri->value = NULL;
  pri->method = NULL;
  return pri;
}