#include "engine/std/weak_ref.h"
#include "core/list.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/variable.h"

NEO_JS_CFUNCTION(neo_js_weak_ref_constructor) {
  neo_js_variable_t value = NULL;
  if (argc) {
    value = argv[0];
  }
  if (!value || (neo_js_variable_get_type(value)->kind != NEO_JS_TYPE_OBJECT &&
                 neo_js_variable_get_type(value)->kind != NEO_JS_TYPE_SYMBOL)) {
    return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, 0,
                                              "invalid target");
  }
  neo_js_handle_t hvalue = neo_js_variable_get_handle(value);
  neo_js_handle_t hself = neo_js_variable_get_handle(self);
  neo_js_handle_add_weak_parent(hvalue, hself);
  return self;
}
NEO_JS_CFUNCTION(neo_js_weak_ref_deref) {
  neo_js_handle_t hself = neo_js_variable_get_handle(self);
  neo_list_t weak_children = neo_js_handle_get_week_children(hself);
  neo_list_node_t it = neo_list_get_first(weak_children);
  if (it) {
    neo_js_handle_t hvalue = neo_list_node_get(it);
    return neo_js_context_create_variable(ctx, hvalue, NULL);
  }
  return neo_js_context_create_undefined(ctx);
}
void neo_js_context_init_std_weak_ref(neo_js_context_t ctx) {
  neo_js_variable_t prototype = neo_js_context_get_string_field(
      ctx, neo_js_context_get_std(ctx).weak_ref_constructor, "prototype");
  NEO_JS_SET_METHOD(ctx, prototype, "deref", neo_js_weak_ref_deref);
}