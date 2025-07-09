#include "engine/variable.h"
#include "core/allocator.h"
#include "engine/handle.h"
#include "engine/type.h"
#include <wchar.h>
struct _neo_js_variable_t {
  neo_js_handle_t handle;
  bool is_const;
  bool is_using;
  bool is_await_using;
};

static void neo_js_variable_dispose(neo_allocator_t allocator,neo_js_variable_t self){
  (void)self;
}

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_handle_t handle) {
  neo_js_variable_t variable =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_variable_t), NULL);
  variable->handle = handle;
  variable->is_const = false;
  variable->is_using = false;
  variable->is_await_using = false;
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
