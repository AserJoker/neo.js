#include "engine/context.h"
#include "core/allocator.h"
#include "engine/boolean.h"
#include "engine/null.h"
#include "engine/number.h"
#include "engine/runtime.h"
#include "engine/scope.h"
#include "engine/string.h"
#include "engine/undefined.h"
#include <string.h>

struct _neo_js_context_t {
  neo_js_runtime_t runtime;
  neo_js_scope_t root_scope;
  neo_js_scope_t current_scope;
};

static void neo_js_context_dispose(neo_allocator_t allocator,
                                   neo_js_context_t self) {
  self->root_scope = NULL;
  while (self->current_scope) {
    neo_js_context_pop_scope(self);
  }
}

neo_js_context_t neo_create_js_context(neo_js_runtime_t runtime) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_context_t ctx = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_context_t), neo_js_context_dispose);
  ctx->runtime = runtime;
  ctx->root_scope = neo_create_js_scope(allocator, NULL);
  ctx->current_scope = ctx->root_scope;
  return ctx;
}

void neo_js_context_push_scope(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  self->current_scope = neo_create_js_scope(allocator, self->current_scope);
}

void neo_js_context_pop_scope(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_scope_t scope = self->current_scope;
  self->current_scope = neo_js_scope_get_parent(scope);
  neo_allocator_free(allocator, scope);
}
neo_js_runtime_t neo_js_context_get_runtime(neo_js_context_t self) {
  return self->runtime;
}
neo_js_variable_t neo_js_context_create_undefined(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_undefined_t undefined = neo_create_js_undefined(allocator);
  neo_js_value_t val = neo_js_undefined_to_value(undefined);
  return neo_js_scope_create_variable(self->current_scope, val, NULL);
}
neo_js_variable_t neo_js_context_create_null(neo_js_context_t self) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_null_t null = neo_create_js_null(allocator);
  neo_js_value_t val = neo_js_null_to_value(null);
  return neo_js_scope_create_variable(self->current_scope, val, NULL);
}
neo_js_variable_t neo_js_context_create_number(neo_js_context_t self,
                                               double value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_number_t number = neo_create_js_number(allocator, value);
  neo_js_value_t val = neo_js_number_to_value(number);
  return neo_js_scope_create_variable(self->current_scope, val, NULL);
}
neo_js_variable_t neo_js_context_create_boolean(neo_js_context_t self,
                                                bool value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_boolean_t boolean = neo_create_js_boolean(allocator, value);
  neo_js_value_t val = neo_js_boolean_to_value(boolean);
  return neo_js_scope_create_variable(self->current_scope, val, NULL);
}
neo_js_variable_t neo_js_context_create_string(neo_js_context_t self,
                                               uint16_t *value) {
  neo_allocator_t allocator = neo_js_runtime_get_allocator(self->runtime);
  neo_js_string_t string = neo_create_js_string(allocator, value);
  neo_js_value_t val = neo_js_string_to_value(string);
  return neo_js_scope_create_variable(self->current_scope, val, NULL);
}