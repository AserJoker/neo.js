#include "js/array.h"
#include "core/allocator.h"
#include "core/hash_map.h"
#include "js/type.h"

neo_js_type_t neo_get_js_array_type() {
  static struct _neo_js_type_t type = {0};
  neo_js_type_t otype = neo_get_js_object_type();
  type.typeof_fn = otype->typeof_fn;
  type.to_boolean_fn = otype->to_boolean_fn;
  type.to_number_fn = otype->to_number_fn;
  type.to_string_fn = otype->to_string_fn;
  return &type;
}

static void neo_js_array_dispose(neo_allocator_t allocator,
                                 neo_js_array_t self) {
  neo_allocator_free(allocator, self->object.properties);
}

neo_js_array_t neo_create_js_array(neo_allocator_t allocator) {
  neo_js_array_t array = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_array_t), neo_js_array_dispose);
  array->length = 0;
  array->object.value.type = neo_get_js_array_type();
  array->object.prototype = NULL;
  array->object.constructor = NULL;
  neo_hash_map_initialize_t initialize = {0};
  initialize.auto_free_key = false;
  initialize.auto_free_value = false;
  initialize.compare = (neo_compare_fn_t)neo_js_object_compare_key;
  initialize.hash = (neo_hash_fn_t)neo_js_object_key_hash;
  array->object.properties = neo_create_hash_map(allocator, &initialize);
  return array;
}

neo_js_array_t neo_js_value_to_array(neo_js_value_t value);