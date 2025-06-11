#include "js/context.h"
#include "core/allocator.h"
#include "js/boolean.h"
#include "js/handle.h"
#include "js/infinity.h"
#include "js/nan.h"
#include "js/number.h"
#include "js/runtime.h"
#include "js/scope.h"
#include "js/string.h"
#include "js/type.h"
#include "js/undefined.h"
#include "js/value.h"
#include "js/variable.h"
struct _neo_js_context_t {
  neo_js_runtime_t runtime;
  neo_js_scope_t scope;
  neo_js_scope_t root;
  neo_js_variable_t global;
};

static void neo_js_context_dispose(neo_allocator_t allocator,
                                   neo_js_context_t self) {
  neo_allocator_free(allocator, self->root);
  self->scope = NULL;
  self->root = NULL;
  self->global = NULL;
  self->runtime = NULL;
}

neo_js_context_t neo_create_js_context(neo_allocator_t allocator,
                                       neo_js_runtime_t runtime) {
  neo_js_context_t ctx = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_context_t), neo_js_context_dispose);
  ctx->runtime = runtime;
  ctx->root = neo_create_js_scope(allocator, NULL);
  ctx->scope = ctx->root;
  ctx->global = NULL;
  return ctx;
}
neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t self) {
  return self->runtime;
}
neo_js_scope_t neo_js_context_get_scope(neo_js_context_t self) {
  return self->scope;
}
neo_js_scope_t neo_js_context_get_root(neo_js_context_t self) {
  return self->root;
}
neo_js_variable_t neo_js_context_get_global(neo_js_context_t self) {
  return self->global;
}
neo_js_scope_t neo_js_context_push_scope(neo_js_context_t self) {
  self->scope = neo_create_js_scope(neo_js_runtime_get_allocator(self->runtime),
                                    self->scope);
  return self->scope;
}
neo_js_scope_t neo_js_context_pop_scope(neo_js_context_t self) {
  neo_js_scope_t current = self->scope;
  self->scope = neo_js_scope_get_parent(self->scope);
  neo_allocator_free(neo_js_runtime_get_allocator(self->runtime), current);
  return self->scope;
}

neo_js_variable_t neo_js_context_clone_variable(neo_js_context_t self,
                                                neo_js_variable_t variable) {
  neo_js_handle_t handle = neo_js_variable_get_handle(variable);
  return neo_js_context_create_variable(self, handle);
}

neo_js_variable_t neo_js_context_create_variable(neo_js_context_t self,
                                                 neo_js_handle_t handle) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_variable_t variable = neo_create_js_variable(allocator, handle);
  neo_js_handle_t current = neo_js_scope_get_root_handle(self->scope);
  neo_js_handle_add_parent(handle, current);
  neo_js_scope_set_variable(allocator, self->scope, variable, NULL);
  return variable;
}

neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_undefined_t undefined = neo_create_js_undefined(allocator);
  return neo_js_context_create_variable(
      self,
      neo_create_js_handle(allocator, neo_js_undefined_to_value(undefined)));
}
neo_js_variable_t neo_js_context_create_nan(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_nan_t nan = neo_create_js_nan(allocator);
  return neo_js_context_create_variable(
      self, neo_create_js_handle(allocator, neo_js_nan_to_value(nan)));
}
neo_js_variable_t neo_js_context_create_infinity(neo_js_context_t self,
                                                 bool negative) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_infinity_t infinity = neo_create_js_infinity(allocator, negative);
  return neo_js_context_create_variable(
      self,
      neo_create_js_handle(allocator, neo_js_infinity_to_value(infinity)));
}

neo_js_variable_t neo_js_context_create_number(neo_js_context_t self,
                                               double value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_number_t number = neo_create_js_number(allocator, value);
  return neo_js_context_create_variable(
      self, neo_create_js_handle(allocator, neo_js_number_to_value(number)));
}

neo_js_variable_t neo_js_context_create_string(neo_js_context_t self,
                                               wchar_t *value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_string_t string = neo_create_js_string(allocator, value);
  return neo_js_context_create_variable(
      self, neo_create_js_handle(allocator, neo_js_string_to_value(string)));
}

neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t self,
                                                bool value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_boolean_t boolean = neo_create_js_boolean(allocator, value);
  return neo_js_context_create_variable(
      self, neo_create_js_handle(allocator, neo_js_boolean_to_value(boolean)));
}
const wchar_t *neo_js_context_typeof(neo_js_context_t self,
                                     neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return value->type->typeof_fn(self, variable);
}
neo_js_variable_t neo_js_context_to_string(neo_js_context_t self,
                                           neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return value->type->to_string_fn(self, variable);
}

neo_js_variable_t neo_js_context_to_boolean(neo_js_context_t self,
                                            neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return value->type->to_boolean_fn(self, variable);
}

neo_js_variable_t neo_js_context_to_number(neo_js_context_t self,
                                           neo_js_variable_t variable) {
  neo_js_value_t value = neo_js_variable_get_value(variable);
  return value->type->to_number_fn(self, variable);
}