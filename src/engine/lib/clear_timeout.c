#include "engine/lib/clear_timeout.h"
#include "engine/context.h"
#include <math.h>

neo_js_variable_t neo_js_clear_timeout(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv) {
  if (argc > 0) {
    neo_js_variable_t vid = neo_js_context_to_number(ctx, argv[0]);
    neo_js_number_t id = neo_js_variable_to_number(vid);
    if (!isnan(id->number) && id->number >= 0) {
      neo_js_context_kill_macro_task(ctx, id->number);
    }
  }
  return neo_js_context_create_undefined(ctx);
}