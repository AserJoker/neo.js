
#include "neojs/runtime/array_iterator.h"
#include "neojs/engine/context.h"
#include "neojs/engine/number.h"
#include "neojs/engine/value.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"
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
  neo_js_variable_t array = neo_js_variable_get_internal(self, ctx, "array");
  neo_js_variable_t index = neo_js_variable_get_internal(self, ctx, "index");
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
  neo_js_variable_t length = neo_js_variable_get_field(
      array, ctx, neo_js_context_create_string(ctx, u"length"));
  length = neo_js_variable_to_number(length, ctx);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double len = ((neo_js_number_t)length->value)->value;
  if (isnan(len) || len < 0) {
    len = 0;
  }
  uint32_t idx = ((neo_js_number_t)index->value)->value;
  if (idx >= len) {
    neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
    neo_js_variable_t key = neo_js_context_create_string(ctx, u"done");
    neo_js_variable_set_field(result, ctx, key, neo_js_context_get_true(ctx));
    key = neo_js_context_create_string(ctx, u"value");
    neo_js_variable_set_field(result, ctx, key,
                              neo_js_context_get_undefined(ctx));
    return result;
  } else {
    neo_js_variable_t value = neo_js_variable_get_field(array, ctx, index);
    if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
      return value;
    }
    neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
    neo_js_variable_t key = neo_js_context_create_string(ctx, u"done");
    neo_js_variable_set_field(result, ctx, key, neo_js_context_get_false(ctx));
    key = neo_js_context_create_string(ctx, u"value");
    if (neo_js_variable_has_opaque(self, "is_entries")) {
      neo_js_variable_t item = neo_js_context_create_array(ctx);
      neo_js_variable_set_field(item, ctx, neo_js_context_create_number(ctx, 0),
                                key);
      neo_js_variable_set_field(item, ctx, neo_js_context_create_number(ctx, 1),
                                value);
      neo_js_variable_set_field(result, ctx, key, item);
    } else {
      neo_js_variable_set_field(result, ctx, key, value);
    }
    ((neo_js_number_t)index->value)->value += 1;
    return result;
  }
}
void neo_initialize_js_array_iterator(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->array_iterator_prototype =
      neo_js_context_create_object(ctx, constant->iterator_prototype);
  NEO_JS_DEF_METHOD(ctx, constant->array_iterator_prototype, u"next",
                    neo_js_array_iterator_next);
  neo_js_variable_t string_tag =
      neo_js_context_create_string(ctx, u"Array Iterator");
  neo_js_variable_set_field(constant->array_iterator_prototype, ctx,
                            constant->symbol_to_string_tag, string_tag);
}