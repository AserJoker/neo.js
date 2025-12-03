#include "engine/handle.h"
#include "core/allocator.h"
#include "core/list.h"
void neo_init_js_handle(neo_js_handle_t self, neo_allocator_t allocator,
                        neo_js_handle_type_t type) {
  self->age = 0;
  self->is_alive = true;
  self->is_check = false;
  self->is_disposed = false;
  self->parent = neo_create_list(allocator, NULL);
  self->children = neo_create_list(allocator, NULL);
  self->is_root = false;
  self->type = type;
}
void neo_deinit_js_handle(neo_js_handle_t self, neo_allocator_t allocator) {
  neo_allocator_free(allocator, self->children);
  neo_allocator_free(allocator, self->parent);
}

void neo_js_handle_add_parent(neo_js_handle_t handle, neo_js_handle_t parent) {
  neo_list_push(handle->parent, parent);
  neo_list_push(parent->children, handle);
}
void neo_js_handle_remove_parent(neo_js_handle_t handle,
                                 neo_js_handle_t parent) {
  neo_list_delete(handle->parent, parent);
  neo_list_delete(parent->children, handle);
}
static void neo_js_handle_check(neo_js_handle_t self, uint32_t age) {
  if (self->age == age) {
    return;
  }
  self->age = age;
  if (self->is_root) {
    self->is_alive = true;
    return;
  }
  self->is_check = true;
  self->is_alive = false;
  neo_list_node_t it = neo_list_get_first(self->parent);
  while (it != neo_list_get_tail(self->parent)) {
    neo_js_handle_t parent = neo_list_node_get(it);
    it = neo_list_node_next(it);
    if (parent->is_check) {
      continue;
    }
    neo_js_handle_check(parent, age);
    if (parent->is_alive) {
      self->is_alive = true;
      break;
    }
  }
  self->is_check = false;
}
void neo_js_handle_gc(neo_allocator_t allocator, neo_list_t handles,
                      neo_list_t gclist, neo_js_handle_on_gc_fn_t cb,
                      void *ctx) {
  static uint32_t age = 0;
  age++;
  while (neo_list_get_size(handles)) {
    neo_list_node_t it = neo_list_get_first(handles);
    neo_js_handle_t handle = neo_list_node_get(it);
    neo_list_erase(handles, it);
    if (handle->is_disposed) {
      continue;
    }
    neo_js_handle_check(handle, age);
    if (!handle->is_alive) {
      handle->is_disposed = true;
      if (cb) {
        cb(allocator, handle, ctx);
      }
      neo_list_push(gclist, handle);
      while (neo_list_get_size(handle->children)) {
        neo_list_node_t it = neo_list_get_first(handle->children);
        neo_js_handle_t child = neo_list_node_get(it);
        neo_list_push(handles, child);
        neo_js_handle_remove_parent(child, handle);
      }
    }
  }
}