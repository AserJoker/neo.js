#include "runtime/time.h"
#include "engine/context.h"
#include "engine/number.h"
#include "engine/value.h"
#include "engine/variable.h"
#include "runtime/constant.h"
#include <math.h>

NEO_JS_CFUNCTION(neo_js_time_set_timeout) {
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  if (callback->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "The \"callback\" argument must be of type function. Received %v",
        callback);
    neo_js_variable_t type_error =
        neo_js_context_get_constant(ctx)->type_error_class;
    neo_js_variable_t error =
        neo_js_variable_construct(type_error, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t vtimeout = neo_js_context_get_argument(ctx, argc, argv, 1);
  if (vtimeout->value->type != NEO_JS_TYPE_NUMBER) {
    vtimeout = neo_js_variable_to_number(vtimeout, ctx);
  }
  if (vtimeout->value->type == NEO_JS_TYPE_EXCEPTION) {
    return vtimeout;
  }
  double timeout = ((neo_js_number_t)vtimeout->value)->value;
  if (isnan(timeout)) {
    timeout = 0;
  }
  timeout = (int32_t)timeout;
  int64_t idx = neo_js_context_create_macro_task(ctx, callback, timeout, false);
  return neo_js_context_create_number(ctx, idx);
}
NEO_JS_CFUNCTION(neo_js_time_clear_timeout) {
  neo_js_variable_t idx = neo_js_context_get_argument(ctx, argc, argv, 0);
  if (idx->value->type != NEO_JS_TYPE_NUMBER) {
    idx = neo_js_variable_to_number(idx, ctx);
  }
  if (idx->value->type == NEO_JS_TYPE_EXCEPTION) {
    return idx;
  }
  double index = ((neo_js_number_t)idx->value)->value;
  if (isnan(index)) {
    index = 0;
  }
  neo_js_context_remove_macro_task(ctx, index);
  return neo_js_context_get_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_time_set_interval) {
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  if (callback->value->type != NEO_JS_TYPE_FUNCTION) {
    neo_js_variable_t message = neo_js_context_format(
        ctx, "The \"callback\" argument must be of type function. Received %v",
        callback);
    neo_js_variable_t type_error =
        neo_js_context_get_constant(ctx)->type_error_class;
    neo_js_variable_t error =
        neo_js_variable_construct(type_error, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t vtimeout = neo_js_context_get_argument(ctx, argc, argv, 1);
  if (vtimeout->value->type != NEO_JS_TYPE_NUMBER) {
    vtimeout = neo_js_variable_to_number(vtimeout, ctx);
  }
  if (vtimeout->value->type == NEO_JS_TYPE_EXCEPTION) {
    return vtimeout;
  }
  double timeout = ((neo_js_number_t)vtimeout->value)->value;
  if (isnan(timeout)) {
    timeout = 0;
  }
  timeout = (int32_t)timeout;
  int64_t idx = neo_js_context_create_macro_task(ctx, callback, timeout, true);
  return neo_js_context_create_number(ctx, idx);
}
NEO_JS_CFUNCTION(neo_js_time_clear_interval) {
  neo_js_variable_t idx = neo_js_context_get_argument(ctx, argc, argv, 0);
  if (idx->value->type != NEO_JS_TYPE_NUMBER) {
    idx = neo_js_variable_to_number(idx, ctx);
  }
  if (idx->value->type == NEO_JS_TYPE_EXCEPTION) {
    return idx;
  }
  double index = ((neo_js_number_t)idx->value)->value;
  if (isnan(index)) {
    index = 0;
  }
  neo_js_context_remove_macro_task(ctx, index);
  return neo_js_context_get_undefined(ctx);
}
void neo_initialize_js_time(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->set_timeout = neo_js_context_create_cfunction(
      ctx, neo_js_time_set_timeout, "setTimeout");
  constant->clear_timeout = neo_js_context_create_cfunction(
      ctx, neo_js_time_set_timeout, "clearTimeout");
  constant->set_interval = neo_js_context_create_cfunction(
      ctx, neo_js_time_set_interval, "setInterval");
  constant->clear_interval = neo_js_context_create_cfunction(
      ctx, neo_js_time_set_interval, "clearInterval");
}