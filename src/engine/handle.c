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