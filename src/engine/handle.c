#include "engine/handle.h"
#include "core/allocator.h"
#include "core/list.h"
#include "engine/value.h"
struct _neo_js_handle_t {
  neo_js_value_t value;
  neo_list_t parents;
  neo_list_t children;
};

static void neo_js_handle_dispose(neo_allocator_t allocator,
                                  neo_js_handle_t self) {
  if (self->value && !--self->value->ref) {
    neo_allocator_free(allocator, self->value);
  }
  neo_allocator_free(allocator, self->parents);
  neo_allocator_free(allocator, self->children);
}

neo_js_handle_t neo_create_js_handle(neo_allocator_t allocator,
                                     neo_js_value_t value) {
  neo_js_handle_t handle = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_handle_t), neo_js_handle_dispose);
  handle->value = value;
  handle->children = neo_create_list(allocator, NULL);
  handle->parents = neo_create_list(allocator, NULL);
  if (value) {
    value->ref++;
  }
  return handle;
}

neo_js_value_t neo_js_handle_get_value(neo_js_handle_t self) {
  return self->value;
}
void neo_js_handle_set_value(neo_allocator_t allocator, neo_js_handle_t self,
                             neo_js_value_t value) {
  if (!--self->value->ref) {
    neo_allocator_free(allocator, self->value);
  }
  self->value = value;
  self->value->ref++;
}

void neo_js_handle_add_parent(neo_js_handle_t self, neo_js_handle_t parent) {
  neo_list_push(self->parents, parent);
  neo_list_push(parent->children, self);
}

void neo_js_handle_remove_parent(neo_js_handle_t self, neo_js_handle_t parent) {
  neo_list_delete(self->parents, parent);
  neo_list_delete(parent->children, self);
}

neo_list_t neo_js_handle_get_parents(neo_js_handle_t self) {
  return self->parents;
}

neo_list_t neo_js_handle_get_children(neo_js_handle_t self) {
  return self->children;
}

bool neo_js_handle_check_alive(neo_allocator_t allocator,
                               neo_js_handle_t handle) {
  neo_hash_map_t cache = neo_create_hash_map(allocator, NULL);
  neo_list_t workflow = neo_create_list(allocator, NULL);
  neo_js_handle_t root = NULL;
  neo_list_push(workflow, handle);
  while (neo_list_get_size(workflow)) {
    neo_list_node_t it = neo_list_get_first(workflow);
    neo_js_handle_t item = neo_list_node_get(it);
    neo_list_erase(workflow, it);
    if (neo_hash_map_get(cache, item, NULL, NULL)) {
      continue;
    }
    neo_hash_map_set(cache, item, item, NULL, NULL);
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

void neo_js_handle_gc(neo_allocator_t allocator, neo_js_handle_t root) {
  neo_list_t children = neo_js_handle_get_children(root);
  neo_list_t workflow = neo_create_list(allocator, NULL);
  neo_hash_map_t cache = neo_create_hash_map(allocator, NULL);
  neo_list_t queue = neo_create_list(allocator, NULL);
  while (neo_list_get_size(children)) {
    neo_list_node_t it = neo_list_get_first(children);
    neo_js_handle_t child = neo_list_node_get(it);
    neo_js_handle_remove_parent(child, root);
    neo_list_push(workflow, child);
  }
  while (neo_list_get_size(workflow)) {
    neo_list_node_t it = neo_list_get_first(workflow);
    neo_js_handle_t item = neo_list_node_get(it);
    neo_list_erase(workflow, it);
    if (neo_hash_map_has(cache, item, NULL, NULL)) {
      continue;
    }
    neo_hash_map_set(cache, item, item, NULL, NULL);
    if (!neo_js_handle_check_alive(allocator, item)) {
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