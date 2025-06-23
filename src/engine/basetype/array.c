#include "engine/basetype/array.h"
#include "core/allocator.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "engine/basetype/number.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"

static neo_engine_variable_t
neo_engine_array_get_field(neo_engine_context_t ctx,
                           neo_engine_variable_t object,
                           neo_engine_variable_t field) {
  neo_engine_array_t array =
      neo_engine_value_to_array(neo_engine_variable_get_value(object));
  neo_engine_type_t otype = neo_get_js_object_type();
  if (neo_engine_variable_get_type(field)->kind == NEO_TYPE_STRING) {
    neo_engine_string_t field_str =
        neo_engine_value_to_string(neo_engine_variable_get_value(field));
    if (wcscmp(field_str->string, L"length") == 0) {
      neo_engine_variable_t length =
          neo_engine_context_create_number(ctx, array->length);
      return length;
    }
  }
  return otype->get_field_fn(ctx, object, field);
}

static neo_engine_variable_t neo_engine_array_set_field(
    neo_engine_context_t ctx, neo_engine_variable_t object,
    neo_engine_variable_t field, neo_engine_variable_t value) {
  neo_engine_type_t otype = neo_get_js_object_type();
  neo_engine_array_t array =
      neo_engine_value_to_array(neo_engine_variable_get_value(object));
  if (neo_engine_variable_get_type(field)->kind == NEO_TYPE_STRING) {
    neo_engine_string_t field_str =
        neo_engine_value_to_string(neo_engine_variable_get_value(field));
    if (wcscmp(field_str->string, L"length") == 0) {
      double length =
          neo_engine_value_to_number(neo_engine_variable_get_value(value))
              ->number;
      if (length < 0) {
        return neo_engine_context_create_error(ctx, L"RangeError",
                                               L"Invalid array length");
      }
      for (size_t i = length; i < array->length; i++) {
        neo_engine_variable_t idx = neo_engine_context_create_number(ctx, i);
        neo_engine_variable_t item =
            neo_engine_array_get_field(ctx, object, idx);
        if (neo_engine_variable_get_type(item)->kind != NEO_TYPE_UNDEFINED) {
          neo_engine_variable_t error =
              neo_engine_context_del_field(ctx, object, idx);
          if (error) {
            return error;
          }
        }
      }
      array->length = (size_t)length;
      return NULL;
    }
  } else if (neo_engine_variable_get_type(field)->kind == NEO_TYPE_NUMBER) {
    neo_engine_number_t field_num =
        neo_engine_value_to_number(neo_engine_variable_get_value(field));
    if (field_num->number >= 0) {
      size_t idx = (size_t)field_num->number;
      if (idx >= array->length) {
        array->length = idx + 1;
      }
    }
  }
  return otype->set_field_fn(ctx, object, field, value);
}

neo_engine_type_t neo_get_js_array_type() {
  static struct _neo_engine_type_t type = {0};
  type.kind = NEO_TYPE_ARRAY;
  neo_engine_type_t otype = neo_get_js_object_type();
  type.typeof_fn = otype->typeof_fn;
  type.to_boolean_fn = otype->to_boolean_fn;
  type.to_number_fn = otype->to_number_fn;
  type.to_string_fn = otype->to_string_fn;
  type.to_primitive_fn = otype->to_primitive_fn;
  type.to_object_fn = otype->to_object_fn;
  type.get_field_fn = neo_engine_array_get_field;
  type.set_field_fn = neo_engine_array_set_field;
  type.del_field_fn = otype->del_field_fn;
  type.is_equal_fn = otype->is_equal_fn;
  type.copy_fn = otype->copy_fn;
  return &type;
}

static void neo_engine_array_dispose(neo_allocator_t allocator,
                                     neo_engine_array_t self) {
  neo_allocator_free(allocator, self->object.properties);
  neo_allocator_free(allocator, self->object.internal);
}

neo_engine_array_t neo_create_js_array(neo_allocator_t allocator) {
  neo_engine_array_t array = neo_allocator_alloc(
      allocator, sizeof(struct _neo_engine_array_t), neo_engine_array_dispose);
  array->length = 0;
  array->object.value.type = neo_get_js_array_type();
  array->object.value.ref = 0;
  array->object.extensible = true;
  array->object.frozen = false;
  array->object.sealed = false;
  array->object.prototype = NULL;
  neo_hash_map_initialize_t initialize = {0};
  initialize.auto_free_key = false;
  initialize.auto_free_value = true;
  initialize.compare = (neo_compare_fn_t)neo_engine_object_compare_key;
  initialize.hash = (neo_hash_fn_t)neo_engine_object_key_hash;
  array->object.properties = neo_create_hash_map(allocator, &initialize);

  initialize.auto_free_key = true;
  initialize.auto_free_value = true;
  initialize.compare = (neo_compare_fn_t)wcscmp;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  array->object.internal = neo_create_hash_map(allocator, &initialize);
  array->object.constructor = NULL;
  return array;
}

neo_engine_array_t neo_engine_value_to_array(neo_engine_value_t value) {
  if (value->type == neo_get_js_array_type()) {
    return (neo_engine_array_t)value;
  }
  return NULL;
}