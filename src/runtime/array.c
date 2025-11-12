#include "runtime/array.h"
#include "core/allocator.h"
#include "core/string.h"
#include "engine/array.h"
#include "engine/context.h"
#include "engine/number.h"
#include "engine/runtime.h"
#include "engine/string.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

NEO_JS_CFUNCTION(neo_js_array_constructor) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_array_t array =
      neo_create_js_array(allocator, constant->array_prototype->value);
  self = neo_js_context_create_variable(ctx, neo_js_array_to_value(array));
  if (argc == 1 && argv[0]->value->type == NEO_JS_TYPE_NUMBER) {
    double lenf = ((neo_js_number_t)argv[0]->value)->value;
    uint32_t len = lenf;
    if (len != lenf) {
      neo_js_variable_t message =
          neo_js_context_format(ctx, "Invalid array length");
      neo_js_variable_t error = neo_js_variable_construct(
          constant->range_error_class, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_variable_t length = neo_js_context_create_number(ctx, len);
    neo_js_variable_t key = neo_js_context_create_cstring(ctx, "length");
    neo_js_variable_def_field(self, ctx, key, length, false, false, true);
  } else {
    neo_js_variable_t length = neo_js_context_create_number(ctx, 0);
    neo_js_variable_t key = neo_js_context_create_cstring(ctx, "length");
    neo_js_variable_def_field(self, ctx, key, length, false, false, true);
    for (size_t idx = 0; idx < argc; idx++) {
      neo_js_variable_t key = neo_js_context_create_number(ctx, idx);
      neo_js_variable_t res =
          neo_js_variable_set_field(self, ctx, key, argv[idx]);
      if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
        return res;
      }
    }
  }
  return self;
}
NEO_JS_CFUNCTION(neo_js_array_to_string) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t length = neo_js_context_create_cstring(ctx, "length");
  length = neo_js_variable_get_field(self, ctx, length);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  if (length->value->type != NEO_JS_TYPE_NUMBER) {
    length = neo_js_variable_to_number(length, ctx);
  }
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double lenf = ((neo_js_number_t)length->value)->value;
  if (lenf < 0 || isnan(lenf)) {
    lenf = 0;
  }
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  size_t max = 16;
  uint16_t *string =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * max, NULL);
  *string = 0;
  uint16_t *dst = string;
  for (size_t idx = 0; idx < argc; idx++) {
    if (idx != argc - 1) {
      uint16_t part[] = {',', 0};
      dst = neo_string16_concat(allocator, dst, &max, part);
    }
    neo_js_variable_t item = argv[idx];
    if (item->value->type != NEO_JS_TYPE_STRING) {
      item = neo_js_variable_to_string(item, ctx);
      if (item->value->type == NEO_JS_TYPE_EXCEPTION) {
        return item;
      }
    }
    dst = neo_string16_concat(allocator, dst, &max,
                              ((neo_js_string_t)item)->value);
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, string);
  neo_allocator_free(allocator, string);
  return result;
}
NEO_JS_CFUNCTION(neo_js_array_values) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t iterator =
      neo_js_context_create_object(ctx, constant->array_iterator_prototype);
  neo_js_variable_set_internal(iterator, ctx, "array", self);
  neo_js_variable_t index = neo_js_context_create_number(ctx, 0);
  neo_js_variable_set_internal(iterator, ctx, "index", index);
  return iterator;
}
NEO_JS_CFUNCTION(neo_js_array_push) {
  if (self->value->type < NEO_JS_TYPE_OBJECT) {
    self = neo_js_variable_to_object(self, ctx);
  }
  if (self->value->type == NEO_JS_TYPE_EXCEPTION) {
    return self;
  }
  neo_js_variable_t key = neo_js_context_create_cstring(ctx, "length");
  neo_js_variable_t length = neo_js_variable_get_field(self, ctx, key);
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  if (length->value->type != NEO_JS_TYPE_NUMBER) {
    length = neo_js_variable_to_number(length, ctx);
  }
  if (length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return length;
  }
  double lenf = ((neo_js_number_t)length->value)->value;
  if (isnan(lenf) || lenf < 0) {
    lenf = 0;
  }
  int64_t len = lenf;
  length = neo_js_context_create_number(ctx, len);
  neo_js_number_t num = (neo_js_number_t)length->value;
  for (size_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t index = neo_js_context_create_number(ctx, num->value);
    num->value++;
    if (num->value >= ((int64_t)2 << 52) - 1) {
      neo_js_constant_t constant = neo_js_context_get_constant(ctx);
      neo_js_variable_t message = neo_js_context_format(
          ctx,
          "Pushing 1 elements on an array-like of length %v "
          "is disallowed, as the total surpasses 2**53-1",
          length);
      neo_js_variable_t error = neo_js_variable_construct(
          constant->type_error_class, ctx, 1, &message);
      return neo_js_context_create_exception(ctx, error);
    }
    neo_js_variable_t res =
        neo_js_variable_set_field(self, ctx, key, argv[idx]);
    if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
      return res;
    }
  }
  neo_js_variable_t res = neo_js_variable_set_field(self, ctx, key, length);
  if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
    return res;
  }
  return length;
}
void neo_initialize_js_array(neo_js_context_t ctx) {
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_array_t array = neo_create_js_array(allocator, constant->null->value);
  constant->array_prototype =
      neo_js_context_create_variable(ctx, neo_js_array_to_value(array));
  constant->array_class =
      neo_js_context_create_cfunction(ctx, neo_js_array_constructor, NULL);
  neo_js_variable_set_prototype_of(constant->array_class, ctx,
                                   constant->array_prototype);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "toString",
                    neo_js_array_to_string);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "values",
                    neo_js_array_values);
  NEO_JS_DEF_METHOD(ctx, constant->array_prototype, "push", neo_js_array_push);
}