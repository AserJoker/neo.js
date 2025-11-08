#include "runtime/array.h"
#include "core/allocator.h"
#include "engine/array.h"
#include "engine/context.h"
#include "engine/number.h"
#include "engine/runtime.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include <stdbool.h>

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
  return neo_js_context_get_undefined(ctx);
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
}