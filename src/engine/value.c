#include "engine/value.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "core/list.h"
#include "core/map.h"
#include "engine/function.h"
#include "engine/variable.h"
#include <string.h>

void neo_init_js_value(neo_js_value_t self, neo_allocator_t allocator,
                       neo_js_value_type_t type) {
  self->type = type;
  self->age = 0;
  self->ref = 0;
  self->is_alive = true;
  self->is_check = false;
  self->is_disposed = false;
  self->parent = neo_create_list(allocator, NULL);
  self->children = neo_create_list(allocator, NULL);
  neo_hash_map_initialize_t initialize = {0};
  initialize.auto_free_key = true;
  initialize.auto_free_value = true;
  initialize.compare = (neo_compare_fn_t)strcmp;
  initialize.hash = (neo_hash_fn_t)neo_hash_sdb;
  self->opaque = neo_create_hash_map(allocator, &initialize);
}

void neo_deinit_js_value(neo_js_value_t self, neo_allocator_t allocator) {
  neo_allocator_free(allocator, self->children);
  neo_allocator_free(allocator, self->parent);
  neo_allocator_free(allocator, self->opaque);
}

neo_js_value_t neo_js_value_add_parent(neo_js_value_t self,
                                       neo_js_value_t parent) {
  neo_list_push(self->parent, parent);
  neo_list_push(parent->children, self);
  return self;
}

neo_js_value_t neo_js_value_remove_parent(neo_js_value_t self,
                                          neo_js_value_t parent) {
  neo_list_delete(self->parent, parent);
  neo_list_delete(parent->children, self);
  return self;
}

static void neo_js_value_check(neo_js_value_t self, uint32_t age) {
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
  neo_list_node_t it = neo_list_get_first(self->parent);
  while (it != neo_list_get_tail(self->parent)) {
    neo_js_value_t parent = neo_list_node_get(it);
    it = neo_list_node_next(it);
    if (parent->is_check) {
      continue;
    }
    neo_js_value_check(parent, age);
    if (parent->is_alive) {
      self->is_alive = true;
      break;
    }
  }
  self->is_check = false;
}

void neo_js_value_gc(neo_allocator_t allocator, neo_list_t gclist) {
  static uint32_t age = 0;
  ++age;
  neo_list_initialize_t initialize = {true};
  neo_list_t destroyed = neo_create_list(allocator, &initialize);
  while (neo_list_get_size(gclist)) {
    neo_list_node_t it = neo_list_get_first(gclist);
    neo_js_value_t value = neo_list_node_get(it);
    neo_list_erase(gclist, it);
    if (value->is_disposed) {
      continue;
    }
    neo_js_value_check(value, age);
    if (!value->is_alive) {
      value->is_disposed = true;
      neo_list_push(destroyed, value);
      neo_list_node_t it = neo_list_get_first(value->children);
      while (it != neo_list_get_tail(value->children)) {
        neo_js_value_t child = neo_list_node_get(it);
        neo_list_push(gclist, child);
        it = neo_list_node_next(it);
      }
      if (value->type == NEO_JS_TYPE_FUNCTION) {
        neo_map_t closure = ((neo_js_function_t)value)->closure;
        neo_map_node_t it = neo_map_get_first(closure);
        while (it != neo_map_get_tail(closure)) {
          neo_js_variable_t variable = neo_map_node_get_value(it);
          if (variable->ref && !--variable->ref) {
            if (variable->value->ref && !--variable->value->ref) {
              neo_list_push(gclist, variable->value);
            }
            neo_allocator_free(allocator, variable);
          }
        }
      }
    }
  }
  neo_allocator_free(allocator, destroyed);
}