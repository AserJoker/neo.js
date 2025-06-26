#include "engine/scope.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/map.h"
#include "core/string.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <string.h>
struct _neo_js_scope_t {
  neo_js_scope_t parent;
  neo_list_t children;
  neo_list_t variables;
  neo_map_t named_variables;
  neo_js_handle_t root;
  uint32_t ref;
};

static bool neo_js_scope_check_alive(neo_allocator_t allocator,
                                     neo_js_handle_t handle) {
  neo_list_t cache = neo_create_list(allocator, NULL);
  neo_list_t workflow = neo_create_list(allocator, NULL);
  neo_js_handle_t root = NULL;
  neo_list_push(workflow, handle);
  while (neo_list_get_size(workflow)) {
    neo_list_node_t it = neo_list_get_first(workflow);
    neo_js_handle_t item = neo_list_node_get(it);
    neo_list_erase(workflow, it);
    if (neo_list_find(cache, item)) {
      continue;
    }
    neo_list_push(cache, item);
    neo_list_t parents = neo_js_handle_get_parents(item);
    for (neo_list_node_t it = neo_list_get_first(parents);
         it != neo_list_get_tail(parents); it = neo_list_node_next(it)) {
      neo_js_handle_t p = neo_list_node_get(it);
      if (!neo_js_handle_get_value(p)) {
        root = p;
        break;
      } else {
        neo_list_push(workflow, p);
      }
    }
    if (root) {
      break;
    }
  }
  neo_allocator_free(allocator, cache);
  neo_allocator_free(allocator, workflow);
  if (root) {
    return true;
  }
  return false;
}

static void neo_js_scope_gc(neo_allocator_t allocator, neo_js_scope_t self) {
  neo_list_t children = neo_js_handle_get_children(self->root);
  neo_list_t workflow = neo_create_list(allocator, NULL);
  neo_list_t cache = neo_create_list(allocator, NULL);
  neo_list_t queue = neo_create_list(allocator, NULL);
  while (neo_list_get_size(children)) {
    neo_list_node_t it = neo_list_get_first(children);
    neo_js_handle_t child = neo_list_node_get(it);
    neo_js_handle_remove_parent(child, self->root);
    neo_list_push(workflow, child);
  }
  while (neo_list_get_size(workflow)) {
    neo_list_node_t it = neo_list_get_first(workflow);
    neo_js_handle_t item = neo_list_node_get(it);
    neo_list_erase(workflow, it);
    if (neo_list_find(cache, item)) {
      continue;
    }
    neo_list_push(cache, item);
    if (!neo_js_scope_check_alive(allocator, item)) {
      children = neo_js_handle_get_children(item);
      while (neo_list_get_size(children)) {
        neo_list_node_t it = neo_list_get_first(children);
        neo_js_handle_t child = neo_list_node_get(it);
        neo_list_push(workflow, child);
        neo_js_handle_remove_parent(child, item);
      }
      neo_list_push(queue, item);
    }
  }
  while (neo_list_get_size(queue)) {
    neo_list_node_t it = neo_list_get_first(queue);
    neo_js_handle_t item = neo_list_node_get(it);
    neo_allocator_free(allocator, item);
    neo_list_erase(queue, it);
  }
  neo_allocator_free(allocator, queue);
  neo_allocator_free(allocator, workflow);
  neo_allocator_free(allocator, cache);
}

static void neo_js_scope_dispose(neo_allocator_t allocator,
                                 neo_js_scope_t self) {
  if (self->parent) {
    neo_list_delete(self->parent->children, self);
  }
  while (neo_list_get_size(self->children)) {
    neo_list_node_t it = neo_list_get_first(self->children);
    neo_js_scope_t child = neo_list_node_get(it);
    neo_allocator_free(allocator, child);
  }
  neo_allocator_free(allocator, self->children);
  neo_allocator_free(allocator, self->variables);
  neo_allocator_free(allocator, self->named_variables);
  neo_js_scope_gc(allocator, self);
  neo_allocator_free(allocator, self->root);
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
  neo_list_initialize_t initialize = {true};
  scope->variables = neo_create_list(allocator, &initialize);
  neo_map_initialize_t map_initialize = {
      true,
      false,
      (neo_compare_fn_t)wcscmp,
  };
  scope->named_variables = neo_create_map(allocator, &map_initialize);
  scope->children = neo_create_list(allocator, NULL);
  scope->root = neo_create_js_handle(allocator, NULL);
  scope->ref = 1;
  return scope;
}
uint32_t neo_js_scope_add_ref(neo_js_scope_t self) { return ++self->ref; }

uint32_t neo_js_scope_ref(neo_js_scope_t self) { return self->ref; }

uint32_t neo_js_scope_release(neo_js_scope_t self) { return --self->ref; }

neo_js_scope_t neo_js_scope_get_parent(neo_js_scope_t self) {
  return self->parent;
}
neo_js_scope_t neo_js_scope_set_parent(neo_js_scope_t self,
                                       neo_js_scope_t parent) {
  neo_js_scope_t current = self->parent;
  self->parent = parent;
  return current;
}
neo_js_handle_t neo_js_scope_get_root_handle(neo_js_scope_t self) {
  return self->root;
}
neo_js_variable_t neo_js_scope_get_variable(neo_js_scope_t self,
                                            const wchar_t *name) {
  return neo_map_get(self->named_variables, name, NULL);
}
void neo_js_scope_set_variable(neo_allocator_t allocator, neo_js_scope_t self,
                               neo_js_variable_t variable,
                               const wchar_t *name) {
  neo_list_push(self->variables, variable);
  if (name) {
    neo_map_set(self->named_variables, neo_create_wstring(allocator, name),
                variable, NULL);
  }
}

neo_js_variable_t neo_js_scope_create_variable(neo_allocator_t allocator,
                                               neo_js_scope_t self,
                                               neo_js_handle_t handle) {
  neo_js_variable_t variable = neo_create_js_variable(allocator, handle);
  neo_js_handle_add_parent(handle, self->root);
  neo_list_push(self->variables, variable);
  return variable;
}