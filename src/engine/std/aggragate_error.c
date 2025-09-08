#include "engine/context.h"
#include "engine/std/aggregate_error.h"
#include "engine/std/array.h"
#include "engine/std/error.h"
#include "engine/type.h"
#include "engine/variable.h"
NEO_JS_CFUNCTION(neo_js_aggregate_error_constructor) {
  if (neo_js_context_get_call_type(ctx) == NEO_JS_FUNCTION_CALL) {
    neo_js_variable_t constructor =
        neo_js_context_get_std(ctx).aggregate_error_constructor;
    neo_js_variable_t prototype = neo_js_context_get_field(
        ctx, constructor, neo_js_context_create_string(ctx, "prototype"), NULL);
    self = neo_js_context_create_object(ctx, prototype);
    neo_js_context_set_field(ctx, self,
                             neo_js_context_create_string(ctx, "constructor"),
                             constructor, NULL);
  }
  neo_js_variable_t arg = NULL;
  if (!argc) {
    arg = neo_js_context_create_undefined(ctx);
  } else {
    arg = argv[0];
  }
  neo_js_variable_t iterator = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).symbol_constructor,
      neo_js_context_create_string(ctx, "iterator"), NULL);
  iterator = neo_js_context_get_field(ctx, arg, iterator, NULL);
  NEO_JS_TRY_AND_THROW(iterator);
  if (neo_js_variable_get_type(iterator)->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "variable is not iterable");
  }
  neo_js_variable_t gen = neo_js_context_call(ctx, iterator, arg, 0, NULL);
  neo_js_variable_t next = neo_js_context_get_field(
      ctx, gen, neo_js_context_create_string(ctx, "next"), NULL);
  NEO_JS_TRY_AND_THROW(next);
  neo_js_variable_t errors = neo_js_context_create_array(ctx);
  for (;;) {
    neo_js_variable_t res = neo_js_context_call(ctx, next, gen, 0, NULL);
    NEO_JS_TRY_AND_THROW(res);
    neo_js_variable_t done = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, "done"), NULL);
    NEO_JS_TRY_AND_THROW(done);
    done = neo_js_context_to_boolean(ctx, done);
    NEO_JS_TRY_AND_THROW(done);
    if (neo_js_variable_to_boolean(done)->boolean) {
      break;
    }
    neo_js_variable_t value = neo_js_context_get_field(
        ctx, res, neo_js_context_create_string(ctx, "value"), NULL);
    NEO_JS_TRY_AND_THROW(value);
    neo_js_array_push(ctx, errors, 1, &value);
  }
  neo_js_variable_t message = NULL;
  if (argc > 1) {
    message = argv[1];
  } else {
    message = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t result = neo_js_error_constructor(ctx, self, 1, &message);
  neo_js_context_set_field(
      ctx, result, neo_js_context_create_string(ctx, "errors"), errors, NULL);
  neo_js_error_info_t info = neo_js_context_get_opaque(ctx, result, "info");
  info->type = "AggregateError";
  return result;
}
void neo_js_context_init_std_aggregate_error(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, neo_js_context_get_std(ctx).aggregate_error_constructor,
      neo_js_context_create_string(ctx, "prototype"), NULL);

  neo_js_context_def_field(
      ctx, prototype, neo_js_context_create_string(ctx, "toString"),
      neo_js_context_create_cfunction(ctx, "toString", neo_js_error_to_string),
      true, false, true);
}