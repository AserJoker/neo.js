#include "runtime/promise.h"
#include "engine/context.h"
#include "engine/variable.h"
#include "runtime/constant.h"

NEO_JS_CFUNCTION(neo_js_promise_resolve) { return self; }
NEO_JS_CFUNCTION(neo_js_promise_constructor) { return self; }
NEO_JS_CFUNCTION(neo_js_promise_then) { return self; }
void neo_initialize_js_promise(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->promise_class = neo_js_context_create_cfunction(
      ctx, neo_js_promise_constructor, "Promise");
  neo_js_variable_t prototype = neo_js_variable_get_field(
      constant->promise_class, ctx, constant->key_prototype);
  NEO_JS_DEF_METHOD(ctx, constant->promise_class, "resolve",
                    neo_js_promise_resolve);
  NEO_JS_DEF_METHOD(ctx, prototype, "then", neo_js_promise_then);
}