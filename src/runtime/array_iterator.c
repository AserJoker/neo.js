
#include "runtime/array_iterator.h"
#include "engine/context.h"
#include "engine/number.h"
#include "engine/object.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include <math.h>
NEO_JS_CFUNCTION(neo_js_array_iterator_next) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t message =
        neo_js_context_format(ctx,
                              "Method Array Iterator.prototype.next called on "
                              "imcompatible receiver %v",
                              self);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t array = neo_js_variable_get_internel(self, ctx, "array");
  neo_js_variable_t index = neo_js_variable_get_internel(self, ctx, "index");
  if (!array || array->value->type < NEO_JS_TYPE_OBJECT) {
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t message =
        neo_js_context_format(ctx,
                              "Method Array Iterator.prototype.next called on "
                              "imcompatible receiver %v",
                              self);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  if (!index || index->value->type != NEO_JS_TYPE_NUMBER) {
    neo_js_constant_t constant = neo_js_context_get_constant(ctx);
    neo_js_variable_t message =
        neo_js_context_format(ctx,
                              "Method Array Iterator.prototype.next called on "
                              "imcompatible receiver %v",
                              self);
    neo_js_variable_t error =
        neo_js_variable_construct(constant->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t length = NULL;
  neo_js_object_property_t prop = neo_js_variable_get_property(
      array, ctx, neo_js_context_create_cstring(ctx, "length"));
  if (prop) {
    length = neo_js_variable_get_field(
        array, ctx, neo_js_context_create_cstring(ctx, "length"));
  } else {
    length = neo_js_context_create_number(ctx, 0);
    neo_js_variable_t res = neo_js_variable_set_field(
        array, ctx, neo_js_context_create_cstring(ctx, "length"), length);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
  }
  length = neo_js_variable_to_number(length, ctx);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  if (isnan(len)) {
    len = 0;
  }
  uint32_t idx = ((neo_js_number_t)index->value)->value;
  if (idx >= len) {
    neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
    neo_js_variable_t key = neo_js_context_create_cstring(ctx, "done");
    neo_js_variable_set_field(result, ctx, key, neo_js_context_get_true(ctx));
    key = neo_js_context_create_cstring(ctx, "value");
    neo_js_variable_set_field(result, ctx, key,
                              neo_js_context_get_undefined(ctx));
    return result;
  } else {
    neo_js_variable_t value = neo_js_variable_get_field(array, ctx, index);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      return value;
    }
    neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
    neo_js_variable_t key = neo_js_context_create_cstring(ctx, "done");
    neo_js_variable_set_field(result, ctx, key, neo_js_context_get_false(ctx));
    key = neo_js_context_create_cstring(ctx, "value");
    neo_js_variable_set_field(result, ctx, key, value);
    ((neo_js_number_t)index->value)->value += 1;
    return result;
  }
}
void neo_initialize_js_array_iterator(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->array_iterator_prototype =
      neo_js_context_create_object(ctx, constant->iterator_prototype);
  NEO_JS_DEF_METHOD(ctx, constant->array_iterator_prototype, "next",
                    neo_js_array_iterator_next);
  neo_js_variable_t string_tag =
      neo_js_context_create_cstring(ctx, "Array Iterator");
  neo_js_variable_set_field(constant->array_iterator_prototype, ctx,
                            constant->symbol_to_string_tag, string_tag);
}