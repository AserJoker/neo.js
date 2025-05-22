#include "compiler/scope.h"
#include "core/allocator.h"
#include "core/list.h"
#include <string.h>
#include <sys/types.h>
neo_compile_scope_t current = NULL;

static void neo_compile_scope_dispose(neo_allocator_t allocator,
                                      neo_compile_scope_t scope) {
  neo_allocator_free(allocator, scope->variables);
}

static neo_compile_scope_t neo_create_compile_scope(neo_allocator_t allocator) {
  neo_compile_scope_t scope = (neo_compile_scope_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_compile_scope_t),
      neo_compile_scope_dispose);
  scope->parent = NULL;
  neo_list_initialize_t initialize = {true};
  scope->variables = neo_create_list(allocator, &initialize);
  scope->type = NEO_COMPILE_SCOPE_FUNCTION;
  return scope;
}

neo_compile_scope_t neo_compile_scope_push(neo_allocator_t allocator,
                                           neo_compile_scope_type_t type) {
  neo_compile_scope_t scope = neo_create_compile_scope(allocator);
  scope->parent = current;
  scope->type = type;
  neo_compile_scope_t curr = current;
  current = scope;
  return curr;
}

neo_compile_scope_t neo_compile_scope_pop(neo_compile_scope_t scope) {
  neo_compile_scope_t curr = current;
  current = scope;
  return curr;
}

static void neo_compile_variable_dispose(neo_allocator_t allocator,
                                         neo_compile_variable_t variable) {
  neo_allocator_free(allocator, variable->name);
}

static neo_compile_variable_t
neo_create_compile_variable(neo_allocator_t allocator) {
  neo_compile_variable_t variable = (neo_compile_variable_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_compile_variable_t),
      neo_compile_variable_dispose);
  variable->name = NULL;
  variable->type = NEO_COMPILE_VARIABLE_VAR;
  return variable;
}

void neo_compile_scope_declar_value(neo_allocator_t allocator,
                                    neo_compile_scope_t self, const char *name,
                                    neo_compile_variable_type_t type) {
  size_t len = strlen(name);
  neo_compile_variable_t variable = neo_create_compile_variable(allocator);
  variable->type = type;
  variable->name = neo_allocator_alloc(allocator, len + 1, NULL);
  strcpy(variable->name, name);
  variable->name[len] = 0;
  while (self->type == NEO_COMPILE_SCOPE_BLOCK) {
    self = self->parent;
  }
  neo_list_push(self->variables, variable);
}

neo_compile_scope_t neo_complile_scope_get_current() { return current; }