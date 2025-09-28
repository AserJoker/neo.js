#include "engine/std/proxy.h"
#include "engine/context.h"
#include "engine/type.h"
#include "engine/variable.h"

NEO_JS_CFUNCTION(neo_js_proxy_constructor) {
  neo_js_variable_t target = NULL;
  neo_js_variable_t handler = NULL;
  if (argc < 2) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Cannot create proxy with a non-object as target or handler");
  }
  target = argv[0];
  handler = argv[1];
  if (neo_js_variable_get_type(target)->kind < NEO_JS_TYPE_OBJECT ||
      neo_js_variable_get_type(handler)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Cannot create proxy with a non-object as target or handler");
  }
  neo_js_context_set_internal(ctx, self, "[[target]]", target);
  neo_js_context_set_internal(ctx, self, "[[handler]]", handler);
  return self;
}
void neo_js_context_init_std_proxy(neo_js_context_t ctx) {}