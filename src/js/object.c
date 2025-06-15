#include "js/object.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/hash_map.h"
#include "js/context.h"
#include "js/handle.h"
#include "js/string.h"
#include "js/type.h"
#include "js/value.h"
#include <stdbool.h>
#include <wchar.h>

static const wchar_t *neo_js_object_typeof(neo_js_context_t ctx,
                                           neo_js_variable_t variable) {
  return L"object";
}

static neo_js_variable_t neo_js_object_to_string(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_string_t string = neo_create_js_string(allocator, L"[object Object]");
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &string->value));
}

static neo_js_variable_t neo_js_object_to_boolean(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  return neo_js_context_create_boolean(ctx, true);
}
static neo_js_variable_t neo_js_object_to_number(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  return neo_js_context_create_nan(ctx);
}

neo_js_type_t neo_get_js_object_type() {
  static struct _neo_js_type_t type = {
      neo_js_object_typeof,
      neo_js_object_to_string,
      neo_js_object_to_boolean,
      neo_js_object_to_number,
  };
  return &type;
}

static void neo_js_object_dispose(neo_allocator_t allocator,
                                  neo_js_object_t self) {
  neo_allocator_free(allocator, self->properties);
}

int8_t neo_js_object_compare_key(neo_js_handle_t handle1,
                                 neo_js_handle_t handle2,
                                 neo_js_context_t ctx) {
  neo_js_variable_t val1 = neo_js_context_create_variable(ctx, handle1);
  neo_js_variable_t val2 = neo_js_context_create_variable(ctx, handle2);
  return 0;
}

uint32_t neo_js_object_key_hash(neo_js_handle_t *handle1, uint32_t max_bucket) {
  return (uintptr_t)handle1 % max_bucket;
}

neo_js_object_t neo_create_js_object(neo_allocator_t allocator) {
  neo_js_object_t object = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_object_t), neo_js_object_dispose);
  object->prototype = NULL;
  neo_hash_map_initialize_t initialize = {0};
  initialize.compare = (neo_compare_fn_t)neo_js_object_compare_key;
  initialize.hash = (neo_hash_fn_t)neo_js_object_key_hash;
  object->properties = neo_create_hash_map(allocator, &initialize);
  return object;
}

neo_js_object_t neo_js_value_to_object(neo_js_value_t value) {
  if (value->type == neo_get_js_object_type()) {
    return (neo_js_object_t)value;
  }
  return NULL;
}
