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
  while (neo_list_get_size(self->parent)) {
    neo_list_node_t it = neo_list_get_first(self->parent);
    neo_js_handle_t parent = neo_list_node_get(it);
    neo_js_handle_remove_parent(self, parent);
  }
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
static bool neo_js_handle_check(neo_allocator_t allocator,
                                neo_js_handle_t self) {
  neo_list_t workqueue = neo_create_list(allocator, NULL);
  neo_list_push(workqueue, self);
  bool result = false;
  neo_list_node_t it = neo_list_get_first(workqueue);
  while (it != neo_list_get_tail(workqueue)) {
    neo_js_handle_t item = neo_list_node_get(it);
    if (item->is_check) {
      it = neo_list_node_next(it);
      continue;
    }
    item->is_check = true;
    if (item->is_root) {
      result = true;
      break;
    }
    for (neo_list_node_t node = neo_list_get_first(item->parent);
         node != neo_list_get_tail(item->parent);
         node = neo_list_node_next(node)) {
      neo_js_handle_t parent = neo_list_node_get(node);
      if (!parent->is_check) {
        neo_list_push(workqueue, parent);
      }
    }
    it = neo_list_node_next(it);
  }
  for (neo_list_node_t it = neo_list_get_first(workqueue);
       it != neo_list_get_tail(workqueue); it = neo_list_node_next(it)) {
    neo_js_handle_t item = neo_list_node_get(it);
    item->is_check = false;
  }
  neo_allocator_free(allocator, workqueue);
  return result;
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
    if (handle->age == age) {
      continue;
    }
    handle->age = age;
    if (handle->is_disposed) {
      continue;
    }
    if (!neo_js_handle_check(allocator, handle)) {
      handle->is_disposed = true;
      neo_list_push(gclist, handle);
      if (cb) {
        cb(allocator, handle, ctx);
      }
      while (neo_list_get_size(handle->children) != 0) {
        neo_list_node_t node = neo_list_get_first(handle->children);
        neo_js_handle_t child = neo_list_node_get(node);
        neo_js_handle_remove_parent(child, handle);
        neo_list_push(handles, child);
      }
    }
  }
}