#include "engine/variable.h"
#include "core/allocator.h"
#include "engine/basetype/ref.h"
#include "engine/chunk.h"
#include "engine/type.h"
struct _neo_js_variable_t {
  neo_js_chunk_t handle;
  bool is_const;
  bool is_using;
  bool is_await_using;
};

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_chunk_t handle) {
  neo_js_variable_t variable =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_variable_t), NULL);
  variable->handle = handle;
  variable->is_const = false;
  variable->is_using = false;
  variable->is_await_using = false;
  return variable;
}

neo_js_chunk_t neo_js_variable_get_handle(neo_js_variable_t variable) {
  neo_js_chunk_t handle = variable->handle;
  while (neo_js_chunk_get_value(handle)->type->kind == NEO_JS_TYPE_REF) {
    neo_js_value_t value = neo_js_chunk_get_value(handle);
    neo_js_ref_t ref = neo_js_value_to_ref(value);
    handle = ref->target;
  }
  return handle;
}
neo_js_chunk_t neo_js_variable_get_raw_handle(neo_js_variable_t variable) {
  return variable->handle;
}

void neo_js_variable_set_const(neo_js_variable_t variable, bool is_const) {
  variable->is_const = is_const;
}

bool neo_js_variable_is_const(neo_js_variable_t variable) {
  return variable->is_const;
}

void neo_js_variable_set_using(neo_js_variable_t variable, bool is_using) {
  variable->is_using = is_using;
}

bool neo_js_variable_is_using(neo_js_variable_t variable) {
  return variable->is_using;
}

void neo_js_variable_set_await_using(neo_js_variable_t variable,
                                     bool is_using) {
  variable->is_await_using = is_using;
}

bool neo_js_variable_is_await_using(neo_js_variable_t variable) {
  return variable->is_await_using;
}

bool neo_js_variable_is_ref(neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_chunk_get_value(variable->handle);
  return value->type == neo_get_js_ref_type();
}