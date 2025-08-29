#include "engine/variable.h"
#include "core/allocator.h"
#include "engine/chunk.h"
#include "engine/handle.h"
#include "engine/type.h"
struct _neo_js_variable_t {
  neo_js_handle_t handle;
  bool is_const;
  bool is_using;
  bool is_await_using;
};

static void neo_js_variable_dispose(neo_allocator_t allocator,
                                    neo_js_variable_t variable) {
  if (!neo_js_handle_release(variable->handle)) {
    neo_allocator_free(allocator, variable->handle);
  }
}

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_chunk_t chunk) {
  neo_js_variable_t variable = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_variable_t), neo_js_variable_dispose);
  variable->handle = neo_create_js_handle(allocator, chunk);
  variable->is_const = false;
  variable->is_using = false;
  variable->is_await_using = false;
  return variable;
}

neo_js_variable_t neo_create_js_ref_variable(neo_allocator_t allocator,
                                             neo_js_handle_t handle) {
  neo_js_variable_t variable = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_variable_t), neo_js_variable_dispose);
  variable->handle = handle;
  neo_js_handle_add_ref(variable->handle);
  variable->is_const = false;
  variable->is_using = false;
  variable->is_await_using = false;
  return variable;
}

neo_js_chunk_t neo_js_variable_get_chunk(neo_js_variable_t variable) {
  neo_js_handle_t handle = variable->handle;
  neo_js_chunk_t chunk = neo_js_handle_get_chunk(handle);
  return chunk;
}

neo_js_handle_t neo_js_variable_get_handle(neo_js_variable_t variable) {
  return variable->handle;
}

void neo_js_variable_set_handle(neo_js_variable_t variable,
                                neo_js_handle_t handle) {
  variable->handle = handle;
}

void neo_js_variable_store(neo_js_variable_t current,
                           neo_js_variable_t target) {
  size_t *ref = neo_js_handle_get_ref(current->handle);
  *ref += 1;
  ref = neo_js_handle_get_ref(target->handle);
  *ref -= 1;
  target->handle = current->handle;
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
