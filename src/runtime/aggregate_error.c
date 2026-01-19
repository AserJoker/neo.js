#include "neojs/runtime/aggregate_error.h"
#include "neojs/engine/context.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"
#include "neojs/runtime/error.h"

static NEO_JS_CFUNCTION(neo_js_ggregate_error_constructor) {
  neo_js_variable_t errors = NULL;
  if (argc > 0) {
    errors = argv[0];
  } else {
    errors = neo_js_context_create_array(ctx);
  }
  neo_js_variable_set_field(
      self, ctx, neo_js_context_create_cstring(ctx, "errors"), errors);
  if (argc <= 1) {
    return neo_js_error_constructor(ctx, self, 0, NULL);
  }
  return neo_js_error_constructor(ctx, self, argc - 1, &argv[0]);
}

void neo_initialize_js_aggregate_error(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->aggregate_error_class = neo_js_context_create_cfunction(
      ctx, neo_js_ggregate_error_constructor, "AggregateError");
  neo_js_variable_extends(constant->range_error_class, ctx,
                          constant->error_class);
  neo_js_variable_t prototype = neo_js_variable_get_field(
      constant->aggregate_error_class, ctx, constant->key_prototype);
  neo_js_variable_t string =
      neo_js_context_create_cstring(ctx, "AggregateError");
  neo_js_variable_t key = neo_js_context_create_cstring(ctx, "name");
  neo_js_variable_def_field(prototype, ctx, key, string, true, false, true);
}