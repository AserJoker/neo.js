#include "engine/lib/set_timeout.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>

neo_js_variable_t neo_js_set_timeout(neo_js_context_t ctx,
                                     neo_js_variable_t self, uint32_t argc,
                                     neo_js_variable_t *argv) {
  if (argc < 1 ||
      neo_js_variable_get_type(argv[0])->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        L"The \" callback\" argument must be of type function.");
  }
  uint32_t timeout = 0;
  if (argc > 1) {
    neo_js_variable_t time = neo_js_context_to_number(ctx, argv[1]);
    neo_js_number_t num = neo_js_variable_to_number(time);
    if (isnan(num->number) || num->number <= 0) {
      timeout = 0;
    } else {
      timeout = num->number;
    }
  }
  uint32_t id = neo_js_context_create_macro_task(
      ctx, argv[0], neo_js_context_create_undefined(ctx), 0, NULL, timeout,
      false);
  return neo_js_context_create_number(ctx, id);
}