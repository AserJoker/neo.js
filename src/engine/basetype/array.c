#include "engine/basetype/array.h"
#include "core/allocator.h"
#include "engine/basetype/number.h"
#include "engine/basetype/object.h"
#include "engine/chunk.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <string.h>

static neo_js_variable_t neo_js_array_set_field(neo_js_context_t ctx,
                                                neo_js_variable_t object,
                                                neo_js_variable_t field,
                                                neo_js_variable_t value,
                                                neo_js_variable_t receiver) {
  neo_js_type_t otype = neo_get_js_object_type();
  neo_js_array_t array = neo_js_variable_to_array(object);
  if (neo_js_variable_get_type(field)->kind != NEO_JS_TYPE_SYMBOL) {
    neo_js_variable_t vlength = neo_js_context_to_string(ctx, field);
    NEO_JS_TRY_AND_THROW(vlength);
    if (strcmp(neo_js_variable_to_string(vlength)->string, "length") == 0) {
      neo_js_object_property_t plength = neo_js_object_get_property(
          ctx, object, neo_js_context_create_string(ctx, "length"));
      neo_js_value_t vlength = neo_js_chunk_get_value(plength->value);
      neo_js_number_t nlength = neo_js_value_to_number(vlength);
      neo_js_variable_t newlength = neo_js_context_to_number(ctx, value);
      NEO_JS_TRY_AND_THROW(newlength);
      neo_js_number_t val = neo_js_variable_to_number(newlength);
      if (val->number < 0 || val->number > NEO_MAX_INTEGER ||
          isnan(val->number) || isinf(val->number)) {
        return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_RANGE, 0,
                                                  "Invalid array length");
      }
      for (int64_t idx = neo_js_variable_to_number(newlength)->number;
           idx < nlength->number; idx++) {
        NEO_JS_TRY_AND_THROW(neo_js_context_del_field(
            ctx, object, neo_js_context_create_number(ctx, idx)));
      }
    } else {
      neo_js_variable_t vidx = neo_js_context_to_number(ctx, field);
      NEO_JS_TRY_AND_THROW(vidx);
      double idx = neo_js_variable_to_number(vidx)->number;
      if (!isnan(idx) && !isinf(idx) && idx >= 0 && idx <= NEO_MAX_INTEGER) {
        neo_js_object_property_t prop = neo_js_object_get_property(
            ctx, object, neo_js_context_create_string(ctx, "length"));
        neo_js_value_t vlength = neo_js_chunk_get_value(prop->value);
        neo_js_number_t nlength = neo_js_value_to_number(vlength);
        nlength->number = idx + 1;
      }
    }
  }
  return otype->set_field_fn(ctx, object, field, value, receiver);
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
  type.get_field_fn = otype->get_field_fn;
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