#include "engine/variable.h"
#include "core/allocator.h"
#include "engine/handle.h"
#include "engine/type.h"
struct _neo_js_variable_t {
  neo_js_handle_t handle;
  bool is_const;
};

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_handle_t handle) {
  neo_js_variable_t variable =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_variable_t), NULL);
  variable->handle = handle;
  variable->is_const = false;
  return variable;
}

neo_js_handle_t neo_js_variable_get_handle(neo_js_variable_t variable) {
  return variable->handle;
}

void neo_js_variable_set_const(neo_js_variable_t variable, bool is_const) {
  variable->is_const = is_const;
}

bool neo_js_variable_is_const(neo_js_variable_t variable) {
  return variable->is_const;
}

bool neo_js_variable_is_primitive(neo_js_variable_t variable) {
  neo_js_type_t type = neo_js_variable_get_type(variable);
  return type->kind == NEO_TYPE_NUMBER || type->kind == NEO_TYPE_STRING ||
         type->kind == NEO_TYPE_BOOLEAN || type->kind == NEO_TYPE_UNDEFINED ||
         type->kind == NEO_TYPE_NULL || type->kind == NEO_TYPE_SYMBOL;
}