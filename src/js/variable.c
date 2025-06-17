#include "js/variable.h"
#include "core/allocator.h"
#include "js/handle.h"
#include "js/type.h"
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

bool neo_js_variable_is_primitive(neo_js_variable_t variable) {
  neo_js_type_t type = neo_js_variable_get_type(variable);
  return type->kind == NEO_TYPE_NUMBER || type->kind == NEO_TYPE_STRING ||
         type->kind == NEO_TYPE_BOOLEAN || type->kind == NEO_TYPE_UNDEFINED ||
         type->kind == NEO_TYPE_NULL || type->kind == NEO_TYPE_SYMBOL;
}