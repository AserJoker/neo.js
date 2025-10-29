#include "engine/scope.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/map.h"
#include "core/string.h"
#include "engine/variable.h"
#include <string.h>

struct _neo_js_scope_t {
  neo_js_scope_t parent;
  neo_list_t children;
  neo_list_t variables;
  neo_map_t named_variables;
  neo_list_t defer_free;
  neo_allocator_t allocator;
};

static void neo_js_scope_dispose(neo_allocator_t allocator,
                                 neo_js_scope_t self) {
  if (self->parent) {
    neo_list_delete(self->parent->children, self);
    self->parent = NULL;
  }
  while (neo_list_get_size(self->children)) {
    neo_list_node_t it = neo_list_get_first(self->children);
    neo_js_scope_t child = neo_list_node_get(it);
    neo_list_erase(self->children, it);
    child->parent = NULL;
    neo_allocator_free(allocator, child);
  }
  neo_list_t gclist = neo_create_list(allocator, NULL);
  neo_list_node_t it = neo_list_get_first(self->variables);
  while (it != neo_list_get_tail(self->variables)) {
    neo_js_variable_t variable = neo_list_node_get(it);
    if (!--variable->ref) {
      neo_list_push(gclist, variable);
    }
    it = neo_list_node_next(it);
  }
  neo_js_variable_gc(allocator, gclist);
  neo_allocator_free(allocator, gclist);
  neo_allocator_free(allocator, self->children);
  neo_allocator_free(allocator, self->variables);
  neo_allocator_free(allocator, self->named_variables);
  neo_allocator_free(allocator, self->defer_free);
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
  return scope;
}

neo_js_scope_t neo_js_scope_get_parent(neo_js_scope_t self) {
  return self->parent;
}

neo_js_variable_t neo_js_scope_get_variable(neo_js_scope_t self,
                                            const char *name) {
  return neo_map_get(self->named_variables, name, NULL);
}
neo_js_variable_t neo_js_scope_set_variable(neo_js_scope_t self,
                                            neo_js_variable_t variable,
                                            const char *name) {
  if (name) {
    neo_map_set(self->named_variables, neo_create_string(self->allocator, name),
                variable, NULL);
  }
  neo_list_push(self->variables, variable);
  variable->ref++;
  return variable;
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

void neo_js_scope_defer_free(neo_js_scope_t scope, void *data) {
  neo_list_push(scope->defer_free, data);
}