#include "engine/variable.h"
#include "core/allocator.h"
#include "core/list.h"
#include <stdint.h>
#include <string.h>

static void neo_js_variable_dispose(neo_allocator_t allocator,
                                    neo_js_variable_t variable) {
  neo_allocator_free(allocator, variable->value);
  neo_allocator_free(allocator, variable->parents);
  neo_allocator_free(allocator, variable->children);
  neo_allocator_free(allocator, variable->weak_children);
  neo_allocator_free(allocator, variable->weak_parents);
}

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_value_t value) {
  neo_js_variable_t variable = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_variable_t), neo_js_variable_dispose);
  variable->allocator = allocator;
  variable->parents = neo_create_list(allocator, NULL);
  variable->children = neo_create_list(allocator, NULL);
  variable->weak_children = neo_create_list(allocator, NULL);
  variable->weak_parents = neo_create_list(allocator, NULL);
  variable->is_const = false;
  variable->is_using = false;
  variable->is_await_using = false;
  variable->ref = 0;
  variable->age = 0;
  variable->is_alive = true;
  variable->is_check = false;
  variable->is_disposed = false;
  variable->value = value;
  return variable;
}

void neo_js_variable_add_parent(neo_js_variable_t self,
                                neo_js_variable_t parent) {
  neo_list_push(self->parents, parent);
  neo_list_push(parent->children, self);
}

void neo_js_variable_remove_parent(neo_js_variable_t self,
                                   neo_js_variable_t parent) {
  neo_list_delete(self->parents, parent);
  neo_list_delete(parent->children, self);
}

void neo_js_variable_add_weak_parent(neo_js_variable_t self,
                                     neo_js_variable_t parent) {
  neo_list_push(self->weak_parents, parent);
  neo_list_push(parent->weak_children, self);
}

void neo_js_variable_remove_weak_parent(neo_js_variable_t self,
                                        neo_js_variable_t parent) {
  neo_list_delete(self->weak_parents, parent);
  neo_list_delete(parent->weak_children, self);
}

static void neo_js_variable_check(neo_allocator_t allocator,
                                  neo_js_variable_t self, uint32_t age) {
  if (self->age == age) {
    return;
  }
  self->age = age;
  if (self->ref) {
    self->is_alive = true;
    return;
  }
  self->is_check = true;
  self->is_alive = false;
  neo_list_node_t it = neo_list_get_first(self->parents);
  while (it != neo_list_get_tail(self->parents)) {
    neo_js_variable_t variable = neo_list_node_get(it);
    if (!variable->is_check) {
      neo_js_variable_check(allocator, variable, age);
      if (variable->is_alive) {
        self->is_alive = true;
        break;
      }
    }
    it = neo_list_node_next(it);
  }
  self->is_check = false;
}

void neo_js_variable_gc(neo_allocator_t allocator, neo_list_t gclist) {
  static uint32_t age = 0;
  ++age;
  neo_list_initialize_t initialize = {true};
  neo_list_t disposed = neo_create_list(allocator, &initialize);
  while (neo_list_get_size(gclist)) {
    neo_list_node_t it = neo_list_get_first(gclist);
    neo_js_variable_t variable = neo_list_node_get(it);
    neo_list_erase(gclist, it);
    neo_js_variable_check(allocator, variable, age);
    if (!variable->is_alive) {
      variable->is_disposed = true;
      neo_list_push(disposed, variable);
      neo_list_node_t it = neo_list_get_first(variable->children);
      while (it != neo_list_get_tail(variable->children)) {
        neo_js_variable_t child = neo_list_node_get(it);
        if (!child->is_disposed) {
          neo_list_push(gclist, child);
        }
        it = neo_list_node_next(it);
      }
    }
  }
  neo_allocator_free(allocator, disposed);
}
