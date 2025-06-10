#include "js/variable.h"
#include "core/allocator.h"
#include "js/handle.h"
struct _neo_js_variable_t {
  neo_js_handle_t handle;
};

static void neo_js_variable_dispose(neo_allocator_t allocator,
                                    neo_js_variable_t variable) {}

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_handle_t handle) {
  neo_js_variable_t variable = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_variable_t), neo_js_variable_dispose);
  variable->handle = handle;
  return variable;
}

neo_js_handle_t neo_js_variable_get_handle(neo_js_variable_t variable) {
  return variable->handle;
}
neo_js_value_t neo_js_variable_get_value(neo_js_variable_t variable) {
  return neo_js_handle_get_value(variable->handle);
}
neo_js_type_t neo_js_variable_get_type(neo_js_variable_t variable) {
  return neo_js_variable_get_value(variable)->type;
}