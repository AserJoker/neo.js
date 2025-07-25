#include "engine/lib/is_nan.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
NEO_JS_CFUNCTION(neo_js_is_nan) {
  if (!argc) {
    return neo_js_context_create_boolean(ctx, true);
  }
  neo_js_variable_t arg = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(arg);
  double test = neo_js_variable_to_number(arg)->number;
  return neo_js_context_create_boolean(ctx, isnan(test));
}