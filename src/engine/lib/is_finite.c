#include "engine/lib/is_finite.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdbool.h>

NEO_JS_CFUNCTION(neo_js_is_finite) {
  if (!argc) {
    return neo_js_context_create_boolean(ctx, false);
  }
  neo_js_variable_t test = neo_js_context_to_number(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(test);
  double val = neo_js_variable_to_number(test)->number;
  if (isnan(val) || isinf(val)) {
    return neo_js_context_create_boolean(ctx, false);
  }
  return neo_js_context_create_boolean(ctx, true);
}