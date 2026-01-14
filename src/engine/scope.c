#include "neo.js/engine/scope.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/hash.h"
#include "neo.js/core/hash_map.h"
#include "neo.js/core/list.h"
#include "neo.js/core/map.h"
#include "neo.js/core/string.h"
#include "neo.js/engine/handle.h"
#include "neo.js/engine/variable.h"
#include <stdbool.h>
#include <string.h>

struct _neo_js_scope_t {
  struct _neo_js_handle_t handle;
  neo_js_scope_t parent;
  neo_list_t children;
  neo_list_t variables;
  neo_list_t using_variables;
  neo_list_t await_using_variables;
  neo_map_t named_variables;
  neo_list_t defer_free;
  neo_allocator_t allocator;
  neo_hash_map_t features;
};

static void neo_js_scope_dispose(neo_allocator_t allocator,
                                 neo_js_scope_t self) {
  if (self->parent) {
    neo_list_delete(self->parent->children, self);
    self->parent = NULL;
  }
  neo_deinit_js_handle(&self->handle, allocator);
  neo_allocator_free(allocator, self->children);
  neo_allocator_free(allocator, self->variables);
  neo_allocator_free(allocator, self->named_variables);
  neo_allocator_free(allocator, self->using_variables);
  neo_allocator_free(allocator, self->await_using_variables);
  neo_allocator_free(allocator, self->defer_free);
  neo_allocator_free(allocator, self->features);
}

neo_js_scope_t neo_create_js_scope(neo_allocator_t allocator,
                                   neo_js_scope_t parent) {
  neo_js_scope_t scope = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_scope_t), neo_js_scope_dispose);
  if (parent) {
    scope->parent = parent;
    neo_list_push(parent->children, scope);
  } else {
    scope->parent = NULL;
  }
  scope->variables = neo_create_list(allocator, NULL);
  neo_map_initialize_t map_initialize = {
      true,
      false,
      (neo_compare_fn_t)strcmp,
  };
  scope->named_variables = neo_create_map(allocator, &map_initialize);
  scope->children = neo_create_list(allocator, NULL);
  neo_list_initialize_t initialize = {true};
  scope->defer_free = neo_create_list(allocator, &initialize);
  scope->allocator = allocator;
  neo_init_js_handle(&scope->handle, allocator, NEO_JS_HANDLE_SCOPE);
  scope->handle.is_root = true;
  neo_hash_map_initialize_t hash_map_initialize = {
      .auto_free_key = true,
      .auto_free_value = false,
      .compare = (neo_compare_fn_t)strcmp,
      .hash = (neo_hash_fn_t)neo_hash_sdb};
  scope->features = neo_create_hash_map(allocator, &hash_map_initialize);
  initialize.auto_free = false;
  scope->using_variables = neo_create_list(allocator, &initialize);
  scope->await_using_variables = neo_create_list(allocator, &initialize);
  return scope;
}

neo_js_scope_t neo_js_scope_get_parent(neo_js_scope_t self) {
  return self->parent;
}

neo_js_variable_t neo_js_scope_get_variable(neo_js_scope_t self,
                                            const char *name) {
  return neo_map_get(self->named_variables, name);
}
neo_js_variable_t neo_js_scope_set_variable(neo_js_scope_t self,
                                            neo_js_variable_t variable,
                                            const char *name) {
  if (name) {
    neo_map_set(self->named_variables, neo_create_string(self->allocator, name),
                variable);
  }
  neo_list_push(self->variables, variable);
  neo_js_handle_add_parent(&variable->handle, &self->handle);
  return variable;
}

void neo_js_scope_set_using(neo_js_scope_t self, neo_js_variable_t variable) {
  neo_list_push(self->using_variables, variable);
}

void neo_js_scope_set_await_using(neo_js_scope_t self,
                                  neo_js_variable_t variable) {
  neo_list_push(self->await_using_variables, variable);
}

neo_js_variable_t neo_js_scope_create_variable(neo_js_scope_t self,
                                               neo_js_value_t value,
                                               const char *name) {
  neo_js_variable_t variable = neo_create_js_variable(self->allocator, value);
  return neo_js_scope_set_variable(self, variable, name);
}

neo_list_t neo_js_scope_get_variables(neo_js_scope_t scope) {
  return scope->variables;
}
neo_list_t neo_js_scope_get_children(neo_js_scope_t self) {
  return self->children;
}
neo_list_t neo_js_scope_get_using(neo_js_scope_t self) {
  return self->using_variables;
}
neo_list_t neo_js_scope_get_await_using(neo_js_scope_t self) {
  return self->await_using_variables;
}

void neo_js_scope_defer_free(neo_js_scope_t scope, void *data) {
  neo_list_push(scope->defer_free, data);
}
void neo_js_scope_set_feature(neo_js_scope_t self, const char *feature) {
  neo_allocator_t allocaotr = self->allocator;
  neo_hash_map_set(self->features, neo_create_string(allocaotr, feature), NULL);
}

bool neo_js_scope_has_feature(neo_js_scope_t self, const char *feature) {
  return neo_hash_map_has(self->features, feature);
}