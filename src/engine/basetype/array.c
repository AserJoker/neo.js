#include "engine/basetype/array.h"
#include "core/allocator.h"
#include "engine/basetype/number.h"
#include "engine/basetype/object.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"

static neo_js_variable_t neo_js_array_get_field(neo_js_context_t ctx,
                                                neo_js_variable_t object,
                                                neo_js_variable_t field) {
  neo_js_array_t array =
      neo_js_value_to_array(neo_js_variable_get_value(object));
  neo_js_type_t otype = neo_get_js_object_type();
  if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_STRING) {
    neo_js_string_t field_str =
        neo_js_value_to_string(neo_js_variable_get_value(field));
    if (wcscmp(field_str->string, L"length") == 0) {
      neo_js_variable_t length =
          neo_js_context_create_number(ctx, array->length);
      return length;
    }
  }
  return otype->get_field_fn(ctx, object, field);
}

static neo_js_variable_t neo_js_array_set_field(neo_js_context_t ctx,
                                                neo_js_variable_t object,
                                                neo_js_variable_t field,
                                                neo_js_variable_t value) {
  neo_js_type_t otype = neo_get_js_object_type();
  neo_js_array_t array =
      neo_js_value_to_array(neo_js_variable_get_value(object));
  if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_STRING) {
    neo_js_string_t field_str =
        neo_js_value_to_string(neo_js_variable_get_value(field));
    if (wcscmp(field_str->string, L"length") == 0) {
      double length =
          neo_js_value_to_number(neo_js_variable_get_value(value))->number;
      if (length < 0) {
        return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_RANGE,
                                                  L"Invalid array length");
      }
      for (size_t i = length; i < array->length; i++) {
        neo_js_variable_t idx = neo_js_context_create_number(ctx, i);
        neo_js_variable_t item = neo_js_array_get_field(ctx, object, idx);
        if (neo_js_variable_get_type(item)->kind != NEO_JS_TYPE_UNDEFINED) {
          neo_js_variable_t error = neo_js_context_del_field(ctx, object, idx);
          if (error) {
            return error;
          }
        }
      }
      array->length = (size_t)length;
      return neo_js_context_create_undefined(ctx);
    }
  } else if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_NUMBER) {
    neo_js_number_t field_num =
        neo_js_value_to_number(neo_js_variable_get_value(field));
    if (field_num->number >= 0) {
      size_t idx = (size_t)field_num->number;
      if (idx >= array->length) {
        array->length = idx + 1;
      }
    }
  }
  return otype->set_field_fn(ctx, object, field, value);
}

neo_js_type_t neo_get_js_array_type() {
  static struct _neo_js_type_t type = {0};
  type.kind = NEO_JS_TYPE_ARRAY;
  neo_js_type_t otype = neo_get_js_object_type();
  type.typeof_fn = otype->typeof_fn;
  type.to_boolean_fn = otype->to_boolean_fn;
  type.to_number_fn = otype->to_number_fn;
  type.to_string_fn = otype->to_string_fn;
  type.to_primitive_fn = otype->to_primitive_fn;
  type.to_object_fn = otype->to_object_fn;
  type.get_field_fn = neo_js_array_get_field;
  type.set_field_fn = neo_js_array_set_field;
  type.del_field_fn = otype->del_field_fn;
  type.is_equal_fn = otype->is_equal_fn;
  type.copy_fn = otype->copy_fn;
  return &type;
}

static void neo_js_array_dispose(neo_allocator_t allocator,
                                 neo_js_array_t self) {
  neo_js_object_dispose(allocator, &self->object);
}

neo_js_array_t neo_create_js_array(neo_allocator_t allocator) {
  neo_js_array_t array = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_array_t), neo_js_array_dispose);
  array->length = 0;
  neo_js_object_init(allocator, &array->object);
  array->object.value.type = neo_get_js_array_type();
  return array;
}

neo_js_array_t neo_js_value_to_array(neo_js_value_t value) {
  if (value->type == neo_get_js_array_type()) {
    return (neo_js_array_t)value;
  }
  return NULL;
}